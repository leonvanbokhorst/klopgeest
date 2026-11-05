#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

#include "ui/Theme.h"

namespace klopgeest
{
class KlopgeestProcessor;
}

namespace klopgeest::ui
{

// -----------------------------------------------------------------------------
// A compact, hand-drawn button. The same widget serves transport actions,
// parameter toggles and the tiny mute/solo/lock lane cells.
// -----------------------------------------------------------------------------
class KlopButton : public juce::Button
{
public:
    enum class Variant { ghost, amber, cyan, red };

    explicit KlopButton (const juce::String& text = {}, Variant variant = Variant::ghost);

    void setVariant (Variant newVariant);
    Variant getVariant() const noexcept { return variant; }

    void setFontHeight (float newHeight);
    void setCornerRadius (float newRadius);
    void setTracking (float newTracking);
    void setCompact (bool shouldBeCompact);

    /** Optional model-driven state, for buttons that edit pattern data rather
        than an APVTS parameter. */
    void setStateProvider (std::function<bool()> provider);

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;

private:
    bool effectiveToggleState() const;
    Variant variant = Variant::ghost;
    float fontHeight = 10.0f;
    float cornerRadius = 4.0f;
    float tracking = 0.5f;
    bool compact = false;
    std::function<bool()> stateProvider;
};

// -----------------------------------------------------------------------------
// Big PLAY / STOP transport key with a breathing cyan ring while running.
// -----------------------------------------------------------------------------
class TransportButton final : public juce::Button
{
public:
    TransportButton();
    void setPlaying (bool shouldBePlaying);
    void setPulse (float newPulse);
    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;

private:
    bool playing = false;
    float pulse = 0.0f;
};

// -----------------------------------------------------------------------------
// APVTS rotary: label above, machined knob, monospaced readout below.
// -----------------------------------------------------------------------------
class ParamKnob final : public juce::Component
{
public:
    ParamKnob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
               const juce::String& label, juce::Colour accent = col::amber);

    void setParamId (const juce::String& parameterId);
    void setLabelText (juce::String newLabel);
    void setAccent (juce::Colour newAccent);

    juce::Slider& getSlider() noexcept { return slider; }
    const juce::String& getLabelText() const noexcept { return label; }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::AudioProcessorValueTreeState& state;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String label;
    juce::String parameterId;
    juce::Colour accent;
};

// -----------------------------------------------------------------------------
// Rotary that edits pattern data (step values) instead of an APVTS parameter.
// -----------------------------------------------------------------------------
class ValueKnob final : public juce::Component
{
public:
    ValueKnob (juce::String label, juce::NormalisableRange<double> range,
               std::function<juce::String (double)> textFunction,
               juce::Colour accent = col::cyan);

    void setValue (double value, juce::NotificationType notification);
    double getValue() const;
    bool isDragging() const noexcept { return dragging; }

    void setTextFunction (std::function<juce::String (double)> function);
    void setRange (juce::NormalisableRange<double> range);
    void setLabelText (juce::String newLabel);
    void setAccent (juce::Colour newAccent);

    std::function<void (double)> onValueChanged;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Slider slider;
    juce::String label;
    std::function<juce::String (double)> textFn;
    juce::Colour accent;
    bool dragging = false;
};

// -----------------------------------------------------------------------------
// Toggle button wired directly to a boolean APVTS parameter.
// -----------------------------------------------------------------------------
class ParamButton final : public KlopButton
{
public:
    ParamButton (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                 const juce::String& text, Variant variant = Variant::ghost);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// -----------------------------------------------------------------------------
// Thin segmented selector (personality, pattern length).
// -----------------------------------------------------------------------------
class SegmentedControl final : public juce::Component
{
public:
    SegmentedControl (juce::StringArray items, juce::Colour accent = col::amber);

    void setSelectedIndex (int index, bool notify);
    int getSelectedIndex() const noexcept { return selected; }

    void setAccent (juce::Colour newAccent);
    void setFontHeight (float newHeight);

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    int indexAt (int x) const;

    juce::StringArray items;
    juce::Colour accent;
    int selected = 0;
    int hovered = -1;
    float fontHeight = 9.5f;
};

// -----------------------------------------------------------------------------
// VOICE / GROOVE / GLUE tab selector.
// -----------------------------------------------------------------------------
class TabStrip final : public juce::Component
{
public:
    explicit TabStrip (juce::StringArray labels);

    void setActive (int index, bool notify);
    int getActive() const noexcept { return active; }

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    int indexAt (int x) const;
    juce::StringArray items;
    int active = 0;
    int hovered = -1;
};

// -----------------------------------------------------------------------------
// Performance seed: editable number plus a randomise key.
// -----------------------------------------------------------------------------
class SeedField final : public juce::Component
{
public:
    explicit SeedField (KlopgeestProcessor& processor);
    ~SeedField() override = default;

    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void commitText();
    void syncTextFromParameter();
    void randomise();

    KlopgeestProcessor& processor;
    juce::RangedAudioParameter* parameter = nullptr;
    juce::TextEditor editor;
    KlopButton dice { "DICE", KlopButton::Variant::amber };
    std::unique_ptr<juce::ParameterAttachment> attachment;
    bool updating = false;
};

// -----------------------------------------------------------------------------
// dB output meter, mono or stereo, with peak hold.
// -----------------------------------------------------------------------------
class LevelMeter final : public juce::Component
{
public:
    LevelMeter (bool stereo, bool showScale);

    void setLevelsDb (float peakL, float peakR, float rmsL, float rmsR);
    void paint (juce::Graphics&) override;

private:
    bool stereo = true;
    bool showScale = false;
    float dispRms[2] { -100.0f, -100.0f };
    float dispPeak[2] { -100.0f, -100.0f };
    int holdFrames[2] { 0, 0 };
};

// -----------------------------------------------------------------------------
// Gain-reduction meter: 0 dB at the top, -24 dB at the bottom.
// -----------------------------------------------------------------------------
class GainReductionMeter final : public juce::Component
{
public:
    void setGainReductionDb (float reductionDb);
    float getDisplayDb() const noexcept { return display; }
    void paint (juce::Graphics&) override;

private:
    float display = 0.0f;
};

// -----------------------------------------------------------------------------
// "Controlled instability" scatter: where each voice actually landed relative
// to the grid, lit up on every hit.
// -----------------------------------------------------------------------------
class DriftScope final : public juce::Component
{
public:
    explicit DriftScope (KlopgeestProcessor& processor);

    void refresh();
    void paint (juce::Graphics&) override;

private:
    KlopgeestProcessor& processor;
    float timing[kNumVoices] {};
    float velocity[kNumVoices] {};
    float pitch[kNumVoices] {};
    float flash[kNumVoices] {};
    unsigned int triggerCount[kNumVoices] {};
};

} // namespace klopgeest::ui
