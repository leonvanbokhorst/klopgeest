#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/Pattern.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace klopgeest
{
class KlopgeestProcessor;
}

namespace klopgeest::ui
{

// -----------------------------------------------------------------------------
// The performance surface: eight voice lanes against the pattern grid.
//
// Rows are independent: every lane owns its own loop length, so the cyan
// "now playing" cell can sit at a different column on every row. Editing is
// copy-on-write against the processor's message-thread pattern and published
// immediately, so no keystroke is ever lost.
// -----------------------------------------------------------------------------
class StepGrid final : public juce::Component,
                       private juce::ScrollBar::Listener
{
public:
    explicit StepGrid (KlopgeestProcessor& processor);
    ~StepGrid() override;

    void setSelection (int voice, int step);
    int getSelectedVoice() const noexcept { return selectedVoice; }
    int getSelectedStep() const noexcept { return selectedStep; }

    /** Called when the user changes the selected step (not on programmatic updates). */
    std::function<void (int voice, int step)> onSelectionChanged;

    /** Called after a pattern edit was published. */
    std::function<void()> onPatternEdited;

    void refresh();

    void resized() override;
    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    void recomputeMetrics();
    void clampScroll();
    void positionLaneButtons();
    void updateScrollBar();

    bool cellAt (juce::Point<int> position, int& voice, int& step) const;
    juce::Rectangle<float> laneCellRect (int voice, int step) const;

    void selectCell (int voice, int step, bool notify);
    void applyPatternEdit (const std::function<void (PatternData&)>& edit);
    void toggleStepAt (int voice, int step);
    void setStepOn (int voice, int step, bool on);
    void toggleLaneFlag (int voice, int flag);

    void paintTitle (juce::Graphics&, juce::Rectangle<float>);
    void paintRows (juce::Graphics&);
    void paintLaneLabels (juce::Graphics&);
    void paintStepNumbers (juce::Graphics&);
    void paintCells (juce::Graphics&);
    void paintCell (juce::Graphics&, juce::Rectangle<float>, int voice, int step,
                    const Step&, bool selected, bool current, bool hovered);

    KlopgeestProcessor& processor;

    juce::ScrollBar hScroll { false };
    juce::OwnedArray<KlopButton> muteButtons;
    juce::OwnedArray<KlopButton> soloButtons;
    juce::OwnedArray<KlopButton> lockButtons;

    int selectedVoice = 0;
    int selectedStep = 0;
    int hoveredVoice = -1;
    int hoveredStep = -1;
    int lastPatternLength = 0;

    bool dragging = false;
    bool dragValue = false;
    int dragVoice = -1;
    int dragLastStep = -1;

    float titleH = 28.0f;
    float numbersH = 20.0f;
    float laneLabelW = 138.0f;
    float cellW = 22.0f;
    float laneH = 20.0f;
    float scrollOffset = 0.0f;
    float contentWidth = 0.0f;
    bool scrollNeeded = false;

    juce::Rectangle<float> titleArea;
    juce::Rectangle<float> numbersArea;
    juce::Rectangle<float> rowsArea;
    juce::Rectangle<float> labelArea;
    juce::Rectangle<float> cellsArea;

    unsigned int lastTriggerCount[kNumVoices] {};
    unsigned int lastPatternChangeCounter = 0;
    float hitPulse[kNumVoices] {};
};

} // namespace klopgeest::ui
