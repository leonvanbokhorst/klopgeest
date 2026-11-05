#include "dsp/voices/VoiceManager.h"

namespace klopgeest
{

VoiceManager::VoiceManager()
{
    for (int v = 0; v < kNumVoices; ++v)
        voices[(size_t) v] = createVoice (v);
}

VoiceManager::~VoiceManager() = default;

void VoiceManager::prepare (double sampleRate, int maxBlockSize)
{
    for (auto& voice : voices)
        if (voice != nullptr)
            voice->prepare (sampleRate, maxBlockSize);
    prepared = true;
}

void VoiceManager::reset()
{
    for (auto& voice : voices)
        if (voice != nullptr)
            voice->reset();
}

void VoiceManager::setParams (const EngineParams& params)
{
    for (int v = 0; v < kNumVoices; ++v)
        if (voices[(size_t) v] != nullptr)
            voices[(size_t) v]->setParams (params.voice[(size_t) v]);
}

void VoiceManager::trigger (int voiceIndex, const VoiceTrigger& trigger)
{
    if (voiceIndex < 0 || voiceIndex >= kNumVoices)
        return;

    if (auto* voice = voices[(size_t) voiceIndex].get())
        voice->trigger (trigger);

    // Classic hat choke: a closed hat silences a ringing open hat.
    if (voiceIndex == VoiceClosedHat)
        if (auto* openHat = voices[(size_t) VoiceOpenHat].get())
            openHat->choke (0.015f);
}

void VoiceManager::render (juce::AudioBuffer<float>& out, int startSample, int numSamples)
{
    for (auto& voice : voices)
        if (voice != nullptr && voice->isActive())
            voice->render (out, startSample, numSamples);
}

float VoiceManager::getEnvelopeLevel (int voiceIndex) const
{
    if (voiceIndex < 0 || voiceIndex >= kNumVoices)
        return 0.0f;
    const auto& voice = voices[(size_t) voiceIndex];
    return voice != nullptr ? voice->getEnvelopeLevel() : 0.0f;
}

bool VoiceManager::isActive (int voiceIndex) const
{
    if (voiceIndex < 0 || voiceIndex >= kNumVoices)
        return false;
    const auto& voice = voices[(size_t) voiceIndex];
    return voice != nullptr && voice->isActive();
}

} // namespace klopgeest
