#include "dsp/voices/Voice.h"
#include "dsp/voices/VoiceConstructors.h"

#include <memory>

namespace klopgeest
{

std::unique_ptr<DrumVoice> createVoice (int voiceIndex)
{
    switch (voiceIndex)
    {
        case VoiceKick:      return createKickVoice();
        case VoiceSnare:     return createSnareVoice();
        case VoiceClap:      return createClapVoice();
        case VoiceClosedHat: return createClosedHatVoice();
        case VoiceOpenHat:   return createOpenHatVoice();
        case VoiceTom:       return createTomVoice();
        case VoicePerc:      return createPercVoice();
        case VoiceOdd:       return createOddVoice();
        default:             return createOddVoice();
    }
}

} // namespace klopgeest
