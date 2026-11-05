#include "ui/Editor.h"

#include "plugin/KlopgeestProcessor.h"
#include "ui/LookAndFeel.h"
#include "ui/Panels.h"
#include "ui/StepGrid.h"
#include "ui/Theme.h"

namespace klopgeest
{

using namespace ui;

namespace
{

// =============================================================================
// KLOPGEEST performance editor.
//
// One glanceable surface: transport and pattern at the top, the eight-lane
// step grid in the centre, step inspector on the right and the VOICE / GROOVE /
// GLUE tabs at the bottom. Everything is drawn, nothing is loaded.
// =============================================================================
class KlopgeestEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit KlopgeestEditor (KlopgeestProcessor& processorToUse)
        : juce::AudioProcessorEditor (&processorToUse),
          processor (processorToUse),
          topBar (processorToUse),
          grid (processorToUse),
          inspector (processorToUse),
          voicePanel (processorToUse),
          groovePanel (processorToUse),
          gluePanel (processorToUse),
          tabStrip ({ "VOICE", "GROOVE", "GLUE" })
    {
        setLookAndFeel (&lookAndFeel);
        setOpaque (true);
        setWantsKeyboardFocus (true);
        setResizable (true, true);
        setResizeLimits (1080, 620, 1920, 1200);

        // Stable ids make the offscreen layout checks (and accessibility tools)
        // able to address the primary regions.
        topBar.setComponentID ("topBar");
        grid.setComponentID ("stepGrid");
        inspector.setComponentID ("stepInspector");
        voicePanel.setComponentID ("voicePanel");
        groovePanel.setComponentID ("groovePanel");
        gluePanel.setComponentID ("gluePanel");
        tabStrip.setComponentID ("tabStrip");

        addAndMakeVisible (topBar);
        addAndMakeVisible (grid);
        addAndMakeVisible (inspector);
        addAndMakeVisible (voicePanel);
        addAndMakeVisible (groovePanel);
        addAndMakeVisible (gluePanel);
        addAndMakeVisible (tabStrip);
        addAndMakeVisible (panicButton);

        panicButton.setFontHeight (8.5f);
        panicButton.setTooltip ("All notes off");
        panicButton.onClick = [this] { processor.panic(); };

        tabStrip.onChange = [this] (int index)
        {
            activeTab = index;
            updateVisiblePanel();
        };

        grid.onSelectionChanged = [this] (int voice, int step)
        {
            selectedVoice = voice;
            selectedStep = step;
            inspector.setSelection (voice, step);
            voicePanel.setVoice (voice);
        };

        grid.onPatternEdited = [this]
        {
            inspector.refresh();
            repaint();
        };

        topBar.onPatternChanged = [this]
        {
            grid.refresh();
            inspector.refresh();
            repaint();
        };

        updateVisiblePanel();

        setSize (1180, 680);
        refreshAll();
        startTimerHz (30);
    }

    ~KlopgeestEditor() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
            return;

        g.setColour (col::bg);
        g.fillRect (bounds);

        // A breath of amber light from above and a faint industrial texture.
        juce::ColourGradient glow (col::amber.withAlpha (0.045f), bounds.getCentreX(), -80.0f,
                                   col::bg.withAlpha (0.0f), bounds.getCentreX(),
                                   bounds.getHeight() * 0.72f, true);
        g.setGradientFill (glow);
        g.fillRect (bounds);

        g.setColour (juce::Colours::white.withAlpha (0.010f));

        for (float x = 0.0f; x < bounds.getRight(); x += 64.0f)
            g.fillRect (juce::Rectangle<float> (x, bounds.getY(), 1.0f, bounds.getHeight()));

        // Bottom strip: one panel holding the tab bar and the active tab.
        if (! bottomPanelArea.isEmpty())
        {
            drawPanel (g, bottomPanelArea, col::panel, col::line, 8.0f);

            const auto lineY = (float) tabStrip.getBottom() + 3.0f;
            g.setColour (col::line.withAlpha (0.85f));
            g.fillRect (juce::Rectangle<float> (bottomPanelArea.getX() + 10.0f, lineY,
                                                bottomPanelArea.getWidth() - 20.0f, 1.0f));

            // Legend: amber for structure, cyan for motion.
            auto legend = juce::Rectangle<float> (panicButton.getX() - 320.0f,
                                                  (float) tabStrip.getY(),
                                                  300.0f, (float) tabStrip.getHeight());

            if (legend.getX() > tabStrip.getRight())
            {
                g.setFont (ty::sans (6.9f, true));
                const auto legendText = "AMBER = STRUCTURE   /   CYAN = MOTION";
                g.setColour (col::textFaint);
                g.drawText (legendText, legend, juce::Justification::centredRight, false);
            }
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().toFloat();

        if (bounds.getWidth() < 80.0f || bounds.getHeight() < 80.0f)
            return;

        auto content = bounds.reduced (12.0f);
        const auto topHeight = juce::jlimit (102.0f, 116.0f, content.getHeight() * 0.175f);
        const auto bottomHeight = juce::jlimit (200.0f, 246.0f, content.getHeight() * 0.365f);
        const auto rightWidth = juce::jlimit (286.0f, 332.0f, content.getWidth() * 0.27f);

        topBar.setBounds (content.removeFromTop (topHeight).toNearestInt());
        content.removeFromTop (8.0f);

        bottomPanelArea = content.removeFromBottom (bottomHeight);
        content.removeFromBottom (8.0f);

        auto right = content.removeFromRight (rightWidth);
        content.removeFromRight (8.0f);

        grid.setBounds (content.toNearestInt());
        inspector.setBounds (right.toNearestInt());

        auto inner = bottomPanelArea.reduced (10.0f, 8.0f);

        auto tabRow = inner.removeFromTop (juce::jmin (26.0f, inner.getHeight() * 0.22f));
        tabStrip.setBounds (tabRow.removeFromLeft (juce::jmin (340.0f, tabRow.getWidth() * 0.4f))
                                .toNearestInt());
        panicButton.setBounds (tabRow.removeFromRight (68.0f)
                                   .withSizeKeepingCentre (68.0f, 22.0f)
                                   .toNearestInt());

        inner.removeFromTop (8.0f);

        const auto panelBounds = inner.toNearestInt();
        voicePanel.setBounds (panelBounds);
        groovePanel.setBounds (panelBounds);
        gluePanel.setBounds (panelBounds);
    }

    void timerCallback() override
    {
        topBar.refresh();
        grid.refresh();

        const auto voice = grid.getSelectedVoice();
        const auto step = grid.getSelectedStep();

        if (voice != selectedVoice)
            voicePanel.setVoice (voice);

        selectedVoice = voice;
        selectedStep = step;

        inspector.setSelection (voice, step);
        inspector.refresh();
        voicePanel.refresh();
        groovePanel.refresh();
        gluePanel.refresh();
    }

private:
    void updateVisiblePanel()
    {
        voicePanel.setVisible (activeTab == 0);
        groovePanel.setVisible (activeTab == 1);
        gluePanel.setVisible (activeTab == 2);
        repaint();
    }

    void refreshAll()
    {
        topBar.refresh();
        grid.refresh();
        inspector.setSelection (grid.getSelectedVoice(), grid.getSelectedStep());
        inspector.refresh();
        voicePanel.setVoice (grid.getSelectedVoice());
        voicePanel.refresh();
        groovePanel.refresh();
        gluePanel.refresh();
        repaint();
    }

    KlopgeestProcessor& processor;

    KlopgeestLookAndFeel lookAndFeel;

    TopBar topBar;
    StepGrid grid;
    StepInspector inspector;
    VoicePanel voicePanel;
    GroovePanel groovePanel;
    GluePanel gluePanel;
    TabStrip tabStrip;
    KlopButton panicButton { "PANIC", KlopButton::Variant::red };

    int activeTab = 0;
    int selectedVoice = 0;
    int selectedStep = 0;
    juce::Rectangle<float> bottomPanelArea;
};

} // namespace

juce::AudioProcessorEditor* createKlopgeestEditor (KlopgeestProcessor& processor)
{
    return new KlopgeestEditor (processor);
}

} // namespace klopgeest
