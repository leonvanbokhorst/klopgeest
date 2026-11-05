#include "ui/Widgets.h"

#include <cmath>

#include "core/ParamIDs.h"
#include "core/Parameters.h"
#include "plugin/KlopgeestProcessor.h"

namespace klopgeest::ui
{

namespace
{
juce::Colour variantColour (KlopButton::Variant variant)
{
    switch (variant)
    {
        case KlopButton::Variant::amber: return col::amber;
        case KlopButton::Variant::cyan:  return col::cyan;
        case KlopButton::Variant::red:   return col::red;
        case KlopButton::Variant::ghost:
        default:                         return col::textDim;
    }
}

float fittedFontHeight (const juce::String& text, juce::Font font, float maxWidth,
                        float minHeight)
{
    auto height = font.getHeight();

    while (height > minHeight
           && juce::GlyphArrangement::getStringWidth (font, text) > maxWidth)
    {
        height -= 0.5f;
        font = juce::Font (juce::FontOptions (height,
                                              font.getStyleFlags() & juce::Font::bold));
    }

    return height;
}

void drawSmallLabel (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                     float height, juce::Colour colour, float tracking = 0.0f)
{
    auto font = ty::sans (height, true);
    const auto usable = area.getWidth() - 2.0f;

    if (juce::GlyphArrangement::getStringWidth (font, text) > usable)
        font = ty::sans (fittedFontHeight (text, font, usable, 6.0f), true);

    drawSpacedText (g, text, area, font, colour, juce::Justification::centred, tracking);
}

void layoutKnob (juce::Slider& slider, juce::Component& owner)
{
    const auto h = (float) owner.getHeight();
    const auto labelH = juce::jlimit (8.0f, 12.0f, h * 0.15f);
    const auto valueH = juce::jlimit (9.0f, 14.0f, h * 0.17f);

    auto r = owner.getLocalBounds();
    r.removeFromTop ((int) std::ceil (labelH));
    r.removeFromBottom ((int) std::ceil (valueH));

    const auto d = juce::jmax (8, juce::jmin (r.getWidth(), r.getHeight()));
    slider.setBounds (r.withSizeKeepingCentre (d, d));
}

void paintKnobText (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& label,
                    const juce::String& value, juce::Colour labelColour, juce::Colour valueColour)
{
    const auto h = bounds.getHeight();

    if (h < 14.0f || bounds.getWidth() < 20.0f)
        return;

    const auto labelH = juce::jlimit (8.0f, 12.0f, h * 0.15f);
    const auto valueH = juce::jlimit (9.0f, 14.0f, h * 0.17f);

    auto labelArea = bounds.removeFromTop (labelH);
    auto valueArea = bounds.removeFromBottom (valueH);

    drawSmallLabel (g, labelArea, label, juce::jlimit (6.0f, 9.0f, labelH * 0.78f),
                    labelColour, 0.7f);

    auto mono = ty::mono (juce::jlimit (7.5f, 10.5f, valueH * 0.78f), true);

    if (juce::GlyphArrangement::getStringWidth (mono, value) > valueArea.getWidth() - 2.0f)
    {
        const auto newHeight = fittedFontHeight (value, mono, valueArea.getWidth() - 2.0f, 6.0f);
        mono = ty::mono (newHeight, true);
    }

    g.setFont (mono);
    g.setColour (valueColour);
    g.drawText (value, valueArea, juce::Justification::centred, false);
}

float dbToNorm (float db)
{
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
}
} // namespace

// =============================================================================
// KlopButton
// =============================================================================
KlopButton::KlopButton (const juce::String& text, Variant variantToUse)
    : juce::Button (text), variant (variantToUse)
{
    setButtonText (text.toUpperCase());
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void KlopButton::setVariant (Variant newVariant)
{
    variant = newVariant;
    repaint();
}

void KlopButton::setFontHeight (float newHeight)
{
    fontHeight = newHeight;
    repaint();
}

void KlopButton::setCornerRadius (float newRadius)
{
    cornerRadius = newRadius;
    repaint();
}

void KlopButton::setTracking (float newTracking)
{
    tracking = newTracking;
    repaint();
}

void KlopButton::setCompact (bool shouldBeCompact)
{
    compact = shouldBeCompact;
    repaint();
}

void KlopButton::setStateProvider (std::function<bool()> provider)
{
    stateProvider = std::move (provider);
    repaint();
}

bool KlopButton::effectiveToggleState() const
{
    return stateProvider != nullptr ? stateProvider() : getToggleState();
}

void KlopButton::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    if (bounds.getWidth() < 2.0f || bounds.getHeight() < 2.0f)
        return;

    const auto on = effectiveToggleState();
    const auto accent = variantColour (variant);
    const auto radius = compact ? juce::jmin (3.0f, bounds.getHeight() * 0.3f)
                                : juce::jmin (cornerRadius, bounds.getHeight() * 0.3f);

    juce::Colour fill;
    juce::Colour border;
    juce::Colour textColour;

    if (on)
    {
        fill = accent.withMultipliedAlpha (isEnabled() ? 0.92f : 0.35f);
        border = accent.brighter (0.25f);
        textColour = col::bgDeep;
    }
    else
    {
        fill = shouldDrawButtonAsDown ? col::panelLow
                                      : (shouldDrawButtonAsHighlighted ? col::offCellHi
                                                                       : col::offCell);

        if (! isEnabled())
            border = col::line;
        else if (shouldDrawButtonAsHighlighted)
            border = accent.withAlpha (0.65f);
        else
            border = variant == Variant::ghost ? col::line : accent.withAlpha (0.28f);

        textColour = ! isEnabled() ? col::textFaint
                                   : (variant == Variant::ghost ? col::textDim : accent);
    }

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);

    g.setColour (border);
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    g.setColour (juce::Colours::white.withAlpha (on ? 0.0f : 0.035f));
    g.drawHorizontalLine ((int) bounds.getY() + 1, bounds.getX() + radius,
                          bounds.getRight() - radius);

    const auto text = getButtonText().toUpperCase();
    const auto area = bounds.reduced (compact ? 1.5f : 3.0f, 0.0f);

    if (text.isNotEmpty() && area.getWidth() > 4.0f)
        drawSmallLabel (g, area, text,
                        juce::jlimit (compact ? 6.0f : 7.0f, 12.0f,
                                      juce::jmin (fontHeight, bounds.getHeight() * 0.6f)),
                        textColour, compact ? 0.0f : tracking);
}

// =============================================================================
// TransportButton
// =============================================================================
TransportButton::TransportButton()
    : juce::Button ("transport")
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void TransportButton::setPlaying (bool shouldBePlaying)
{
    if (playing != shouldBePlaying)
    {
        playing = shouldBePlaying;
        repaint();
    }
}

void TransportButton::setPulse (float newPulse)
{
    const auto clamped = juce::jlimit (0.0f, 1.0f, newPulse);

    if (std::abs (clamped - pulse) < 0.01f)
        return;

    pulse = clamped;
    repaint();
}

void TransportButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    if (bounds.isEmpty())
        return;

    const auto radius = 6.0f;

    if (playing)
    {
        g.setColour (col::cyan.withAlpha (0.10f + 0.20f * pulse));
        g.drawRoundedRectangle (bounds.expanded (1.0f + pulse * 1.5f), radius + 1.0f, 1.2f);

        juce::ColourGradient fill (col::cyanHi, bounds.getX(), bounds.getY(),
                                   col::cyan, bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (col::cyanHi);
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }
    else
    {
        g.setColour (down ? col::panelLow : (highlighted ? col::offCellHi : col::offCell));
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (col::cyan.withAlpha (highlighted ? 0.85f : 0.55f));
        g.drawRoundedRectangle (bounds, radius, 1.2f);
    }

    const auto iconColour = playing ? col::bgDeep : col::cyan;
    const auto iconArea = bounds.removeFromLeft (30.0f);

    if (playing)
    {
        g.setColour (iconColour);
        g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, 10.0f)
                                    .withCentre (iconArea.getCentre()),
                                1.5f);
    }
    else
    {
        juce::Path triangle;
        const auto c = iconArea.getCentre();
        triangle.addTriangle (c.x - 4.0f, c.y - 6.0f,
                              c.x - 4.0f, c.y + 6.0f,
                              c.x + 6.0f, c.y);
        g.setColour (iconColour);
        g.fillPath (triangle);
    }

    g.setColour (playing ? col::bgDeep.withAlpha (0.9f) : col::cyan);
    g.setFont (ty::sans (juce::jlimit (8.0f, 12.5f, bounds.getHeight() * 0.30f), true));
    g.drawText (playing ? "STOP" : "PLAY", bounds.reduced (4.0f, 0.0f),
                juce::Justification::centredLeft, false);
}

// =============================================================================
// ParamKnob
// =============================================================================
ParamKnob::ParamKnob (juce::AudioProcessorValueTreeState& stateToUse,
                      const juce::String& id, const juce::String& labelToUse,
                      juce::Colour accentToUse)
    : state (stateToUse), label (labelToUse), accent (accentToUse)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (slider);

    setParamId (id);
}

void ParamKnob::setParamId (const juce::String& id)
{
    attachment.reset();
    parameterId = id;

    if (state.getParameter (parameterId) != nullptr)
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, parameterId, slider);

    repaint();
}

void ParamKnob::setLabelText (juce::String newLabel)
{
    label = std::move (newLabel);
    repaint();
}

void ParamKnob::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    repaint();
}

void ParamKnob::resized()
{
    layoutKnob (slider, *this);
}

void ParamKnob::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 12.0f || bounds.getHeight() < 16.0f)
        return;

    const auto text = slider.getTextFromValue (slider.getValue());
    const auto valueText = text.isNotEmpty() ? text : juce::String (slider.getValue(), 2);

    const auto labelColour = accent == col::amber ? col::textDim
                                                  : accent.withMultipliedAlpha (0.85f);

    paintKnobText (g, bounds, label.toUpperCase(), valueText, labelColour,
                   accent.brighter (0.15f));
}

// =============================================================================
// ValueKnob
// =============================================================================
ValueKnob::ValueKnob (juce::String labelToUse, juce::NormalisableRange<double> range,
                      std::function<juce::String (double)> textFunction, juce::Colour accentToUse)
    : label (std::move (labelToUse)), textFn (std::move (textFunction)), accent (accentToUse)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setNormalisableRange (range);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setDoubleClickReturnValue (false, 0.0);

    slider.onValueChange = [this]
    {
        if (! dragging && onValueChanged != nullptr)
            onValueChanged (slider.getValue());
    };

    slider.onDragStart = [this] { dragging = true; };

    slider.onDragEnd = [this]
    {
        dragging = false;
        repaint();
    };

    addAndMakeVisible (slider);
}

void ValueKnob::setValue (double value, juce::NotificationType notification)
{
    if (dragging)
        return;

    slider.setValue (value, notification);
}

double ValueKnob::getValue() const
{
    return slider.getValue();
}

void ValueKnob::setTextFunction (std::function<juce::String (double)> function)
{
    textFn = std::move (function);
    repaint();
}

void ValueKnob::setRange (juce::NormalisableRange<double> range)
{
    slider.setNormalisableRange (range);
}

void ValueKnob::setLabelText (juce::String newLabel)
{
    label = std::move (newLabel);
    repaint();
}

void ValueKnob::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    repaint();
}

void ValueKnob::resized()
{
    layoutKnob (slider, *this);
}

void ValueKnob::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 12.0f || bounds.getHeight() < 16.0f)
        return;

    const auto value = slider.getValue();
    const auto text = textFn != nullptr ? textFn (value) : juce::String (value, 2);

    const auto labelColour = accent == col::amber ? col::textDim
                                                  : accent.withMultipliedAlpha (0.85f);

    paintKnobText (g, bounds, label.toUpperCase(), text, labelColour,
                   accent.brighter (0.15f));
}

// =============================================================================
// ParamButton
// =============================================================================
ParamButton::ParamButton (juce::AudioProcessorValueTreeState& state,
                          const juce::String& parameterId, const juce::String& text,
                          Variant variant)
    : KlopButton (text, variant)
{
    setClickingTogglesState (true);

    if (state.getParameter (parameterId) != nullptr)
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            state, parameterId, *this);
}

// =============================================================================
// SegmentedControl
// =============================================================================
SegmentedControl::SegmentedControl (juce::StringArray itemsToUse, juce::Colour accentToUse)
    : items (std::move (itemsToUse)), accent (accentToUse)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SegmentedControl::setSelectedIndex (int index, bool notify)
{
    const auto newIndex = juce::jlimit (0, juce::jmax (0, items.size() - 1), index);

    if (newIndex == selected)
        return;

    selected = newIndex;
    repaint();

    if (notify && onChange != nullptr)
        onChange (selected);
}

void SegmentedControl::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    repaint();
}

void SegmentedControl::setFontHeight (float newHeight)
{
    fontHeight = newHeight;
    repaint();
}

int SegmentedControl::indexAt (int x) const
{
    const auto count = items.size();

    if (count <= 0)
        return -1;

    const auto index = (int) ((float) x / (float) juce::jmax (1, getWidth()) * (float) count);
    return juce::jlimit (0, count - 1, index);
}

void SegmentedControl::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    if (bounds.getWidth() < 6.0f || bounds.getHeight() < 6.0f)
        return;

    const auto radius = juce::jmin (5.0f, bounds.getHeight() * 0.32f);

    g.setColour (col::panelLow);
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (col::line);
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    const auto count = items.size();

    if (count <= 0)
        return;

    const auto segW = bounds.getWidth() / (float) count;

    for (int i = 0; i < count; ++i)
    {
        auto seg = bounds.withWidth (segW).withX (bounds.getX() + segW * (float) i);

        if (i > 0)
        {
            g.setColour (col::line);
            g.fillRect (juce::Rectangle<float> (seg.getX(), seg.getY() + 3.0f, 1.0f,
                                                seg.getHeight() - 6.0f));
        }

        if (i == selected)
        {
            auto pill = seg.reduced (2.0f, 2.0f);
            g.setColour (accent.withAlpha (0.92f));
            g.fillRoundedRectangle (pill, juce::jmax (2.0f, radius - 1.5f));
        }
        else if (i == hovered)
        {
            g.setColour (juce::Colours::white.withAlpha (0.045f));
            g.fillRoundedRectangle (seg.reduced (2.0f, 2.0f), juce::jmax (2.0f, radius - 1.5f));
        }

        drawSmallLabel (g, seg.reduced (2.0f, 0.0f), items[i].toUpperCase(),
                        juce::jlimit (6.5f, 11.5f,
                                      juce::jmin (fontHeight, seg.getHeight() * 0.48f)),
                        i == selected ? col::bgDeep
                                      : (i == hovered ? col::text : col::textDim),
                        0.4f);
    }
}

void SegmentedControl::mouseDown (const juce::MouseEvent& event)
{
    const auto index = indexAt (event.x);

    if (index < 0 || index == selected)
        return;

    selected = index;
    repaint();

    if (onChange != nullptr)
        onChange (selected);
}

void SegmentedControl::mouseMove (const juce::MouseEvent& event)
{
    const auto index = indexAt (event.x);

    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void SegmentedControl::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1)
    {
        hovered = -1;
        repaint();
    }
}

// =============================================================================
// TabStrip
// =============================================================================
TabStrip::TabStrip (juce::StringArray labels)
    : items (std::move (labels))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void TabStrip::setActive (int index, bool notify)
{
    const auto newIndex = juce::jlimit (0, juce::jmax (0, items.size() - 1), index);

    if (newIndex == active)
        return;

    active = newIndex;
    repaint();

    if (notify && onChange != nullptr)
        onChange (active);
}

int TabStrip::indexAt (int x) const
{
    const auto count = juce::jmax (1, items.size());
    return juce::jlimit (0, count - 1, x / juce::jmax (1, getWidth() / count));
}

void TabStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 4.0f || bounds.getHeight() < 4.0f)
        return;

    const auto count = items.size();

    if (count <= 0)
        return;

    const auto tabW = bounds.getWidth() / (float) count;

    for (int i = 0; i < count; ++i)
    {
        auto tab = bounds.withWidth (tabW).withX (bounds.getX() + tabW * (float) i);
        const auto isActive = i == active;

        if (isActive)
        {
            g.setColour (juce::Colours::white.withAlpha (0.03f));
            g.fillRect (tab);

            g.setColour (col::amber);
            g.fillRect (tab.removeFromBottom (2.0f).reduced (8.0f, 0.0f));
            g.setColour (col::amber);
            g.setFont (ty::sans (juce::jlimit (9.0f, 12.0f, bounds.getHeight() * 0.46f), true));
            drawSpacedText (g, items[i].toUpperCase(), tab, ty::sans (11.0f, true), col::amberHi,
                            juce::Justification::centred, 1.6f);
        }
        else
        {
            g.setColour (i == hovered ? col::textDim : col::textFaint);
            drawSpacedText (g, items[i].toUpperCase(), tab, ty::sans (11.0f, true),
                            i == hovered ? col::text : col::textFaint,
                            juce::Justification::centred, 1.6f);
        }
    }
}

void TabStrip::mouseDown (const juce::MouseEvent& event)
{
    const auto index = indexAt (event.x);

    if (index < 0 || index == active)
        return;

    active = index;
    repaint();

    if (onChange != nullptr)
        onChange (active);
}

void TabStrip::mouseMove (const juce::MouseEvent& event)
{
    const auto index = indexAt (event.x);

    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void TabStrip::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1)
    {
        hovered = -1;
        repaint();
    }
}

// =============================================================================
// SeedField
// =============================================================================
SeedField::SeedField (KlopgeestProcessor& processorToUse)
    : processor (processorToUse)
{
    parameter = processor.getAPVTS().getParameter (pid::seed);

    editor.setInputRestrictions (6, "0123456789");
    editor.setJustification (juce::Justification::centred);
    editor.setFont (ty::mono (12.0f, true));
    editor.setSelectAllWhenFocused (true);
    editor.setIndents (2, 0);
    editor.setColour (juce::TextEditor::textColourId, col::cyan);
    editor.setColour (juce::TextEditor::backgroundColourId, col::panelLow);
    editor.setColour (juce::TextEditor::outlineColourId, col::line);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, col::cyanDim);

    editor.onReturnKey = [this]
    {
        commitText();
        editor.giveAwayKeyboardFocus();
    };

    editor.onFocusLost = [this] { commitText(); };

    editor.onEscapeKey = [this]
    {
        syncTextFromParameter();
        editor.giveAwayKeyboardFocus();
    };

    addAndMakeVisible (editor);

    dice.setTooltip ("New performance seed");
    dice.onClick = [this] { randomise(); };
    addAndMakeVisible (dice);

    if (parameter != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (
            *parameter, [this] (float value)
            {
                if (updating)
                    return;

                updating = true;

                if (! editor.hasKeyboardFocus (false))
                    editor.setText (juce::String (juce::roundToInt (value)), false);

                updating = false;
            });

        attachment->sendInitialUpdate();
    }
}

void SeedField::refresh()
{
    if (parameter == nullptr || editor.hasKeyboardFocus (false))
        return;

    syncTextFromParameter();
}

void SeedField::resized()
{
    auto r = getLocalBounds();

    if (r.getWidth() <= 0)
        return;

    auto diceArea = r.removeFromRight (juce::jmin (44, juce::jmax (30, r.getWidth() / 3)));
    r.removeFromRight (4);
    dice.setBounds (diceArea.reduced (0, 1));
    editor.setBounds (r.reduced (0, 1));
}

void SeedField::paint (juce::Graphics& g)
{
    juce::ignoreUnused (g);
}

void SeedField::commitText()
{
    if (parameter == nullptr || updating)
        return;

    const auto text = editor.getText().retainCharacters ("0123456789");

    if (text.isEmpty())
    {
        syncTextFromParameter();
        return;
    }

    const auto value = juce::jlimit (0, 999999, text.getIntValue());

    updating = true;
    parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) value));
    updating = false;

    syncTextFromParameter();
}

void SeedField::syncTextFromParameter()
{
    if (parameter == nullptr)
        return;

    const auto value = juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue()));
    editor.setText (juce::String (value), false);
}

void SeedField::randomise()
{
    if (parameter == nullptr)
        return;

    const auto value = juce::Random::getSystemRandom().nextInt (1000000);

    updating = true;
    parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) value));
    updating = false;

    syncTextFromParameter();
}

// =============================================================================
// LevelMeter
// =============================================================================
LevelMeter::LevelMeter (bool stereoToUse, bool showScaleToUse)
    : stereo (stereoToUse), showScale (showScaleToUse)
{
}

void LevelMeter::setLevelsDb (float peakL, float peakR, float rmsL, float rmsR)
{
    const float targets[2][2] = { { rmsL, peakL }, { rmsR, peakR } };
    bool changed = false;

    for (int ch = 0; ch < (stereo ? 2 : 1); ++ch)
    {
        const auto targetRms = juce::jlimit (-100.0f, 6.0f, targets[ch][0]);
        const auto targetPeak = juce::jlimit (-100.0f, 6.0f, targets[ch][1]);

        if (targetRms > dispRms[ch])
            dispRms[ch] = targetRms;
        else
            dispRms[ch] = juce::jmax (-100.0f, dispRms[ch] - 5.0f);

        if (targetPeak >= dispPeak[ch])
        {
            dispPeak[ch] = targetPeak;
            holdFrames[ch] = 20;
        }
        else if (holdFrames[ch] > 0)
        {
            --holdFrames[ch];
            dispPeak[ch] = juce::jmax (dispPeak[ch], targetPeak);
        }
        else
        {
            dispPeak[ch] = juce::jmax (dispRms[ch], dispPeak[ch] - 1.8f);
        }

        changed = changed || targetRms > -99.0f || targetPeak > -99.0f;
    }

    if (changed)
        repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 6.0f || bounds.getHeight() < 10.0f)
        return;

    const auto scaleW = showScale ? 18.0f : 0.0f;
    auto barsArea = bounds.withTrimmedRight (scaleW);
    const auto channels = stereo ? 2 : 1;
    const auto gap = 2.0f;
    const auto barW = (barsArea.getWidth() - gap * (float) (channels - 1))
                      / (float) channels;

    if (barW < 2.0f)
        return;

    for (int ch = 0; ch < channels; ++ch)
    {
        auto bar = barsArea.withWidth (barW)
                       .withX (barsArea.getX() + (barW + gap) * (float) ch);

        g.setColour (col::bgDeep);
        g.fillRoundedRectangle (bar, 2.0f);

        // Segmented backplate.
        g.setColour (juce::Colours::white.withAlpha (0.03f));

        for (float y = bar.getBottom() - 4.0f; y > bar.getY(); y -= 4.0f)
            g.drawHorizontalLine ((int) y, bar.getX() + 1.0f, bar.getRight() - 1.0f);

        const auto rmsNorm = dbToNorm (dispRms[ch]);

        if (rmsNorm > 0.001f)
        {
            auto lit = bar.withTrimmedTop (bar.getHeight() * (1.0f - rmsNorm));
            lit = lit.withTrimmedBottom (0.5f).withTrimmedTop (0.5f);

            g.saveState();
            g.reduceClipRegion (lit.toNearestInt());
            juce::ColourGradient gradient (col::cyanDim, 0.0f, bar.getBottom(),
                                           col::red, 0.0f, bar.getY(), false);
            gradient.addColour (0.55, col::cyan);
            gradient.addColour (0.82, col::amber);
            g.setGradientFill (gradient);
            g.fillRoundedRectangle (bar, 2.0f);
            g.restoreState();
        }

        g.setColour (col::line);
        g.drawRoundedRectangle (bar.reduced (0.5f), 2.0f, 1.0f);

        const auto peakNorm = dbToNorm (dispPeak[ch]);

        if (peakNorm > 0.01f)
        {
            const auto y = bar.getBottom() - bar.getHeight() * peakNorm;
            g.setColour (dispPeak[ch] > -1.0f ? col::red : col::amberHi);
            g.fillRect (bar.getX(), y, bar.getWidth(), 1.5f);
        }
    }

    if (showScale)
    {
        g.setFont (ty::mono (6.5f));
        const float marks[] = { 0.0f, -12.0f, -24.0f, -48.0f };

        for (auto db : marks)
        {
            const auto y = barsArea.getBottom() - barsArea.getHeight() * dbToNorm (db);
            g.setColour (col::textFaint);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (
                            barsArea.getRight() + 3.0f, y - 4.0f, scaleW - 3.0f, 8.0f),
                        juce::Justification::centredLeft, false);
        }
    }
}

// =============================================================================
// GainReductionMeter
// =============================================================================
void GainReductionMeter::setGainReductionDb (float reductionDb)
{
    const auto target = juce::jlimit (0.0f, 24.0f, reductionDb);
    const auto previous = display;

    if (target > display)
        display = target;
    else
        display = juce::jmax (0.0f, display - 0.9f);

    if (std::abs (display - previous) > 0.02f)
        repaint();
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 8.0f || bounds.getHeight() < 20.0f)
        return;

    auto scale = bounds.removeFromRight (16.0f);
    auto bar = bounds;

    g.setColour (col::bgDeep);
    g.fillRoundedRectangle (bar, 2.0f);

    g.setColour (juce::Colours::white.withAlpha (0.04f));

    for (float y = bar.getBottom() - 5.0f; y > bar.getY(); y -= 5.0f)
        g.drawHorizontalLine ((int) y, bar.getX() + 1.0f, bar.getRight() - 1.0f);

    const auto norm = juce::jlimit (0.0f, 1.0f, display / 24.0f);

    if (norm > 0.004f)
    {
        auto lit = bar.withHeight (bar.getHeight() * norm);

        g.saveState();
        g.reduceClipRegion (lit.toNearestInt());
        juce::ColourGradient gradient (col::cyanDim, 0.0f, bar.getY(),
                                       col::red, 0.0f, bar.getBottom(), false);
        gradient.addColour (0.45, col::cyan);
        gradient.addColour (0.75, col::amber);
        g.setGradientFill (gradient);
        g.fillRoundedRectangle (bar, 2.0f);
        g.restoreState();

        g.setColour (col::amberHi);
        g.fillRect (bar.getX(), lit.getBottom() - 1.0f, bar.getWidth(), 1.2f);
    }

    g.setColour (col::line);
    g.drawRoundedRectangle (bar.reduced (0.5f), 2.0f, 1.0f);

    g.setFont (ty::mono (6.5f));

    for (float db = 0.0f; db <= 24.0f; db += 6.0f)
    {
        const auto y = bar.getY() + bar.getHeight() * (db / 24.0f);
        g.setColour (col::textFaint);
        g.fillRect (bar.getRight(), y, 3.0f, 1.0f);
        g.drawText (juce::String ((int) -db), juce::Rectangle<float> (
                        scale.getX() + 2.0f, y - 4.0f, scale.getWidth() - 2.0f, 8.0f),
                    juce::Justification::centredLeft, false);
    }
}

// =============================================================================
// DriftScope
// =============================================================================
DriftScope::DriftScope (KlopgeestProcessor& processorToUse)
    : processor (processorToUse)
{
}

void DriftScope::refresh()
{
    auto& visual = processor.getVisualState();
    bool changed = false;

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto count = visual.triggerCount[v].load (std::memory_order_relaxed);

        if (count != triggerCount[v])
        {
            triggerCount[v] = count;
            flash[v] = 1.0f;
            changed = true;
        }
        else
        {
            if (flash[v] > 0.005f)
            {
                flash[v] *= 0.86f;
                changed = true;
            }
        }

        const auto newTiming = visual.lastTimingMs[v].load (std::memory_order_relaxed);
        const auto newVelocity = visual.lastVelocity[v].load (std::memory_order_relaxed);
        const auto newPitch = visual.lastPitchSemis[v].load (std::memory_order_relaxed);

        if (std::abs (newTiming - timing[v]) > 0.001f
            || std::abs (newVelocity - velocity[v]) > 0.001f
            || std::abs (newPitch - pitch[v]) > 0.001f)
        {
            changed = true;
        }

        timing[v] = newTiming;
        velocity[v] = newVelocity;
        pitch[v] = newPitch;
    }

    if (changed)
        repaint();
}

void DriftScope::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 80.0f || bounds.getHeight() < 50.0f)
        return;

    g.setColour (col::panelLow);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    auto inner = bounds.reduced (7.0f, 6.0f);
    auto labels = inner.removeFromLeft (36.0f);
    auto timeRow = inner.removeFromBottom (9.0f);
    auto plot = inner;

    if (plot.getHeight() < 20.0f)
        return;

    const auto centreX = plot.getCentreX();
    const auto halfW = plot.getWidth() * 0.5f;
    const auto rowH = plot.getHeight() / (float) kNumVoices;

    // Time grid.
    const float marks[] = { -40.0f, -20.0f, 0.0f, 20.0f, 40.0f };

    for (int i = 0; i < 5; ++i)
    {
        const auto ms = marks[i];
        const auto x = centreX + (ms / 40.0f) * halfW;

        if (i == 2)
        {
            g.setColour (col::cyan.withAlpha (0.22f));
            g.fillRect (juce::Rectangle<float> (x - 0.6f, plot.getY(), 1.2f, plot.getHeight()));
        }
        else
        {
            g.setColour (col::line.withAlpha (i == 0 || i == 4 ? 0.5f : 0.32f));
            g.fillRect (juce::Rectangle<float> (x - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
        }
    }

    // Lane separators and voice names.
    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto rowY = plot.getY() + rowH * (float) v;
        const auto centreY = rowY + rowH * 0.5f;
        const auto accent = voiceIsAnchor (v) ? col::amber : col::cyan;
        const auto lit = 0.30f + 0.70f * flash[v];

        if (v % 2 == 1)
        {
            g.setColour (juce::Colours::white.withAlpha (0.014f));
            g.fillRect (juce::Rectangle<float> (labels.getX(), rowY, labels.getWidth() + plot.getWidth(),
                                                rowH));
        }

        g.setColour (col::line.withAlpha (0.30f));
        g.fillRect (juce::Rectangle<float> (labels.getX(), rowY + rowH - 0.5f,
                                            labels.getWidth() + plot.getWidth(), 0.8f));

        drawSpacedText (g, voiceShortName (v),
                        juce::Rectangle<float> (labels.getX(), rowY, labels.getWidth() - 4.0f, rowH),
                        ty::mono (6.2f, true), accent.withAlpha (lit),
                        juce::Justification::centredRight, 0.0f);

        const auto ms = juce::jlimit (-40.0f, 40.0f, timing[v]);
        const auto x = centreX + (ms / 40.0f) * halfW;
        const auto y = centreY + juce::jlimit (-3.0f, 3.0f, pitch[v] * 0.8f);
        const auto radius = 1.8f + 2.4f * juce::jlimit (0.0f, 1.0f, velocity[v]);

        // Trail from the grid line to where the hit actually landed.
        g.setColour (accent.withAlpha (0.10f + 0.24f * flash[v]));
        g.fillRect (juce::Rectangle<float> (juce::jmin (x, centreX), centreY - 0.5f,
                                            std::abs (x - centreX), 1.0f));

        if (flash[v] > 0.08f)
        {
            g.setColour (accent.withAlpha (0.16f * flash[v]));
            g.fillEllipse (juce::Rectangle<float> (radius * 4.0f, radius * 4.0f)
                               .withCentre ({ x, y }));
        }

        g.setColour (accent.withAlpha (0.35f + 0.65f * flash[v]));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre ({ x, y }));
    }

    // Time labels underneath.
    g.setFont (ty::mono (6.2f));
    g.setColour (col::textFaint);

    for (auto ms : marks)
    {
        const auto x = centreX + (ms / 40.0f) * halfW;
        const auto text = ms == 0.0f ? juce::String ("0") : juce::String ((int) ms);
        g.drawText (text, juce::Rectangle<float> (x - 12.0f, timeRow.getY(), 24.0f,
                                                  timeRow.getHeight()),
                    juce::Justification::centred, false);
    }

    // Axis unit hint, kept clear of the tick labels.
    g.setFont (ty::mono (6.0f));
    g.setColour (col::textFaint.withAlpha (0.75f));
    g.drawText ("ms", juce::Rectangle<float> (labels.getX(), timeRow.getY(),
                                              labels.getWidth() - 2.0f, timeRow.getHeight()),
                juce::Justification::centredRight, false);
}

} // namespace klopgeest::ui
