#include "plugin/KlopgeestProcessor.h"

#include <cmath>
#include <memory>

#include "ui/Editor.h"

namespace klopgeest
{

namespace
{
constexpr double kMaxHostJumpPpq = 0.75;

float loadOrDefault (const std::atomic<float>* pointer, float fallback)
{
    return pointer != nullptr ? pointer->load (std::memory_order_relaxed) : fallback;
}
} // namespace

KlopgeestProcessor::KlopgeestProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, kProductName, createParameterLayout())
{
    cacheParameterPointers();

    // Start on the first factory pattern so the host program list, the pattern
    // selector and the audible groove all agree from the first frame.
    loadFactoryPattern (0);

    startTimerHz (20);
}

KlopgeestProcessor::~KlopgeestProcessor()
{
    stopTimer();
}

void KlopgeestProcessor::cacheParameterPointers()
{
    for (int v = 0; v < kNumVoices; ++v)
        for (int p = 0; p < kNumVoiceParams; ++p)
            pointers.voice[(size_t) v][(size_t) p] =
                apvts.getRawParameterValue (pid::voiceId (v, detail::kVoiceParamSuffix[p]));

    pointers.tempo        = apvts.getRawParameterValue (pid::tempo);
    pointers.swing        = apvts.getRawParameterValue (pid::swing);
    pointers.humanize     = apvts.getRawParameterValue (pid::humanize);
    pointers.stability    = apvts.getRawParameterValue (pid::stability);
    pointers.personality  = apvts.getRawParameterValue (pid::personality);
    pointers.ghost        = apvts.getRawParameterValue (pid::ghost);
    pointers.accent       = apvts.getRawParameterValue (pid::accent);
    pointers.seed         = apvts.getRawParameterValue (pid::seed);
    pointers.mutateAmount = apvts.getRawParameterValue (pid::mutateAmount);
    pointers.evolve       = apvts.getRawParameterValue (pid::evolve);

    pointers.threshold   = apvts.getRawParameterValue (pid::threshold);
    pointers.ratio       = apvts.getRawParameterValue (pid::ratio);
    pointers.attack      = apvts.getRawParameterValue (pid::attack);
    pointers.release     = apvts.getRawParameterValue (pid::release);
    pointers.knee        = apvts.getRawParameterValue (pid::knee);
    pointers.autoRelease = apvts.getRawParameterValue (pid::autoRelease);
    pointers.sidechainHz = apvts.getRawParameterValue (pid::sidechainHz);
    pointers.detect      = apvts.getRawParameterValue (pid::detect);
    pointers.mix         = apvts.getRawParameterValue (pid::mix);
    pointers.satPre      = apvts.getRawParameterValue (pid::satPre);
    pointers.satPost     = apvts.getRawParameterValue (pid::satPost);
    pointers.output      = apvts.getRawParameterValue (pid::output);
    pointers.ceiling     = apvts.getRawParameterValue (pid::ceiling);
    pointers.glue        = apvts.getRawParameterValue (pid::glue);
    pointers.heat        = apvts.getRawParameterValue (pid::heat);
    pointers.limiter     = apvts.getRawParameterValue (pid::limiter);
}

void KlopgeestProcessor::readParams (EngineParams& out) const noexcept
{
    auto load = [this] (std::atomic<float>* p, float fallback) { return loadOrDefault (p, fallback); };

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto& ptr = pointers.voice[(size_t) v];
        auto& dst = out.voice[(size_t) v];

        dst.pitchSemis     = loadOrDefault (ptr[(size_t) VPitch], 0.0f);
        dst.decay          = loadOrDefault (ptr[(size_t) VDecay], 0.5f);
        dst.tone           = loadOrDefault (ptr[(size_t) VTone], 0.5f);
        dst.transient      = loadOrDefault (ptr[(size_t) VTransient], 0.5f);
        dst.drive          = loadOrDefault (ptr[(size_t) VDrive], 0.15f);
        dst.levelDb        = loadOrDefault (ptr[(size_t) VLevel], -6.0f);
        dst.pan            = loadOrDefault (ptr[(size_t) VPan], 0.0f);
        dst.specialA       = loadOrDefault (ptr[(size_t) VSpecialA], 0.5f);
        dst.specialB       = loadOrDefault (ptr[(size_t) VSpecialB], 0.5f);
        dst.randPitch      = loadOrDefault (ptr[(size_t) VRandPitch], 0.15f);
        dst.randLevel      = loadOrDefault (ptr[(size_t) VRandLevel], 0.30f);
        dst.randDecay      = loadOrDefault (ptr[(size_t) VRandDecay], 0.20f);
        dst.randTransient  = loadOrDefault (ptr[(size_t) VRandTransient], 0.20f);
        dst.randTiming     = loadOrDefault (ptr[(size_t) VRandTiming], 0.30f);
        dst.timingBiasMs   = loadOrDefault (ptr[(size_t) VBias], 0.0f);
    }

    out.global.tempo        = load (pointers.tempo, 120.0f);
    out.global.swing        = load (pointers.swing, 0.12f);
    out.global.humanize     = load (pointers.humanize, 0.35f);
    out.global.stability    = load (pointers.stability, 0.75f);
    out.global.personality  = juce::jlimit (0, 2, (int) std::lround (load (pointers.personality, 1.0f)));
    out.global.ghost        = load (pointers.ghost, 0.15f);
    out.global.accent       = load (pointers.accent, 0.35f);
    out.global.seed         = juce::jlimit (0, 999999, (int) std::lround (load (pointers.seed, 12345.0f)));
    out.global.mutateAmount = load (pointers.mutateAmount, 0.35f);
    out.global.evolve       = load (pointers.evolve, 0.0f) > 0.5f;

    out.master.thresholdDb = load (pointers.threshold, -18.0f);
    out.master.ratio       = load (pointers.ratio, 4.0f);
    out.master.attackMs    = load (pointers.attack, 8.0f);
    out.master.releaseMs   = load (pointers.release, 180.0f);
    out.master.kneeDb      = load (pointers.knee, 8.0f);
    out.master.autoRelease = load (pointers.autoRelease, 1.0f) > 0.5f;
    out.master.sidechainHz = load (pointers.sidechainHz, 90.0f);
    out.master.detect      = load (pointers.detect, 0.5f);
    out.master.mix         = load (pointers.mix, 1.0f);
    out.master.satPre      = load (pointers.satPre, 0.15f);
    out.master.satPost     = load (pointers.satPost, 0.10f);
    out.master.outputDb    = load (pointers.output, 0.0f);
    out.master.ceilingDb   = load (pointers.ceiling, -0.4f);
    out.master.glue        = load (pointers.glue, 0.35f);
    out.master.heat        = load (pointers.heat, 0.25f);
    out.master.limiter     = load (pointers.limiter, 1.0f) > 0.5f;
}

void KlopgeestProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    audioPattern = editPattern;
    prepared = true;
    lastEvolveBar = -1;
}

void KlopgeestProcessor::releaseResources()
{
    prepared = false;
}

void KlopgeestProcessor::reset()
{
    engine.reset();
    lastEvolveBar = -1;
}

void KlopgeestProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    if (panicRequested.exchange (false, std::memory_order_relaxed))
        engine.reset();

    if (patternQueue.pullLatest (audioPattern))
        visual.patternChangeCounter.fetch_add (1u, std::memory_order_relaxed);

    EngineParams params;
    readParams (params);
    engine.setParams (params);
    engine.setRunning (playing.load (std::memory_order_relaxed));

    HostTransport host;
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            host.available = true;
            host.playing = position->getIsPlaying();
            if (auto bpm = position->getBpm())
                host.bpm = juce::jlimit (20.0, 999.0, *bpm);
            if (auto ppq = position->getPpqPosition())
                host.ppqPosition = *ppq;
        }
    }

    if (! host.available || host.bpm <= 0.0)
        host.bpm = params.global.tempo;

    engine.process (buffer, midi, numSamples, audioPattern, host, visual);

    // Keep free-run tempo honest for the GUI when the host has no clock.
    if (! host.playing)
        visual.bpm.store (params.global.tempo, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* KlopgeestProcessor::createEditor()
{
    return createKlopgeestEditor (*this);
}

bool KlopgeestProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    const auto input = layouts.getMainInputChannelSet();
    return input.isDisabled() || input == output;
}

// -----------------------------------------------------------------------------
// Programs: the factory rhythms double as program starting points.
// -----------------------------------------------------------------------------
int KlopgeestProcessor::getNumPrograms()
{
    return juce::jmax (1, factoryPatternCount());
}

int KlopgeestProcessor::getCurrentProgram()
{
    return juce::jlimit (0, getNumPrograms() - 1, currentPatternIndex);
}

void KlopgeestProcessor::setCurrentProgram (int index)
{
    loadFactoryPattern (index);
}

const juce::String KlopgeestProcessor::getProgramName (int index)
{
    return factoryPatternName (index);
}

void KlopgeestProcessor::changeProgramName (int, const juce::String&)
{
    // Factory programs are fixed; user presets live in host sessions.
}

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------
void KlopgeestProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // copyState() (not createCopy()) flushes the parameter flush timer first, so
    // a save immediately after a parameter change cannot serialise stale values.
    auto parameterState = apvts.copyState();

    juce::ValueTree root ("KLOPGEEST_STATE");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("patternIndex", currentPatternIndex, nullptr);
    root.appendChild (parameterState, nullptr);
    root.appendChild (editPattern.toValueTree(), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void KlopgeestProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("KLOPGEEST_STATE"))
        return;

    if (auto parameterState = root.getChildWithName (kProductName); parameterState.isValid())
        apvts.replaceState (parameterState.createCopy());

    const auto patternState = root.getChildWithName ("PATTERN");
    if (patternState.isValid())
    {
        editPattern = PatternData::fromValueTree (patternState);
        publishPattern (editPattern);
    }

    currentPatternIndex = (int) root.getProperty ("patternIndex", 0);
    mutation.reset();
    lastEvolveBar = -1;
}

// -----------------------------------------------------------------------------
// Pattern / performance actions (message thread)
// -----------------------------------------------------------------------------
void KlopgeestProcessor::publishPattern (const PatternData& newPattern)
{
    editPattern = newPattern;
    editPattern.sanitise();

    pendingPattern = editPattern;
    pendingPublish.store (true, std::memory_order_relaxed);
    if (patternQueue.push (editPattern))
        pendingPublish.store (false, std::memory_order_relaxed);

    visual.patternChangeCounter.fetch_add (1u, std::memory_order_relaxed);
}

void KlopgeestProcessor::publishPendingPattern()
{
    if (pendingPublish.exchange (false, std::memory_order_relaxed))
    {
        if (! patternQueue.push (pendingPattern))
            pendingPublish.store (true, std::memory_order_relaxed);
    }
}

void KlopgeestProcessor::loadFactoryPattern (int index)
{
    const auto clamped = juce::jlimit (0, factoryPatternCount() - 1, index);
    editPattern = factoryPattern (clamped);
    currentPatternIndex = clamped;
    mutation.reset();
    publishPattern (editPattern);
}

void KlopgeestProcessor::setPlaying (bool shouldPlay)
{
    playing.store (shouldPlay, std::memory_order_relaxed);
}

void KlopgeestProcessor::nudge()
{
    const auto seed = (std::uint32_t) getSeed() * 2654435761u + (mutationCounter++) * 0x9e3779b9u;
    auto edited = editPattern;
    if (mutation.nudge (edited, getMutateAmount(), seed))
    {
        publishPattern (edited);
        visual.mutationCounter.fetch_add (1u, std::memory_order_relaxed);
    }
}

void KlopgeestProcessor::mutate()
{
    const auto seed = (std::uint32_t) getSeed() * 0x85ebca6bu + (mutationCounter++) * 0xc2b2ae35u;
    auto edited = editPattern;
    if (mutation.mutate (edited, getMutateAmount(), seed))
    {
        publishPattern (edited);
        visual.mutationCounter.fetch_add (1u, std::memory_order_relaxed);
    }
}

void KlopgeestProcessor::recall()
{
    auto edited = editPattern;
    if (mutation.recall (edited))
    {
        publishPattern (edited);
        visual.mutationCounter.fetch_add (1u, std::memory_order_relaxed);
    }
}

juce::String KlopgeestProcessor::getPatternDisplayName() const
{
    return editPattern.getName();
}

float KlopgeestProcessor::getMutateAmount() const
{
    return loadOrDefault (pointers.mutateAmount, 0.35f);
}

int KlopgeestProcessor::getSeed() const
{
    return (int) std::lround (loadOrDefault (pointers.seed, 12345.0f));
}

// -----------------------------------------------------------------------------
// EVOLVE + publish retry
// -----------------------------------------------------------------------------
void KlopgeestProcessor::timerCallback()
{
    publishPendingPattern();

    if (! prepared)
        return;

    if (loadOrDefault (pointers.evolve, 0.0f) < 0.5f)
        return;

    if (! visual.playing.load (std::memory_order_relaxed))
        return;

    const auto tick = visual.currentTick.load (std::memory_order_relaxed);
    if (tick < 0)
        return;

    const auto bar = tick / kStepsPerBar;
    if (bar == lastEvolveBar)
        return;
    lastEvolveBar = bar;

    if (bar <= 0 || (bar % 2) != 0)
        return;

    const auto seed = (std::uint32_t) getSeed() * 0x9e3779b1u
                      + (std::uint32_t) (tick / kStepsPerBar) * 0x85ebca6bu
                      + (mutationCounter++) * 0x27d4eb2fu;
    auto edited = editPattern;
    if (mutation.nudge (edited, getMutateAmount() * 0.5f, seed))
    {
        publishPattern (edited);
        visual.mutationCounter.fetch_add (1u, std::memory_order_relaxed);
    }
}

} // namespace klopgeest

// -----------------------------------------------------------------------------
// Plugin entry point.
// -----------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new klopgeest::KlopgeestProcessor();
}
