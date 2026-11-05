#pragma once

#include <memory>

#include "dsp/voices/Voice.h"

// -----------------------------------------------------------------------------
// Voice constructors, one per engine. Kept out of Voice.h so the frozen contract
// does not need to know about the concrete voice classes.
// -----------------------------------------------------------------------------
namespace klopgeest
{

std::unique_ptr<DrumVoice> createKickVoice();
std::unique_ptr<DrumVoice> createSnareVoice();
std::unique_ptr<DrumVoice> createClapVoice();
std::unique_ptr<DrumVoice> createClosedHatVoice();
std::unique_ptr<DrumVoice> createOpenHatVoice();
std::unique_ptr<DrumVoice> createTomVoice();
std::unique_ptr<DrumVoice> createPercVoice();
std::unique_ptr<DrumVoice> createOddVoice();

} // namespace klopgeest
