// -----------------------------------------------------------------------------
// Headless tool: tests, offline demo render, offscreen editor snapshot.
//
//   klop-tool test
//   klop-tool render [output.wav]
//   klop-tool snapshot [output.png]
// -----------------------------------------------------------------------------
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>
#include <memory>

#include "core/ParamIDs.h"
#include "core/Patterns.h"
#include "plugin/KlopgeestProcessor.h"
#include "tests/AllTests.h"

namespace
{

void setParam (klopgeest::KlopgeestProcessor& processor, const char* id, float realValue)
{
    if (auto* parameter = processor.getAPVTS().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (realValue));
}

int renderDemo (const juce::File& outputFile)
{
    using namespace klopgeest;

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr double durationSeconds = 48.0;

    KlopgeestProcessor processor;
    processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);
    processor.setPlaying (true);

    // A 48 s performance that builds: each section adds energy, and the last
    // two sections get a NUDGE / MUTATE fill so the demo ends on a high rather
    // than fading out. The ceiling is set below -1 dBFS so the render can be
    // loud without ever clipping (validation requires peak <= -1 dBFS and zero
    // full-scale samples).
    setParam (processor, pid::seed, 4242.0f);
    setParam (processor, pid::swing, 0.16f);
    setParam (processor, pid::humanize, 0.38f);
    setParam (processor, pid::stability, 0.66f);
    setParam (processor, pid::threshold, -16.0f);
    setParam (processor, pid::mix, 0.90f);
    setParam (processor, pid::ceiling, -1.2f);
    processor.loadFactoryPattern (0);

    outputFile.getParentDirectory().createDirectory();
    outputFile.deleteFile();

    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = outputFile.createOutputStream();
    if (stream == nullptr)
    {
        std::fprintf (stderr, "could not open %s\n", outputFile.getFullPathName().toRawUTF8());
        return 1;
    }

    auto options = juce::AudioFormatWriterOptions()
                       .withSampleRate (sampleRate)
                       .withNumChannels (2)
                       .withBitsPerSample (24);
    auto writer = format.createWriterFor (stream, options);
    if (writer == nullptr)
    {
        std::fprintf (stderr, "could not create wav writer\n");
        return 1;
    }

    enum class Action { none, nudge, mutate };

    struct Section
    {
        int pattern;
        double start;
        int personality;
        float glue;
        float heat;
        float outputDb;
        Action action;
    };

    const Section sections[] =
    {
        // pattern, start, personality, glue, heat, output, action
        { 0,  0.0, 1, 0.30f, 0.20f, 0.0f, Action::none },   // STRAIGHT FIRE: state the pulse
        { 3, 12.0, 1, 0.42f, 0.28f, 1.5f, Action::none },   // WAREHOUSE: wider, louder
        { 1, 24.0, 2, 0.52f, 0.38f, 2.5f, Action::mutate }, // BROKEN STEP: FERAL, mutate lift
        { 4, 36.0, 2, 0.58f, 0.46f, 4.0f, Action::nudge },  // FERAL DUB: nudge fill
        { 5, 42.0, 2, 0.60f, 0.48f, 3.5f, Action::mutate }  // SIXTEENTH ANXIETY: end strong
    };

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    const int totalBlocks = (int) (durationSeconds * sampleRate / blockSize);
    double peak = 0.0;
    int fullScaleSamples = 0;
    bool finite = true;
    int nextSection = 0;

    for (int block = 0; block < totalBlocks; ++block)
    {
        const double time = block * blockSize / sampleRate;
        while (nextSection < (int) (sizeof (sections) / sizeof (sections[0]))
               && time >= sections[nextSection].start)
        {
            const auto& section = sections[nextSection];
            processor.loadFactoryPattern (section.pattern);
            setParam (processor, pid::personality, (float) section.personality);
            setParam (processor, pid::glue, section.glue);
            setParam (processor, pid::heat, section.heat);
            setParam (processor, pid::output, section.outputDb);

            if (section.action == Action::nudge)
                processor.nudge();
            else if (section.action == Action::mutate)
                processor.mutate();

            ++nextSection;
        }

        processor.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            for (int i = 0; i < blockSize; ++i)
            {
                const float x = data[i];
                finite = finite && std::isfinite (x);
                peak = juce::jmax (peak, (double) std::abs (x));
                if (std::abs (x) >= 0.9999f)
                    ++fullScaleSamples;
            }
        }

        writer->writeFromAudioSampleBuffer (buffer, 0, blockSize);

        if (block == 0)
            std::printf ("rendering %s ...\n", outputFile.getFullPathName().toRawUTF8());
    }

    writer.reset();
    stream.reset();

    const float peakDb = peak > 1.0e-7 ? (float) (20.0 * std::log10 (peak)) : -100.0f;
    std::printf ("bars: %d  peak: %.2f dBFS  full-scale samples: %d  finite: %s\n",
                 (int) (durationSeconds / 2.0), peakDb, fullScaleSamples, finite ? "yes" : "NO");
    return finite ? 0 : 1;
}

int snapshot (const juce::File& outputFile)
{
    using namespace klopgeest;

    KlopgeestProcessor processor;
    processor.setPlayConfigDetails (0, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
    {
        std::fprintf (stderr, "could not create editor\n");
        return 1;
    }

    editor->setSize (1180, 680);
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true);
    if (! image.isValid())
    {
        std::fprintf (stderr, "could not render editor snapshot\n");
        return 1;
    }

    outputFile.getParentDirectory().createDirectory();
    outputFile.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = outputFile.createOutputStream();
    if (stream == nullptr)
    {
        std::fprintf (stderr, "could not open %s\n", outputFile.getFullPathName().toRawUTF8());
        return 1;
    }

    juce::PNGImageFormat png;
    if (! png.writeImageToStream (image, *stream))
    {
        std::fprintf (stderr, "could not encode png\n");
        return 1;
    }

    std::printf ("wrote %s (%d x %d)\n", outputFile.getFullPathName().toRawUTF8(),
                 image.getWidth(), image.getHeight());
    return 0;
}

} // namespace

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String mode = argc > 1 ? juce::String (argv[1]) : juce::String ("test");

    if (mode == "render")
    {
        const juce::File output = argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])
                                          : juce::File::getCurrentWorkingDirectory().getChildFile ("Demo/Klopgeest-Demo.wav");
        return renderDemo (output);
    }

    if (mode == "snapshot")
    {
        const juce::File output = argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])
                                          : juce::File::getCurrentWorkingDirectory().getChildFile ("docs/ui-preview.png");
        return snapshot (output);
    }

    return runAllTests();
}
