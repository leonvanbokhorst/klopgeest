// -----------------------------------------------------------------------------
// Temporary transparent glue machine. Replaced by the master dynamics
// workstream; keeps the build playable while the compressor is written.
// -----------------------------------------------------------------------------
#include "dsp/master/GlueMachine.h"

#include <cmath>

namespace klopgeest
{

struct GlueMachine::Impl
{
    double sampleRate = 48000.0;
    MasterParams params;
    float peakDb = -100.0f;
    float rmsDb = -100.0f;

    void reset() { peakDb = -100.0f; rmsDb = -100.0f; }

    void process (juce::AudioBuffer<float>& buffer, int numSamples)
    {
        const int channels = juce::jmin (2, buffer.getNumChannels());
        float peak = 0.0f;
        double sum = 0.0;

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                peak = juce::jmax (peak, std::abs (data[i]));
                sum += (double) data[i] * data[i];
            }
        }

        peakDb = peak > 1.0e-7f ? 20.0f * std::log10 (peak) : -100.0f;
        const auto rms = std::sqrt (sum / (double) juce::jmax (1, numSamples * juce::jmax (1, channels)));
        rmsDb = rms > 1.0e-7 ? (float) (20.0 * std::log10 (rms)) : -100.0f;
    }
};

GlueMachine::GlueMachine() : impl (std::make_unique<Impl>()) {}
GlueMachine::~GlueMachine() = default;

void GlueMachine::prepare (double sampleRate, int)
{
    impl->sampleRate = sampleRate;
    reset();
}

void GlueMachine::reset()
{
    impl->reset();
}

void GlueMachine::setParams (const MasterParams& params)
{
    impl->params = params;
}

void GlueMachine::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    impl->process (buffer, numSamples);
}

float GlueMachine::getGainReductionDb() const noexcept { return 0.0f; }
float GlueMachine::getOutputPeakDb() const noexcept { return impl->peakDb; }
float GlueMachine::getOutputRmsDb() const noexcept { return impl->rmsDb; }

} // namespace klopgeest
