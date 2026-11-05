#include "ui/Panels.h"

#include <cmath>

#include "core/ParamIDs.h"
#include "plugin/KlopgeestProcessor.h"

namespace klopgeest::ui
{

namespace
{
const juce::Colour kHeatColour { 0xffef7d3a };

juce::String percentText (double value)
{
    return juce::String (juce::roundToInt (value * 100.0)) + "%";
}

juce::String microText (double value)
{
    return (value >= 0.0 ? "+" : "") + juce::String (juce::roundToInt (value * 200.0)) + "%";
}

juce::String semisText (double value)
{
    return (value >= 0.0 ? "+" : "") + juce::String (value, 1) + " st";
}

juce::String ratchetText (double value)
{
    return "x" + juce::String (juce::roundToInt (value));
}

juce::String loopText (double value)
{
    return juce::String (juce::roundToInt (value));
}

juce::String msText (double value)
{
    return (value >= 0.0 ? "+" : "") + juce::String (value, 1) + " ms";
}

int lengthToIndex (int length)
{
    if (length >= 64) return 2;
    if (length >= 32) return 1;
    return 0;
}

juce::String stepNumberText (int step)
{
    return juce::String (step + 1).paddedLeft ('0', 2);
}
} // namespace

// =============================================================================
// TopBar
// =============================================================================
TopBar::TopBar (KlopgeestProcessor& processorToUse)
    : processor (processorToUse),
      swingKnob (processorToUse.getAPVTS(), pid::swing, "SWING", col::amber),
      humaniseKnob (processorToUse.getAPVTS(), pid::humanize, "HUMANIZE", col::amber),
      stabilityKnob (processorToUse.getAPVTS(), pid::stability, "STABILITY", col::amber),
      mutateAmountKnob (processorToUse.getAPVTS(), pid::mutateAmount, "MUTATE", col::cyan),
      personality (personalityNames(), col::amber),
      lengthControl ({ "16", "32", "64" }, col::amber),
      seedField (processorToUse),
      evolveButton (processorToUse.getAPVTS(), pid::evolve, "EVOLVE", KlopButton::Variant::cyan)
{
    loadPatternList();

    transport.onClick = [this]
    {
        processor.setPlaying (! processor.isPlaying());
        refresh();
    };

    personality.onChange = [this] (int index)
    {
        if (auto* parameter = processor.getAPVTS().getParameter (pid::personality))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) index));
    };

    patternBox.onChange = [this]
    {
        const auto index = patternBox.getSelectedId() - 1;

        if (index >= 0 && index < kNumFactoryPatterns)
        {
            processor.loadFactoryPattern (index);

            if (onPatternChanged != nullptr)
                onPatternChanged();
        }
    };

    lengthControl.onChange = [this] (int index)
    {
        static const int lengths[] = { 16, 32, 64 };
        setPatternLength (lengths[juce::jlimit (0, 2, index)]);
    };

    nudgeButton.setFontHeight (10.0f);
    mutateButton.setFontHeight (10.0f);
    recallButton.setFontHeight (10.0f);
    evolveButton.setFontHeight (9.0f);

    nudgeButton.setTooltip ("Small reversible groove edit");
    mutateButton.setTooltip ("Substantial variation that keeps the identity");
    recallButton.setTooltip ("Take back the last mutation");

    nudgeButton.onClick = [this]
    {
        processor.nudge();

        if (onPatternChanged != nullptr)
            onPatternChanged();

        refresh();
    };

    mutateButton.onClick = [this]
    {
        processor.mutate();

        if (onPatternChanged != nullptr)
            onPatternChanged();

        refresh();
    };

    recallButton.onClick = [this]
    {
        processor.recall();

        if (onPatternChanged != nullptr)
            onPatternChanged();

        refresh();
    };

    addAndMakeVisible (transport);
    addAndMakeVisible (swingKnob);
    addAndMakeVisible (humaniseKnob);
    addAndMakeVisible (stabilityKnob);
    addAndMakeVisible (mutateAmountKnob);
    addAndMakeVisible (personality);
    addAndMakeVisible (patternBox);
    addAndMakeVisible (lengthControl);
    addAndMakeVisible (seedField);
    addAndMakeVisible (nudgeButton);
    addAndMakeVisible (mutateButton);
    addAndMakeVisible (recallButton);
    addAndMakeVisible (evolveButton);

    refresh();
}

void TopBar::loadPatternList()
{
    patternBox.clear (juce::dontSendNotification);
    const auto names = processor.getFactoryPatternNames();

    for (int i = 0; i < names.size(); ++i)
        patternBox.addItem (names[i].toUpperCase(), i + 1);

    patternBox.setSelectedId (processor.getFactoryPatternIndex() + 1, juce::dontSendNotification);
    lastPatternIndex = processor.getFactoryPatternIndex();
}

void TopBar::setPatternLength (int length)
{
    auto pattern = processor.getPattern();

    if (pattern.length == length)
        return;

    const auto oldLength = pattern.length;
    bool allLanesFull = true;

    for (const auto& lane : pattern.lanes)
        if (lane.length != oldLength)
            allLanesFull = false;

    pattern.length = length;

    for (auto& lane : pattern.lanes)
        lane.length = allLanesFull ? length : juce::jmin (lane.length, length);

    processor.publishPattern (pattern);

    if (onPatternChanged != nullptr)
        onPatternChanged();

    refresh();
}

void TopBar::refresh()
{
    const auto playing = processor.isPlaying();
    transport.setPlaying (playing);

    if (playing)
    {
        const auto phase = (float) (juce::Time::getMillisecondCounterHiRes() * 0.0022);
        playPulse = 0.5f + 0.5f * std::sin (phase);
        transport.setPulse (playPulse);
    }
    else
    {
        transport.setPulse (0.0f);
    }

    lastPlaying = playing;

    if (auto* parameter = processor.getAPVTS().getParameter (pid::personality))
    {
        const auto index = juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue()));
        personality.setSelectedIndex (index, false);
    }

    if (processor.getFactoryPatternIndex() != lastPatternIndex)
    {
        lastPatternIndex = processor.getFactoryPatternIndex();
        patternBox.setSelectedId (lastPatternIndex + 1, juce::dontSendNotification);
    }

    lengthControl.setSelectedIndex (lengthToIndex (processor.getPattern().length), false);
    recallButton.setEnabled (processor.canRecall());

    seedField.refresh();

    const auto bpm = processor.getVisualState().bpm.load (std::memory_order_relaxed);
    const auto host = processor.getVisualState().hostSynced.load (std::memory_order_relaxed);
    lastBpm = bpm;
    lastHostSynced = host;
}

void TopBar::resized()
{
    auto area = getLocalBounds().toFloat().reduced (12.0f, 8.0f);

    if (area.getHeight() < 40.0f)
        return;

    auto row1 = area.removeFromTop (juce::jmax (40.0f, area.getHeight() * 0.50f));
    area.removeFromTop (4.0f);
    auto row2 = area;

    auto r1 = row1;
    wordmarkArea = r1.removeFromLeft (212.0f);
    r1.removeFromLeft (8.0f);

    transport.setBounds (r1.removeFromLeft (88.0f)
                             .withSizeKeepingCentre (88.0f, juce::jmin (44.0f, row1.getHeight()))
                             .toNearestInt());
    r1.removeFromLeft (10.0f);
    clockArea = r1.removeFromLeft (104.0f);

    // Right aligned configuration groups.
    auto lengthGroup = r1.removeFromRight (104.0f);
    r1.removeFromRight (10.0f);
    auto patternGroup = r1.removeFromRight (188.0f);
    r1.removeFromRight (10.0f);
    auto personalityGroup = r1.removeFromRight (150.0f);

    auto placeControl = [] (juce::Rectangle<float> group, int controlHeight)
    {
        group.removeFromTop (11.0f);
        return group.withHeight (juce::jmin ((float) controlHeight, group.getHeight()))
                    .toNearestInt();
    };

    personality.setBounds (placeControl (personalityGroup, 28));
    patternBox.setBounds (placeControl (patternGroup, 28));
    lengthControl.setBounds (placeControl (lengthGroup, 28));

    separatorX[0] = clockArea.getX() - 7.0f;
    separatorX[1] = personalityGroup.getX() - 8.0f;
    separatorX[2] = patternGroup.getX() - 8.0f;
    separatorX[3] = lengthGroup.getX() - 8.0f;

    // ---- row two ------------------------------------------------------------
    auto r2 = row2;

    const auto knobW = juce::jmin (96.0f, r2.getWidth() * 0.09f);
    swingKnob.setBounds (r2.removeFromLeft (knobW).toNearestInt());
    r2.removeFromLeft (2.0f);
    humaniseKnob.setBounds (r2.removeFromLeft (knobW).toNearestInt());
    r2.removeFromLeft (2.0f);
    stabilityKnob.setBounds (r2.removeFromLeft (knobW).toNearestInt());

    auto evolveGroup = r2.removeFromRight (84.0f);
    r2.removeFromRight (8.0f);
    auto mutateAmountGroup = r2.removeFromRight (juce::jmin (88.0f, r2.getWidth() * 0.10f));
    r2.removeFromRight (8.0f);
    auto recallGroup = r2.removeFromRight (72.0f);
    r2.removeFromRight (6.0f);
    auto mutateGroup = r2.removeFromRight (76.0f);
    r2.removeFromRight (6.0f);
    auto nudgeGroup = r2.removeFromRight (72.0f);

    auto centreButton = [] (juce::Rectangle<float> group, juce::Component& button)
    {
        button.setBounds (group.withSizeKeepingCentre (group.getWidth(),
                                                       juce::jmin (28.0f, group.getHeight()))
                               .toNearestInt());
    };

    centreButton (nudgeGroup, nudgeButton);
    centreButton (mutateGroup, mutateButton);
    centreButton (recallGroup, recallButton);
    centreButton (evolveGroup, evolveButton);

    mutateAmountKnob.setBounds (mutateAmountGroup.toNearestInt());

    auto seedGroup = r2.removeFromRight (154.0f);
    seedField.setBounds (seedGroup.withTrimmedTop (11.0f)
                             .withHeight (juce::jmin (28.0f, seedGroup.getHeight() - 11.0f))
                             .toNearestInt());
}

void TopBar::paintWordmark (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (area.getWidth() < 60.0f || area.getHeight() < 24.0f)
        return;

    auto font = ty::sans (juce::jmin (24.0f, area.getHeight() * 0.46f), true);
    const auto tracking = 1.8f;
    const auto klopWidth = juce::GlyphArrangement::getStringWidth (font, "KLOP") + tracking * 3.0f;

    auto line = area.removeFromTop (juce::jmin (30.0f, area.getHeight() * 0.58f));
    auto klopArea = line.withWidth (klopWidth + 8.0f);
    auto geestArea = line.withTrimmedLeft (klopWidth + 2.0f);

    drawSpacedText (g, "KLOP", klopArea, font, col::text, juce::Justification::centredLeft,
                    tracking);
    drawSpacedText (g, "GEEST", geestArea, font, col::amber, juce::Justification::centredLeft,
                    tracking);

    drawSpacedText (g, "CONTROLLED INSTABILITY", area.removeFromTop (12.0f),
                    ty::sans (7.0f, true), col::textFaint, juce::Justification::centredLeft,
                    1.6f);
}

void TopBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 40.0f || bounds.getHeight() < 30.0f)
        return;

    drawPanel (g, bounds, col::panelHi, col::line, 8.0f);

    paintWordmark (g, wordmarkArea);

    // --- Clock ---------------------------------------------------------------
    if (clockArea.getWidth() > 40.0f)
    {
        auto clock = clockArea;
        const auto bpm = lastBpm > 0.0f ? lastBpm
                                        : processor.getVisualState().bpm.load (std::memory_order_relaxed);
        const auto host = lastHostSynced;
        const auto playing = processor.isPlaying();
        const auto text = juce::String (bpm, 1);
        const auto numberFont = ty::mono (juce::jmin (21.0f, clock.getHeight() * 0.50f), true);

        auto numberArea = clock.removeFromTop (juce::jmin (26.0f, clock.getHeight() * 0.60f));
        const auto numberWidth = juce::GlyphArrangement::getStringWidth (numberFont, text);

        g.setFont (numberFont);
        g.setColour (col::amberHi);
        g.drawText (text, numberArea.removeFromLeft (numberWidth + 2.0f),
                    juce::Justification::centredLeft, false);

        g.setFont (ty::sans (7.0f, true));
        g.setColour (col::textDim);
        g.drawText ("BPM", numberArea.removeFromLeft (26.0f).withTrimmedTop (4.0f),
                    juce::Justification::centredLeft, false);

        const auto statusY = clock.getBottom() - 12.0f;
        g.setFont (ty::sans (6.6f, true));
        g.setColour (host ? col::cyan : col::textFaint);
        g.drawText (host ? (playing ? "HOST LOCK" : "HOST SYNC")
                         : (playing ? "INTERNAL RUN" : "INTERNAL"),
                    juce::Rectangle<float> (clockArea.getX(), statusY, clockArea.getWidth() - 46.0f,
                                            10.0f),
                    juce::Justification::centredLeft, false);

        // Four-beat indicator.
        const auto tick = processor.getVisualState().currentTick.load (std::memory_order_relaxed);
        const auto beat = playing && tick >= 0 ? (int) ((tick / 4) % 4) : -1;

        for (int i = 0; i < 4; ++i)
        {
            const auto x = clockArea.getRight() - 40.0f + (float) i * 8.0f;
            const auto isBeat = i == beat;
            g.setColour (isBeat ? col::cyan : col::lineHi);
            g.fillEllipse (juce::Rectangle<float> (isBeat ? 4.5f : 3.0f,
                                                   isBeat ? 4.5f : 3.0f)
                               .withCentre ({ x, statusY + 5.0f }));
        }
    }

    // --- Group labels --------------------------------------------------------
    auto groupLabel = [&] (juce::Component& component, const juce::String& text)
    {
        const auto r = component.getBounds().toFloat();
        drawSectionLabel (g, { r.getX(), r.getY() - 11.0f, r.getWidth(), 10.0f },
                          text, col::textFaint, 7.2f);
    };

    groupLabel (personality, "PERSONALITY");
    groupLabel (patternBox, "PATTERN");
    groupLabel (lengthControl, "LENGTH");
    groupLabel (seedField, "SEED");

    // --- Separators ----------------------------------------------------------
    for (auto x : separatorX)
        if (x > bounds.getX() + 10.0f)
            drawVerticalHairline (g, x, bounds.getY() + 8.0f, bounds.getBottom() - 8.0f,
                                  col::line);
}

// =============================================================================
// StepInspector
// =============================================================================
StepInspector::StepInspector (KlopgeestProcessor& processorToUse)
    : processor (processorToUse),
      velocityKnob ("VELOCITY", juce::NormalisableRange<double> (0.02, 1.0, 0.01),
                    [] (double value) { return percentText (value); }, col::amber),
      probabilityKnob ("PROB", juce::NormalisableRange<double> (0.0, 1.0, 0.01),
                       [] (double value) { return percentText (value); }, col::cyan),
      microKnob ("MICRO", juce::NormalisableRange<double> (-0.5, 0.5, 0.005),
                 [] (double value) { return microText (value); }, col::cyan),
      pitchKnob ("PITCH", juce::NormalisableRange<double> (-12.0, 12.0, 0.1),
                 [] (double value) { return semisText (value); }, col::amber),
      ratchetKnob ("RATCHET", juce::NormalisableRange<double> (1.0, 4.0, 1.0),
                   [] (double value) { return ratchetText (value); }, col::amber),
      laneLoopKnob ("LOOP", juce::NormalisableRange<double> (1.0, 64.0, 1.0),
                    [] (double value) { return loopText (value); }, col::cyan)
{
    addAndMakeVisible (velocityKnob);
    addAndMakeVisible (probabilityKnob);
    addAndMakeVisible (microKnob);
    addAndMakeVisible (pitchKnob);
    addAndMakeVisible (ratchetKnob);
    addAndMakeVisible (onButton);
    addAndMakeVisible (stepLockButton);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);
    addAndMakeVisible (laneLockButton);
    addAndMakeVisible (laneLoopKnob);

    onButton.setFontHeight (9.0f);
    stepLockButton.setFontHeight (9.0f);
    muteButton.setFontHeight (8.5f);
    soloButton.setFontHeight (8.5f);
    laneLockButton.setFontHeight (8.5f);

    onButton.setStateProvider ([this]
    {
        return processor.getPattern().step (voice, step).on;
    });

    stepLockButton.setStateProvider ([this]
    {
        return processor.getPattern().step (voice, step).locked;
    });

    muteButton.setStateProvider ([this]
    {
        return processor.getPattern().lanes[(size_t) voice].muted;
    });

    soloButton.setStateProvider ([this]
    {
        return processor.getPattern().lanes[(size_t) voice].soloed;
    });

    laneLockButton.setStateProvider ([this]
    {
        return processor.getPattern().lanes[(size_t) voice].locked;
    });

    onButton.onClick = [this]
    {
        applyStepEdit ([] (Step& stepData, Lane&, PatternData&) { stepData.on = ! stepData.on; });
        refresh();
    };

    stepLockButton.onClick = [this]
    {
        applyStepEdit ([] (Step& stepData, Lane&, PatternData&)
                       { stepData.locked = ! stepData.locked; });
        refresh();
    };

    muteButton.onClick = [this]
    {
        applyStepEdit ([] (Step&, Lane& lane, PatternData&) { lane.muted = ! lane.muted; });
        refresh();
    };

    soloButton.onClick = [this]
    {
        applyStepEdit ([] (Step&, Lane& lane, PatternData&) { lane.soloed = ! lane.soloed; });
        refresh();
    };

    laneLockButton.onClick = [this]
    {
        applyStepEdit ([] (Step&, Lane& lane, PatternData&) { lane.locked = ! lane.locked; });
        refresh();
    };

    laneLoopKnob.onValueChanged = [this] (double value)
    {
        applyStepEdit ([value] (Step&, Lane& lane, PatternData&)
                       { lane.length = juce::jlimit (1, kMaxSteps, (int) juce::roundToInt (value)); });
        refresh();
    };

    refresh();
}

void StepInspector::setSelection (int newVoice, int newStep)
{
    const auto length = processor.getPattern().length;
    const auto v = juce::jlimit (0, kNumVoices - 1, newVoice);
    const auto s = juce::jlimit (0, juce::jmax (0, length - 1), newStep);

    if (v == voice && s == step)
        return;

    voice = v;
    step = s;
    refreshValues (true);
    resized();
    repaint();
}

void StepInspector::refresh()
{
    const auto& pattern = processor.getPattern();
    const auto length = juce::jmax (1, pattern.length);

    if (step >= length)
        step = length - 1;

    refreshValues (false);
    onButton.repaint();
    stepLockButton.repaint();
    muteButton.repaint();
    soloButton.repaint();
    laneLockButton.repaint();
    repaint();
}

void StepInspector::refreshValues (bool force)
{
    const auto& pattern = processor.getPattern();
    const auto& stepData = pattern.step (voice, step);
    const auto& lane = pattern.lanes[(size_t) voice];

    if (force || ! velocityKnob.isDragging())
        velocityKnob.setValue ((double) stepData.velocity, juce::dontSendNotification);

    if (force || ! probabilityKnob.isDragging())
        probabilityKnob.setValue ((double) stepData.probability, juce::dontSendNotification);

    if (force || ! microKnob.isDragging())
        microKnob.setValue ((double) stepData.microtiming, juce::dontSendNotification);

    if (force || ! pitchKnob.isDragging())
        pitchKnob.setValue ((double) stepData.pitchSemis, juce::dontSendNotification);

    if (force || ! ratchetKnob.isDragging())
        ratchetKnob.setValue ((double) stepData.ratchet, juce::dontSendNotification);

    const auto maxLoop = (double) juce::jmax (1, pattern.length);
    laneLoopKnob.setRange (juce::NormalisableRange<double> (1.0, maxLoop, 1.0));

    if (force || ! laneLoopKnob.isDragging())
        laneLoopKnob.setValue ((double) juce::jlimit (1, pattern.length, lane.length),
                               juce::dontSendNotification);
}

void StepInspector::applyStepEdit (
    const std::function<void (Step&, Lane&, PatternData&)>& edit)
{
    auto pattern = processor.getPattern();
    auto& lane = pattern.lanes[(size_t) voice];

    if (step >= lane.length)
        lane.length = juce::jmin (pattern.length, step + 1);

    edit (lane.steps[(size_t) step], lane, pattern);
    processor.publishPattern (pattern);

    if (onPatternEdited != nullptr)
        onPatternEdited();

    repaint();
}

void StepInspector::resized()
{
    auto bounds = getLocalBounds().toFloat().reduced (10.0f, 8.0f);

    if (bounds.getHeight() < 80.0f)
        return;

    headerArea = bounds.removeFromTop (20.0f);
    bounds.removeFromTop (4.0f);

    const auto laneHeight = juce::jlimit (52.0f, 74.0f, bounds.getHeight() * 0.30f);
    laneArea = bounds.removeFromBottom (laneHeight);
    bounds.removeFromBottom (6.0f);

    knobsArea = bounds;

    const auto colW = knobsArea.getWidth() / 3.0f;
    const auto rowH = knobsArea.getHeight() * 0.5f;

    auto cell = [&] (int col, int row)
    {
        return juce::Rectangle<float> (knobsArea.getX() + colW * (float) col,
                                       knobsArea.getY() + rowH * (float) row,
                                       colW, rowH);
    };

    velocityKnob.setBounds (cell (0, 0).toNearestInt());
    probabilityKnob.setBounds (cell (1, 0).toNearestInt());
    microKnob.setBounds (cell (2, 0).toNearestInt());
    pitchKnob.setBounds (cell (0, 1).toNearestInt());
    ratchetKnob.setBounds (cell (1, 1).toNearestInt());

    compoundArea = cell (2, 1).reduced (6.0f, 4.0f);
    {
        auto c = compoundArea;
        const auto gap = 4.0f;
        const auto buttonH = (c.getHeight() - gap) * 0.5f;
        onButton.setBounds (c.removeFromTop (buttonH).toNearestInt());
        c.removeFromTop (gap);
        stepLockButton.setBounds (c.toNearestInt());
    }

    // ---- Lane section -------------------------------------------------------
    loopKnobArea = laneArea.removeFromRight (juce::jmin (78.0f, laneArea.getWidth() * 0.28f));
    laneArea.removeFromRight (6.0f);
    laneButtonArea = laneArea;

    {
        auto la = laneButtonArea;

        if (la.getHeight() > 16.0f)
            la.removeFromTop (14.0f);

        const auto gap = 4.0f;
        const auto buttonW = (la.getWidth() - gap * 2.0f) / 3.0f;
        const auto buttonH = juce::jmin (24.0f, la.getHeight() - 2.0f);
        auto row = la.withHeight (buttonH);

        muteButton.setBounds (row.removeFromLeft (buttonW).toNearestInt());
        row.removeFromLeft (gap);
        soloButton.setBounds (row.removeFromLeft (buttonW).toNearestInt());
        row.removeFromLeft (gap);
        laneLockButton.setBounds (row.removeFromLeft (buttonW).toNearestInt());
    }

    laneLoopKnob.setBounds (loopKnobArea.toNearestInt());
}

void StepInspector::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 60.0f || bounds.getHeight() < 80.0f)
        return;

    drawPanel (g, bounds, col::panel, col::line, 8.0f);

    const auto& pattern = processor.getPattern();
    const auto& stepData = pattern.step (voice, step);
    const auto& lane = pattern.lanes[(size_t) voice];

    drawSectionLabel (g, headerArea, "STEP INSPECTOR", col::textDim, 8.0f);

    auto header = headerArea.withTrimmedLeft (88.0f);

    drawSpacedText (g, juce::String (voiceShortName (voice)) + "  " + stepNumberText (step),
                    header, ty::sans (11.5f, true), col::amberHi,
                    juce::Justification::centredLeft, 1.1f);

    // Step status tags on the right.
    auto status = headerArea;
    juce::String statusText;

    if (! stepData.on)
        statusText = "OFF";
    else if (stepData.ratchet > 1)
        statusText = "RATCHET x" + juce::String (stepData.ratchet);
    else if (stepData.locked)
        statusText = "LOCKED";
    else if (stepData.probability < 0.995f)
        statusText = "CHANCE";
    else
        statusText = "HIT";

    g.setFont (ty::mono (7.5f, true));
    g.setColour (stepData.on ? col::cyan : col::textFaint);
    g.drawText (statusText, status, juce::Justification::centredRight, false);

    g.setColour (col::line);
    g.fillRect (juce::Rectangle<float> (bounds.getX() + 8.0f, knobsArea.getY() - 3.0f,
                                        bounds.getWidth() - 16.0f, 1.0f));
    g.fillRect (juce::Rectangle<float> (bounds.getX() + 8.0f, laneArea.getY() - 3.0f,
                                        bounds.getWidth() - 16.0f, 1.0f));

    // Lane section headings.
    auto laneTitle = laneArea.withHeight (13.0f);
    drawSectionLabel (g, laneTitle, "LANE", col::textDim, 7.5f);

    g.setFont (ty::mono (7.0f));
    g.setColour (col::textFaint);
    g.drawText ("LOOP " + juce::String (lane.length) + " OF " + juce::String (pattern.length),
                laneTitle.withTrimmedLeft (40.0f), juce::Justification::centredLeft, false);
}

// =============================================================================
// VoicePanel
// =============================================================================
VoicePanel::VoicePanel (KlopgeestProcessor& processorToUse)
    : processor (processorToUse)
{
    for (int i = 0; i < kNumVoiceParams; ++i)
    {
        const auto accent = (i >= VRandPitch && i <= VRandTiming) || i == VBias
                                ? col::cyan : col::amber;
        knobs[(size_t) i] = std::make_unique<ParamKnob> (
            processor.getAPVTS(), pid::voiceId (0, detail::kVoiceParamSuffix[i]),
            voiceParamLabel (0, i), accent);
        addAndMakeVisible (*knobs[(size_t) i]);
    }

    addAndMakeVisible (meter);
    rebindKnobs();
}

void VoicePanel::setVoice (int newVoice)
{
    const auto v = juce::jlimit (0, kNumVoices - 1, newVoice);

    if (v == voice)
        return;

    voice = v;
    rebindKnobs();
    repaint();
}

void VoicePanel::rebindKnobs()
{
    for (int i = 0; i < kNumVoiceParams; ++i)
    {
        auto* knob = knobs[(size_t) i].get();

        if (knob == nullptr)
            continue;

        knob->setLabelText (voiceParamLabel (voice, i));
        knob->setParamId (pid::voiceId (voice, detail::kVoiceParamSuffix[i]));
    }
}

void VoicePanel::refresh()
{
    auto& visual = processor.getVisualState();

    const auto count = visual.triggerCount[(size_t) voice].load (std::memory_order_relaxed);

    if (count != lastTriggerCount)
    {
        lastTriggerCount = count;
        hitFlash = 1.0f;
    }
    else
    {
        hitFlash *= 0.85f;
    }

    lastVelocity = visual.lastVelocity[(size_t) voice].load (std::memory_order_relaxed);
    lastTimingMs = visual.lastTimingMs[(size_t) voice].load (std::memory_order_relaxed);
    lastPitchSemis = visual.lastPitchSemis[(size_t) voice].load (std::memory_order_relaxed);
    lastDecayScale = visual.lastDecayScale[(size_t) voice].load (std::memory_order_relaxed);
    lastFlags = visual.lastFlags[(size_t) voice].load (std::memory_order_relaxed);

    const auto level = visual.voiceLevel[(size_t) voice].load (std::memory_order_relaxed);
    const auto db = level > 1.0e-4f ? 20.0f * std::log10 (level) : -100.0f;
    meter.setLevelsDb (db, db, db, db);

    repaint();
}

void VoicePanel::resized()
{
    auto bounds = getLocalBounds().toFloat().reduced (10.0f, 8.0f);

    if (bounds.getHeight() < 60.0f)
        return;

    const auto leftW = juce::jmin (330.0f, bounds.getWidth() * 0.33f);
    leftArea = bounds.removeFromLeft (leftW);
    bounds.removeFromLeft (12.0f);
    knobsArea = bounds;

    auto la = leftArea;
    nameArea = la.removeFromTop (28.0f);
    flagsArea = la.removeFromBottom (18.0f);
    la.removeFromBottom (4.0f);

    meterArea = la.removeFromLeft (13.0f).reduced (1.0f, 2.0f);
    readoutArea = la.withTrimmedLeft (9.0f);

    const auto gap = 4.0f;
    const auto rowH = (knobsArea.getHeight() - gap) * 0.5f;
    const auto cellW = knobsArea.getWidth() / 8.0f;

    for (int i = 0; i < kNumVoiceParams; ++i)
    {
        if (knobs[(size_t) i] == nullptr)
            continue;

        const auto row = i / 8;
        const auto col = i % 8;
        const auto cell = juce::Rectangle<float> (knobsArea.getX() + cellW * (float) col,
                                                  knobsArea.getY() + (rowH + gap) * (float) row,
                                                  cellW, rowH);
        knobs[(size_t) i]->setBounds (cell.toNearestInt());
    }

    meter.setBounds (meterArea.toNearestInt());
}

void VoicePanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 120.0f || bounds.getHeight() < 60.0f)
        return;

    // Voice identity.
    auto name = nameArea;
    const auto nameFont = ty::sans (juce::jmin (17.0f, nameArea.getHeight() * 0.62f), true);
    drawSpacedText (g, voiceName (voice), name.withHeight (name.getHeight() * 0.62f), nameFont,
                    col::amberHi, juce::Justification::centredLeft, 1.2f);

    drawSectionLabel (g, nameArea.withTrimmedTop (nameArea.getHeight() * 0.58f),
                      voiceRole (voice), col::textDim, 7.0f);

    // Hit flash underline.
    if (hitFlash > 0.02f)
    {
        g.setColour (col::cyan.withAlpha (0.5f * hitFlash));
        g.fillRect (juce::Rectangle<float> (nameArea.getX(), nameArea.getBottom() - 2.0f,
                                            nameArea.getWidth() * 0.7f, 1.4f));
    }

    // Hits counter.
    g.setFont (ty::mono (7.5f));
    g.setColour (col::textFaint);
    g.drawText (juce::String (lastTriggerCount) + " HITS",
                nameArea, juce::Justification::centredRight, false);

    paintReadout (g, readoutArea);

    // Flag pills.
    struct FlagName { const char* label; unsigned int bit; };
    static const FlagName flagNames[] =
    {
        { "GHOST", (unsigned int) HitFlag::ghost },
        { "ACCENT", (unsigned int) HitFlag::accent },
        { "RATCH", (unsigned int) HitFlag::ratchet },
        { "FERAL", (unsigned int) HitFlag::feral },
        { "LIVE", (unsigned int) HitFlag::live },
        { "MUT", (unsigned int) HitFlag::mutation }
    };

    auto flags = flagsArea;
    const auto count = (int) (sizeof (flagNames) / sizeof (flagNames[0]));
    const auto gap = 3.0f;
    const auto pillW = (flags.getWidth() - gap * (float) (count - 1)) / (float) count;
    const auto hasHit = lastTriggerCount > 0;

    for (int i = 0; i < count; ++i)
    {
        auto pill = flags.removeFromLeft (pillW);
        flags.removeFromLeft (gap);

        const auto lit = hasHit && (lastFlags & flagNames[i].bit) != 0;

        g.setColour (lit ? col::amber.withAlpha (0.85f) : col::panelLow);
        g.fillRoundedRectangle (pill, 3.0f);
        g.setColour (lit ? col::amberHi : col::line);
        g.drawRoundedRectangle (pill.reduced (0.5f), 3.0f, 1.0f);

        drawSmallLabel (g, pill, flagNames[i].label, juce::jlimit (6.0f, 7.5f, pill.getHeight() * 0.5f),
                        lit ? col::bgDeep : col::textFaint, 0.3f);
    }

    // Divider between identity and knobs.
    g.setColour (col::line);
    g.fillRect (juce::Rectangle<float> (knobsArea.getX() - 6.0f, bounds.getY() + 8.0f,
                                        1.0f, bounds.getHeight() - 16.0f));
}

void VoicePanel::paintReadout (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (area.getHeight() < 40.0f)
        return;

    drawSectionLabel (g, area.removeFromTop (12.0f), "WHY THIS HIT DIFFERS", col::textFaint,
                      6.8f);

    const auto hasHit = lastTriggerCount > 0;
    const auto rowH = juce::jmin (13.0f, area.getHeight() / 4.5f);

    struct Row { const char* label; juce::String value; };
    const Row rows[] =
    {
        { "VEL", hasHit ? juce::String (lastVelocity, 2) : juce::String ("--") },
        { "TIME", hasHit ? msText ((double) lastTimingMs) : juce::String ("--") },
        { "PITCH", hasHit ? semisText ((double) lastPitchSemis) : juce::String ("--") },
        { "DECAY", hasHit ? "x" + juce::String (lastDecayScale, 2) : juce::String ("--") }
    };

    for (const auto& row : rows)
    {
        auto line = area.removeFromTop (rowH);
        g.setFont (ty::sans (7.0f, true));
        g.setColour (col::textFaint);
        g.drawText (row.label, line.removeFromLeft (38.0f), juce::Justification::centredLeft,
                    false);
        g.setFont (ty::mono (8.8f, true));
        g.setColour (hasHit ? col::cyan : col::textFaint);
        g.drawText (row.value, line, juce::Justification::centredRight, false);
    }
}

// =============================================================================
// GroovePanel
// =============================================================================
GroovePanel::GroovePanel (KlopgeestProcessor& processorToUse)
    : processor (processorToUse),
      scope (processorToUse),
      swingKnob (processorToUse.getAPVTS(), pid::swing, "SWING", col::amber),
      humaniseKnob (processorToUse.getAPVTS(), pid::humanize, "HUMANIZE", col::amber),
      stabilityKnob (processorToUse.getAPVTS(), pid::stability, "STABILITY", col::amber),
      ghostKnob (processorToUse.getAPVTS(), pid::ghost, "GHOST", col::amber),
      accentKnob (processorToUse.getAPVTS(), pid::accent, "ACCENT", col::amber),
      mutateAmountKnob (processorToUse.getAPVTS(), pid::mutateAmount, "MUTATE AMT", col::cyan),
      personality (personalityNames(), col::amber),
      seedField (processorToUse)
{
    addAndMakeVisible (scope);
    addAndMakeVisible (swingKnob);
    addAndMakeVisible (humaniseKnob);
    addAndMakeVisible (stabilityKnob);
    addAndMakeVisible (ghostKnob);
    addAndMakeVisible (accentKnob);
    addAndMakeVisible (mutateAmountKnob);
    addAndMakeVisible (personality);
    addAndMakeVisible (seedField);

    for (int v = 0; v < kNumVoices; ++v)
    {
        biasKnobs[(size_t) v] = std::make_unique<ParamKnob> (
            processor.getAPVTS(), pid::voiceId (v, pid::v::bias),
            voiceShortName (v), col::cyan);
        addAndMakeVisible (*biasKnobs[(size_t) v]);
    }

    personality.onChange = [this] (int index)
    {
        if (auto* parameter = processor.getAPVTS().getParameter (pid::personality))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) index));
    };

    refresh();
}

void GroovePanel::refresh()
{
    scope.refresh();
    seedField.refresh();

    if (auto* parameter = processor.getAPVTS().getParameter (pid::personality))
    {
        const auto index = juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue()));
        personality.setSelectedIndex (index, false);
    }
}

void GroovePanel::resized()
{
    auto bounds = getLocalBounds().toFloat().reduced (10.0f, 8.0f);

    if (bounds.getHeight() < 80.0f || bounds.getWidth() < 300.0f)
        return;

    const auto leftW = juce::jmin (272.0f, bounds.getWidth() * 0.26f);
    leftArea = bounds.removeFromLeft (leftW);
    bounds.removeFromLeft (12.0f);

    auto la = leftArea;
    la.removeFromTop (13.0f); // drift scope title

    scopeArea = la.removeFromTop (juce::jmin (100.0f, la.getHeight() * 0.60f));
    la.removeFromTop (6.0f);
    explainArea = la;

    const auto row1H = juce::jmin (94.0f, bounds.getHeight() * 0.53f);
    row1Area = bounds.removeFromTop (row1H);
    bounds.removeFromTop (4.0f);
    row2Area = bounds;

    auto r1 = row1Area;
    const auto specialW = juce::jmin (144.0f, r1.getWidth() * 0.21f);

    auto seedGroup = r1.removeFromRight (specialW);
    r1.removeFromRight (10.0f);
    auto personalityGroup = r1.removeFromRight (specialW);
    r1.removeFromRight (10.0f);

    seedField.setBounds (seedGroup.withTrimmedTop (11.0f)
                             .withHeight (juce::jmin (28.0f, seedGroup.getHeight() - 11.0f))
                             .toNearestInt());
    personality.setBounds (personalityGroup.withTrimmedTop (11.0f)
                               .withHeight (juce::jmin (28.0f, personalityGroup.getHeight() - 11.0f))
                               .toNearestInt());

    ParamKnob* row1Knobs[] =
    {
        &swingKnob, &humaniseKnob, &stabilityKnob,
        &ghostKnob, &accentKnob, &mutateAmountKnob
    };

    const auto knobW = r1.getWidth() / 6.0f;

    for (int i = 0; i < 6; ++i)
        row1Knobs[i]->setBounds (juce::Rectangle<float> (r1.getX() + knobW * (float) i,
                                                         r1.getY(), knobW, r1.getHeight())
                                     .toNearestInt());

    // Per-voice timing bias row.
    auto r2 = row2Area;
    r2.removeFromTop (13.0f);
    const auto biasW = r2.getWidth() / (float) kNumVoices;

    for (int v = 0; v < kNumVoices; ++v)
    {
        if (biasKnobs[(size_t) v] != nullptr)
            biasKnobs[(size_t) v]->setBounds (juce::Rectangle<float> (r2.getX() + biasW * (float) v,
                                                                      r2.getY(), biasW, r2.getHeight())
                                                  .toNearestInt());
    }
}

void GroovePanel::paintExplainer (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (area.getHeight() < 20.0f)
        return;

    auto line = [&] (const char* lead, const char* rest, juce::Colour leadColour)
    {
        auto row = area.removeFromTop (11.0f);
        g.setFont (ty::sans (7.0f, true));
        const auto leadText = juce::String (lead).toUpperCase();
        const auto leadWidth = juce::GlyphArrangement::getStringWidth (ty::sans (7.0f, true),
                                                                       leadText) + 4.0f;
        g.setColour (leadColour);
        g.drawText (leadText, row.removeFromLeft (leadWidth), juce::Justification::centredLeft,
                    false);
        g.setFont (ty::sans (7.2f));
        g.setColour (col::textDim);
        g.drawFittedText (rest, row.toNearestInt(), juce::Justification::centredLeft, 1);
    };

    line ("ANCHORS", "kick & snare hold the grid", col::amber);
    line ("COLOUR", "hats & perc drift and flam", col::cyan);
    line ("STABILITY", "holds the centre line", col::amber);
    line ("HUMANISE", "opens the field around it", col::cyan);
    line ("PERSONALITY", "picks how far the machine dares", col::amber);
}

void GroovePanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 300.0f || bounds.getHeight() < 80.0f)
        return;

    drawSectionLabel (g, { scopeArea.getX(), scopeArea.getY() - 13.0f,
                           scopeArea.getWidth(), 12.0f },
                      "DRIFT SCOPE  -  MS", col::textFaint, 7.0f);

    paintExplainer (g, explainArea);

    // Divider.
    g.setColour (col::line);
    g.fillRect (juce::Rectangle<float> (leftArea.getRight() + 5.0f, bounds.getY() + 8.0f,
                                        1.0f, bounds.getHeight() - 16.0f));

    // Group labels above the special controls.
    auto labelFor = [&] (juce::Component& component, const juce::String& text)
    {
        const auto r = component.getBounds().toFloat();
        drawSectionLabel (g, { r.getX(), r.getY() - 11.0f, r.getWidth(), 10.0f }, text,
                          col::textFaint, 7.0f);
    };

    labelFor (personality, "PERSONALITY");
    labelFor (seedField, "SEED");

    // Bias row header.
    drawSectionLabel (g, { row2Area.getX(), row2Area.getY(), row2Area.getWidth(), 12.0f },
                      "PER-VOICE TIMING BIAS", col::textFaint, 7.0f);

    g.setFont (ty::mono (7.0f));
    g.setColour (col::textFaint);
    g.drawText ("NEG = PUSH   POS = LAG",
                juce::Rectangle<float> (row2Area.getX(), row2Area.getY(),
                                        row2Area.getWidth(), 12.0f),
                juce::Justification::centredRight, false);

    // Vertical separators between time and tone controls in row one.
    for (int i = 1; i < 6; ++i)
    {
        if (i == 3)
            continue;

        const auto x = row1Area.getX() + row1Area.getWidth() * (float) i / 6.0f;
        juce::ignoreUnused (x);
    }
}

// =============================================================================
// GluePanel
// =============================================================================
GluePanel::GluePanel (KlopgeestProcessor& processorToUse)
    : processor (processorToUse),
      glueKnob (processorToUse.getAPVTS(), pid::glue, "GLUE", col::amber),
      heatKnob (processorToUse.getAPVTS(), pid::heat, "HEAT", kHeatColour),
      thresholdKnob (processorToUse.getAPVTS(), pid::threshold, "THRESHOLD", col::amber),
      ratioKnob (processorToUse.getAPVTS(), pid::ratio, "RATIO", col::amber),
      attackKnob (processorToUse.getAPVTS(), pid::attack, "ATTACK", col::amber),
      releaseKnob (processorToUse.getAPVTS(), pid::release, "RELEASE", col::amber),
      kneeKnob (processorToUse.getAPVTS(), pid::knee, "KNEE", col::amber),
      sidechainKnob (processorToUse.getAPVTS(), pid::sidechainHz, "SC HPF", col::amber),
      detectKnob (processorToUse.getAPVTS(), pid::detect, "DETECT", col::cyan),
      mixKnob (processorToUse.getAPVTS(), pid::mix, "MIX", col::amber),
      satPreKnob (processorToUse.getAPVTS(), pid::satPre, "SAT PRE", col::amber),
      satPostKnob (processorToUse.getAPVTS(), pid::satPost, "SAT POST", col::amber),
      outputKnob (processorToUse.getAPVTS(), pid::output, "OUTPUT", col::amber),
      ceilingKnob (processorToUse.getAPVTS(), pid::ceiling, "CEILING", col::amber),
      autoReleaseButton (processorToUse.getAPVTS(), pid::autoRelease, "AUTO REL",
                         KlopButton::Variant::amber),
      limiterButton (processorToUse.getAPVTS(), pid::limiter, "LIMITER",
                     KlopButton::Variant::amber)
{
    for (auto* knob : { &glueKnob, &heatKnob, &thresholdKnob, &ratioKnob, &attackKnob,
                        &releaseKnob, &kneeKnob, &sidechainKnob, &detectKnob, &mixKnob,
                        &satPreKnob, &satPostKnob, &outputKnob, &ceilingKnob })
    {
        addAndMakeVisible (*knob);
    }

    addAndMakeVisible (autoReleaseButton);
    addAndMakeVisible (limiterButton);
    addAndMakeVisible (grMeter);
    addAndMakeVisible (outputMeter);

    autoReleaseButton.setFontHeight (8.0f);
    limiterButton.setFontHeight (8.0f);

    refresh();
}

void GluePanel::refresh()
{
    auto& visual = processor.getVisualState();
    grMeter.setGainReductionDb (visual.gainReductionDb.load (std::memory_order_relaxed));
    outputMeter.setLevelsDb (visual.outPeakL.load (std::memory_order_relaxed),
                             visual.outPeakR.load (std::memory_order_relaxed),
                             visual.outRmsL.load (std::memory_order_relaxed),
                             visual.outRmsR.load (std::memory_order_relaxed));
}

void GluePanel::resized()
{
    auto bounds = getLocalBounds().toFloat().reduced (10.0f, 8.0f);

    if (bounds.getHeight() < 80.0f || bounds.getWidth() < 500.0f)
        return;

    const auto macroW = juce::jmin (186.0f, bounds.getWidth() * 0.18f);
    macroArea = bounds.removeFromLeft (macroW);
    bounds.removeFromLeft (10.0f);

    const auto meterW = juce::jmin (188.0f, bounds.getWidth() * 0.19f);
    meterArea = bounds.removeFromRight (meterW);
    bounds.removeFromRight (10.0f);

    compressorArea = bounds;

    // ---- Macros -------------------------------------------------------------
    auto ma = macroArea;
    ma.removeFromTop (12.0f);
    auto macroRow = ma.removeFromTop (juce::jmax (40.0f, ma.getHeight() * 0.64f));
    glueKnob.setBounds (macroRow.removeFromLeft (macroRow.getWidth() * 0.5f).toNearestInt());
    heatKnob.setBounds (macroRow.toNearestInt());

    auto toggleRow = ma.removeFromTop (juce::jmin (26.0f, ma.getHeight() - 4.0f));
    autoReleaseButton.setBounds (toggleRow.removeFromLeft (toggleRow.getWidth() * 0.5f - 3.0f)
                                     .toNearestInt());
    toggleRow.removeFromLeft (6.0f);
    limiterButton.setBounds (toggleRow.toNearestInt());

    // ---- Master processors --------------------------------------------------
    const auto rowH = (compressorArea.getHeight() - 6.0f) * 0.5f;
    const auto cellW = compressorArea.getWidth() / 6.0f;

    ParamKnob* knobs[12] =
    {
        &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob, &kneeKnob, &sidechainKnob,
        &detectKnob, &mixKnob, &satPreKnob, &satPostKnob, &outputKnob, &ceilingKnob
    };

    for (int i = 0; i < 12; ++i)
    {
        const auto row = i / 6;
        const auto col = i % 6;
        knobs[i]->setBounds (juce::Rectangle<float> (compressorArea.getX() + cellW * (float) col,
                                                     compressorArea.getY()
                                                         + (rowH + 6.0f) * (float) row,
                                                     cellW, rowH)
                                 .toNearestInt());
    }

    // ---- Meters -------------------------------------------------------------
    auto meters = meterArea;
    meters.removeFromTop (14.0f);
    grArea = meters.removeFromLeft (juce::jmin (56.0f, meters.getWidth() * 0.42f));
    meters.removeFromLeft (8.0f);
    outputMeterArea = meters;

    grMeter.setBounds (grArea.toNearestInt());
    outputMeter.setBounds (outputMeterArea.toNearestInt());
}

void GluePanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 500.0f || bounds.getHeight() < 80.0f)
        return;

    // Macro block attracts the eye without shouting.
    auto macroPanel = juce::Rectangle<float> (macroArea.getX() - 4.0f, macroArea.getY() - 2.0f,
                                              macroArea.getWidth() + 8.0f,
                                              macroArea.getHeight() + 4.0f);
    g.setColour (col::amber.withAlpha (0.035f));
    g.fillRoundedRectangle (macroPanel, 6.0f);
    g.setColour (col::amberDim.withAlpha (0.6f));
    g.drawRoundedRectangle (macroPanel.reduced (0.5f), 6.0f, 1.0f);

    drawSectionLabel (g, { macroArea.getX(), macroArea.getY(), macroArea.getWidth(), 12.0f },
                      "PERFORMANCE MACROS", col::amber, 7.0f);

    g.setFont (ty::sans (6.6f));
    g.setColour (col::textFaint);
    g.drawText ("one turn moves the whole bus",
                juce::Rectangle<float> (macroArea.getX(), limiterButton.getBottom() + 4.0f,
                                        macroArea.getWidth(), 10.0f),
                juce::Justification::centredLeft, false);

    // Meter headings + live GR value.
    auto meterHeading = juce::Rectangle<float> (grArea.getX(), grArea.getY() - 14.0f,
                                                meterArea.getWidth(), 12.0f);
    drawSectionLabel (g, meterHeading, "GR", col::textFaint, 7.0f);

    g.setFont (ty::mono (8.0f, true));
    g.setColour (grMeter.getDisplayDb() > 0.25f ? col::amberHi : col::textFaint);
    g.drawText ("-" + juce::String (grMeter.getDisplayDb(), 1) + " dB", meterHeading,
                juce::Justification::centredRight, false);

    drawSectionLabel (g, { outputMeterArea.getX(), outputMeterArea.getY() - 14.0f,
                           outputMeterArea.getWidth(), 12.0f },
                      "OUT", col::textFaint, 7.0f);

    g.setColour (col::line);
    g.fillRect (juce::Rectangle<float> (macroArea.getRight() + 5.0f, bounds.getY() + 8.0f,
                                        1.0f, bounds.getHeight() - 16.0f));
    g.fillRect (juce::Rectangle<float> (meterArea.getX() - 5.0f, bounds.getY() + 8.0f,
                                        1.0f, bounds.getHeight() - 16.0f));
}

} // namespace klopgeest::ui
