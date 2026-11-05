#include "core/Parameters.h"

#include <functional>

namespace klopgeest
{

namespace
{
using FloatTextFn = std::function<juce::String (float, int)>;

juce::String formatPercent (float v, int)  { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
juce::String formatDb (float v, int)       { return juce::String (v, 1) + " dB"; }
juce::String formatMs (float v, int)       { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
juce::String formatHz (float v, int)       { return juce::String (juce::roundToInt (v)) + " Hz"; }
juce::String formatSemis (float v, int)    { return juce::String (v, 1) + " st"; }
juce::String formatRatio (float v, int)    { return juce::String (v, 1) + ":1"; }
juce::String formatBpm (float v, int)      { return juce::String (v, 1) + " BPM"; }
juce::String formatSwing (float v, int)    { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

juce::String formatPan (float v, int)
{
    if (std::abs (v) < 0.005f) return "C";
    return juce::String (std::abs (v) * 100.0f, 0) + (v < 0.0f ? " L" : " R");
}

juce::String formatDetect (float v, int)
{
    const auto pct = juce::roundToInt (v * 100.0f);
    return juce::String (pct) + "% " + (pct < 50 ? "PEAK" : "RMS");
}

void addFloat (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
               const juce::String& id, const juce::String& name, const ParamRange& range,
               float defaultValue, const juce::String& label, FloatTextFn text)
{
    auto attrs = juce::AudioParameterFloatAttributes().withLabel (label);
    if (text != nullptr)
        attrs = attrs.withStringFromValueFunction (std::move (text));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, makeRange (range), defaultValue, attrs));
}

ParamRange rangeForVoiceParam (int paramIndex)
{
    switch (paramIndex)
    {
        case VPitch:     return ranges::pitch;
        case VDecay:     return ranges::unit;
        case VTone:      return ranges::unit;
        case VTransient: return ranges::unit;
        case VDrive:     return ranges::unit;
        case VLevel:     return ranges::levelDb;
        case VPan:       return ranges::pan;
        case VSpecialA:
        case VSpecialB:  return ranges::unit;
        case VRandPitch:
        case VRandLevel:
        case VRandDecay:
        case VRandTransient:
        case VRandTiming: return ranges::randAmount;
        case VBias:      return ranges::timingBias;
        default:         return ranges::unit;
    }
}

float defaultForVoiceParam (int voice, int paramIndex)
{
    const auto& d = voiceDefaults (voice);
    switch (paramIndex)
    {
        case VPitch:     return d.pitch;
        case VDecay:     return d.decay;
        case VTone:      return d.tone;
        case VTransient: return d.transient;
        case VDrive:     return d.drive;
        case VLevel:     return d.levelDb;
        case VPan:       return d.pan;
        case VSpecialA:  return d.specialA;
        case VSpecialB:  return d.specialB;
        case VRandPitch: return 0.18f;
        case VRandLevel: return 0.30f;
        case VRandDecay: return 0.22f;
        case VRandTransient: return 0.20f;
        case VRandTiming: return 0.28f;
        case VBias:      return 0.0f;
        default:         return 0.0f;
    }
}

FloatTextFn textForVoiceParam (int paramIndex)
{
    switch (paramIndex)
    {
        case VPitch:       return formatSemis;
        case VLevel:       return formatDb;
        case VPan:         return formatPan;
        case VBias:        return formatMs;
        case VDecay:
        case VTone:
        case VTransient:
        case VDrive:
        case VSpecialA:
        case VSpecialB:
        case VRandPitch:
        case VRandLevel:
        case VRandDecay:
        case VRandTransient:
        case VRandTiming:  return formatPercent;
        default:           return nullptr;
    }
}

/** A stable human-readable parameter name, e.g. "Kick Pitch". */
juce::String voiceParamName (int voice, int paramIndex)
{
    juce::String n (voiceName (voice));
    n = n.toLowerCase().replaceCharacter (0, (juce::juce_wchar) juce::CharacterFunctions::toUpperCase (n[0]));
    n << " " << voiceParamLabel (voice, paramIndex);
    return n;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // --- Voice shaping -------------------------------------------------------
    for (int v = 0; v < kNumVoices; ++v)
    {
        for (int p = 0; p < kNumVoiceParams; ++p)
        {
            const auto id    = pid::voiceId (v, detail::kVoiceParamSuffix[p]);
            const auto name  = voiceParamName (v, p);
            const auto range = rangeForVoiceParam (p);
            const auto def   = defaultForVoiceParam (v, p);
            juce::String label (voiceParamLabel (v, p));
            label = label.toLowerCase();
            addFloat (layout, id, name, range, def, label, textForVoiceParam (p));
        }
    }

    // --- Global performance --------------------------------------------------
    addFloat (layout, pid::tempo, "Tempo", ranges::tempo, ranges::tempo.def, "bpm", formatBpm);

    auto swingRange = ranges::swing;
    addFloat (layout, pid::swing, "Swing", swingRange, swingRange.def, "%", formatSwing);
    addFloat (layout, pid::humanize, "Humanize", ranges::humanize, ranges::humanize.def, "%", formatPercent);
    addFloat (layout, pid::stability, "Stability", ranges::stability, ranges::stability.def, "%", formatPercent);
    addFloat (layout, pid::ghost, "Ghost Notes", ranges::ghost, ranges::ghost.def, "%", formatPercent);
    addFloat (layout, pid::accent, "Accent", ranges::accent, ranges::accent.def, "%", formatPercent);
    addFloat (layout, pid::mutateAmount, "Mutate Amount", ranges::mutateAmount, ranges::mutateAmount.def, "%", formatPercent);

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { pid::personality, 1 }, "Personality", personalityNames(),
        (int) Personality::loose));

    auto seedAttrs = juce::AudioParameterIntAttributes()
                        .withLabel ("seed")
                        .withStringFromValueFunction ([] (int value, int) { return juce::String (value); });
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { pid::seed, 1 }, "Performance Seed", 0, 999999, 12345, seedAttrs));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { pid::evolve, 1 }, "Evolve", false));

    // --- Master glue machine -------------------------------------------------
    addFloat (layout, pid::threshold, "Threshold", ranges::threshold, ranges::threshold.def, "dB", formatDb);
    addFloat (layout, pid::ratio, "Ratio", ranges::ratio, ranges::ratio.def, ":1", formatRatio);
    addFloat (layout, pid::attack, "Attack", ranges::attackMs, ranges::attackMs.def, "ms", formatMs);
    addFloat (layout, pid::release, "Release", ranges::releaseMs, ranges::releaseMs.def, "ms", formatMs);
    addFloat (layout, pid::knee, "Knee", ranges::knee, ranges::knee.def, "dB", formatDb);
    addFloat (layout, pid::sidechainHz, "Sidechain HPF", ranges::sidechainHz, ranges::sidechainHz.def, "Hz", formatHz);
    addFloat (layout, pid::detect, "Detection", ranges::unit, 0.5f, "peak/rms", formatDetect);
    addFloat (layout, pid::mix, "Parallel Mix", ranges::unit, 1.0f, "%", formatPercent);
    addFloat (layout, pid::satPre, "Pre Saturation", ranges::unit, 0.15f, "%", formatPercent);
    addFloat (layout, pid::satPost, "Post Saturation", ranges::unit, 0.10f, "%", formatPercent);
    addFloat (layout, pid::output, "Output", ranges::outputDb, ranges::outputDb.def, "dB", formatDb);
    addFloat (layout, pid::ceiling, "Ceiling", ranges::ceilingDb, ranges::ceilingDb.def, "dB", formatDb);
    addFloat (layout, pid::glue, "Glue", ranges::unit, 0.35f, "%", formatPercent);
    addFloat (layout, pid::heat, "Heat", ranges::unit, 0.25f, "%", formatPercent);

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { pid::autoRelease, 1 }, "Auto Release", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { pid::limiter, 1 }, "Safe Limiter", true));

    return layout;
}

} // namespace klopgeest
