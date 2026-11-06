#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""NAGLOED v2 — SpectraStrobe proof-of-concept: deterministic renderer.

EXPERIMENT. Not part of NAGLOED v1.0.4, no integration, no DSP contract.
Offline and Python-stdlib only.

Reads score.json and writes (by default into out/):
  spectrastrobe-48k-24bit.wav        full mix, 48 kHz/24-bit (technical reference)
  spectrastrobe-44k1-16bit.wav       full mix, 44.1 kHz/16-bit (recommended playback)
  spectrastrobe-mid-44k1-16bit.wav   brightness ladder: 12.5% max
  spectrastrobe-dim-44k1-16bit.wav   brightness ladder: 6.25% max (first hardware exposure)
  program-only-48k-24bit.wav         A/B without carriers
  program-only-44k1-16bit.wav        A/B without carriers, 44.1 kHz
  events.csv, timeline.svg, render-manifest.json

The order is binding (docs/v2/SPECTRASTROBE.md §6): process and band-limit the
programme completely first, then add the carriers, then nothing else.

Safety (R3 from docs/v2/SAFETY-PRIVACY.md): every colour channel is hard-clamped
to 25% of the reference peak, even if the score asks for more. The sum per ear
is reported separately; 25% applies per carrier.
"""

from __future__ import annotations

import argparse
import array
import csv
import hashlib
import json
import math
import random
import sys
import wave
from pathlib import Path

TAU = 2.0 * math.pi
EXPERIMENT_TITLE = "Eerste Gloed — SpectraStrobe proof-of-concept (EXPERIMENT)"
POC_VERSION = "0.2.0"
POC_DIR = Path(__file__).resolve().parent

LIGHT_KEYS = ("red", "green", "blue")
DEFAULT_EXPOSURES = {
    "full": {"scale": 1.0, "max_colour_ratio": 0.20, "label": "full 20%"},
    "mid": {"scale": 0.625, "max_colour_ratio": 0.125, "label": "middle 12.5%"},
    "dim": {"scale": 0.3125, "max_colour_ratio": 0.0625, "label": "dim 6.25% (first hardware exposure)"},
}


# --------------------------------------------------------------------------- #
# Basic calculations
# --------------------------------------------------------------------------- #
def db_to_lin(db: float) -> float:
    return 10.0 ** (db / 20.0)


def lin_to_db(x: float) -> float:
    if x <= 0.0:
        return float("-inf")
    return 20.0 * math.log10(x)


def stable_seed(seed: int, tag: str) -> int:
    digest = hashlib.sha256(f"{seed}:{tag}".encode("utf-8")).digest()
    return int.from_bytes(digest[:8], "big")


def smoothstep(u: float) -> float:
    u = 0.0 if u < 0.0 else (1.0 if u > 1.0 else u)
    return u * u * (3.0 - 2.0 * u)


def segments_array(points, count: int, fs: float) -> array.array:
    """Breakpoint envelope with smoothstep interpolation; click-free."""
    pts = [(float(t), float(v)) for t, v in points]
    if not pts:
        raise ValueError("empty breakpoint path")
    arr = array.array("d", [pts[0][1]]) * count
    for (t0, v0), (t1, v1) in zip(pts, pts[1:]):
        i0 = int(round(t0 * fs))
        i1 = int(round(t1 * fs))
        i0 = 0 if i0 < 0 else (count if i0 > count else i0)
        i1 = 0 if i1 < 0 else (count if i1 > count else i1)
        span = i1 - i0
        if span <= 0:
            continue
        dv = v1 - v0
        for i in range(i0, i1):
            s = smoothstep((i - i0) / span)
            arr[i] = v0 + dv * s
    last = int(round(pts[-1][0] * fs))
    for i in range(max(0, last), count):
        arr[i] = pts[-1][1]
    return arr


def fade_array(count: int, fs: float, fade_in_ms: float, fade_out_ms: float) -> array.array:
    """Click-free raised-cosine fade-in/out for the carriers at the track edges."""
    fade = array.array("d", [1.0]) * count
    k_in = max(0, min(count // 2, int(round(fade_in_ms * fs / 1000.0))))
    for i in range(k_in):
        fade[i] = 0.5 - 0.5 * math.cos(math.pi * i / k_in) if k_in else 1.0
    k_out = max(0, min(count // 2, int(round(fade_out_ms * fs / 1000.0))))
    for i in range(k_out):
        fade[count - 1 - i] = 0.5 - 0.5 * math.cos(math.pi * i / k_out) if k_out else 1.0
    return fade


# --------------------------------------------------------------------------- #
# Panning
# --------------------------------------------------------------------------- #
def pan_equal_power(p: float):
    """Equal-power panning (reference): total carrier energy stays constant."""
    p = -1.0 if p < -1.0 else (1.0 if p > 1.0 else p)
    theta = 0.25 * math.pi + 0.25 * math.pi * p
    return math.cos(theta), math.sin(theta)


def pan_linear_0db(p: float):
    """Documented linear panning with 0 dB in the centre (colours).

    Deliberately no -3 dB centre-pan law: that would halve the decoded
    brightness per ear. p=-1: left only; p=0: both ears full; p=+1: right only.
    """
    p = -1.0 if p < -1.0 else (1.0 if p > 1.0 else p)
    return (1.0 - max(0.0, p), 1.0 + min(0.0, p))


# --------------------------------------------------------------------------- #
# Score validation (R4/R5, pan laws, ceilings)
# --------------------------------------------------------------------------- #
def validate_score(score: dict) -> dict:
    """Hard score validation; returns the normalised configuration."""
    meta = score.get("meta") or {}
    duration = float(meta.get("duration_s", 0.0))
    if not (60.0 <= duration <= 90.0):
        raise ValueError(f"score duration must be 60–90 s, got {duration} s")
    carriers = score.get("carriers") or {}
    ref = carriers.get("reference") or {}
    if ref.get("pan_law") != "equal_power":
        raise ValueError("reference pan law must be 'equal_power' (constant carrier energy)")
    if carriers.get("colour_pan_law") != "linear_0db_center":
        raise ValueError("colour pan law must be 'linear_0db_center' (no -3 dB centre law)")
    max_ratio = float(carriers.get("max_colour_ratio", 0.25))
    if not (0.0 < max_ratio <= 0.25):
        raise ValueError(f"max_colour_ratio must be in (0, 0.25], got {max_ratio}")
    rules = score.get("envelope_rules") or {}
    min_attack = float(rules.get("min_attack_ms", 20.0)) / 1000.0
    min_release = float(rules.get("min_release_ms", 20.0)) / 1000.0
    events = (score.get("light") or {}).get("events") or []
    if not events:
        raise ValueError("score has no light events")
    for ev in events:
        env = ev.get("env") or []
        start = float(ev["start_s"])
        end = float(ev["end_s"])
        span = end - start
        if span <= 0.0:
            raise ValueError(f"{ev['id']}: invalid duration")
        if abs(float(env[0][0])) > 1e-9 or abs(float(env[0][1])) > 1e-9:
            raise ValueError(f"{ev['id']}: envelope must start at (0, 0) (R5)")
        if abs(float(env[-1][0]) - span) > 1e-6 or abs(float(env[-1][1])) > 1e-9:
            raise ValueError(f"{ev['id']}: envelope must end at (duration, 0) (R4/R5)")
        values = [float(pair[1]) for pair in env]
        peak = max(values)
        if peak <= 0.0:
            raise ValueError(f"{ev['id']}: empty envelope")
        attack = next((float(env[i][0]) for i, v in enumerate(values)
                       if v >= 0.5 * peak), span)
        release = span - float(env[-2][0])
        if attack < min_attack - 1e-9:
            raise ValueError(f"{ev['id']}: attack {attack * 1000:.0f} ms < "
                             f"{min_attack * 1000:.0f} ms (R4)")
        if release < min_release - 1e-9:
            raise ValueError(f"{ev['id']}: release {release * 1000:.0f} ms < "
                             f"{min_release * 1000:.0f} ms (R4)")
        if peak > 1.0 + 1e-9:
            raise ValueError(f"{ev['id']}: envelope above 1.0")
    exposures = score.get("exposures") or DEFAULT_EXPOSURES
    for name, item in exposures.items():
        scale = float(item.get("scale", 0.0))
        if not (0.0 < scale <= 1.0):
            raise ValueError(f"exposure {name}: scale must be in (0, 1]")
        expected = float(item.get("max_colour_ratio", scale * 0.20))
        if abs(expected - scale * 0.20) > 1e-6:
            raise ValueError(f"exposure {name}: max_colour_ratio inconsistent with scale")
    return {"duration": duration, "max_ratio": max_ratio, "exposures": exposures,
            "min_attack": min_attack, "min_release": min_release}


# --------------------------------------------------------------------------- #
# Biquads (RBJ) for programme band-limiting and noise shaping
# --------------------------------------------------------------------------- #
def biquad_coeffs(fs: float, kind: str, f0: float, q: float):
    w0 = TAU * f0 / fs
    cw = math.cos(w0)
    sw = math.sin(w0)
    alpha = sw / (2.0 * q)
    if kind == "lowpass":
        b0 = (1.0 - cw) * 0.5
        b1 = 1.0 - cw
        b2 = b0
    elif kind == "highpass":
        b0 = (1.0 + cw) * 0.5
        b1 = -(1.0 + cw)
        b2 = b0
    elif kind == "bandpass":
        b0 = alpha
        b1 = 0.0
        b2 = -alpha
    else:
        raise ValueError(f"unknown filter type: {kind}")
    a0 = 1.0 + alpha
    a1 = -2.0 * cw
    a2 = 1.0 - alpha
    return (b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0)


def apply_biquad(buf: array.array, coeffs) -> None:
    b0, b1, b2, a1, a2 = coeffs
    z1 = 0.0
    z2 = 0.0
    for i in range(len(buf)):
        x = buf[i]
        y = b0 * x + z1
        z1 = b1 * x - a1 * y + z2
        z2 = b2 * x - a2 * y
        buf[i] = y


def programme_lowpass(buf: array.array, fs: float, cutoff_hz: float, order: int) -> array.array:
    """Butterworth cascade (default: 8th order, 48 dB/oct) on the programme."""
    if cutoff_hz <= 0 or cutoff_hz >= fs * 0.5:
        return buf
    pairs = max(1, order // 2)
    for k in range(1, pairs + 1):
        q = 1.0 / (2.0 * math.sin((2 * k - 1) * math.pi / (2.0 * order)))
        apply_biquad(buf, biquad_coeffs(fs, "lowpass", cutoff_hz, q))
    return buf


# --------------------------------------------------------------------------- #
# Programme synthesis (sparse, synthesised "found" material)
# --------------------------------------------------------------------------- #
def synth_impact(ev: dict, fs: float, seed: int) -> array.array:
    count = int(round(float(ev["duration_s"]) * fs))
    rng = random.Random(stable_seed(seed, ev["id"]))
    env = segments_array(ev["env"], count, fs)
    sweep = ev["sweep"]
    f0 = float(sweep["f_start_hz"])
    f1 = float(sweep["f_end_hz"])
    sweep_s = float(sweep["sweep_s"])
    harmonics = [float(g) for g in sweep["harmonics"]]
    sweep_gain = float(sweep["gain"])
    ratio = f1 / f0
    out = array.array("d", [0.0]) * count
    phase = 0.0
    for j in range(count):
        t = j / fs
        f = f0 * (ratio ** (t / sweep_s)) if t < sweep_s else f1
        phase += TAU * f / fs
        value = harmonics[0] * math.sin(phase)
        for h in range(1, len(harmonics)):
            value += harmonics[h] * math.sin(phase * (h + 1))
        out[j] = sweep_gain * value
    noise_spec = ev.get("noise") or {}
    filters = noise_spec.get("filters") or []
    if filters:
        noise = array.array("d", [rng.gauss(0.0, 1.0) for _ in range(count)])
        for filt in filters:
            apply_biquad(noise, biquad_coeffs(fs, filt["type"], float(filt["hz"]), float(filt["q"])))
        gain = float(noise_spec.get("gain", 0.0))
        for j in range(count):
            out[j] += gain * noise[j]
    peak = max(max(out), -min(out)) or 1.0
    inv = 1.0 / peak
    for j in range(count):
        out[j] = out[j] * inv * env[j]
    return out


def transform_buffer(ev: dict, source: array.array, fs: float) -> array.array:
    """Memory transformation: reverse and lower (pitch_ratio < 1)."""
    tr = ev.get("transform") or {}
    buf = list(source)
    if tr.get("reverse"):
        buf.reverse()
    ratio = float(tr.get("pitch_ratio", 1.0))
    if abs(ratio - 1.0) > 1e-12:
        out_len = max(1, int(round(len(buf) / ratio)))
        out = array.array("d", [0.0]) * out_len
        for i in range(out_len):
            pos = i * ratio
            k = int(pos)
            frac = pos - k
            v0 = buf[k] if k < len(buf) else 0.0
            v1 = buf[k + 1] if (k + 1) < len(buf) else 0.0
            out[i] = v0 + (v1 - v0) * frac
        buf = out
    return buf


def scale_to_peak(buf: array.array, target: float) -> array.array:
    peak = max(max(buf), -min(buf)) or 1.0
    gain = target / peak
    for i in range(len(buf)):
        buf[i] *= gain
    return buf


def render_program(score: dict, fs: float, count: int):
    """Render the programme on its own bus. No carriers, no processing afterwards."""
    left = array.array("d", [0.0]) * count
    right = array.array("d", [0.0]) * count
    cache = {}
    seed = int(score["meta"]["seed"])
    config = score.get("program") or {}
    cutoff = float(config.get("band_limit_hz", 15500.0))
    order = int(config.get("lowpass_order", 8))
    for ev in config.get("events", []):
        kind = ev["type"]
        if kind == "impact":
            mono = synth_impact(ev, fs, seed)
        elif kind == "transform":
            src_id = ev["source"]
            if src_id not in cache:
                raise ValueError(f"transform source {src_id!r} has not been rendered yet")
            mono = transform_buffer(ev, cache[src_id], fs)
        else:
            raise ValueError(f"unknown programme type: {kind}")
        mono = programme_lowpass(mono, fs, cutoff, order)
        mono = scale_to_peak(mono, db_to_lin(float(ev["peak_dbfs"])))
        cache[ev["id"]] = mono
        i0 = int(round(float(ev["start_s"]) * fs))
        gl, gr = pan_equal_power(float(ev.get("pan", 0.0)))
        for j in range(min(len(mono), count - i0)):
            left[i0 + j] += mono[j] * gl
            right[i0 + j] += mono[j] * gr
    return left, right


# --------------------------------------------------------------------------- #
# SpectraStrobe carriers
# --------------------------------------------------------------------------- #
def render_carriers(score: dict, fs: float, count: int, colour_scale: float = 1.0,
                    clamp_log: list | None = None):
    """Render the reference (continuous, sine pan LFO) + colour channels per light grain.

    Safety clamp: every colour channel is limited to
    score["carriers"]["max_colour_ratio"] (0.25) of the reference peak. If the
    score or exposure asks for more, the request is logged in clamp_log and the
    applied level is used.
    """
    car = score["carriers"]
    ref = car["reference"]
    max_ratio = float(car.get("max_colour_ratio", 0.25))
    amp_ref = db_to_lin(float(ref["level_dbfs"]))
    fade = fade_array(count, fs, float(ref["fade_in_ms"]), float(ref["fade_out_ms"]))
    shape = ref.get("pan_lfo_shape", "sine")
    offset = float(ref.get("pan_lfo_offset", 0.0))
    depth = float(ref.get("pan_lfo_depth", 1.0))
    left = array.array("d", [0.0]) * count
    right = array.array("d", [0.0]) * count

    d_ref = TAU * float(ref["hz"]) / fs
    d_lfo = TAU * float(ref["pan_lfo_hz"]) / fs
    sin = math.sin
    cos = math.cos
    phase_ref = 0.0
    phase_lfo = 0.0
    quarter_pi = 0.25 * math.pi
    for i in range(count):
        base = sin(phase_lfo)
        if shape == "square":
            p = offset + depth * (1.0 if base >= 0.0 else -1.0)
        elif shape == "pulse":
            p = offset + depth * (1.0 if base >= 0.0 else -1.0)
        else:
            p = offset + depth * base
        p = -1.0 if p < -1.0 else (1.0 if p > 1.0 else p)
        theta = quarter_pi + quarter_pi * p
        sample = amp_ref * sin(phase_ref) * fade[i]
        left[i] = sample * cos(theta)
        right[i] = sample * sin(theta)
        phase_ref += d_ref
        phase_lfo += d_lfo

    for ev in score["light"]["events"]:
        i0 = int(round(float(ev["start_s"]) * fs))
        dur = float(ev["end_s"]) - float(ev["start_s"])
        nev = int(round(dur * fs))
        if i0 < 0 or i0 >= count or nev <= 0:
            continue
        nev = min(nev, count - i0)
        env = segments_array(ev["env"], nev, fs)
        pan = segments_array(ev["pan"], nev, fs)
        gain_l = array.array("d", [0.0]) * nev
        gain_r = array.array("d", [0.0]) * nev
        for j in range(nev):
            g_l, g_r = pan_linear_0db(pan[j])
            gain_l[j] = g_l
            gain_r[j] = g_r
        for idx, key in enumerate(LIGHT_KEYS):
            requested = float(ev["rgb"][idx]) * colour_scale
            applied = min(requested, max_ratio)
            if requested > max_ratio + 1e-12 and clamp_log is not None:
                entry = {"event": ev["id"], "colour": key,
                         "requested_ratio": round(requested, 6),
                         "applied_ratio": round(applied, 6),
                         "clamp_ratio": max_ratio}
                if entry not in clamp_log:
                    clamp_log.append(entry)
            if applied <= 0.0:
                continue
            spec = car["colours"][key]
            amp = amp_ref * applied
            d_col = TAU * float(spec["hz"]) / fs
            phase = float(spec.get("phase_rad", 0.0))
            for j in range(nev):
                s = amp * env[j] * sin(phase) * fade[i0 + j]
                left[i0 + j] += s * gain_l[j]
                right[i0 + j] += s * gain_r[j]
                phase += d_col
    return left, right


def render_mix(score: dict, fs: float, with_programme: bool = True,
               colour_scale: float = 1.0, clamp_log: list | None = None):
    """Return ((mix_l, mix_r), (prog_l, prog_r)) for the requested rate."""
    count = int(round(float(score["meta"]["duration_s"]) * fs))
    if with_programme:
        prog_l, prog_r = render_program(score, fs, count)
    else:
        prog_l = array.array("d", [0.0]) * count
        prog_r = array.array("d", [0.0]) * count
    car_l, car_r = render_carriers(score, fs, count, colour_scale, clamp_log)
    mix_l = array.array("d", [0.0]) * count
    mix_r = array.array("d", [0.0]) * count
    for i in range(count):
        mix_l[i] = prog_l[i] + car_l[i]
        mix_r[i] = prog_r[i] + car_r[i]
    return (mix_l, mix_r), (prog_l, prog_r)


# --------------------------------------------------------------------------- #
# WAV export (PCM, stereo, no metadata, no dither)
# --------------------------------------------------------------------------- #
def write_pcm(path, fs: int, bits: int, left, right) -> float:
    """Write a 16/24-bit PCM WAV and return the sample peak in dBFS."""
    count = len(left)
    peak = max(max(left), -min(left), max(right), -min(right))
    if bits == 24:
        scale = 8388607.0
        payload = bytearray(count * 6)
        j = 0
        for i in range(count):
            v = int(round(left[i] * scale))
            v = 8388607 if v > 8388607 else (-8388608 if v < -8388608 else v)
            payload[j] = v & 0xFF
            payload[j + 1] = (v >> 8) & 0xFF
            payload[j + 2] = (v >> 16) & 0xFF
            v = int(round(right[i] * scale))
            v = 8388607 if v > 8388607 else (-8388608 if v < -8388608 else v)
            payload[j + 3] = v & 0xFF
            payload[j + 4] = (v >> 8) & 0xFF
            payload[j + 5] = (v >> 16) & 0xFF
            j += 6
    elif bits == 16:
        payload = bytearray(count * 4)
        j = 0
        for i in range(count):
            v = int(round(left[i] * 32767.0))
            v = 32767 if v > 32767 else (-32768 if v < -32768 else v)
            payload[j] = v & 0xFF
            payload[j + 1] = (v >> 8) & 0xFF
            v = int(round(right[i] * 32767.0))
            v = 32767 if v > 32767 else (-32768 if v < -32768 else v)
            payload[j + 2] = v & 0xFF
            payload[j + 3] = (v >> 8) & 0xFF
            j += 4
    else:
        raise ValueError("16- or 24-bit PCM only")
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(2)
        handle.setsampwidth(bits // 8)
        handle.setframerate(fs)
        handle.writeframes(bytes(payload))
    return lin_to_db(peak)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


# --------------------------------------------------------------------------- #
# events.csv and timeline.svg
# --------------------------------------------------------------------------- #
def _transform_duration(score: dict, ev: dict) -> float:
    source = next(e for e in score["program"]["events"] if e["id"] == ev["source"])
    ratio = float((ev.get("transform") or {}).get("pitch_ratio", 1.0))
    return float(source["duration_s"]) / ratio


def build_events_rows(score: dict):
    rows = []
    for ev in score["light"]["events"]:
        rgb = ev["rgb"]
        peak_dbfs = lin_to_db(max(rgb) * db_to_lin(float(score["carriers"]["reference"]["level_dbfs"])))
        pans = [float(p) for _, p in ev["pan"]]
        rows.append({
            "id": ev["id"], "domein": "licht", "rol": ev["role"],
            "start_s": f"{float(ev['start_s']):.3f}", "einde_s": f"{float(ev['end_s']):.3f}",
            "duur_s": f"{float(ev['end_s']) - float(ev['start_s']):.3f}",
            "piek_dbfs": f"{peak_dbfs:.1f}",
            "rgb": "/".join(f"{c:.5f}" for c in rgb),
            "pan_van": f"{pans[0]:.2f}", "pan_naar": f"{pans[-1]:.2f}",
            "relatie": ev.get("relation", ""),
        })
    for ev in score["program"]["events"]:
        dur = _transform_duration(score, ev) if ev["type"] == "transform" else float(ev["duration_s"])
        start = float(ev["start_s"])
        rows.append({
            "id": ev["id"], "domein": "audio", "rol": ev["role"],
            "start_s": f"{start:.3f}", "einde_s": f"{start + dur:.3f}",
            "duur_s": f"{dur:.3f}", "piek_dbfs": f"{float(ev['peak_dbfs']):.1f}",
            "rgb": "", "pan_van": f"{float(ev.get('pan', 0.0)):.2f}",
            "pan_naar": f"{float(ev.get('pan', 0.0)):.2f}",
            "relatie": ev.get("relation", ""),
        })
    rows.sort(key=lambda r: (float(r["start_s"]), 0 if r["domein"] == "licht" else 1))
    return rows


def write_events_csv(path: Path, score: dict) -> None:
    # CSV schema (column names and domain values) is kept stable for compatibility.
    fields = ["id", "domein", "rol", "start_s", "einde_s", "duur_s",
              "piek_dbfs", "rgb", "pan_van", "pan_naar", "relatie"]
    with open(path, "w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in build_events_rows(score):
            writer.writerow(row)


def _hex_colour(rgb) -> str:
    scale = 1.0 / 0.25
    parts = [f"{max(0, min(255, int(round(255.0 * min(1.0, c * scale))))):02x}" for c in rgb]
    return "#" + "".join(parts)


def _short_role(role: str, limit: int = 30) -> str:
    text = str(role).split(":")[0].split(";")[0].strip()
    return text[:limit] + ("…" if len(text) > limit else "")


def write_timeline_svg(path: Path, score: dict) -> None:
    """Timeline fully derived from the score (sections, counts, labels, relations)."""
    meta = score["meta"]
    duration = float(meta["duration_s"])
    sections = meta.get("sections") or [{"t0": 0.0, "t1": duration, "label": "piece"}]
    width, height = 1280, 500
    left, right = 150, 1230
    lane_light, lane_audio, lane_ref = 120, 235, 350
    bar_h = 52
    light_events = score["light"]["events"]
    audio_events = score["program"]["events"]
    exposures = score.get("exposures") or DEFAULT_EXPOSURES

    def x(t: float) -> float:
        return left + (right - left) * max(0.0, min(1.0, t / duration))

    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
           f'viewBox="0 0 {width} {height}" font-family="Helvetica, Arial, sans-serif">',
           f'<rect width="{width}" height="{height}" fill="#f7f5f0"/>',
           f'<text x="{left}" y="34" font-size="19" font-weight="bold" fill="#1c1a17">'
           f'{EXPERIMENT_TITLE}</text>',
           f'<text x="{left}" y="56" font-size="12" fill="#5a544c">'
           f'seed {meta["seed"]} · {duration:.1f} s · {len(light_events)} light events · '
           f'{len(audio_events)} audio events · exposures: '
           + " / ".join(f'{item["label"]}' for item in exposures.values()) + '</text>',
           f'<text x="{left}" y="74" font-size="11" fill="#8a2f1f">'
           f'EXPERIMENT — not part of NAGLOED v1.0.4</text>']
    for t0, t1, label in [(float(s["t0"]), float(s["t1"]), s["label"]) for s in sections]:
        out.append(f'<rect x="{x(t0):.1f}" y="88" width="{x(t1) - x(t0):.1f}" height="22" '
                   f'fill="#e7e1d6" stroke="#d4ccbe"/>')
        out.append(f'<text x="{x(t0) + 6:.1f}" y="104" font-size="10" fill="#6b645a">{label}</text>')
    step = 5.0 if duration <= 90.0 else 10.0
    t = 0.0
    while t <= duration + 0.01:
        out.append(f'<line x1="{x(t):.1f}" y1="88" x2="{x(t):.1f}" y2="410" '
                   f'stroke="#d9d2c6" stroke-width="1"/>')
        out.append(f'<text x="{x(t):.1f}" y="428" font-size="10" fill="#6b645a" '
                   f'text-anchor="middle">{t:.0f}</text>')
        t += step
    out.append(f'<text x="{right}" y="448" font-size="10" fill="#6b645a" '
               f'text-anchor="end">time (s)</text>')
    for y, label in ((lane_light, "LIGHT"), (lane_audio, "AUDIO"), (lane_ref, "CARRIER 18.2 kHz")):
        out.append(f'<text x="{left - 12}" y="{y + 32}" font-size="11" font-weight="bold" '
                   f'fill="#3e3a34" text-anchor="end">{label}</text>')
        out.append(f'<line x1="{left}" y1="{y + 26}" x2="{right}" y2="{y + 26}" stroke="#c9c1b4"/>')

    for ev in light_events:
        x0, x1 = x(float(ev["start_s"])), x(float(ev["end_s"]))
        out.append(f'<rect x="{x0:.1f}" y="{lane_light}" width="{max(3.0, x1 - x0):.1f}" '
                   f'height="{bar_h}" rx="3" fill="{_hex_colour(ev["rgb"])}" '
                   f'stroke="#3a352e" stroke-width="0.7"/>')
        out.append(f'<text x="{x0 + 4:.1f}" y="{lane_light + 18}" font-size="10" fill="#1c1a17">'
                   f'{ev["id"]} · {_short_role(ev["role"], 26)}</text>')
        out.append(f'<text x="{x1:.1f}" y="{lane_light + 45}" font-size="9" fill="#5a544c" '
                   f'text-anchor="end">{float(ev["start_s"]):.1f}–{float(ev["end_s"]):.1f} s</text>')
    for ev in audio_events:
        start = float(ev["start_s"])
        dur = _transform_duration(score, ev) if ev["type"] == "transform" else float(ev["duration_s"])
        x0, x1 = x(start), x(start + dur)
        out.append(f'<rect x="{x0:.1f}" y="{lane_audio}" width="{max(3.0, x1 - x0):.1f}" '
                   f'height="{bar_h}" rx="3" fill="#3b4a52" stroke="#263038" stroke-width="0.7"/>')
        out.append(f'<text x="{x0 + 4:.1f}" y="{lane_audio + 18}" font-size="10" fill="#f2efe8">'
                   f'{ev["id"]} · {_short_role(ev["role"], 28)}</text>')
        out.append(f'<text x="{x0 + 4:.1f}" y="{lane_audio + 34}" font-size="9" fill="#cfd8dc">'
                   f'{float(ev["peak_dbfs"]):.0f} dBFS</text>')
    out.append(f'<rect x="{left}" y="{lane_ref}" width="{right - left}" height="{bar_h}" rx="3" '
               f'fill="#6d4a2f" stroke="#3a2818" stroke-width="0.7"/>')
    out.append(f'<text x="{left + 8}" y="{lane_ref + 20}" font-size="10" fill="#f6efe6">'
               f'{float(score["carriers"]["reference"]["hz"]):.0f} Hz · '
               f'{float(score["carriers"]["reference"]["level_dbfs"]):+.0f} dBFS · '
               f'{float(score["carriers"]["reference"]["pan_lfo_hz"]):g} Hz sine pan LFO · continuous</text>')
    out.append(f'<text x="{left + 8}" y="{lane_ref + 37}" font-size="9" fill="#e2d3c2">'
               f'colours 18.700 R · 19.200 G · 19.700 B follow the light traces; '
               f'ceiling 25% per carrier</text>')

    # Relation annotations, derived from the score (no fixed ids/times).
    def start_end(ev):
        start = float(ev["start_s"])
        if "end_s" in ev:
            return start, float(ev["end_s"])
        dur = (_transform_duration(score, ev) if ev["type"] == "transform"
               else float(ev["duration_s"]))
        return start, start + dur

    by_id = {ev["id"]: ev for ev in light_events + audio_events}
    for rel in score.get("relations", []):
        to_ids = rel.get("to") or []
        from_ids = rel.get("from") or []
        if not to_ids:
            if from_ids and from_ids[0] in by_id:
                ev = by_id[from_ids[0]]
                out.append(f'<text x="{x(float(ev["start_s"])) + 4:.1f}" y="{lane_light - 8}" '
                           f'font-size="9" fill="#6b645a">no answer</text>')
            continue
        if not from_ids or from_ids[0] not in by_id or to_ids[0] not in by_id:
            continue
        src, dst = by_id[from_ids[0]], by_id[to_ids[0]]
        _, src_end = start_end(src)
        dst_start, _ = start_end(dst)
        delta = dst_start - src_end
        if 0.0 < delta < 10.0:
            label = f"{delta * 1000:.0f} ms"
        elif delta >= 10.0:
            label = f"{delta:.0f} s later"
        else:
            continue
        out.append(f'<text x="{x(dst_start):.1f}" y="{lane_audio - 8}" '
                   f'font-size="9" fill="#8a2f1f" text-anchor="middle">{label}</text>')
    out.append(f'<text x="{left}" y="{height - 18}" font-size="10" fill="#6b645a">'
               f'Use: ./run.sh · report in out/verify-report.md · '
               f'do not use on hardware without HARDWARE-PROTOCOL.md</text>')
    out.append("</svg>")
    path.write_text("\n".join(out) + "\n", encoding="utf-8")


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #
def load_score(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _exposure_filename(rate: int, bits: int, exposure: str) -> str:
    tag = "" if exposure == "full" else f"-{exposure}"
    rate_tag = "48k-24bit" if rate == 48000 else "44k1-16bit"
    return f"spectrastrobe{tag}-{rate_tag}.wav"


def _program_filename(rate: int) -> str:
    rate_tag = "48k-24bit" if rate == 48000 else "44k1-16bit"
    return f"program-only-{rate_tag}.wav"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="NAGLOED v2 SpectraStrobe PoC renderer (EXPERIMENT)")
    parser.add_argument("--score", default=str(POC_DIR / "score.json"))
    parser.add_argument("--out-dir", default=str(POC_DIR / "out"))
    parser.add_argument("--colour-scale", type=float, default=None,
                        help="render only this exposure (e.g. 0.3125 = 6.25%% max); "
                             "by default full/mid/dim are rendered")
    parser.add_argument("--pan-lfo-hz", type=float, default=None,
                        help="corruption tests only: override the reference pan LFO")
    parser.add_argument("--pan-lfo-shape", default=None,
                        help="corruption tests only: sine/square/pulse")
    parser.add_argument("--carriers-only", action="store_true",
                        help="corruption tests only: skip the programme")
    args = parser.parse_args(argv)

    score_path = Path(args.score)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    score = load_score(score_path)
    if args.pan_lfo_hz is not None:
        score["carriers"]["reference"]["pan_lfo_hz"] = args.pan_lfo_hz
    if args.pan_lfo_shape is not None:
        score["carriers"]["reference"]["pan_lfo_shape"] = args.pan_lfo_shape
    config = validate_score(score)
    exposures = config["exposures"]
    full_scale = float(exposures["full"]["scale"])
    if args.colour_scale is not None:
        full_scale = float(args.colour_scale)
        if not (0.0 < full_scale <= 1.0):
            raise ValueError("--colour-scale must be in (0, 1]")
        exposures = {"custom": {"scale": full_scale,
                                "max_colour_ratio": full_scale * 0.20,
                                "label": f"custom {full_scale * 0.20 * 100:.2f}%"}}
    clamp_log = []
    outputs = {}

    def render_to(name, rate, bits, scale):
        (mix, _) = render_mix(score, rate, with_programme=not args.carriers_only,
                              colour_scale=scale, clamp_log=clamp_log)
        path = out_dir / name
        peak = write_pcm(path, rate, bits, mix[0], mix[1])
        outputs[name] = {"rate_hz": rate, "bit_depth": bits, "peak_dbfs": round(peak, 2)}

    # Technical reference and recommended playback version: full score.
    render_to(_exposure_filename(48000, 24, "full"), 48000, 24, full_scale)
    render_to(_exposure_filename(44100, 16, "full"), 44100, 16, full_scale)
    # Brightness ladder for hardware, in the recommended 44.1 kHz/16-bit format.
    if args.colour_scale is None:
        for exposure in ("mid", "dim"):
            if exposure in exposures:
                render_to(_exposure_filename(44100, 16, exposure), 44100, 16,
                          float(exposures[exposure]["scale"]))
    if not args.carriers_only:
        for rate, bits in ((48000, 24), (44100, 16)):
            program = render_program_pair(score, rate)
            name = _program_filename(rate)
            path = out_dir / name
            peak = write_pcm(path, rate, bits, program[0], program[1])
            outputs[name] = {"rate_hz": rate, "bit_depth": bits, "peak_dbfs": round(peak, 2)}

    write_events_csv(out_dir / "events.csv", score)
    write_timeline_svg(out_dir / "timeline.svg", score)

    car = score["carriers"]
    requested_max = max(max(float(c) for c in ev["rgb"]) for ev in score["light"]["events"])
    clamped = bool(clamp_log)
    if clamped:
        print("WARNING: colour ceiling active (max 25% of the reference peak).", file=sys.stderr)
        for entry in clamp_log:
            print(f"  {entry['event']}.{entry['colour']}: requested "
                  f"{entry['requested_ratio'] * 100:.1f}%, applied "
                  f"{entry['applied_ratio'] * 100:.1f}%", file=sys.stderr)
    manifest = {
        "generator": "render.py",
        "experiment": EXPERIMENT_TITLE,
        "poc_version": POC_VERSION,
        "version": score["meta"]["version"],
        "score_file": score_path.name,
        "score_sha256": sha256_file(score_path),
        "scripts": {
            "render.py": sha256_file(POC_DIR / "render.py"),
            "verify.py": sha256_file(POC_DIR / "verify.py") if (POC_DIR / "verify.py").exists() else "",
        },
        "seed": score["meta"]["seed"],
        "duration_s": config["duration"],
        "carriers": {
            "reference_hz": car["reference"]["hz"],
            "reference_dbfs": car["reference"]["level_dbfs"],
            "pan_lfo_hz": car["reference"]["pan_lfo_hz"],
            "pan_lfo_shape": car["reference"]["pan_lfo_shape"],
            "pan_lfo_offset": car["reference"].get("pan_lfo_offset", 0.0),
            "colours_hz": {k: v["hz"] for k, v in car["colours"].items()},
            "colour_pan_law": car.get("colour_pan_law"),
            "max_colour_ratio": car["max_colour_ratio"],
        },
        "safety": {
            "colour_clamp_ratio": float(car["max_colour_ratio"]),
            "requested_max_colour_ratio": round(requested_max, 6),
            "clamped": clamped,
            "clamped_occurrences": clamp_log,
            "peak_target_dbfs": float(score["safety"]["target_true_peak_dbfs"]),
            "stop": score["safety"]["stop"],
            "exposures": {name: {"scale": float(item["scale"]),
                                 "max_colour_ratio": float(item["max_colour_ratio"]),
                                 "label": item["label"]}
                          for name, item in exposures.items()},
        },
        "outputs": {},
    }
    for name, info in outputs.items():
        path = out_dir / name
        info = dict(info)
        info["sha256"] = sha256_file(path)
        info["bytes"] = path.stat().st_size
        manifest["outputs"][name] = info
    with open(out_dir / "render-manifest.json", "w", encoding="utf-8", newline="\n") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=True, ensure_ascii=False)
        handle.write("\n")

    light_count = len(score["light"]["events"])
    audio_count = len(score["program"]["events"])
    audio_seconds = sum(
        (_transform_duration(score, ev) if ev["type"] == "transform" else float(ev["duration_s"]))
        for ev in score["program"]["events"])
    print("NAGLOED v2 — SpectraStrobe PoC render (EXPERIMENT, not v1.0.4)")
    print(f"score         : {score_path.name} v{score['meta']['version']}  "
          f"seed {score['meta']['seed']}  duration {config['duration']:.1f} s")
    for name, info in outputs.items():
        print(f"  {name:<36} {info['peak_dbfs']:+6.2f} dBFS  "
              f"({info['rate_hz']} Hz/{info['bit_depth']}-bit)")
    print(f"carriers      : 18200.0 Hz reference {car['reference']['level_dbfs']:+.1f} dBFS "
          f"(pan LFO {car['reference']['pan_lfo_hz']:.1f} Hz, "
          f"{car['reference']['pan_lfo_shape']}, offset "
          f"{car['reference'].get('pan_lfo_offset', 0.0):g})")
    print(f"colour ceiling: max {float(car['max_colour_ratio']) * 100:.0f}% per carrier; "
          f"score requests max {requested_max * 100:.1f}%"
          + (" — CLAMPED" if clamped else " (within ceiling)"))
    for name, item in exposures.items():
        print(f"  exposure {name:<5}: scale {item['scale']:.4f} → max "
              f"{item['max_colour_ratio'] * 100:.2f}% ({item['label']})")
    print(f"events        : {light_count} light, {audio_count} audio; "
          f"programme {audio_seconds:.1f} s of {config['duration']:.1f} s "
          f"({100.0 * (1.0 - audio_seconds / config['duration']):.0f}% silence)")
    print(f"output        : {out_dir.resolve()}")
    print("  - events.csv, timeline.svg, render-manifest.json")
    return 0


def render_program_pair(score: dict, rate: int):
    """Programme-only pair (L, R) for the A/B render."""
    count = int(round(float(score["meta"]["duration_s"]) * rate))
    return render_program(score, rate, count)


if __name__ == "__main__":
    sys.exit(main())
