#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace klopgeest
{
class KlopgeestProcessor;
}

namespace klopgeest
{
/** Creates the editor. Implemented by the UI workstream (src/ui/Editor.cpp). */
juce::AudioProcessorEditor* createKlopgeestEditor (KlopgeestProcessor& processor);
} // namespace klopgeest
