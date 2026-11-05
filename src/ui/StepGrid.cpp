#include "ui/StepGrid.h"

#include <cmath>

#include "core/ParamIDs.h"
#include "core/Parameters.h"
#include "plugin/KlopgeestProcessor.h"

namespace klopgeest::ui
{

StepGrid::StepGrid (KlopgeestProcessor& processorToUse)
    : processor (processorToUse)
{
    hScroll.setAutoHide (false);
    hScroll.setVisible (false);
    hScroll.addListener (this);
    addAndMakeVisible (hScroll);

    for (int v = 0; v < kNumVoices; ++v)
    {
        auto* mute = muteButtons.add (new KlopButton ("M", KlopButton::Variant::red));
        mute->setCompact (true);
        mute->setFontHeight (8.0f);
        mute->setTooltip ("Mute " + juce::String (voiceName (v)));
        mute->setStateProvider ([this, v] { return processor.getPattern().lanes[(size_t) v].muted; });
        mute->onClick = [this, v] { toggleLaneFlag (v, 0); };
        addAndMakeVisible (mute);

        auto* solo = soloButtons.add (new KlopButton ("S", KlopButton::Variant::cyan));
        solo->setCompact (true);
        solo->setFontHeight (8.0f);
        solo->setTooltip ("Solo " + juce::String (voiceName (v)));
        solo->setStateProvider ([this, v] { return processor.getPattern().lanes[(size_t) v].soloed; });
        solo->onClick = [this, v] { toggleLaneFlag (v, 1); };
        addAndMakeVisible (solo);

        auto* lock = lockButtons.add (new KlopButton ("L", KlopButton::Variant::amber));
        lock->setCompact (true);
        lock->setFontHeight (8.0f);
        lock->setTooltip ("Lock " + juce::String (voiceName (v)) + " from humanisation");
        lock->setStateProvider ([this, v] { return processor.getPattern().lanes[(size_t) v].locked; });
        lock->onClick = [this, v] { toggleLaneFlag (v, 2); };
        addAndMakeVisible (lock);
    }

    lastPatternLength = processor.getPattern().length;
}

StepGrid::~StepGrid()
{
    hScroll.removeListener (this);
}

void StepGrid::setSelection (int voice, int step)
{
    const auto& pattern = processor.getPattern();
    const auto newVoice = juce::jlimit (0, kNumVoices - 1, voice);
    const auto newStep = juce::jlimit (0, juce::jmax (0, pattern.length - 1), step);

    if (newVoice == selectedVoice && newStep == selectedStep)
        return;

    selectedVoice = newVoice;
    selectedStep = newStep;
    repaint();
}

void StepGrid::selectCell (int voice, int step, bool notify)
{
    const auto oldVoice = selectedVoice;
    const auto oldStep = selectedStep;

    setSelection (voice, step);

    if (notify && (oldVoice != selectedVoice || oldStep != selectedStep)
        && onSelectionChanged != nullptr)
    {
        onSelectionChanged (selectedVoice, selectedStep);
    }
}

void StepGrid::applyPatternEdit (const std::function<void (PatternData&)>& edit)
{
    auto pattern = processor.getPattern();
    edit (pattern);
    processor.publishPattern (pattern);

    if (onPatternEdited != nullptr)
        onPatternEdited();

    repaint();
}

void StepGrid::toggleStepAt (int voice, int step)
{
    bool newValue = false;

    applyPatternEdit ([voice, step, &newValue] (PatternData& pattern)
    {
        auto& lane = pattern.lanes[(size_t) voice];
        auto& target = lane.steps[(size_t) step];
        target.on = ! target.on;
        newValue = target.on;

        // Editing beyond a short lane loop should extend the loop so the hit is
        // actually heard.
        if (newValue && step >= lane.length)
            lane.length = juce::jmin (pattern.length, step + 1);
    });

    dragValue = newValue;
}

void StepGrid::setStepOn (int voice, int step, bool on)
{
    const auto& current = processor.getPattern().lanes[(size_t) voice].steps[(size_t) step];

    if (current.on == on)
        return;

    applyPatternEdit ([voice, step, on] (PatternData& pattern)
    {
        auto& lane = pattern.lanes[(size_t) voice];
        auto& target = lane.steps[(size_t) step];
        target.on = on;

        if (on && step >= lane.length)
            lane.length = juce::jmin (pattern.length, step + 1);
    });
}

void StepGrid::toggleLaneFlag (int voice, int flag)
{
    applyPatternEdit ([voice, flag] (PatternData& pattern)
    {
        auto& lane = pattern.lanes[(size_t) voice];

        if (flag == 0)
            lane.muted = ! lane.muted;
        else if (flag == 1)
            lane.soloed = ! lane.soloed;
        else
            lane.locked = ! lane.locked;
    });
}

void StepGrid::refresh()
{
    auto& visual = processor.getVisualState();

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto count = visual.triggerCount[(size_t) v].load (std::memory_order_relaxed);

        if (count != lastTriggerCount[(size_t) v])
        {
            lastTriggerCount[(size_t) v] = count;
            hitPulse[v] = 1.0f;
        }
        else
        {
            hitPulse[v] *= 0.86f;
        }
    }

    const auto length = processor.getPattern().length;
    bool needsRepaint = false;

    const auto changeCounter = visual.patternChangeCounter.load (std::memory_order_relaxed);

    if (changeCounter != lastPatternChangeCounter)
    {
        lastPatternChangeCounter = changeCounter;
        needsRepaint = true;
    }

    if (length != lastPatternLength)
    {
        lastPatternLength = length;
        scrollOffset = 0.0f;
        recomputeMetrics();
        clampScroll();
        needsRepaint = true;
    }

    if (selectedStep >= length)
    {
        setSelection (selectedVoice, length - 1);
        needsRepaint = true;
    }

    if (visual.playing.load (std::memory_order_relaxed))
        needsRepaint = true;

    for (int v = 0; v < kNumVoices; ++v)
        if (hitPulse[v] > 0.02f)
            needsRepaint = true;

    if (needsRepaint)
        repaint();
}

void StepGrid::resized()
{
    recomputeMetrics();
    clampScroll();
}

void StepGrid::recomputeMetrics()
{
    const auto full = getLocalBounds().toFloat();

    if (full.getWidth() < 40.0f || full.getHeight() < 40.0f)
        return;

    auto area = full.reduced (6.0f);

    titleArea = area.removeFromTop (juce::jmin (titleH, area.getHeight() * 0.16f));
    area.removeFromTop (2.0f);

    const auto scrollBarH = area.getHeight() > 40.0f ? 10.0f : 0.0f;

    if (scrollBarH > 0.0f)
    {
        auto scrollArea = area.removeFromBottom (scrollBarH);
        area.removeFromBottom (2.0f);
        hScroll.setBounds (scrollArea.withTrimmedLeft (laneLabelW).toNearestInt());
    }

    const auto numbersHActual = juce::jmin (numbersH, area.getHeight() * 0.12f);
    auto numbersBand = area.removeFromTop (numbersHActual);
    numbersArea = numbersBand.withTrimmedLeft (laneLabelW);
    numbersArea.setHeight (numbersHActual);

    rowsArea = area;

    if (rowsArea.getHeight() < 8.0f || rowsArea.getWidth() < laneLabelW + 20.0f)
        return;

    labelArea = rowsArea.withWidth (laneLabelW);
    cellsArea = rowsArea.withTrimmedLeft (laneLabelW);

    const auto steps = juce::jmax (1, processor.getPattern().length);
    const auto available = cellsArea.getWidth();
    cellW = juce::jlimit (17.0f, 44.0f, available / (float) steps);
    contentWidth = cellW * (float) steps;
    scrollNeeded = contentWidth > available + 0.5f;
    laneH = cellsArea.getHeight() / (float) kNumVoices;

    positionLaneButtons();
    updateScrollBar();
}

void StepGrid::positionLaneButtons()
{
    if (rowsArea.getHeight() < 8.0f || labelArea.getWidth() < 60.0f)
        return;

    const auto buttonSize = juce::jlimit (12.0f, 17.0f, laneH - 9.0f);
    const auto gap = 2.0f;
    const auto right = labelArea.getRight() - 4.0f;

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto centreY = rowsArea.getY() + laneH * ((float) v + 0.5f);
        auto x = right - buttonSize;

        auto place = [&] (KlopButton* button)
        {
            if (button != nullptr)
                button->setBounds (juce::Rectangle<float> (x, centreY - buttonSize * 0.5f,
                                                           buttonSize, buttonSize)
                                       .toNearestInt());
            x -= buttonSize + gap;
        };

        place (lockButtons[v]);
        place (soloButtons[v]);
        place (muteButtons[v]);
    }
}

void StepGrid::updateScrollBar()
{
    if (scrollNeeded && cellsArea.getWidth() > 4.0f)
    {
        hScroll.setVisible (true);
        hScroll.setRangeLimits (0.0, (double) contentWidth, juce::dontSendNotification);
        hScroll.setCurrentRange ((double) scrollOffset, (double) cellsArea.getWidth(),
                                 juce::dontSendNotification);
        hScroll.setSingleStepSize ((double) cellW * 2.0);
    }
    else
    {
        hScroll.setVisible (false);
    }
}

void StepGrid::clampScroll()
{
    const auto maxOffset = juce::jmax (0.0f, contentWidth - cellsArea.getWidth());

    if (! scrollNeeded)
        scrollOffset = 0.0f;
    else
        scrollOffset = juce::jlimit (0.0f, maxOffset, scrollOffset);

    if (scrollNeeded)
        hScroll.setCurrentRange ((double) scrollOffset, (double) cellsArea.getWidth(),
                                 juce::dontSendNotification);
}

void StepGrid::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    scrollOffset = juce::jmax (0.0f, (float) newRangeStart);
    clampScroll();
    repaint();
}

bool StepGrid::cellAt (juce::Point<int> position, int& voice, int& step) const
{
    const auto point = position.toFloat();

    if (! cellsArea.contains (point) || laneH <= 0.0f)
        return false;

    voice = (int) std::floor ((point.y - rowsArea.getY()) / laneH);
    step = (int) std::floor ((point.x - cellsArea.getX() + scrollOffset) / cellW);

    const auto steps = processor.getPattern().length;

    if (voice < 0 || voice >= kNumVoices || step < 0 || step >= steps)
        return false;

    return true;
}

juce::Rectangle<float> StepGrid::laneCellRect (int voice, int step) const
{
    return { cellsArea.getX() - scrollOffset + cellW * (float) step + 1.0f,
             rowsArea.getY() + laneH * (float) voice + 1.2f,
             juce::jmax (1.0f, cellW - 2.0f),
             juce::jmax (1.0f, laneH - 2.4f) };
}

void StepGrid::mouseDown (const juce::MouseEvent& event)
{
    const auto position = event.getPosition();

    if (numbersArea.contains (position.toFloat()))
    {
        const auto step = (int) std::floor ((position.x - cellsArea.getX() + scrollOffset) / cellW);
        selectCell (selectedVoice, step, true);
        dragging = false;
        return;
    }

    if (labelArea.contains (position.toFloat()))
    {
        const auto voice = (int) std::floor ((position.y - rowsArea.getY()) / juce::jmax (1.0f, laneH));

        if (voice >= 0 && voice < kNumVoices)
            selectCell (voice, selectedStep, true);

        dragging = false;
        return;
    }

    int voice = 0, step = 0;

    if (! cellAt (position, voice, step))
        return;

    selectCell (voice, step, true);
    toggleStepAt (voice, step);
    dragging = true;
    dragVoice = voice;
    dragLastStep = step;
}

void StepGrid::mouseDrag (const juce::MouseEvent& event)
{
    if (! dragging)
        return;

    int voice = 0, step = 0;

    if (! cellAt (event.getPosition(), voice, step) || voice != dragVoice)
        return;

    if (step == dragLastStep)
        return;

    dragLastStep = step;
    selectCell (voice, step, true);
    setStepOn (voice, step, dragValue);
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    dragVoice = -1;
    dragLastStep = -1;
}

void StepGrid::mouseMove (const juce::MouseEvent& event)
{
    int voice = -1, step = -1;

    if (cellsArea.contains (event.getPosition().toFloat()))
        cellAt (event.getPosition(), voice, step);

    if (voice != hoveredVoice || step != hoveredStep)
    {
        hoveredVoice = voice;
        hoveredStep = step;
        repaint();
    }
}

void StepGrid::mouseExit (const juce::MouseEvent&)
{
    if (hoveredVoice != -1 || hoveredStep != -1)
    {
        hoveredVoice = -1;
        hoveredStep = -1;
        repaint();
    }
}

void StepGrid::mouseWheelMove (const juce::MouseEvent& event,
                               const juce::MouseWheelDetails& wheel)
{
    if (! scrollNeeded)
        return;

    auto delta = wheel.deltaX;

    if (std::abs (delta) < 0.001f)
        delta = wheel.deltaY;

    scrollOffset -= delta * cellW * 2.0f;
    clampScroll();
    repaint();
    juce::ignoreUnused (event);
}

void StepGrid::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 40.0f || bounds.getHeight() < 40.0f)
        return;

    drawPanel (g, bounds, col::panel, col::line, 8.0f);

    paintRows (g);
    paintTitle (g, titleArea);
    paintStepNumbers (g);

    if (labelArea.getWidth() > 20.0f)
        paintLaneLabels (g);

    paintCells (g);

    if (labelArea.getWidth() > 10.0f)
    {
        g.setColour (col::line);
        g.fillRect (juce::Rectangle<float> (cellsArea.getX() - 1.0f, rowsArea.getY(),
                                            1.0f, rowsArea.getHeight()));

        g.setColour (col::line.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (labelArea.getX(), rowsArea.getBottom() - 1.0f,
                                            bounds.getWidth() - 12.0f, 1.0f));
    }
}

void StepGrid::paintRows (juce::Graphics& g)
{
    if (rowsArea.getHeight() <= 0.0f)
        return;

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto row = juce::Rectangle<float> (rowsArea.getX(),
                                                 rowsArea.getY() + laneH * (float) v,
                                                 rowsArea.getWidth(), laneH);

        if (v % 2 == 1)
        {
            g.setColour (col::panelLow.withAlpha (0.45f));
            g.fillRect (row);
        }

        if (v == selectedVoice)
        {
            g.setColour (col::amber.withAlpha (0.055f));
            g.fillRect (row);
        }
        else if (v == hoveredVoice)
        {
            g.setColour (juce::Colours::white.withAlpha (0.02f));
            g.fillRect (row);
        }

        g.setColour (col::line.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (row.getX(), row.getBottom() - 1.0f,
                                            row.getWidth(), 1.0f));
    }
}

void StepGrid::paintTitle (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (area.getHeight() < 10.0f)
        return;

    auto title = area;
    auto labelAreaA = title.removeFromLeft (52.0f);
    drawSectionLabel (g, labelAreaA, "PATTERN", col::textFaint, 8.5f);

    auto& pattern = processor.getPattern();
    const auto name = processor.getPatternDisplayName().toUpperCase();

    auto nameArea = title.removeFromLeft (juce::jmin (300.0f, title.getWidth() * 0.55f));

    auto font = ty::sans (13.0f, true);

    if (juce::GlyphArrangement::getStringWidth (font, name) > nameArea.getWidth())
        font = ty::sans (juce::jmax (9.0f, nameArea.getWidth() / (float) juce::jmax (1, name.length()) * 1.7f), true);

    drawSpacedText (g, name, nameArea, font, col::amberHi,
                    juce::Justification::centredLeft, 1.2f);

    // Right-hand readout: selected step, grid size and the motion legend.
    const auto rightEdge = title.getRight();

    auto drawRight = [&] (const juce::String& text, juce::Font font, juce::Colour colour,
                          float rightX, float gap)
    {
        const auto width = juce::GlyphArrangement::getStringWidth (font, text) + 2.0f;
        const auto target = juce::Rectangle<float> (rightX - width, area.getY(),
                                                    width, area.getHeight());
        g.setFont (font);
        g.setColour (colour);
        g.drawText (text, target, juce::Justification::centredRight, false);
        return rightX - width - gap;
    };

    auto x = rightEdge;

    if (title.getWidth() > 420.0f)
    {
        const auto stepsText = juce::String (pattern.length) + " STEPS";
        x = drawRight (stepsText, ty::mono (8.0f), col::textFaint, x, 16.0f);

        // Cyan legend dot plus "NOW PLAYING".
        g.setColour (col::cyan);
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f)
                           .withCentre ({ x - 4.0f, area.getCentreY() }));
        x = drawRight ("NOW", ty::mono (7.5f), col::cyanDim, x - 10.0f, 16.0f);
    }

    if (title.getWidth() > 300.0f)
    {
        const auto selected = juce::String (voiceShortName (selectedVoice))
                              + "  " + juce::String (selectedStep + 1).paddedLeft ('0', 2);

        g.setColour (col::amber.withAlpha (0.25f));
        g.fillRoundedRectangle (juce::Rectangle<float> (x - 72.0f, area.getY() + 4.0f,
                                                        68.0f, area.getHeight() - 8.0f),
                                3.0f);
        drawRight (selected, ty::mono (8.5f, true), col::amberHi, x - 6.0f, 0.0f);
    }
}

void StepGrid::paintStepNumbers (juce::Graphics& g)
{
    if (numbersArea.getHeight() <= 2.0f || cellW <= 0.0f)
        return;

    const auto& pattern = processor.getPattern();
    const auto steps = pattern.length;

    g.saveState();
    g.reduceClipRegion (numbersArea.toNearestInt());

    const auto font = ty::mono (7.3f);

    for (int s = 0; s < steps; ++s)
    {
        const auto x = cellsArea.getX() - scrollOffset + cellW * (float) s;

        if (x + cellW < numbersArea.getX() - 1.0f || x > numbersArea.getRight() + 1.0f)
            continue;

        const auto isBeat = s % 4 == 0;
        const auto isBar = s % 16 == 0;

        if (cellW >= 26.0f || isBeat)
        {
            g.setFont (font);
            g.setColour (isBar ? col::textDim : col::textFaint);
            g.drawText (juce::String (s + 1),
                        juce::Rectangle<float> (x, numbersArea.getY(),
                                                cellW, numbersArea.getHeight()),
                        juce::Justification::centred, false);
        }
        else if (isBar)
        {
            g.setColour (col::textFaint);
            g.fillRect (juce::Rectangle<float> (x + cellW * 0.5f - 0.5f,
                                                numbersArea.getBottom() - 4.0f, 1.0f, 3.0f));
        }
    }

    g.restoreState();
}

void StepGrid::paintLaneLabels (juce::Graphics& g)
{
    auto& visual = processor.getVisualState();
    auto& pattern = processor.getPattern();

    g.saveState();
    g.reduceClipRegion (labelArea.toNearestInt());

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto row = juce::Rectangle<float> (labelArea.getX(),
                                                 rowsArea.getY() + laneH * (float) v,
                                                 labelArea.getWidth(), laneH);
        const auto accent = voiceIsAnchor (v) ? col::amber : col::cyan;
        const auto audible = pattern.laneAudible (v);
        const auto isSelected = v == selectedVoice;

        // Role spine.
        g.setColour (accent.withAlpha (audible ? 0.8f : 0.2f));
        g.fillRect (juce::Rectangle<float> (row.getX(), row.getY() + 3.0f,
                                            2.5f, juce::jmax (1.0f, row.getHeight() - 6.0f)));

        // Trigger LED.
        const auto pulse = juce::jlimit (0.0f, 1.0f, hitPulse[v]);
        const auto ledCentre = juce::Point<float> (row.getX() + 13.0f, row.getCentreY());

        g.setColour (accent.withAlpha (0.14f + 0.86f * pulse));
        g.fillEllipse (juce::Rectangle<float> (4.4f, 4.4f).withCentre (ledCentre));

        if (pulse > 0.1f)
        {
            g.setColour (accent.withAlpha (0.18f * pulse));
            g.drawEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (ledCentre), 1.0f);
        }

        // Name.
        const auto lane = pattern.lanes[(size_t) v];
        const auto showLoop = lane.length != pattern.length;
        auto nameArea = row.withTrimmedLeft (21.0f).withTrimmedRight (showLoop ? 78.0f : 62.0f);
        nameArea = nameArea.reduced (0.0f, 1.0f);

        drawSpacedText (g, voiceShortName (v), nameArea,
                        ty::sans (juce::jlimit (8.0f, 11.0f, laneH * 0.40f), true),
                        isSelected ? col::amberHi : (audible ? col::text : col::textFaint),
                        juce::Justification::centredLeft, 0.7f);

        if (showLoop && laneH >= 17.0f)
        {
            const auto loopArea = juce::Rectangle<float> (row.getRight() - 79.0f,
                                                          row.getY(),
                                                          14.0f, row.getHeight());
            g.setColour (col::textFaint);
            g.setFont (ty::mono (6.6f, true));
            g.drawText (juce::String (lane.length), loopArea, juce::Justification::centredRight,
                        false);
        }

        // Voice activity hairline under the name.
        const auto level = juce::jlimit (0.0f, 1.0f,
                                         visual.voiceLevel[(size_t) v].load (std::memory_order_relaxed));
        const auto meterY = row.getBottom() - 2.4f;
        const auto meterW = nameArea.getWidth();

        g.setColour (accent.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (nameArea.getX(), meterY, meterW, 1.4f));
        g.setColour (accent.withAlpha (0.75f));

        if (level > 0.002f)
            g.fillRect (juce::Rectangle<float> (nameArea.getX(), meterY,
                                                meterW * level, 1.4f));

        juce::ignoreUnused (isSelected);
    }

    g.restoreState();
}

void StepGrid::paintCells (juce::Graphics& g)
{
    if (cellsArea.getHeight() <= 0.0f || cellW <= 0.0f)
        return;

    auto& pattern = processor.getPattern();
    auto& visual = processor.getVisualState();
    const auto steps = pattern.length;
    const auto playing = visual.playing.load (std::memory_order_relaxed);

    g.saveState();
    g.reduceClipRegion (cellsArea.toNearestInt());

    const auto first = juce::jmax (0, (int) std::floor (scrollOffset / cellW));
    const auto last = juce::jmin (steps - 1,
                                  (int) std::ceil ((scrollOffset + cellsArea.getWidth()) / cellW));

    // Alternate beat-group shading for an easy 1-2-3-4 read.
    for (int group = first / 4; group <= last / 4; ++group)
    {
        if (group % 2 == 0)
            continue;

        const auto x = cellsArea.getX() - scrollOffset + cellW * (float) (group * 4);
        g.setColour (juce::Colours::white.withAlpha (0.022f));
        g.fillRect (juce::Rectangle<float> (x, cellsArea.getY(), cellW * 4.0f,
                                            cellsArea.getHeight()));
    }

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto laneStep = visual.laneStep[(size_t) v].load (std::memory_order_relaxed);
        const auto laneLength = pattern.lanes[(size_t) v].length;

        for (int s = first; s <= last; ++s)
        {
            const auto& step = pattern.step (v, s);
            const auto rect = laneCellRect (v, s);
            const auto current = playing && laneStep >= 0 && laneStep == s;
            const auto selected = v == selectedVoice && s == selectedStep;
            const auto hovered = v == hoveredVoice && s == hoveredStep;

            paintCell (g, rect, v, s, step, selected, current, hovered);

            if (s >= laneLength)
            {
                g.setColour (col::bgDeep.withAlpha (0.42f));
                g.fillRoundedRectangle (rect, 2.5f);
            }
        }

        // Lane loop boundary: shorter lanes get a visible wrap marker.
        if (laneLength < steps)
        {
            const auto x = cellsArea.getX() - scrollOffset + cellW * (float) laneLength;
            const auto colour = voiceIsAnchor (v) ? col::amber : col::cyan;
            g.setColour (colour.withAlpha (0.30f));
            g.fillRect (juce::Rectangle<float> (x - 1.0f,
                                                rowsArea.getY() + laneH * (float) v + 2.0f,
                                                1.5f, juce::jmax (2.0f, laneH - 4.0f)));
        }
    }

    // Beat and bar separators, drawn over the cells for a score-like grid.
    for (int s = first; s <= last; ++s)
    {
        if (s <= 0 || s % 4 != 0)
            continue;

        const auto x = cellsArea.getX() - scrollOffset + cellW * (float) s;
        const auto isBar = s % 16 == 0;
        g.setColour (isBar ? col::amber.withAlpha (0.20f) : col::line.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (x - 0.5f, cellsArea.getY(),
                                            isBar ? 2.0f : 1.0f, cellsArea.getHeight()));
    }

    g.restoreState();
}

void StepGrid::paintCell (juce::Graphics& g, juce::Rectangle<float> rect, int voice, int step,
                          const Step& stepData, bool selected, bool current, bool hovered)
{
    if (rect.getWidth() < 2.0f || rect.getHeight() < 2.0f)
        return;

    const auto radius = juce::jmin (3.5f, rect.getHeight() * 0.22f);
    const auto inset = rect.reduced (0.0f, 0.0f);
    juce::ignoreUnused (step, voice);

    const auto probability = juce::jlimit (0.0f, 1.0f, stepData.probability);
    const auto velocity = juce::jlimit (0.0f, 1.0f, stepData.velocity);

    if (! stepData.on)
    {
        g.setColour (hovered ? col::offCellHi : col::offCell);
        g.fillRoundedRectangle (inset, radius);

        g.setColour (col::line.withAlpha (hovered ? 0.9f : 0.6f));
        g.drawRoundedRectangle (inset.reduced (0.5f), radius, 1.0f);
    }
    else
    {
        g.setColour (col::offCellHi);
        g.fillRoundedRectangle (inset, radius);

        const auto probAlpha = 0.42f + 0.58f * probability;
        const auto barHeight = inset.getHeight() * (0.16f + 0.80f * velocity);
        auto bar = inset.withHeight (barHeight)
                       .withY (inset.getBottom() - barHeight);

        juce::ColourGradient fill (col::amber.withAlpha (0.55f * probAlpha),
                                   inset.getCentreX(), bar.getY(),
                                   col::amber.withAlpha (0.95f * probAlpha),
                                   inset.getCentreX(), inset.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (bar, radius);

        g.setColour (col::amberHi.withAlpha (0.65f * probAlpha));
        g.fillRect (juce::Rectangle<float> (bar.getX() + 1.5f, bar.getY(),
                                            juce::jmax (0.0f, bar.getWidth() - 3.0f), 1.2f));

        if (velocity > 0.9f)
        {
            g.setColour (col::amber.withAlpha (0.20f));
            g.drawRoundedRectangle (inset.expanded (1.0f), radius + 1.0f, 1.2f);
        }

        if (probability < 0.995f)
        {
            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle (inset, radius);
            g.reduceClipRegion (clip);
            g.setColour (col::bgDeep.withAlpha (0.5f));

            for (float x = inset.getX() - inset.getHeight(); x < inset.getRight();
                 x += 4.5f)
            {
                g.drawLine (x, inset.getBottom(), x + inset.getHeight(), inset.getY(), 1.0f);
            }

            g.restoreState();
        }

        if (stepData.ratchet > 1)
        {
            const auto ticks = juce::jlimit (2, 4, stepData.ratchet);
            g.setColour (col::bgDeep.withAlpha (0.8f));

            for (int t = 1; t < ticks; ++t)
            {
                const auto x = inset.getX() + inset.getWidth() * (float) t / (float) ticks;
                g.fillRect (juce::Rectangle<float> (x - 0.5f, inset.getY() + 1.5f,
                                                    1.2f, juce::jmin (4.0f, inset.getHeight() * 0.22f)));
            }

            if (inset.getWidth() > 22.0f)
            {
                g.setColour (col::bgDeep.withAlpha (0.85f));
                g.setFont (ty::mono (juce::jlimit (6.0f, 8.0f, inset.getHeight() * 0.28f), true));
                g.drawText ("x" + juce::String (ticks),
                            inset.reduced (2.5f, 1.0f).withTrimmedTop (1.0f),
                            juce::Justification::topRight, false);
            }
        }
    }

    if (stepData.locked)
    {
        g.setColour (col::amberHi.withAlpha (stepData.on ? 0.9f : 0.45f));
        g.fillRect (juce::Rectangle<float> (inset.getRight() - 4.5f,
                                            inset.getY() + 3.0f, 2.4f, 2.4f));
    }

    if (hovered && ! selected && ! current)
    {
        g.setColour (col::lineHi);
        g.drawRoundedRectangle (inset.reduced (0.5f), radius, 1.0f);
    }

    if (current)
    {
        g.setColour (col::cyan.withAlpha (0.10f));
        g.fillRoundedRectangle (inset, radius);

        g.setColour (col::cyan.withAlpha (0.16f));
        g.drawRoundedRectangle (inset.expanded (1.5f), radius + 1.5f, 1.6f);

        g.setColour (col::cyan);
        g.drawRoundedRectangle (inset.reduced (0.5f), radius, 1.5f);
    }

    if (selected)
    {
        g.setColour (col::text.withAlpha (0.92f));
        g.drawRoundedRectangle (inset.reduced (0.8f), juce::jmax (1.5f, radius - 0.5f), 1.4f);
    }

    if (selected && stepData.on)
    {
        const auto barHeight = inset.getHeight() * (0.16f + 0.80f * velocity);
        const auto y = inset.getBottom() - barHeight;
        g.setColour (col::text.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (inset.getX() + 1.0f, y,
                                            juce::jmax (0.0f, inset.getWidth() - 2.0f), 1.0f));
    }
}

} // namespace klopgeest::ui
