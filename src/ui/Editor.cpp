// -----------------------------------------------------------------------------
// Temporary editor. Replaced by the UI workstream (theme, step grid, voice
// strips, groove and glue panels). It exists so the plug-in is openable and the
// offscreen snapshot pipeline can be validated from day one.
// -----------------------------------------------------------------------------
#include "ui/Editor.h"

#include "plugin/KlopgeestProcessor.h"

namespace klopgeest
{

namespace
{
class PlaceholderEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit PlaceholderEditor (KlopgeestProcessor& p)
        : juce::AudioProcessorEditor (&p), processor (p)
    {
        setSize (1180, 680);
        setResizable (true, true);
        startTimerHz (30);
    }

    ~PlaceholderEditor() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101113));

        auto bounds = getLocalBounds().toFloat().reduced (16.0f);
        auto header = bounds.removeFromTop (56.0f);

        g.setColour (juce::Colour (0xffe8a33d));
        g.setFont (juce::Font (juce::FontOptions (26.0f).withStyle ("Bold")));
        g.drawText ("KLOPGEEST", header.removeFromLeft (240.0f), juce::Justification::centredLeft);

        g.setColour (juce::Colour (0xff6fd7e0));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (processor.getPatternDisplayName() + "   //   placeholder editor",
                    header, juce::Justification::centredRight);

        bounds.removeFromTop (12.0f);

        const auto& pattern = processor.getPattern();
        const auto& visual = processor.getVisualState();
        const int steps = pattern.length;
        const int rows = kNumVoices;
        const float rowHeight = bounds.getHeight() / (float) rows;
        const float labelWidth = 110.0f;
        const float cellWidth = (bounds.getWidth() - labelWidth) / (float) steps;

        for (int v = 0; v < rows; ++v)
        {
            auto row = juce::Rectangle<float> (bounds.getX(), bounds.getY() + rowHeight * (float) v,
                                               bounds.getWidth(), rowHeight);

            g.setColour (juce::Colour (0xff17181b));
            g.fillRect (row.reduced (0.0f, 2.0f));

            g.setColour (juce::Colour (0xffc9cdd1));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawText (voiceName (v), row.removeFromLeft (labelWidth).reduced (8.0f, 0.0f),
                        juce::Justification::centredLeft);

            const int laneStep = visual.laneStep[v].load();
            for (int s = 0; s < steps; ++s)
            {
                auto cell = juce::Rectangle<float> (row.getX() + cellWidth * (float) s,
                                                    row.getY(), cellWidth, row.getHeight())
                                .reduced (2.0f);

                const auto& step = pattern.step (v, s);
                const bool current = laneStep == s;

                g.setColour (step.on ? juce::Colour (0xffffb347).withAlpha (0.35f + 0.55f * step.velocity)
                                     : juce::Colour (0xff1e2024));
                g.fillRoundedRectangle (cell, 3.0f);

                if (current)
                {
                    g.setColour (juce::Colour (0xff6fd7e0));
                    g.drawRoundedRectangle (cell, 3.0f, 1.6f);
                }
            }
        }
    }

private:
    void timerCallback() override { repaint(); }

    KlopgeestProcessor& processor;
};
} // namespace

juce::AudioProcessorEditor* createKlopgeestEditor (KlopgeestProcessor& processor)
{
    return new PlaceholderEditor (processor);
}

} // namespace klopgeest
