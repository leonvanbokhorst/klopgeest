#pragma once

#include <juce_core/juce_core.h>

#include "core/Pattern.h"

// -----------------------------------------------------------------------------
// Factory patterns.
//
// Each pattern is a different rhythmic personality, not a different sound: the
// instrument's character comes from the voice engine, the groove engine and the
// glue machine. Names are shown in the GUI.
// -----------------------------------------------------------------------------
namespace klopgeest
{

PatternData makeInitPattern();

int factoryPatternCount();
juce::StringArray factoryPatternNames();
PatternData factoryPattern (int index);
juce::String factoryPatternName (int index);

} // namespace klopgeest
