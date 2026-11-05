#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

#include "core/Parameters.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace klopgeest
{
class KlopgeestProcessor;
}

namespace klopgeest::ui
{

// -----------------------------------------------------------------------------
// The performance strip: transport, clock, personality and the mutation keys.
// -----------------------------------------------------------------------------
class TopBar final : public juce::Component
{
public:
    explicit TopBar (KlopgeestProcessor& processor);

    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

    std::function<void()> onPatternChanged;

private:
    void loadPatternList();
    void setPatternLength (int length);
    void paintWordmark (juce::Graphics&, juce::Rectangle<float>);

    KlopgeestProcessor& processor;

    TransportButton transport;
    ParamKnob swingKnob;
    ParamKnob humaniseKnob;
    ParamKnob stabilityKnob;
    ParamKnob mutateAmountKnob;
    SegmentedControl personality;
    juce::ComboBox patternBox;
    SegmentedControl lengthControl;
    SeedField seedField;
    KlopButton nudgeButton { "NUDGE", KlopButton::Variant::amber };
    KlopButton mutateButton { "MUTATE", KlopButton::Variant::cyan };
    KlopButton recallButton { "RECALL", KlopButton::Variant::ghost };
    ParamButton evolveButton;

    int lastPatternIndex = -2;
    bool lastPlaying = false;
    bool lastHostSynced = false;
    float lastBpm = -1.0f;
    float playPulse = 0.0f;

    juce::Rectangle<float> wordmarkArea;
    juce::Rectangle<float> clockArea;

    float separatorX[4] {};
};

// -----------------------------------------------------------------------------
// Everything about the selected step: the five per-step values, the step lock
// and the lane loop length.
// -----------------------------------------------------------------------------
class StepInspector final : public juce::Component
{
public:
    explicit StepInspector (KlopgeestProcessor& processor);

    void setSelection (int voice, int step);
    void refresh();

    void resized() override;
    void paint (juce::Graphics&) override;

    std::function<void()> onPatternEdited;

private:
    void applyStepEdit (const std::function<void (Step&, Lane&, PatternData&)>& edit);
    void refreshValues (bool force);

    KlopgeestProcessor& processor;
    int voice = 0;
    int step = 0;

    ValueKnob velocityKnob;
    ValueKnob probabilityKnob;
    ValueKnob microKnob;
    ValueKnob pitchKnob;
    ValueKnob ratchetKnob;

    KlopButton onButton { "ON", KlopButton::Variant::amber };
    KlopButton stepLockButton { "LOCK", KlopButton::Variant::amber };

    KlopButton muteButton { "MUTE", KlopButton::Variant::red };
    KlopButton soloButton { "SOLO", KlopButton::Variant::cyan };
    KlopButton laneLockButton { "LOCK", KlopButton::Variant::amber };

    ValueKnob laneLoopKnob;

    juce::Rectangle<float> headerArea;
    juce::Rectangle<float> knobsArea;
    juce::Rectangle<float> compoundArea;
    juce::Rectangle<float> laneArea;
    juce::Rectangle<float> laneButtonArea;
    juce::Rectangle<float> loopKnobArea;
};

// -----------------------------------------------------------------------------
// Voice panel: all fifteen parameters plus the honest "why this hit differs"
// telemetry readout.
// -----------------------------------------------------------------------------
class VoicePanel final : public juce::Component
{
public:
    explicit VoicePanel (KlopgeestProcessor& processor);

    void setVoice (int voice);
    int getVoice() const noexcept { return voice; }

    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void rebindKnobs();
    void paintReadout (juce::Graphics&, juce::Rectangle<float>);

    KlopgeestProcessor& processor;
    int voice = 0;

    std::array<std::unique_ptr<ParamKnob>, kNumVoiceParams> knobs;
    LevelMeter meter { false, false };

    juce::Rectangle<float> leftArea;
    juce::Rectangle<float> knobsArea;
    juce::Rectangle<float> meterArea;
    juce::Rectangle<float> readoutArea;
    juce::Rectangle<float> nameArea;
    juce::Rectangle<float> flagsArea;

    float lastVelocity = 0.0f;
    float lastTimingMs = 0.0f;
    float lastPitchSemis = 0.0f;
    float lastDecayScale = 1.0f;
    unsigned int lastFlags = 0;
    unsigned int lastTriggerCount = 0;
    float hitFlash = 0.0f;
};

// -----------------------------------------------------------------------------
// Groove panel: the controlled-instability section of the instrument.
// -----------------------------------------------------------------------------
class GroovePanel final : public juce::Component
{
public:
    explicit GroovePanel (KlopgeestProcessor& processor);

    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void paintExplainer (juce::Graphics&, juce::Rectangle<float>);

    KlopgeestProcessor& processor;

    DriftScope scope;
    ParamKnob swingKnob;
    ParamKnob humaniseKnob;
    ParamKnob stabilityKnob;
    ParamKnob ghostKnob;
    ParamKnob accentKnob;
    ParamKnob mutateAmountKnob;
    SegmentedControl personality;
    SeedField seedField;
    std::array<std::unique_ptr<ParamKnob>, kNumVoices> biasKnobs;

    juce::Rectangle<float> leftArea;
    juce::Rectangle<float> scopeArea;
    juce::Rectangle<float> explainArea;
    juce::Rectangle<float> row1Area;
    juce::Rectangle<float> row2Area;
};

// -----------------------------------------------------------------------------
// Master glue: compression, saturation, macros and the meters.
// -----------------------------------------------------------------------------
class GluePanel final : public juce::Component
{
public:
    explicit GluePanel (KlopgeestProcessor& processor);

    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    KlopgeestProcessor& processor;

    ParamKnob glueKnob;
    ParamKnob heatKnob;
    ParamKnob thresholdKnob;
    ParamKnob ratioKnob;
    ParamKnob attackKnob;
    ParamKnob releaseKnob;
    ParamKnob kneeKnob;
    ParamKnob sidechainKnob;
    ParamKnob detectKnob;
    ParamKnob mixKnob;
    ParamKnob satPreKnob;
    ParamKnob satPostKnob;
    ParamKnob outputKnob;
    ParamKnob ceilingKnob;

    ParamButton autoReleaseButton;
    ParamButton limiterButton;

    GainReductionMeter grMeter;
    LevelMeter outputMeter { true, true };

    juce::Rectangle<float> macroArea;
    juce::Rectangle<float> compressorArea;
    juce::Rectangle<float> meterArea;
    juce::Rectangle<float> grArea;
    juce::Rectangle<float> outputMeterArea;
};

} // namespace klopgeest::ui
