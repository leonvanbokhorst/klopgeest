#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""NAGLOED v2 — SpectraStrobe proof-of-concept: pure-Python verification.

EXPERIMENT. Not part of NAGLOED v1.0.4. Python stdlib only (no numpy).
Analyses the WAVs in out/ against measurement targets M1–M10 from
docs/v2/SPECTRASTROBE.md, the safety ceiling (R3), the brightness ladder and
the advisory corruption matrix (M14). Writes verify-report.json and
verify-report.md.

Honest scope: this verifies the ENCODER and signal integrity. There is no
event engine, memory or budget engine yet (that is phase 2 / the A1 signal
half of the experiment).

Usage:
    python3 verify.py [--out-dir out] [--score score.json]
                      [--skip-corruption] [--strict-corruption]

Exit code 0 = all core checks PASS. Exit code 1 = a core check FAIL (or, with
--strict-corruption, a corruption row with status ADVISORY-FAIL).
"""

from __future__ import annotations

import argparse
import array
import copy
import hashlib
import json
import math
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

TAU = 2.0 * math.pi
FFT_N = 65536
POC_VERSION = "0.2.0"
POC_DIR = Path(__file__).resolve().parent
EXPERIMENT = "Eerste Gloed — SpectraStrobe proof-of-concept (EXPERIMENT)"
WARNING = ("EXPERIMENT — not part of NAGLOED v1.0.4, no DSP contract, no integration. "
           "Encoder/signal integrity only, offline; hardware is not available. "
           "No medical or wellness claims.")

GROUP_LABELS = {
    "F": "Format",
    "S": "Score & safety rules",
    "SA": "Safety clamp (adversarial)",
    "L": "Brightness ladder",
    "M1": "M1 carrier frequencies",
    "M2": "M2 reference level (peak amplitude)",
    "M3": "M3 relative channel amplitude",
    "M4": "M4 L/R balance and isolation",
    "M5": "M5 reference pan modulation",
    "M6": "M6 inter-carrier isolation",
    "M7": "M7 programme leakage into the control band",
    "M8": "M8 noise floor with colours off",
    "M9": "M9 peak and true peak",
    "M10": "M10 determinism and manifest",
    "M14": "M14 corruption matrix",
}


# --------------------------------------------------------------------------- #
# Reading and basic operations
# --------------------------------------------------------------------------- #
def read_wav(path: Path):
    with wave.open(str(path), "rb") as handle:
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        fs = handle.getframerate()
        frames = handle.getnframes()
        raw = handle.readframes(frames)
    if channels != 2:
        raise ValueError(f"{path.name}: expected stereo, got {channels} channels")
    left = array.array("d", [0.0]) * frames
    right = array.array("d", [0.0]) * frames
    if width == 2:
        pcm = array.array("h")
        pcm.frombytes(raw)
        scale = 1.0 / 32768.0
        for i in range(frames):
            left[i] = pcm[2 * i] * scale
            right[i] = pcm[2 * i + 1] * scale
    elif width == 3:
        scale = 1.0 / 8388608.0
        for i in range(frames):
            j = 6 * i
            v = raw[j] | (raw[j + 1] << 8) | (raw[j + 2] << 16)
            if v & 0x800000:
                v -= 0x1000000
            left[i] = v * scale
            v = raw[j + 3] | (raw[j + 4] << 8) | (raw[j + 5] << 16)
            if v & 0x800000:
                v -= 0x1000000
            right[i] = v * scale
    else:
        raise ValueError(f"{path.name}: only 16/24-bit PCM")
    return fs, width, left, right


def db(x: float) -> float:
    return 10.0 * math.log10(x) if x > 0.0 else float("-inf")


def amp_db(x: float) -> float:
    return 20.0 * math.log10(x) if x > 0.0 else float("-inf")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def render_module():
    if str(POC_DIR) not in sys.path:
        sys.path.insert(0, str(POC_DIR))
    import render as poc_render  # noqa: PLC0415 - deliberately lazy (own code)
    return poc_render


# --------------------------------------------------------------------------- #
# DFT/FFT (radix-2, no numpy) and spectral measurements
# --------------------------------------------------------------------------- #
_WINDOW_CACHE = {}


def hann_window(n: int):
    cached = _WINDOW_CACHE.get(n)
    if cached is None:
        cached = [0.5 - 0.5 * math.cos(TAU * i / (n - 1)) for i in range(n)]
        _WINDOW_CACHE[n] = cached
    return cached


def _fft(x) -> None:
    n = len(x)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            x[i], x[j] = x[j], x[i]
    length = 2
    while length <= n:
        ang = -TAU / length
        wl = complex(math.cos(ang), math.sin(ang))
        half = length >> 1
        for start in range(0, n, length):
            w = 1.0 + 0.0j
            for k in range(start, start + half):
                u = x[k]
                v = x[k + half] * w
                x[k] = u + v
                x[k + half] = u - v
                w *= wl
        length <<= 1


def spectrum_pair(left, right, fs: float, t0: float, n: int = FFT_N):
    """Hann-weighted DFT of L and R in one complex FFT (L + jR)."""
    i0 = max(0, int(round(t0 * fs)))
    window = hann_window(n)
    count = len(left)
    x = [0.0j] * n
    for i in range(n):
        if i0 + i < count:
            w = window[i]
            x[i] = complex(left[i0 + i] * w, right[i0 + i] * w)
    _fft(x)
    half = n // 2
    spec_left = [0.0] * (half + 1)
    spec_right = [0.0] * (half + 1)
    for k in range(half + 1):
        a = x[k]
        b = x[(n - k) % n].conjugate()
        xl = (a + b) * 0.5
        xr = (a - b) * (-0.5j)
        spec_left[k] = xl.real * xl.real + xl.imag * xl.imag
        spec_right[k] = xr.real * xr.real + xr.imag * xr.imag
    sum_w2 = sum(w * w for w in window)
    return spec_left, spec_right, sum_w2


def peak_frequency(spec_power, fs: float, n: int, nominal: float, span_hz: float = 80.0):
    bin_hz = fs / n
    k_lo = max(1, int((nominal - span_hz) / bin_hz))
    k_hi = min(len(spec_power) - 2, int((nominal + span_hz) / bin_hz) + 2)
    if k_hi <= k_lo:
        return float("nan")
    k_peak = max(range(k_lo, k_hi), key=lambda k: spec_power[k])
    y0 = math.sqrt(spec_power[k_peak - 1])
    y1 = math.sqrt(spec_power[k_peak])
    y2 = math.sqrt(spec_power[k_peak + 1])
    denominator = y0 - 2.0 * y1 + y2
    delta = 0.5 * (y0 - y2) / denominator if abs(denominator) > 1e-30 else 0.0
    delta = -0.5 if delta < -0.5 else (0.5 if delta > 0.5 else delta)
    return (k_peak + delta) * bin_hz


def band_rms_sq(spec_power, fs: float, n: int, sum_w2: float, f_lo: float, f_hi: float) -> float:
    bin_hz = fs / n
    k_lo = max(0, int(math.ceil(f_lo / bin_hz)))
    k_hi = min(len(spec_power) - 1, int(math.floor(f_hi / bin_hz)))
    total = 0.0
    for k in range(k_lo, k_hi + 1):
        total += spec_power[k]
    return 2.0 * total / (n * sum_w2) if sum_w2 > 0 else 0.0


def band_rms_dbfs(spec_power, fs: float, n: int, sum_w2: float, f_lo: float, f_hi: float) -> float:
    return db(band_rms_sq(spec_power, fs, n, sum_w2, f_lo, f_hi))


def combined_power(spec_left, spec_right):
    return [spec_left[k] + spec_right[k] for k in range(len(spec_left))]


# --------------------------------------------------------------------------- #
# Complex demodulation (single-bin DFT)
# --------------------------------------------------------------------------- #
def demod_envelopes(left, right, fs: float, freq: float, t0: float, t1: float, block_s: float,
                    windowed: bool = False):
    """Per block: carrier amplitude in L and R (2|Z|/sum(window)).

    windowed=True applies a Hann window per block. That suppresses leakage from
    the 18.2 kHz reference (500 Hz away) into a colour band; needed for
    accurate colour/brightness ratios. Frequency and LFO measurements keep
    using the rectangular window.
    """
    i0 = max(0, int(round(t0 * fs)))
    i1 = min(len(left), int(round(t1 * fs)))
    block = max(1, int(round(block_s * fs)))
    weights = hann_window(block) if windowed else None
    normaliser = sum(weights) if windowed else float(block)
    env_l = []
    env_r = []
    for start in range(i0, i1 - block + 1, block):
        re_l = im_l = re_r = im_r = 0.0
        angle = TAU * freq * start / fs
        step = TAU * freq / fs
        for offset in range(block):
            n = start + offset
            weight = weights[offset] if windowed else 1.0
            c = math.cos(angle)
            s = math.sin(angle)
            lv = left[n] * weight
            rv = right[n] * weight
            re_l += lv * c
            im_l -= lv * s
            re_r += rv * c
            im_r -= rv * s
            angle += step
        factor = 2.0 / normaliser
        env_l.append(factor * math.hypot(re_l, im_l))
        env_r.append(factor * math.hypot(re_r, im_r))
    return env_l, env_r


def estimate_frequency(left, right, fs: float, nominal: float, t0: float, t1: float,
                       block_s: float = 0.02) -> float:
    i0 = max(0, int(round(t0 * fs)))
    i1 = min(len(left), int(round(t1 * fs)))
    block = max(1, int(round(block_s * fs)))
    if i1 - i0 < 4 * block:
        return float("nan")
    phases = []
    for start in range(i0, i1 - block + 1, block):
        total = 0.0 + 0.0j
        angle = TAU * nominal * start / fs
        step = TAU * nominal / fs
        for n in range(start, start + block):
            total += (left[n] + right[n]) * complex(math.cos(angle), -math.sin(angle))
            angle += step
        phases.append(math.atan2(total.imag, total.real))
    unwrapped = [phases[0]]
    for value in phases[1:]:
        delta = value - unwrapped[-1]
        while delta > math.pi:
            delta -= TAU
        while delta < -math.pi:
            delta += TAU
        unwrapped.append(unwrapped[-1] + delta)
    m = len(unwrapped)
    sum_x = m * (m - 1) / 2.0
    sum_xx = sum(i * i for i in range(m))
    sum_y = sum(unwrapped)
    sum_xy = sum(i * v for i, v in enumerate(unwrapped))
    slope = (m * sum_xy - sum_x * sum_y) / (m * sum_xx - sum_x * sum_x)
    return nominal + slope * fs / (TAU * block)


def goertzel_amplitude(sequence, rate_hz: float, freq: float) -> float:
    n = len(sequence)
    if n == 0:
        return 0.0
    w = TAU * freq / rate_hz
    coeff = 2.0 * math.cos(w)
    s1 = 0.0
    s2 = 0.0
    for value in sequence:
        s0 = value + coeff * s1 - s2
        s2 = s1
        s1 = s0
    power = s1 * s1 + s2 * s2 - coeff * s1 * s2
    return 2.0 * math.sqrt(max(power, 0.0)) / n


def pearson(a, b) -> float:
    n = min(len(a), len(b))
    if n < 2:
        return 0.0
    mean_a = sum(a[:n]) / n
    mean_b = sum(b[:n]) / n
    num = sum((a[i] - mean_a) * (b[i] - mean_b) for i in range(n))
    den_a = math.sqrt(sum((v - mean_a) ** 2 for v in a[:n]))
    den_b = math.sqrt(sum((v - mean_b) ** 2 for v in b[:n]))
    return num / (den_a * den_b) if den_a * den_b > 0.0 else 0.0


def lfo_metrics(left, right, fs: float, t0: float, t1: float,
                search_center_hz: float = 20.0):
    """Measure the reference panning from the power balance of the 18.2 kHz band.

    The amplitude envelope per block is linear in the panning; the power
    (env^2) is flat (quadratic) at the extreme positions, so a block average
    does not underestimate the depth. The encoder pans hard per sample; the
    analysis uses 1 ms blocks (measurement convention).
    """
    env_l, env_r = demod_envelopes(left, right, fs, 18200.0, t0, t1, 0.001)
    n = min(len(env_l), len(env_r))
    if n < 8:
        return {"freq_hz": float("nan"), "depth_pct": 0.0, "corr": 0.0,
                "rest_db": -999.0, "amplitude": 0.0}
    power_l = [value * value for value in env_l]
    power_r = [value * value for value in env_r]
    balance = [(power_l[i] - power_r[i]) / (power_l[i] + power_r[i] + 1e-30)
               for i in range(n)]
    depth = 0.5 * (max(balance) - min(balance))
    q_max = min(max(abs(min(balance)), abs(max(balance))), 1.0 - 1e-12)
    rest = 10.0 * math.log10((1.0 - q_max) / (1.0 + q_max))
    block_rate = n / (t1 - t0)
    search_lo = max(0.5, search_center_hz - 2.0)
    search_hi = search_center_hz + 2.0
    steps = int(round((search_hi - search_lo) / 0.02)) + 1
    best_freq = float("nan")
    best_amp = -1.0
    for step in range(steps):
        freq = search_lo + 0.02 * step
        amp = goertzel_amplitude(balance, block_rate, freq)
        if amp > best_amp:
            best_amp = amp
            best_freq = freq
    return {"freq_hz": best_freq, "depth_pct": 100.0 * depth,
            "corr": pearson(power_l, power_r), "rest_db": rest,
            "amplitude": best_amp}


# --------------------------------------------------------------------------- #
# Peak and true peak (4x oversampled, local windowed sinc)
# --------------------------------------------------------------------------- #
def _sinc(x: float) -> float:
    if abs(x) < 1e-12:
        return 1.0
    return math.sin(math.pi * x) / (math.pi * x)


def true_peak(left, right, fs: float) -> float:
    peak = max(max(left), -min(left), max(right), -min(right))
    if peak <= 0.0:
        return 0.0
    threshold = peak * 0.5
    candidates = []
    for channel in (left, right):
        for i, value in enumerate(channel):
            if abs(value) > threshold:
                candidates.append((abs(value), channel, i))
    candidates.sort(reverse=True, key=lambda item: item[0])
    candidates = candidates[:4000]
    best = peak
    taps = 12
    for _, channel, index in candidates:
        for phase in range(1, 4):
            t = index + phase / 4.0
            acc = 0.0
            for k in range(index - taps, index + taps + 1):
                if 0 <= k < len(channel):
                    x = t - k
                    window = 0.5 + 0.5 * math.cos(math.pi * x / (taps + 1)) if abs(x) <= taps + 1 else 0.0
                    acc += channel[k] * _sinc(x) * window
            if abs(acc) > best:
                best = abs(acc)
    return best


# --------------------------------------------------------------------------- #
# Score helpers (independent implementation of the score semantics)
# --------------------------------------------------------------------------- #
def smoothstep(u: float) -> float:
    u = 0.0 if u < 0.0 else (1.0 if u > 1.0 else u)
    return u * u * (3.0 - 2.0 * u)


def points_value(points, t: float) -> float:
    pts = [(float(a), float(b)) for a, b in points]
    if t <= pts[0][0]:
        return pts[0][1]
    for (t0, v0), (t1, v1) in zip(pts, pts[1:]):
        if t <= t1:
            return v0 + (v1 - v0) * smoothstep((t - t0) / (t1 - t0)) if t1 > t0 else v1
    return pts[-1][1]


def expected_colour_ratio(score: dict, event: dict, t_abs: float, colour_index: int):
    start = float(event["start_s"])
    t_rel = t_abs - start
    env = points_value(event["env"], t_rel)
    pan = points_value(event["pan"], t_rel)
    brightness = float(event["rgb"][colour_index]) * env
    return brightness * (1.0 - max(0.0, pan)), brightness * (1.0 + min(0.0, pan))


def event_metrics(ev: dict) -> dict:
    """Attack/release follow time from the score (same rules as R4/R5)."""
    span = float(ev["end_s"]) - float(ev["start_s"])
    env = [(float(t), float(v)) for t, v in ev["env"]]
    peak = max(v for _, v in env)
    attack = next((t for t, v in env if v >= 0.5 * peak), span)
    release = span - env[-2][0]
    return {"span_s": span, "attack_ms": 1000.0 * attack,
            "release_ms": 1000.0 * release,
            "starts_at_zero": abs(env[0][0]) < 1e-9 and abs(env[0][1]) < 1e-9,
            "ends_at_zero": abs(env[-1][0] - span) < 1e-6 and abs(env[-1][1]) < 1e-9,
            "peak_value": peak}


# --------------------------------------------------------------------------- #
# Check collection
# --------------------------------------------------------------------------- #
class Checks:
    def __init__(self):
        self.items = []

    def add(self, group, check_id, description, criterion, measured, status, detail=""):
        self.items.append({
            "group": group, "id": check_id, "description": description,
            "criterion": criterion, "measured": measured, "status": status,
            "detail": detail,
        })

    def ok(self, group, check_id, description, criterion, measured, passed, detail=""):
        self.add(group, check_id, description, criterion, measured,
                 "PASS" if passed else "FAIL", detail)

    def info(self, group, check_id, description, criterion, measured, detail=""):
        self.add(group, check_id, description, criterion, measured, "INFO", detail)

    def summary(self):
        core = [item for item in self.items if item["group"] != "M14"]
        corrupt = [item for item in self.items if item["group"] == "M14"]
        counts = {status: 0 for status in ("PASS", "FAIL", "SKIP", "INFO")}
        for item in core:
            counts[item["status"]] = counts.get(item["status"], 0) + 1
        corr = {status: 0 for status in ("PASS", "EXPECTED-DEGRADED", "INFO", "ADVISORY-FAIL", "SKIP")}
        for item in corrupt:
            corr[item["status"]] = corr.get(item["status"], 0) + 1
        return {
            "total": len(core), "pass": counts["PASS"], "fail": counts["FAIL"],
            "skip": counts["SKIP"], "info": counts["INFO"],
            "verdict": "PASS" if counts["FAIL"] == 0 else "FAIL",
            "corruption_total": len(corrupt),
            "corruption_pass": corr["PASS"],
            "corruption_expected_degraded": corr["EXPECTED-DEGRADED"],
            "corruption_info": corr["INFO"],
            "corruption_advisory_fail": corr["ADVISORY-FAIL"],
            "corruption_skip": corr["SKIP"],
        }


def _format(value):
    if isinstance(value, float):
        if value != value:
            return "n/a"
        if abs(value) >= 1000.0:
            return f"{value:.1f}"
        if abs(value) >= 1.0:
            return f"{value:.2f}"
        return f"{value:.4f}"
    return str(value)


def _cell(value):
    return _format(value).replace("|", "\\|").replace("\n", " ")


# --------------------------------------------------------------------------- #
# Format, score and safety rules
# --------------------------------------------------------------------------- #
def format_check(checks: Checks, label: str, fs_expected: int, width_expected: int,
                 duration_expected: float, fs: int, width: int, frames: int):
    ok = fs == fs_expected and width == width_expected
    checks.ok("F", f"formaat-{label}",
              f"{label}: sample rate / bit depth / channels",
              f"{fs_expected} Hz, {width_expected * 8}-bit, stereo",
              f"{fs} Hz, {width * 8}-bit, {frames} frames", ok)
    duration = frames / fs
    checks.ok("F", f"duur-{label}", f"{label}: duration", f"{duration_expected:.1f} s ± 0.05 s",
              f"{duration:.3f} s", abs(duration - duration_expected) <= 0.05)


def score_checks(checks: Checks, score: dict):
    meta = score["meta"]
    duration = float(meta["duration_s"])
    checks.ok("S", "score-duur", "score duration", "60–90 s", f"{duration} s",
              60.0 <= duration <= 90.0)
    checks.ok("S", "score-seed", "seed present and fixed", "integer", str(meta.get("seed")),
              isinstance(meta.get("seed"), int))
    checks.ok("S", "score-secties", "sections declared in the score",
              "≥ 2 sections with valid boundaries",
              f"{len(meta.get('sections') or [])} sections",
              len(meta.get("sections") or []) >= 2)

    events = {ev["id"]: ev for ev in score["light"]["events"]}
    audio = {ev["id"]: ev for ev in score["program"]["events"]}
    rules = score["envelope_rules"]
    min_attack = float(rules["min_attack_ms"])
    min_release = float(rules["min_release_ms"])

    # R4/R5 — envelopes: all events, no steps, attack/release ≥ 20 ms.
    env_failures = []
    for ev in score["light"]["events"]:
        metrics = event_metrics(ev)
        problems = []
        if not metrics["starts_at_zero"]:
            problems.append("does not start at (0,0)")
        if not metrics["ends_at_zero"]:
            problems.append("does not end at (duration,0)")
        if metrics["attack_ms"] < min_attack - 1e-9:
            problems.append(f"attack {metrics['attack_ms']:.0f} ms")
        if metrics["release_ms"] < min_release - 1e-9:
            problems.append(f"release {metrics['release_ms']:.0f} ms")
        if problems:
            env_failures.append(f"{ev['id']}: " + ", ".join(problems))
    checks.ok("S", "score-envelope-regels",
              "all light envelopes: attack/release ≥ 20 ms, no step at start/end",
              "R4/R5: start (0,0), end (duration,0), attack/release ≥ 20 ms",
              "conform" if not env_failures else "; ".join(env_failures),
              not env_failures, str(rules.get("note", "")))

    l1, a1 = events.get("L1"), audio.get("A1")
    l1_metrics = event_metrics(l1) if l1 else None
    release_range = rules.get("l1_release_ms_range", [20.0, 40.0])
    checks.ok("S", "score-L1-extinctie",
              "L1 extinction is single, isolated and reasonably abrupt",
              f"release {release_range[0]:.0f}–{release_range[1]:.0f} ms",
              f"{l1_metrics['release_ms']:.1f} ms" if l1_metrics else "L1 missing",
              bool(l1_metrics) and release_range[0] - 1e-6 <= l1_metrics["release_ms"] <= release_range[1] + 1e-6)
    moved = l1 is not None and float(l1["pan"][0][1]) < 0.0 < float(l1["pan"][-1][1])
    checks.ok("S", "score-L1-beweging", "L1 moves left→right",
              "pan from < 0 to > 0",
              f"pan {l1['pan'][0][1]}→{l1['pan'][-1][1]}" if l1 else "L1 missing", moved)
    delay = float(a1["start_s"]) - float(l1["end_s"]) if a1 and l1 else float("nan")
    checks.ok("S", "score-A1-vertraging", "A1 follows L1 with a deliberate delay",
              "150–200 ms", f"{delay * 1000:.0f} ms", 0.150 <= delay <= 0.200)
    a1_attack = float(a1["env"][1][0]) if a1 else float("nan")
    a1_decay = float(a1["env"][-1][0]) if a1 else float("nan")
    checks.ok("S", "score-A1-envelope", "A1 envelope differs from the light",
              "own attack/decay, not identical to L1",
              f"attack {a1_attack * 1000:.0f} ms, decay {a1_decay:.1f} s",
              a1_decay >= 1.5 and a1_attack <= 0.1)

    l2 = events.get("L2")
    l2_metrics = event_metrics(l2) if l2 else None
    checks.ok("S", "score-L2-fadein", "L2 starts with a fade-in, no step from black to light",
              f"fade-in ≥ {rules.get('l2_fade_in_ms', 50.0):.0f} ms",
              f"{l2_metrics['attack_ms']:.0f} ms" if l2_metrics else "L2 missing",
              bool(l2_metrics) and l2_metrics["attack_ms"] >= float(rules.get("l2_fade_in_ms", 50.0)))
    l3 = events.get("L3")
    checks.ok("S", "score-L3-onbeantwoord",
              "at least one light event deliberately remains unanswered",
              "L3 has no audio answer",
              "L3 relation: unanswered" if l3 and "unanswered" in str(l3.get("relation", ""))
              else "not found",
              bool(l3) and "unanswered" in str(l3.get("relation", "")))
    l4, a2 = events.get("L4"), audio.get("A2")
    if l4 is not None and a2 is not None and l1 is not None:
        gap = float(l4["start_s"]) - float(l1["start_s"])
        transform = a2.get("transform") or {}
        dimmer = max(l4["rgb"]) < max(l1["rgb"])
        reversed_pan = float(l4["pan"][0][1]) > 0.0 > float(l4["pan"][-1][1])
        checks.ok("S", "score-L4-herinnering",
                  "L4/A2 are a transformed return much later",
                  "≥ 20 s later, dimmer, reversed movement, lowered",
                  f"{gap:.0f} s later, dimmer={dimmer}, reversed={reversed_pan}, "
                  f"pitch={transform.get('pitch_ratio')}",
                  gap >= 20.0 and dimmer and reversed_pan
                  and float(transform.get("pitch_ratio", 1.0)) < 1.0)

    # R3 — safety ceiling and brightness ladder in the score.
    carrier_max = float(score["carriers"]["max_colour_ratio"])
    checks.ok("S", "score-kleursplafond",
              "colour ceiling is 25% per carrier", "max_colour_ratio = 0.25",
              f"{carrier_max:.4f}", abs(carrier_max - 0.25) < 1e-9)
    exposures = score.get("exposures") or {}
    expected = {"full": (1.0, 0.20), "mid": (0.625, 0.125), "dim": (0.3125, 0.0625)}
    ladder_ok = all(name in exposures
                    and abs(float(exposures[name]["scale"]) - scale) < 1e-9
                    and abs(float(exposures[name]["max_colour_ratio"]) - ratio) < 1e-9
                    for name, (scale, ratio) in expected.items())
    checks.ok("S", "score-heldrerheidsladder",
              "brightness ladder: dim 6.25% → mid 12.5% → full 20%",
              "dim scale 0.3125; mid 0.625; full 1.0",
              ", ".join(f"{n}={float(exposures[n]['max_colour_ratio']) * 100:.2f}%"
                        for n in expected if n in exposures), ladder_ok)


# --------------------------------------------------------------------------- #
# Core analysis
# --------------------------------------------------------------------------- #
def analyse_core(checks: Checks, measurements: dict, score: dict, fs: int,
                 left, right, label: str, exhaustive: bool, intercarrier: bool):
    car = score["carriers"]
    ref_hz = float(car["reference"]["hz"])
    colour_hz = {key: float(value["hz"]) for key, value in car["colours"].items()}
    verify_cfg = score["verify"]
    ref_windows = [(float(a), float(b)) for a, b in verify_cfg["reference_windows_s"]]
    lfo_window = tuple(float(v) for v in verify_cfg["lfo_window_s"])

    # M1 — carrier frequencies (FFT ≥ 65536, Hann, parabolic interpolation)
    spec_l, spec_r, sum_w2 = spectrum_pair(left, right, fs, ref_windows[0][0])
    ref_freq = peak_frequency(combined_power(spec_l, spec_r), fs, FFT_N, ref_hz)
    colour_t0 = 12.2
    spec_l_c, spec_r_c, sum_w2_c = spectrum_pair(left, right, fs, colour_t0)
    colour_freqs = {key: peak_frequency(combined_power(spec_l_c, spec_r_c), fs, FFT_N, hz)
                    for key, hz in colour_hz.items()}
    measurements.setdefault("m1", {})[label] = {
        "reference_hz": ref_freq, "colours_hz": colour_freqs}
    checks.ok("M1", f"m1-frequenties-{label}", f"{label}: carrier frequencies within ±1 Hz",
              "18200 / 18700 / 19200 / 19700 Hz ±1 Hz",
              f"{ref_freq:.2f} / " + " / ".join(f"{colour_freqs[k]:.2f}"
                                                for k in ("red", "green", "blue")),
              abs(ref_freq - ref_hz) <= 1.0
              and all(abs(colour_freqs[k] - hz) <= 1.0 for k, hz in colour_hz.items()))

    # M2 — reference level as PEAK AMPLITUDE (binding; band RMS informative)
    ref_peaks = []
    band_rms_values = []
    for t0, t1 in ref_windows:
        env_l, env_r = demod_envelopes(left, right, fs, ref_hz, t0, t1, 0.005)
        if not env_l:
            continue
        ref_peaks.append(max(math.hypot(a, b) for a, b in zip(env_l, env_r)))
        band_rms_values.append(band_rms_dbfs(spec_l, fs, FFT_N, sum_w2, ref_hz - 100.0,
                                             ref_hz + 100.0))
    ref_peak_db = amp_db(max(ref_peaks)) if ref_peaks else float("-inf")
    spread = 20.0 * math.log10(max(ref_peaks) / min(ref_peaks)) if len(ref_peaks) > 1 else 0.0
    target = float(car["reference"]["level_dbfs"])
    band_rms_informative = max(band_rms_values) if band_rms_values else float("-inf")
    measurements.setdefault("m2", {})[label] = {
        "reference_peak_amplitude_dbfs": ref_peak_db,
        "band_rms_dbfs_informative": band_rms_informative,
        "variation_db": spread,
        "criterion": "peak amplitude (binding, SPECTRASTROBE §6); band RMS informative",
    }
    checks.ok("M2", f"m2-referentieniveau-{label}",
              f"{label}: reference peak amplitude (binding; band RMS informative)",
              f"peak {target:+.1f} dBFS ± 0.5 dB, constant",
              f"peak {ref_peak_db:+.2f} dBFS (variation {spread:.2f} dB; "
              f"band RMS informative {band_rms_informative:+.2f} dBFS)",
              abs(ref_peak_db - target) <= 0.5 and spread <= 0.5)
    ref_band_power = band_rms_sq(spec_l, fs, FFT_N, sum_w2, ref_hz - 100.0, ref_hz + 100.0) \
        + band_rms_sq(spec_r, fs, FFT_N, sum_w2, ref_hz - 100.0, ref_hz + 100.0)
    ref_amplitude = math.sqrt(2.0 * ref_band_power) if ref_band_power > 0.0 else 1.0
    measurements["m2"][label]["reference_amplitude_linear_fft"] = round(ref_amplitude, 8)
    measurements["m2"][label]["reference_peak_from_fft_dbfs"] = round(amp_db(ref_amplitude), 3)

    # M5 — reference pan modulation
    lfo = lfo_metrics(left, right, fs, lfo_window[0], lfo_window[1])
    measurements.setdefault("m5", {})[label] = lfo
    checks.ok("M5", f"m5-pan-lfo-{label}",
              f"{label}: 20 Hz pan LFO, depth ≥ 99%, out-of-phase ≥ 30 dB down",
              "20.0 Hz ± 0.5; depth ≥ 99%; corr < 0; rest ≥ 30 dB down",
              f"{lfo['freq_hz']:.2f} Hz, depth {lfo['depth_pct']:.1f}%, "
              f"corr {lfo['corr']:.2f}, rest {lfo['rest_db']:.1f} dB",
              abs(lfo["freq_hz"] - 20.0) <= 0.5 and lfo["depth_pct"] >= 99.0
              and lfo["corr"] <= -0.2 and lfo["rest_db"] <= -30.0)

    # M3 — relative channel amplitude per colour, follows the score
    probes = verify_cfg["colour_probes"]
    light_events = {ev["id"]: ev for ev in score["light"]["events"]}
    measured_ratios = []
    for probe in probes:
        event = light_events[probe["event"]]
        t0, t1 = float(probe["t0"]), float(probe["t1"])
        t_center = 0.5 * (t0 + t1)
        row = {"event": event["id"], "t_center_s": t_center, "colours": {}}
        for index, key in enumerate(("red", "green", "blue")):
            env_l, env_r = demod_envelopes(left, right, fs, colour_hz[key], t0, t1, t1 - t0,
                                           windowed=True)
            if not env_l:
                continue
            amp_l = sum(env_l) / len(env_l)
            amp_r = sum(env_r) / len(env_r)
            measured_l = amp_l / ref_amplitude if ref_amplitude > 0 else 0.0
            measured_r = amp_r / ref_amplitude if ref_amplitude > 0 else 0.0
            expected_l, expected_r = expected_colour_ratio(score, event, t_center, index)
            delta_l = amp_db(measured_l / expected_l) if measured_l > 0 and expected_l > 0 else 99.0
            delta_r = amp_db(measured_r / expected_r) if measured_r > 0 and expected_r > 0 else 99.0
            row["colours"][key] = {
                "measured_ratio_L": measured_l, "measured_ratio_R": measured_r,
                "expected_ratio_L": expected_l, "expected_ratio_R": expected_r,
                "delta_db_L": delta_l, "delta_db_R": delta_r,
            }
            measured_ratios.append(max(measured_l, measured_r))
        measurements.setdefault("m3", {}).setdefault(label, []).append(row)
    deltas = [max(abs(c["delta_db_L"]), abs(c["delta_db_R"]))
              for row in measurements["m3"][label] for c in row["colours"].values()]
    worst_delta = max(deltas) if deltas else 99.0
    probe_max = max(measured_ratios) if measured_ratios else 0.0
    checks.ok("M3", f"m3-helderheid-{label}",
              f"{label}: colour brightness follows the score (probes)",
              "|deviation| ≤ 0.5 dB per probe; ≤ 25% per carrier",
              f"max deviation {worst_delta:.2f} dB, max probe ratio {probe_max * 100:.1f}%",
              worst_delta <= 0.5)
    if label == "48k":
        # Actual maximum coupling over the whole render + sum per ear (R3).
        coupling = max_coupling(left, right, fs, score, ref_amplitude)
        measurements["m3_overall"] = coupling
        checks.ok("M3", "m3-max-koppeling",
                  "maximum colour coupling over the whole render, per carrier ≤ 25%",
                  "≤ 25% per carrier; sum per ear reported separately (R3)",
                  "per carrier: " + ", ".join(f"{k} {v * 100:.1f}%"
                                              for k, v in coupling["per_colour"].items())
                  + f"; sum per ear L {coupling['max_sum_per_ear']['L'] * 100:.1f}% / "
                  f"R {coupling['max_sum_per_ear']['R'] * 100:.1f}%",
                  all(v <= 0.25 + 0.005 for v in coupling["per_colour"].values()))

    # M6 — real inter-carrier isolation via a dedicated probe (one colour at a time)
    if intercarrier:
        probe = intercarrier_probe(score)
        measurements["m6"] = probe
        checks.ok("M6", "m6-inter-drager-isolatie",
                  "inter-carrier isolation: probe with one colour at a time",
                  "inactive colour bands ≤ -40 dB relative to the active carrier",
                  "worst " + ", ".join(
                      f"{k}: {v['worst_other_band_db']:.1f} dB" for k, v in probe["probes"].items())
                  + f" (active range {probe['own_band_dbfs']:.1f} dBFS)",
                  probe["worst_db"] <= -40.0)

    # M4 — centre balance and isolation at hard panning
    if exhaustive:
        hard = verify_cfg["hard_pan_probe"]
        t0, t1 = float(hard["t0"]), float(hard["t1"])
        isolations = []
        for key in ("red", "green", "blue"):
            env_l, env_r = demod_envelopes(left, right, fs, colour_hz[key], t0, t1, t1 - t0)
            if env_l:
                amp_l = sum(env_l) / len(env_l)
                amp_r = sum(env_r) / len(env_r)
                isolations.append(amp_db((amp_l + 1e-12) / (amp_r + 1e-12)))
        isolation = min(isolations) if isolations else -999.0
        balance_probe = next(p for p in probes if p["event"] == "L2")
        b0, b1 = float(balance_probe["t0"]), float(balance_probe["t1"])
        balances = []
        for key in ("red", "green", "blue"):
            env_l, env_r = demod_envelopes(left, right, fs, colour_hz[key], b0, b1, b1 - b0)
            if env_l:
                balances.append(abs(amp_db((sum(env_l) + 1e-12) / (sum(env_r) + 1e-12))))
        balance = max(balances) if balances else 99.0
        measurements.setdefault("m4", {})[label] = {"balance_db": balance,
                                                    "isolation_db": isolation}
        checks.ok("M4", f"m4-balans-{label}", f"{label}: centre balance and hard-pan isolation",
                  "centre ± 0.5 dB; hard pan ≥ 40 dB isolation",
                  f"balance {balance:.2f} dB, isolation {isolation:.1f} dB",
                  balance <= 0.5 and isolation >= 40.0)

        # M8 — noise floor with colours off
        silent_t0 = float(verify_cfg["silent_window_s"][0])
        spec_l_s, spec_r_s, sum_w2_s = spectrum_pair(left, right, fs, silent_t0)
        noise = []
        for key in ("red", "green", "blue"):
            hz = colour_hz[key]
            noise.append(max(
                band_rms_dbfs(spec_l_s, fs, FFT_N, sum_w2_s, hz - 100.0, hz + 100.0),
                band_rms_dbfs(spec_r_s, fs, FFT_N, sum_w2_s, hz - 100.0, hz + 100.0)))
        worst_noise = max(noise) if noise else 0.0
        measurements.setdefault("m8", {})[label] = {"worst_colour_band_noise_dbfs": worst_noise}
        checks.ok("M8", f"m8-ruisvloer-{label}",
                  f"{label}: noise floor in colour bands with no colour signal",
                  "≤ -80 dBFS", f"{worst_noise:.1f} dBFS", worst_noise <= -80.0)

    # M9 — peak and true peak
    peak = max(max(left), -min(left), max(right), -min(right))
    true_peak_value = true_peak(left, right, fs)
    finite = all(math.isfinite(v) for v in left) and all(math.isfinite(v) for v in right)
    peak_dbfs = amp_db(peak)
    true_peak_dbtp = amp_db(true_peak_value)
    measurements.setdefault("m9", {})[label] = {
        "sample_peak_dbfs": peak_dbfs, "true_peak_dbtp": true_peak_dbtp,
        "clipped": peak >= 1.0, "finite": finite,
    }
    checks.ok("M9", f"m9-piek-{label}", f"{label}: peak / true peak / no clipping",
              "sample ≤ -6 dBFS, true peak ≤ -6 dBTP, no clipping, all finite",
              f"sample {peak_dbfs:+.2f} dBFS, true peak {true_peak_dbtp:+.2f} dBTP",
              peak_dbfs <= -6.0 and true_peak_dbtp <= -6.0 and peak < 1.0 and finite)


def max_coupling(left, right, fs: float, score: dict, ref_amplitude: float):
    """Maximum colour coupling over the whole render, plus the sum per ear."""
    colour_hz = {key: float(value["hz"]) for key, value in score["carriers"]["colours"].items()}
    per_colour = {key: 0.0 for key in colour_hz}
    sum_l = sum_r = 0.0
    for ev in score["light"]["events"]:
        t0, t1 = float(ev["start_s"]), float(ev["end_s"])
        envelopes = {}
        for key, hz in colour_hz.items():
            env_l, env_r = demod_envelopes(left, right, fs, hz, t0, t1, 0.02, windowed=True)
            envelopes[key] = (env_l, env_r)
        n = min(len(envelopes[key][0]) for key in colour_hz)
        for i in range(n):
            block_l = block_r = 0.0
            for key in colour_hz:
                rl = envelopes[key][0][i] / ref_amplitude if ref_amplitude > 0 else 0.0
                rr = envelopes[key][1][i] / ref_amplitude if ref_amplitude > 0 else 0.0
                per_colour[key] = max(per_colour[key], rl, rr)
                block_l += rl
                block_r += rr
            sum_l = max(sum_l, block_l)
            sum_r = max(sum_r, block_r)
    return {
        "per_colour": {k: round(v, 5) for k, v in per_colour.items()},
        "max_sum_per_ear": {"L": round(sum_l, 5), "R": round(sum_r, 5)},
        "criterion": "≤25% per carrier (R3); sum per ear reported separately",
    }


def intercarrier_probe(score: dict) -> dict:
    """Render a short probe per colour (one colour at a time) and measure isolation."""
    render = render_module()
    colour_hz = {key: float(value["hz"]) for key, value in score["carriers"]["colours"].items()}
    probe_cfg = score["verify"]["intercarrier_probe_s"]
    t0 = float(probe_cfg[0])
    results = {}
    worst = -999.0
    own_levels = []
    duration = max(float(probe_cfg[1]) + 0.2, 3.5)
    for active in ("red", "green", "blue"):
        test = copy.deepcopy(score)
        test["meta"]["duration_s"] = duration
        test["program"]["events"] = []
        rgb = [0.0, 0.0, 0.0]
        rgb[("red", "green", "blue").index(active)] = 0.20
        span = (duration - 0.3) - 0.5
        test["light"]["events"] = [{
            "id": f"XT-{active}", "start_s": 0.5, "end_s": 0.5 + span,
            "rgb": rgb, "env": [[0.0, 0.0], [0.02, 1.0],
                                [span - 0.02, 1.0], [span, 0.0]],
            "pan": [[0.0, 0.0], [span, 0.0]],
            "role": "inter-carrier probe", "relation": "measurement probe",
        }]
        (mix, _) = render.render_mix(test, 48000, with_programme=False)
        spec_l, spec_r, sum_w2 = spectrum_pair(mix[0], mix[1], 48000.0, t0)
        levels = {}
        for key, hz in colour_hz.items():
            levels[key] = band_rms_sq(spec_l, 48000.0, FFT_N, sum_w2, hz - 100.0, hz + 100.0) \
                + band_rms_sq(spec_r, 48000.0, FFT_N, sum_w2, hz - 100.0, hz + 100.0)
        own = levels[active]
        own_levels.append(db(own))
        others = {key: db(levels[key] / own) if own > 0 and key != active else -999.0
                  for key in colour_hz}
        worst_other = max(value for key, value in others.items() if key != active)
        worst = max(worst, worst_other)
        results[active] = {"own_band_dbfs": db(own), "other_bands_db": others,
                           "worst_other_band_db": worst_other}
    return {"probes": results, "worst_db": worst,
            "own_band_dbfs": max(own_levels) if own_levels else 0.0,
            "note": ("Measurement floor: the carriers are ideal sines; the Kasina decoder "
                     "bandwidth is unknown [open]. This shows signal isolation, not the "
                     "hardware decoder.")}


def programme_checks(checks: Checks, measurements: dict, score: dict,
                     prog48, prog44, fs48: int, fs44: int):
    """M7: programme leakage for the real (sparse) material + honest broadband test."""
    cfg = score["verify"]
    t0 = float(cfg["programme_window_s"][0])
    results = {}
    for label, (fs, left, right) in {"48k": (fs48, prog48[0], prog48[1]),
                                     "44k1": (fs44, prog44[0], prog44[1])}.items():
        spec_l, spec_r, sum_w2 = spectrum_pair(left, right, fs, t0)
        rms_l = band_rms_dbfs(spec_l, fs, FFT_N, sum_w2, 17000.0, 20000.0)
        rms_r = band_rms_dbfs(spec_r, fs, FFT_N, sum_w2, 17000.0, 20000.0)
        carrier_floor = -999.0
        for hz in (18200.0, 18700.0, 19200.0, 19700.0):
            carrier_floor = max(
                carrier_floor,
                band_rms_dbfs(spec_l, fs, FFT_N, sum_w2, hz - 50.0, hz + 50.0),
                band_rms_dbfs(spec_r, fs, FFT_N, sum_w2, hz - 50.0, hz + 50.0))
        results[label] = {"programme_17k_20k_dbfs_L": rms_l,
                          "programme_17k_20k_dbfs_R": rms_r,
                          "program_only_carrier_floor_dbfs": carrier_floor}
        checks.ok("M7", f"m7-programmaleak-{label}",
                  f"{label}: programme-only, energy 17–20 kHz (synthesised sparse material)",
                  "≤ -60 dBFS per channel", f"{max(rms_l, rms_r):.1f} dBFS",
                  max(rms_l, rms_r) <= -60.0)
        checks.ok("M7", f"m7-program-only-dragerloos-{label}",
                  f"{label}: programme-only A/B contains no carriers",
                  "carrier bands ≤ -80 dBFS", f"{carrier_floor:.1f} dBFS",
                  carrier_floor <= -80.0)
    measurements["m7"] = results

    # Broadband test: white noise through the same 8th-order LPF, measured attenuation.
    render = render_module()
    import random
    rng = random.Random(int(score["meta"]["seed"]) ^ 0x5EED)
    count = int(round(2.0 * fs48))
    noise = array.array("d", [rng.uniform(-1.0, 1.0) for _ in range(count)])
    cutoff = float(score["program"]["band_limit_hz"])
    order = int(score["program"]["lowpass_order"])
    render.programme_lowpass(noise, fs48, cutoff, order)
    spec_l, spec_r, sum_w2 = spectrum_pair(noise, noise, fs48, 0.2)
    spec = combined_power(spec_l, spec_r)
    reference_1k = band_rms_dbfs(spec, fs48, FFT_N, sum_w2, 800.0, 1200.0)
    attenuation = {}
    for hz in (17000.0, 18200.0, 19700.0):
        level = band_rms_dbfs(spec, fs48, FFT_N, sum_w2, hz - 100.0, hz + 100.0)
        attenuation[f"{int(hz)}"] = round(level - reference_1k, 2)
    measurements["m7_broadband"] = {
        "reference_band_800_1200_dbfs": round(reference_1k, 2),
        "attenuation_db_at": attenuation,
        "honest_label": ("The -60 dBFS requirement is demonstrated for the sparse, "
                         "low-frequency synthesised programme material. For broadband "
                         "v1 material (white noise) it is NOT demonstrated; the 8th-order "
                         "LPF alone is not steep enough. This is an INFO measurement, "
                         "not a PASS."),
    }
    checks.info("M7", "m7-breedband-eerlijk",
                "broadband test: white noise through the same 8th-order LPF (17/18.2/19.7 kHz)",
                "INFO: -60 dBFS requirement for broadband v1 material not demonstrated",
                "attenuation relative to 1 kHz: " + ", ".join(
                    f"{hz} Hz {value:+.1f} dB" for hz, value in attenuation.items()),
                "honest label: no false PASS for broadband material")


# --------------------------------------------------------------------------- #
# Safety clamp, brightness ladder, determinism and manifest
# --------------------------------------------------------------------------- #
def safety_clamp_check(checks: Checks, measurements: dict, score: dict):
    """Adversarial score (rgb = 0.90) must be clamped to 25%."""
    render = render_module()
    test = copy.deepcopy(score)
    test["meta"]["duration_s"] = 2.0
    test["program"]["events"] = []
    test["light"]["events"] = [{
        "id": "ADV", "start_s": 0.4, "end_s": 1.6,
        "rgb": [0.90, 0.90, 0.90],
        "env": [[0.0, 0.0], [0.02, 1.0], [1.18, 1.0], [1.2, 0.0]],
        "pan": [[0.0, 0.0], [1.2, 0.0]],
        "role": "adversarial clamp test", "relation": "test",
    }]
    clamp_log = []
    (mix, _) = render.render_mix(test, 48000, with_programme=False, clamp_log=clamp_log)
    fs = 48000
    ref_env_l, ref_env_r = demod_envelopes(mix[0], mix[1], fs, 18200.0, 0.5, 1.5, 0.005)
    ref_amp = max(math.hypot(a, b) for a, b in zip(ref_env_l, ref_env_r)) if ref_env_l else 0.0
    ratios = {}
    for key, hz in (("red", 18700.0), ("green", 19200.0), ("blue", 19700.0)):
        env_l, env_r = demod_envelopes(mix[0], mix[1], fs, hz, 0.5, 1.5, 0.02,
                                       windowed=True)
        ratios[key] = max(max(env_l), max(env_r)) / ref_amp if ref_amp > 0 else 0.0
    measured_max = max(ratios.values())
    clamped = bool(clamp_log) and any(
        abs(float(entry["requested_ratio"]) - 0.90) < 1e-9 for entry in clamp_log)
    measurements["safety_clamp"] = {
        "adversarial_requested_ratio": 0.90,
        "clamp_ratio": float(score["carriers"]["max_colour_ratio"]),
        "clamped": clamped,
        "clamp_log": clamp_log,
        "measured_max_ratio": round(measured_max, 5),
        "per_colour": {k: round(v, 5) for k, v in ratios.items()},
    }
    checks.ok("SA", "sa-clamp-adversarieel",
              "adversarial score rgb=0.90 is hard-clamped to 25% of the reference peak",
              "measured max ≤ 0.25 and clamp logged",
              f"measured max {measured_max * 100:.2f}%, clamp log {len(clamp_log)} entries",
              measured_max <= 0.25 + 0.005 and clamped)


def exposure_checks(checks: Checks, measurements: dict, score: dict,
                    files: dict, ref_amplitude_48: float):
    """Brightness ladder: dim 6.25% and mid 12.5% really lower than full 20%."""
    probe = {"t0": 13.95, "t1": 13.98}  # L1 peak window: maximum colour coupling
    t0, t1 = float(probe["t0"]), float(probe["t1"])
    t_center = 0.5 * (t0 + t1)
    event = next(ev for ev in score["light"]["events"] if ev["id"] == "L1")
    measurements["exposure_ladder"] = {}
    for name in ("full", "mid", "dim"):
        if name not in files:
            continue
        fs, _, left, right = files[name]
        ref_env_l, ref_env_r = demod_envelopes(left, right, fs, 18200.0, 20.0, 21.0, 0.005)
        ref_amp = max(math.hypot(a, b) for a, b in zip(ref_env_l, ref_env_r)) if ref_env_l else 0.0
        scale = float(score["exposures"][name]["scale"])
        per_colour = {}
        worst_delta = 0.0
        for index, (key, hz) in enumerate((("red", 18700.0), ("green", 19200.0),
                                           ("blue", 19700.0))):
            env_l, env_r = demod_envelopes(left, right, fs, hz, t0, t1, t1 - t0, windowed=True)
            if not env_l:
                continue
            measured_l = (sum(env_l) / len(env_l)) / ref_amp if ref_amp > 0 else 0.0
            measured_r = (sum(env_r) / len(env_r)) / ref_amp if ref_amp > 0 else 0.0
            expected_l, expected_r = expected_colour_ratio(score, event, t_center, index)
            expected_l *= scale
            expected_r *= scale
            delta = max(abs(amp_db((measured_l + 1e-12) / (expected_l + 1e-12))),
                        abs(amp_db((measured_r + 1e-12) / (expected_r + 1e-12))))
            per_colour[key] = {"measured_max": max(measured_l, measured_r),
                               "expected_max": max(expected_l, expected_r),
                               "delta_db": delta}
            worst_delta = max(worst_delta, delta)
        nominal_max = 0.20 * scale
        measurements["exposure_ladder"][name] = {
            "scale": round(scale, 5), "nominal_max_ratio": round(nominal_max, 5),
            "per_colour": per_colour, "worst_delta_db": round(worst_delta, 3),
        }
        checks.ok("L", f"ladder-{name}",
                  f"brightness ladder '{name}': expected max "
                  f"{float(score['exposures'][name]['max_colour_ratio']) * 100:.2f}%",
                  "|deviation| ≤ 0.5 dB relative to score × scale",
                  f"max deviation {worst_delta:.2f} dB", worst_delta <= 0.5)
    # Exposures must really differ from each other (dim < mid < full).
    dim = measurements["exposure_ladder"].get("dim", {}).get("per_colour", {}).get("red", {})
    mid = measurements["exposure_ladder"].get("mid", {}).get("per_colour", {}).get("red", {})
    full = measurements["exposure_ladder"].get("full", {}).get("per_colour", {}).get("red", {})
    order_ok = (dim.get("measured_max", 0.0) < mid.get("measured_max", 0.0)
                < full.get("measured_max", 0.0))
    checks.ok("L", "ladder-orde", "ladder is ascending: dim < mid < full",
              "dim 6.25% < mid 12.5% < full 20%",
              f"{dim.get('measured_max', 0.0) * 100:.2f}% < "
              f"{mid.get('measured_max', 0.0) * 100:.2f}% < "
              f"{full.get('measured_max', 0.0) * 100:.2f}%", order_ok)


def determinism_check(checks: Checks, measurements: dict, render_script: Path,
                      score_path: Path, out_dir: Path, names, timeout: int = 300):
    with tempfile.TemporaryDirectory(prefix="specstroop-repro-") as tmp:
        command = [sys.executable, str(render_script), "--score", str(score_path),
                   "--out-dir", tmp]
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
        except Exception as error:  # noqa: BLE001 - the report must never crash
            checks.add("M10", "m10-render-2", "second render", "start render.py",
                       str(error), "FAIL")
            return
        if result.returncode != 0:
            checks.add("M10", "m10-render-2", "second render", "exit code 0",
                       f"exit code {result.returncode}", "FAIL", result.stderr[-400:])
            return
        mismatches = []
        compared = []
        for name in names:
            first, second = out_dir / name, Path(tmp) / name
            if not first.exists() or not second.exists():
                mismatches.append(name)
                continue
            same = sha256(first) == sha256(second)
            compared.append({"file": name, "identical": same})
            if not same:
                mismatches.append(name)
        measurements["m10"] = compared
        checks.ok("M10", "m10-determinisme", "two renders with the same seed are byte-identical",
                  "all WAV output identical",
                  "identical" if not mismatches else f"differs: {', '.join(mismatches)}",
                  not mismatches)


def manifest_check(checks: Checks, measurements: dict, out_dir: Path,
                   render_script: Path, verify_script: Path, names):
    path = out_dir / "render-manifest.json"
    if not path.exists():
        checks.add("M10", "m10-manifest", "render manifest present", "file exists",
                   "missing", "FAIL")
        return
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except Exception as error:  # noqa: BLE001
        checks.add("M10", "m10-manifest", "render manifest readable", "valid JSON",
                   str(error), "FAIL")
        return
    problems = []
    if manifest.get("poc_version") != POC_VERSION:
        problems.append(f"poc_version {manifest.get('poc_version')!r}")
    for script, actual in (("render.py", render_script), ("verify.py", verify_script)):
        recorded = (manifest.get("scripts") or {}).get(script)
        if recorded != sha256(actual):
            problems.append(f"sha256 {script} stale — run render.py again")
    safety = manifest.get("safety") or {}
    if abs(float(safety.get("colour_clamp_ratio", 0.0)) - 0.25) > 1e-9:
        problems.append("colour_clamp_ratio")
    exposures = (manifest.get("safety") or {}).get("exposures") or {}
    if not all(name in exposures for name in ("full", "mid", "dim")):
        problems.append("exposures missing")
    outputs = manifest.get("outputs") or {}
    for name in names:
        recorded = (outputs.get(name) or {}).get("sha256")
        target = out_dir / name
        if target.exists() and recorded != sha256(target):
            problems.append(f"sha256 output {name}")
    measurements["manifest"] = {
        "poc_version": manifest.get("poc_version"),
        "scripts": manifest.get("scripts"),
        "clamp_ratio": safety.get("colour_clamp_ratio"),
        "exposures": list(exposures),
        "outputs": sorted(outputs),
    }
    checks.ok("M10", "m10-manifest",
              "manifest contains version, script sha256s, clamp and exposures; output hashes match",
              "poc_version, scripts.render.py/verify.py, safety.colour_clamp_ratio, exposures",
              "conform" if not problems else "; ".join(problems), not problems)


# --------------------------------------------------------------------------- #
# Corruption matrix (M14, advisory)
# --------------------------------------------------------------------------- #
def _ffmpeg_command(src: Path, dst: Path, args) -> list:
    return ["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
            "-i", str(src), *args, str(dst)]


def _expected_colour_ratios(score: dict) -> dict:
    probe = next(p for p in score["verify"]["colour_probes"] if p["event"] == "L1")
    t_center = 0.5 * (float(probe["t0"]) + float(probe["t1"]))
    event = next(ev for ev in score["light"]["events"] if ev["id"] == "L1")
    out = {}
    for index, key in enumerate(("red", "green", "blue")):
        left, right = expected_colour_ratio(score, event, t_center, index)
        out[key] = max(left, right)
    return out


def _quick_metrics(fs: int, left, right, expected_colours: dict,
                   lfo_search_center_hz: float = 20.0):
    peak = max(max(left), -min(left), max(right), -min(right))
    ref_env_l, ref_env_r = demod_envelopes(left, right, fs, 18200.0, 20.0, 22.0, 0.005)
    ref_amp = max(math.hypot(a, b) for a, b in zip(ref_env_l, ref_env_r)) if ref_env_l else 0.0
    ref_freq = estimate_frequency(left, right, fs, 18200.0, 20.0, 22.0)
    lfo = lfo_metrics(left, right, fs, 20.0, 24.0, lfo_search_center_hz)
    colour_freqs = {}
    colour_ratios = {}
    for key, hz in (("red", 18700.0), ("green", 19200.0), ("blue", 19700.0)):
        colour_freqs[key] = estimate_frequency(left, right, fs, hz, 12.2, 13.5)
        env_l, env_r = demod_envelopes(left, right, fs, hz, 13.0, 13.1, 0.1, windowed=True)
        colour_ratios[key] = (max(max(env_l), max(env_r)) / ref_amp if ref_amp > 0 and env_l else 0.0)
    return {
        "ref_freq_hz": ref_freq,
        "ref_peak_dbfs": amp_db(ref_amp),
        "colour_freqs_hz": colour_freqs,
        "colour_ratios": colour_ratios,
        "colour_ratio_errors_db": {
            key: round(amp_db((colour_ratios[key] + 1e-12) / (expected_colours[key] + 1e-12)), 2)
            for key in expected_colours},
        "lfo_freq_hz": lfo["freq_hz"],
        "lfo_depth_pct": lfo["depth_pct"],
        "lfo_corr": lfo["corr"],
        "peak_dbfs": amp_db(peak),
    }


def _row(checks: Checks, measurements: dict, row_id: str, label: str, tool: str,
         command: str, expectation: str, metrics: dict, passed: bool,
         observed: str, extra: str = "", kind: str = "preserved"):
    if kind == "info":
        status = "INFO"
    elif passed and kind == "degraded":
        status = "EXPECTED-DEGRADED"
    elif passed:
        status = "PASS"
    else:
        status = "ADVISORY-FAIL"
    row = {"id": row_id, "label": label, "tool": tool, "command": command,
           "expectation": expectation, "observed": observed, "status": status,
           "kind": kind, "metrics": metrics, "detail": extra}
    measurements.setdefault("m14", []).append(row)
    checks.add("M14", f"m14-{row_id}", label, expectation, observed, status, extra)


def preserved(metrics: dict, expected_colours: dict, tol_amp_db: float = 3.0,
              tol_colour_db: float = 2.0, tol_freq_hz: float = 1.0) -> bool:
    ok = (abs(metrics["ref_freq_hz"] - 18200.0) <= tol_freq_hz
          and abs(metrics["ref_peak_dbfs"] + 30.0) <= tol_amp_db
          and abs(metrics["lfo_freq_hz"] - 20.0) <= 0.5
          and metrics["lfo_depth_pct"] >= 50.0)
    for key, expected in expected_colours.items():
        ok = ok and abs(metrics["colour_freqs_hz"][key] - {"red": 18700.0, "green": 19200.0,
                                                           "blue": 19700.0}[key]) <= tol_freq_hz
        ok = ok and abs(metrics["colour_ratio_errors_db"][key]) <= tol_colour_db
    return ok


def _linear_resample(left, right, fs_in: float, fs_out: float):
    count = int(round(len(left) * fs_out / fs_in))
    ratio = fs_in / fs_out
    out_l = array.array("d", [0.0]) * count
    out_r = array.array("d", [0.0]) * count
    for i in range(count):
        pos = i * ratio
        k = int(pos)
        frac = pos - k
        v0 = left[k] if k < len(left) else 0.0
        v1 = left[k + 1] if (k + 1) < len(left) else 0.0
        out_l[i] = v0 + (v1 - v0) * frac
        v0 = right[k] if k < len(right) else 0.0
        v1 = right[k + 1] if (k + 1) < len(right) else 0.0
        out_r[i] = v0 + (v1 - v0) * frac
    return out_l, out_r


def corruption_matrix(checks: Checks, measurements: dict, score: dict, out_dir: Path,
                      master_name: str, variant_name: str):
    has_ffmpeg = shutil.which("ffmpeg") is not None
    master, variant = out_dir / master_name, out_dir / variant_name
    expected_colours = _expected_colour_ratios(score)
    fs_m, _, l_m, r_m = read_wav(master)
    clean = _quick_metrics(fs_m, l_m, r_m, expected_colours)
    clean_ref_peak = clean["ref_peak_dbfs"]

    with tempfile.TemporaryDirectory(prefix="specstroop-corrupt-") as tmp:
        tmp = Path(tmp)
        if has_ffmpeg:
            # 1. neat SRC 48 -> 44.1
            dst = tmp / "src48_441.wav"
            subprocess.run(_ffmpeg_command(master, dst, ["-ar", "44100", "-c:a", "pcm_s24le"]),
                           check=True, capture_output=True)
            fs2, _, l2, r2 = read_wav(dst)
            metrics = _quick_metrics(fs2, l2, r2, expected_colours)
            _row(checks, measurements, "src48_441", "neat SRC 48 → 44.1 kHz", "ffmpeg",
                 "ffmpeg -i master -ar 44100 -c:a pcm_s24le",
                 "carriers + colour ratios intact (freq ±1 Hz, LFO 20 Hz, colour ±2 dB)",
                 metrics, preserved(metrics, expected_colours),
                 f"ref {metrics['ref_freq_hz']:.2f} Hz, {metrics['ref_peak_dbfs']:+.1f} dBFS, "
                 f"LFO {metrics['lfo_freq_hz']:.2f} Hz; max colour error "
                 f"{max(abs(v) for v in metrics['colour_ratio_errors_db'].values()):.1f} dB")
            # 2. neat SRC 44.1 -> 48
            dst = tmp / "src441_48.wav"
            subprocess.run(_ffmpeg_command(variant, dst, ["-ar", "48000", "-c:a", "pcm_s24le"]),
                           check=True, capture_output=True)
            fs2, _, l2, r2 = read_wav(dst)
            metrics = _quick_metrics(fs2, l2, r2, expected_colours)
            _row(checks, measurements, "src441_48", "neat SRC 44.1 → 48 kHz", "ffmpeg",
                 "ffmpeg -i variant -ar 48000 -c:a pcm_s24le",
                 "carriers + colour ratios intact", metrics, preserved(metrics, expected_colours),
                 f"ref {metrics['ref_freq_hz']:.2f} Hz, {metrics['ref_peak_dbfs']:+.1f} dBFS, "
                 f"LFO {metrics['lfo_freq_hz']:.2f} Hz")
            # 3. lowpass 16 kHz (8 cascaded 2-pole stages = 16th order)
            dst = tmp / "lowpass16.wav"
            chain = ",".join(["lowpass=f=16000:poles=2"] * 8)
            subprocess.run(_ffmpeg_command(master, dst, ["-af", chain, "-c:a", "pcm_s24le"]),
                           check=True, capture_output=True)
            fs2, _, l2, r2 = read_wav(dst)
            metrics = _quick_metrics(fs2, l2, r2, expected_colours)
            degraded = metrics["ref_peak_dbfs"] <= -30.0 - 10.0
            _row(checks, measurements, "lowpass16", "lowpass 16 kHz on the master", "ffmpeg",
                 f"ffmpeg -af '{chain}'", "expected: carriers attenuated (≥ 10 dB), i.e. degraded",
                 metrics, degraded,
                 f"ref {metrics['ref_peak_dbfs']:+.1f} dBFS (was {clean_ref_peak:+.1f}); "
                 "carrier gone — LFO measurement not meaningful", kind="degraded")
            # 4. MP3 320
            mp3 = tmp / "x320.mp3"
            subprocess.run(_ffmpeg_command(master, mp3, ["-c:a", "libmp3lame", "-b:a", "320k"]),
                           check=True, capture_output=True)
            dst = tmp / "x320.wav"
            subprocess.run(_ffmpeg_command(mp3, dst, ["-c:a", "pcm_s24le"]),
                           check=True, capture_output=True)
            fs2, _, l2, r2 = read_wav(dst)
            metrics = _quick_metrics(fs2, l2, r2, expected_colours)
            _row(checks, measurements, "mp3_320", "MP3 320 kbps CBR stereo", "ffmpeg",
                 "ffmpeg -c:a libmp3lame -b:a 320k (encode + decode)",
                 "expected: intact (Lumasonic advice) including colour ratios", metrics,
                 preserved(metrics, expected_colours),
                 f"ref {metrics['ref_freq_hz']:.2f} Hz, {metrics['ref_peak_dbfs']:+.1f} dBFS, "
                 f"LFO {metrics['lfo_freq_hz']:.2f} Hz; max colour error "
                 f"{max(abs(v) for v in metrics['colour_ratio_errors_db'].values()):.1f} dB")
            # 5. MP3 128 joint stereo
            mp3 = tmp / "x128.mp3"
            subprocess.run(_ffmpeg_command(master, mp3, ["-c:a", "libmp3lame", "-b:a", "128k",
                                                         "-joint_stereo", "1"]),
                           check=True, capture_output=True)
            dst = tmp / "x128.wav"
            subprocess.run(_ffmpeg_command(mp3, dst, ["-c:a", "pcm_s24le"]),
                           check=True, capture_output=True)
            fs2, _, l2, r2 = read_wav(dst)
            metrics = _quick_metrics(fs2, l2, r2, expected_colours)
            worst_colour = max(abs(v) for v in metrics["colour_ratio_errors_db"].values())
            degraded = (metrics["ref_peak_dbfs"] <= -33.0 or worst_colour >= 3.0
                        or metrics["lfo_depth_pct"] < 80.0)
            _row(checks, measurements, "mp3_128_joint", "MP3 128 kbps joint stereo", "ffmpeg",
                 "ffmpeg -c:a libmp3lame -b:a 128k -joint_stereo 1",
                 "expected: loss (reference and/or colour channels degrade)", metrics, degraded,
                 f"ref {metrics['ref_peak_dbfs']:+.1f} dBFS, max colour error {worst_colour:.1f} dB, "
                 f"LFO {metrics['lfo_depth_pct']:.0f}%", kind="degraded")
            # 6. AAC 256
            aac = tmp / "x256.m4a"
            subprocess.run(_ffmpeg_command(master, aac, ["-c:a", "aac", "-b:a", "256k"]),
                           check=True, capture_output=True)
            dst = tmp / "x256.wav"
            subprocess.run(_ffmpeg_command(aac, dst, ["-c:a", "pcm_s24le"]),
                           check=True, capture_output=True)
            fs2, _, l2, r2 = read_wav(dst)
            metrics = _quick_metrics(fs2, l2, r2, expected_colours)
            _row(checks, measurements, "aac_256", "AAC 256 kbps", "ffmpeg",
                 "ffmpeg -c:a aac -b:a 256k (encode + decode)",
                 "expected: carriers measurable (colour ratios possibly attenuated)", metrics,
                 preserved(metrics, expected_colours, tol_amp_db=3.0, tol_colour_db=2.0),
                 f"ref {metrics['ref_peak_dbfs']:+.1f} dBFS, max colour error "
                 f"{max(abs(v) for v in metrics['colour_ratio_errors_db'].values()):.1f} dB")
        else:
            for row_id, label in (("src48_441", "neat SRC 48 → 44.1 kHz"),
                                  ("src441_48", "neat SRC 44.1 → 48 kHz"),
                                  ("lowpass16", "lowpass 16 kHz"),
                                  ("mp3_320", "MP3 320 kbps"),
                                  ("mp3_128_joint", "MP3 128 kbps joint stereo"),
                                  ("aac_256", "AAC 256 kbps")):
                checks.add("M14", f"m14-{row_id}", label, "ffmpeg available",
                           "ffmpeg not found", "SKIP")

        # 7. cheap/naive SRC 48 -> 44.1 (linear interpolation, own code)
        l_g, r_g = _linear_resample(l_m, r_m, 48000.0, 44100.0)
        metrics = _quick_metrics(44100, l_g, r_g, expected_colours)
        drop = clean_ref_peak - metrics["ref_peak_dbfs"]
        worst_colour = max(abs(v) for v in metrics["colour_ratio_errors_db"].values())
        degraded = drop >= 1.5 or worst_colour >= 2.0
        _row(checks, measurements, "naive_src", "cheap/naive SRC 48 → 44.1 (linear)",
             "python", "linear interpolation 48000 → 44100",
             "expected: HF loss and/or colour error", metrics, degraded,
             f"ref {metrics['ref_peak_dbfs']:+.1f} dBFS (−{drop:.1f} dB vs clean), "
             f"max colour error {worst_colour:.1f} dB", kind="degraded")

        # 8. gain −6 dB (in-memory)
        gain = 0.5
        l_g = array.array("d", [v * gain for v in l_m])
        r_g = array.array("d", [v * gain for v in r_m])
        metrics = _quick_metrics(fs_m, l_g, r_g, expected_colours)
        gain_ok = (abs(metrics["ref_freq_hz"] - 18200.0) <= 1.0
                   and abs(metrics["ref_peak_dbfs"] + 36.0) <= 1.0
                   and abs(metrics["lfo_freq_hz"] - 20.0) <= 0.5)
        _row(checks, measurements, "gain_min6", "gain −6 dB (in-memory)", "python",
             "sample × 0.5", "expected: frequencies/LFO/colour ratios intact, level −6 dB",
             metrics, gain_ok,
             f"ref {metrics['ref_peak_dbfs']:+.1f} dBFS, LFO {metrics['lfo_freq_hz']:.2f} Hz")

        # 9. L/R swapped
        metrics = _quick_metrics(fs_m, r_m, l_m, expected_colours)
        _row(checks, measurements, "lr_swap", "L/R swapped", "python", "channels swapped",
             "expected: detection intact, but eyes swapped (finding)", metrics,
             preserved(metrics, expected_colours),
             f"ref {metrics['ref_freq_hz']:.2f} Hz, LFO {metrics['lfo_freq_hz']:.2f} Hz; "
             "note: L/R balance is mirrored")

        # 10. mono sum
        l_mono = array.array("d", [0.5 * (l_m[i] + r_m[i]) for i in range(len(l_m))])
        metrics = _quick_metrics(fs_m, l_mono, l_mono, expected_colours)
        mono_ok = metrics["lfo_depth_pct"] < 20.0
        _row(checks, measurements, "mono", "mono sum (L+R)/2", "python", "(L+R)/2",
             "expected: reference panning disappears (LFO detection fails)", metrics, mono_ok,
             f"LFO depth {metrics['lfo_depth_pct']:.1f}% (expected < 20%), "
             f"ref {metrics['ref_peak_dbfs']:+.1f} dBFS", kind="degraded")

        # 11. rate mismatch: 44.1 file interpreted as 48 kHz
        fs_v, _, l_v, r_v = read_wav(variant)
        spec_l, spec_r, _ = spectrum_pair(l_v, r_v, 48000.0, 20.0)
        combined = combined_power(spec_l, spec_r)
        bin_hz = 48000.0 / FFT_N
        k_lo, k_hi = int(17000.0 / bin_hz), int(21000.0 / bin_hz)
        shifted = max(range(k_lo, k_hi), key=lambda k: combined[k]) * bin_hz
        _row(checks, measurements, "rate_mismatch", "44.1 file played at 48 kHz", "python",
             "44.1 data interpreted as 48 kHz",
             "expected: frequencies +8.84% (18.2 → ~19.8 kHz), detection fails",
             {"peak_hz": shifted}, 19000.0 <= shifted <= 20600.0,
             f"strongest peak 17–21 kHz at {shifted:.1f} Hz (expected ~19.81 kHz)",
             kind="degraded")

        # 12-15. LFO variants via our own renderer (carriers only)
        render = render_module()
        variants = [
            ("lfo_10", 10.0, "sine", 10.0, "within spec (10–50 Hz)", "preserved"),
            ("lfo_50", 50.0, "sine", 50.0, "within spec (10–50 Hz)", "preserved"),
            ("lfo_5", 5.0, "sine", 5.0, "outside spec: detection is expected to fail", "info"),
            ("pan_square", 20.0, "square", 20.0, "square panning: detection ok, more artefacts",
             "preserved"),
        ]
        for row_id, lfo_hz, shape, expected_hz, note, kind in variants:
            score2 = copy.deepcopy(score)
            score2["carriers"]["reference"]["pan_lfo_hz"] = lfo_hz
            score2["carriers"]["reference"]["pan_lfo_shape"] = shape
            (mix, _) = render.render_mix(score2, 48000, with_programme=False)
            metrics = _quick_metrics(48000, mix[0], mix[1], expected_colours,
                                     lfo_search_center_hz=lfo_hz)
            detected = (abs(metrics["lfo_freq_hz"] - expected_hz) <= 0.5
                        and metrics["lfo_depth_pct"] >= 50.0)
            _row(checks, measurements, row_id, f"reference pan LFO {lfo_hz:g} Hz ({shape})",
                 "render.py", f"carriers-only, pan_lfo_hz={lfo_hz:g}, shape={shape}", note,
                 metrics, detected,
                 f"LFO {metrics['lfo_freq_hz']:.2f} Hz, depth {metrics['lfo_depth_pct']:.0f}%, "
                 f"ref {metrics['ref_peak_dbfs']:+.1f} dBFS", kind=kind)


# --------------------------------------------------------------------------- #
# Reporting
# --------------------------------------------------------------------------- #
def write_reports(out_dir: Path, score: dict, checks: Checks, measurements: dict,
                  summary: dict, strict: bool):
    report = {
        "experiment": EXPERIMENT,
        "warning": WARNING,
        "scope": ("Encoder + signal integrity of an offline WAV. NO event engine, "
                  "NO memory/budgets and NO hardware (that is phase 2 / the "
                  "A1 signal half; M11–M13 not measured)."),
        "verdict": summary["verdict"],
        "summary": summary,
        "score": {
            "title": score["meta"]["title"], "version": score["meta"]["version"],
            "seed": score["meta"]["seed"], "duration_s": score["meta"]["duration_s"],
            "carriers": score["carriers"], "exposures": score.get("exposures"),
            "envelope_rules": score.get("envelope_rules"),
        },
        "measurements": measurements,
        "checks": checks.items,
        "corruption_advisory": not strict,
        "note": ("The corruption matrix is advisory: only the core checks (F, S, SA, L, "
                 "M1–M10) determine the exit code. With --strict-corruption, corruption "
                 "rows with status ADVISORY-FAIL are also hard-gated. Hardware (M11–M13) "
                 "is not available here."),
    }
    (out_dir / "verify-report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    lines = ["# Verification report — Eerste Gloed (SpectraStrobe PoC)", "",
             "> **EXPERIMENT — not part of NAGLOED v1.0.4.** Offline signal verification of "
             "the encoder. **No event engine, no memory/budgets** (phase 2 / A1 signal "
             "half) and **no hardware** (M11–M13 not measured).",
             "",
             f"- **Score:** {score['meta']['title']} v{score['meta']['version']} · "
             f"seed {score['meta']['seed']} · {score['meta']['duration_s']} s",
             f"- **Carriers:** 18.200 Hz reference "
             f"({float(score['carriers']['reference']['level_dbfs']):+.1f} dBFS peak, "
             f"{float(score['carriers']['reference']['pan_lfo_hz']):g} Hz pan LFO), "
             f"colours 18.7/19.2/19.7 kHz (ceiling "
             f"{float(score['carriers']['max_colour_ratio']) * 100:.0f}% per carrier)",
             f"- **Core outcome:** {summary['verdict']} — {summary['pass']} PASS, "
             f"{summary['fail']} FAIL, {summary['info']} INFO, {summary['skip']} SKIP "
             f"of {summary['total']} core checks",
             f"- **Corruption matrix (advisory):** {summary['corruption_total']} rows — "
             f"{summary['corruption_pass']} PASS, "
             f"{summary['corruption_expected_degraded']} EXPECTED-DEGRADED, "
             f"{summary['corruption_info']} INFO, {summary['corruption_advisory_fail']} "
             f"ADVISORY-FAIL, {summary['corruption_skip']} SKIP",
             ""]
    lines += ["## Scope and limits", "",
              "- This verifies the **encoder + signal integrity**: carriers, levels, "
              "colour coupling, crosstalk, LFO, band-limiting, headroom and determinism.",
              "- **Not** an event engine, memory, budgets or feedback protection; that is "
              "phase 2 and the A1 signal half of the experiment.",
              "- **No hardware:** whether the Kasina recognises the signal and whether the "
              "Ganzfeld shows the intended colours is not measured (M11–M13).",
              "- **No medical/wellness claims.** 'Signal-safe' is about the file, never "
              "about health (R12).",
              "- **Stop/panic:** the offline WAV has no encoder panic and no tested 50 ms "
              "software ramp. The primary stop is human: stop playback + Kasina brightness "
              "low + glasses off (or cover one eye).",
              "- **Inaudibility** of 18–20 kHz is an open assumption (U15: age, hearing, "
              "transducers, playback level), not a fact.",
              "- **20 Hz reference panning** is prescribed by the format; there is no "
              "intended luminance modulation in the colour channels. That remains an open "
              "assumption.",
              ""]
    lines += ["## Core checks", "",
              "| # | Group | Check | Criterion | Measured | Status |",
              "|---|---|---|---|---|---|"]
    for item in checks.items:
        if item["group"] == "M14":
            continue
        lines.append(f"| {_cell(item['id'])} | {_cell(GROUP_LABELS.get(item['group'], item['group']))} | "
                     f"{_cell(item['description'])} | {_cell(item['criterion'])} | "
                     f"{_cell(item['measured'])} | **{item['status']}** |")
    lines += ["", "## Core measurements", "", "| Measurement | Value |", "|---|---|"]
    for label, value in measurements.get("m1", {}).items():
        lines.append(f"| M1 frequencies ({label}) | ref {value['reference_hz']:.2f} Hz; "
                     + ", ".join(f"{k} {v:.2f} Hz" for k, v in value["colours_hz"].items()) + " |")
    for label, value in measurements.get("m2", {}).items():
        lines.append(f"| M2 reference ({label}) | **peak {value['reference_peak_amplitude_dbfs']:+.2f} dBFS** "
                     f"(criterion); band RMS informative "
                     f"{value['band_rms_dbfs_informative']:+.2f} dBFS; variation "
                     f"{value['variation_db']:.2f} dB |")
    for label, rows in measurements.get("m3", {}).items():
        for row in rows:
            parts = ", ".join(f"{key} ΔL {value['delta_db_L']:+.2f} dB"
                              for key, value in row["colours"].items())
            lines.append(f"| M3 brightness ({label}, {row['event']}) | {parts} |")
    if "m3_overall" in measurements:
        coupling = measurements["m3_overall"]
        lines.append(f"| M3 max coupling whole render (48k) | per carrier "
                     + ", ".join(f"{k} {v * 100:.1f}%" for k, v in coupling["per_colour"].items())
                     + f"; **sum per ear L {coupling['max_sum_per_ear']['L'] * 100:.1f}% / "
                     f"R {coupling['max_sum_per_ear']['R'] * 100:.1f}%** (informative) |")
    for label, value in measurements.get("m4", {}).items():
        lines.append(f"| M4 balance/isolation ({label}) | centre {value['balance_db']:.2f} dB, "
                     f"hard-pan isolation {value['isolation_db']:.1f} dB |")
    for label, value in measurements.get("m5", {}).items():
        lines.append(f"| M5 pan LFO ({label}) | {value['freq_hz']:.2f} Hz, "
                     f"depth {value['depth_pct']:.1f}%, corr {value['corr']:.2f}, "
                     f"rest {value['rest_db']:.1f} dB |")
    if "m6" in measurements:
        for key, value in measurements["m6"]["probes"].items():
            others = ", ".join(f"{k} {v:.1f} dB" for k, v in value["other_bands_db"].items()
                               if k != key and v != -999.0)
            lines.append(f"| M6 inter-carrier ({key} active) | own band "
                         f"{value['own_band_dbfs']:.1f} dBFS; other bands: {others} "
                         f"(measurement floor: ideal sines, decoder bandwidth unknown) |")
    if "m7" in measurements:
        for label, value in measurements["m7"].items():
            lines.append(f"| M7 programme leakage ({label}) | 17–20 kHz "
                         f"{max(value['programme_17k_20k_dbfs_L'], value['programme_17k_20k_dbfs_R']):.1f} dBFS; "
                         f"programme-only carrier floor "
                         f"{value['program_only_carrier_floor_dbfs']:.1f} dBFS |")
    if "m7_broadband" in measurements:
        bb = measurements["m7_broadband"]
        lines.append(f"| M7 broadband test (INFO) | attenuation relative to 1 kHz: "
                     + ", ".join(f"{k} Hz {v:+.1f} dB" for k, v in bb["attenuation_db_at"].items())
                     + " — -60 dBFS requirement for broadband v1 material **not demonstrated** |")
    for label, value in measurements.get("m8", {}).items():
        lines.append(f"| M8 noise floor colour bands ({label}) | "
                     f"{value['worst_colour_band_noise_dbfs']:.1f} dBFS |")
    for label, value in measurements.get("m9", {}).items():
        lines.append(f"| M9 peak ({label}) | sample {value['sample_peak_dbfs']:+.2f} dBFS, "
                     f"true peak {value['true_peak_dbtp']:+.2f} dBTP, clipped {value['clipped']} |")
    if "m10" in measurements:
        identical = sum(1 for item in measurements["m10"] if item["identical"])
        lines.append(f"| M10 determinism | {identical}/{len(measurements['m10'])} files "
                     f"byte-identical |")
    if "safety_clamp" in measurements:
        sc = measurements["safety_clamp"]
        lines.append(f"| SA clamp (adversarial rgb=0.90) | measured max "
                     f"{sc['measured_max_ratio'] * 100:.2f}%; clamp log "
                     f"{len(sc['clamp_log'])} entries; clamped={sc['clamped']} |")
    for name, value in measurements.get("exposure_ladder", {}).items():
        red = value["per_colour"].get("red", {})
        lines.append(f"| Ladder {name} | scale {value['scale']:.4f}; red measured max "
                     f"{red.get('measured_max', 0.0) * 100:.2f}% (nominal "
                     f"{value.get('nominal_max_ratio', 0.0) * 100:.2f}%); max deviation "
                     f"{value['worst_delta_db']:.2f} dB |")
    lines += ["", "## Corruption matrix (M14, advisory)", "",
              "Rows are never called FAIL: PASS = expectation met, EXPECTED-DEGRADED = "
              "demonstrable degradation as predicted, INFO = measurement without verdict, "
              "ADVISORY-FAIL = expectation not met. Only `--strict-corruption` makes "
              "ADVISORY-FAIL affect the exit code. The matrix is therefore advisory; "
              "colour ratios are included in the preserved rows.", "",
              "| Variant | Expectation | Measured | Status |", "|---|---|---|---|"]
    for row in measurements.get("m14", []):
        lines.append(f"| {_cell(row['label'])} | {_cell(row['expectation'])} | "
                     f"{_cell(row['observed'])} | **{row['status']}** |")
    lines += ["", "## Stop, ladder and honest limits", "",
              "- **Primary stop (offline):** stop playback, Kasina brightness to minimum, "
              "glasses off or cover one eye. There is **no** tested software panic/50 ms "
              "ramp in this offline version; that belongs to a later realtime encoder "
              "(SPECTRASTROBE §6, phase B).",
              "- **Brightness ladder:** dim 6.25% → mid 12.5% → full 20%, then 48 kHz "
              "and DeepVision. See HARDWARE-PROTOCOL.md.",
              "- **Inaudibility** of the carriers is open assumption U15 (age, hearing, "
              "transducers, level); do a comfort listening test before the session.",
              "- **Proven:** carrier frequencies, peak amplitude, colour ratios, clamp, "
              "inter-carrier isolation, LFO, band-limiting of this material, headroom and "
              "determinism on this machine (CPython stdlib).",
              "- **Not proven:** hardware detection, perceived colour/brightness, "
              "broadband v1 band-limiting (INFO only), DAW/host behaviour and lossy "
              "playback chains outside this matrix."]
    (out_dir / "verify-report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report


# --------------------------------------------------------------------------- #
# Main
# --------------------------------------------------------------------------- #
def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="NAGLOED v2 SpectraStrobe PoC verification (EXPERIMENT)")
    parser.add_argument("--out-dir", default=str(POC_DIR / "out"))
    parser.add_argument("--score", default=str(POC_DIR / "score.json"))
    parser.add_argument("--render-script", default=str(POC_DIR / "render.py"))
    parser.add_argument("--skip-corruption", action="store_true")
    parser.add_argument("--strict-corruption", action="store_true")
    args = parser.parse_args(argv)

    out_dir = Path(args.out_dir)
    score_path = Path(args.score)
    render_script = Path(args.render_script)
    score = json.loads(score_path.read_text(encoding="utf-8"))
    duration = float(score["meta"]["duration_s"])
    checks = Checks()
    measurements = {}

    names = ["spectrastrobe-48k-24bit.wav", "spectrastrobe-44k1-16bit.wav",
             "spectrastrobe-mid-44k1-16bit.wav", "spectrastrobe-dim-44k1-16bit.wav",
             "program-only-48k-24bit.wav", "program-only-44k1-16bit.wav"]
    missing = [name for name in names if not (out_dir / name).exists()]
    if missing:
        print(f"ERROR: missing renders in {out_dir}: {', '.join(missing)}", file=sys.stderr)
        print("Run first: python3 render.py", file=sys.stderr)
        return 2

    fs_m, width_m, l_m, r_m = read_wav(out_dir / names[0])
    fs_v, width_v, l_v, r_v = read_wav(out_dir / names[1])
    fs_mid, width_mid, l_mid, r_mid = read_wav(out_dir / names[2])
    fs_dim, width_dim, l_dim, r_dim = read_wav(out_dir / names[3])
    fs_p, width_p, l_p, r_p = read_wav(out_dir / names[4])
    fs_p44, width_p44, l_p44, r_p44 = read_wav(out_dir / names[5])

    format_check(checks, "48k", 48000, 3, duration, fs_m, width_m, len(l_m))
    format_check(checks, "44k1", 44100, 2, duration, fs_v, width_v, len(l_v))
    format_check(checks, "mid-44k1", 44100, 2, duration, fs_mid, width_mid, len(l_mid))
    format_check(checks, "dim-44k1", 44100, 2, duration, fs_dim, width_dim, len(l_dim))
    format_check(checks, "program-only-48k", 48000, 3, duration, fs_p, width_p, len(l_p))
    format_check(checks, "program-only-44k1", 44100, 2, duration, fs_p44, width_p44, len(l_p44))

    score_checks(checks, score)
    analyse_core(checks, measurements, score, fs_m, l_m, r_m, "48k",
                 exhaustive=True, intercarrier=True)
    ref_amplitude_48 = float(
        measurements["m2"]["48k"]["reference_amplitude_linear_fft"])
    analyse_core(checks, measurements, score, fs_v, l_v, r_v, "44k1",
                 exhaustive=True, intercarrier=False)
    programme_checks(checks, measurements, score, (l_p, r_p), (l_p44, r_p44), fs_p, fs_p44)
    safety_clamp_check(checks, measurements, score)
    exposure_checks(checks, measurements, score,
                    {"full": (fs_v, None, l_v, r_v), "mid": (fs_mid, None, l_mid, r_mid),
                     "dim": (fs_dim, None, l_dim, r_dim)}, ref_amplitude_48)
    determinism_check(checks, measurements, render_script, score_path, out_dir, names)
    manifest_check(checks, measurements, out_dir, render_script, Path(__file__).resolve(), names)

    if not args.skip_corruption:
        corruption_matrix(checks, measurements, score, out_dir, names[0], names[1])

    summary = checks.summary()
    write_reports(out_dir, score, checks, measurements, summary, strict=args.strict_corruption)

    core_fail = summary["fail"]
    advisory_fail = summary["corruption_advisory_fail"]
    print("NAGLOED v2 — SpectraStrobe PoC verification (EXPERIMENT, not v1.0.4)")
    print(f"score        : {score_path.name} v{score['meta']['version']} "
          f"(seed {score['meta']['seed']}, {duration:.1f} s)")
    print("scope        : encoder + signal integrity; NO event engine/memory/hardware")
    print("")
    for item in checks.items:
        prefix = "" if item["group"] == "M14" else "  "
        print(f"{prefix}{item['id']:<26} {item['status']:<5} {_format(item['measured'])}")
    print("")
    print(f"CORE           : {summary['verdict']} ({summary['pass']} PASS, {core_fail} FAIL, "
          f"{summary['info']} INFO, {summary['skip']} SKIP of {summary['total']})")
    if not args.skip_corruption:
        print(f"CORRUPTION     : {summary['corruption_total']} rows — "
              f"{summary['corruption_pass']} PASS, "
              f"{summary['corruption_expected_degraded']} EXPECTED-DEGRADED, "
              f"{summary['corruption_info']} INFO, {advisory_fail} ADVISORY-FAIL, "
              f"{summary['corruption_skip']} SKIP"
              + (" (hard-gated)" if args.strict_corruption else " (advisory)"))
    print(f"report         : {out_dir.resolve()}/verify-report.json and verify-report.md")
    failed = core_fail > 0 or (args.strict_corruption and advisory_fail > 0)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
