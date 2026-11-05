#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cstdint>

#include "core/Parameters.h"
#include "core/Pattern.h"
#include "core/Patterns.h"
#include "core/VisualState.h"
#include "dsp/Engine.h"
#include "sequencer/MutationEngine.h"

// -----------------------------------------------------------------------------
// KLOPGEEST plug-in glue: APVTS, host transport, pattern handoff, state,
// program list, live performance actions.
//
// All pattern editing and mutation happens on the message thread; the audio
// thread only ever reads immutable snapshots pulled from a lock-free queue.
// -----------------------------------------------------------------------------
namespace klopgeest
{

class KlopgeestProcessor final : public juce::AudioProcessor,
                                 private juce::Timer
{
public:
    KlopgeestProcessor();
    ~KlopgeestProcessor() override;

    // --- AudioProcessor ------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return kProductName; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // --- Access --------------------------------------------------------------
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    const juce::AudioProcessorValueTreeState& getAPVTS() const noexcept { return apvts; }
    Engine& getEngine() noexcept { return engine; }
    VisualState& getVisualState() noexcept { return visual; }

    /** Message-thread view of the pattern being edited. */
    const PatternData& getPattern() const noexcept { return editPattern; }

    /** Publishes an edited pattern to the audio thread (lock-free). */
    void publishPattern (const PatternData& newPattern);

    void loadFactoryPattern (int index);
    int getFactoryPatternIndex() const noexcept { return currentPatternIndex; }
    juce::StringArray getFactoryPatternNames() const { return factoryPatternNames(); }

    // --- Transport / performance --------------------------------------------
    void setPlaying (bool shouldPlay);
    bool isPlaying() const noexcept { return playing.load (std::memory_order_relaxed); }
    void panic() noexcept { panicRequested.store (true, std::memory_order_relaxed); }

    void nudge();
    void mutate();
    void recall();
    bool canRecall() const noexcept { return mutation.canRecall(); }
    juce::String getPatternDisplayName() const;

    float getMutateAmount() const;
    int   getSeed() const;

private:
    void timerCallback() override;
    void cacheParameterPointers();
    void readParams (EngineParams& out) const noexcept;
    void applyProgram (int index);
    void publishPendingPattern();

    struct ParamPointers
    {
        std::array<std::array<std::atomic<float>*, kNumVoiceParams>, kNumVoices> voice {};
        std::atomic<float>* tempo = nullptr;
        std::atomic<float>* swing = nullptr;
        std::atomic<float>* humanize = nullptr;
        std::atomic<float>* stability = nullptr;
        std::atomic<float>* personality = nullptr;
        std::atomic<float>* ghost = nullptr;
        std::atomic<float>* accent = nullptr;
        std::atomic<float>* seed = nullptr;
        std::atomic<float>* mutateAmount = nullptr;
        std::atomic<float>* evolve = nullptr;

        std::atomic<float>* threshold = nullptr;
        std::atomic<float>* ratio = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* knee = nullptr;
        std::atomic<float>* autoRelease = nullptr;
        std::atomic<float>* sidechainHz = nullptr;
        std::atomic<float>* detect = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* satPre = nullptr;
        std::atomic<float>* satPost = nullptr;
        std::atomic<float>* output = nullptr;
        std::atomic<float>* ceiling = nullptr;
        std::atomic<float>* glue = nullptr;
        std::atomic<float>* heat = nullptr;
        std::atomic<float>* limiter = nullptr;
    };

    juce::AudioProcessorValueTreeState apvts;
    ParamPointers pointers;
    Engine engine;
    VisualState visual;

    PatternData editPattern;       // message thread
    PatternData audioPattern;      // audio thread only
    PatternQueue patternQueue;
    MutationEngine mutation;

    std::atomic<bool> playing { false };
    std::atomic<bool> panicRequested { false };
    std::atomic<bool> pendingPublish { false };
    PatternData pendingPattern;    // message thread
    int currentPatternIndex = 0;
    bool prepared = false;
    long long lastEvolveBar = -1;
    std::uint32_t mutationCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KlopgeestProcessor)
};

} // namespace klopgeest
