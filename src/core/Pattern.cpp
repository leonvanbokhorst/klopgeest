#include "core/Pattern.h"

namespace klopgeest
{

void PatternData::setName (const juce::String& newName) noexcept
{
    name.fill ('\0');
    const auto utf8 = newName.toRawUTF8();
    std::strncpy (name.data(), utf8, name.size() - 1);
}

juce::String PatternData::getName() const
{
    return juce::String::fromUTF8 (name.data());
}

void PatternData::sanitise() noexcept
{
    length = juce::jlimit (kMinSteps, kMaxSteps, length);

    for (int v = 0; v < kNumVoices; ++v)
    {
        auto& lane = lanes[(size_t) v];
        lane.length = juce::jlimit (1, length, lane.length);

        for (int s = 0; s < kMaxSteps; ++s)
        {
            auto& st = lane.steps[(size_t) s];
            st.velocity    = juce::jlimit (0.02f, 1.0f, st.velocity);
            st.probability = juce::jlimit (0.0f, 1.0f, st.probability);
            st.microtiming = juce::jlimit (-0.5f, 0.5f, st.microtiming);
            st.pitchSemis  = juce::jlimit (-12.0f, 12.0f, st.pitchSemis);
            st.ratchet     = juce::jlimit (1, 4, st.ratchet);
        }
    }
}

Step& PatternData::step (int voice, int index) noexcept
{
    return lanes[(size_t) juce::jlimit (0, kNumVoices - 1, voice)]
        .steps[(size_t) juce::jlimit (0, kMaxSteps - 1, index)];
}

const Step& PatternData::step (int voice, int index) const noexcept
{
    return lanes[(size_t) juce::jlimit (0, kNumVoices - 1, voice)]
        .steps[(size_t) juce::jlimit (0, kMaxSteps - 1, index)];
}

int PatternData::laneLength (int voice) const noexcept
{
    return juce::jlimit (1, kMaxSteps, lanes[(size_t) juce::jlimit (0, kNumVoices - 1, voice)].length);
}

bool PatternData::anySoloed() const noexcept
{
    for (const auto& lane : lanes)
        if (lane.soloed)
            return true;
    return false;
}

bool PatternData::laneAudible (int voice) const noexcept
{
    const auto& lane = lanes[(size_t) juce::jlimit (0, kNumVoices - 1, voice)];
    if (lane.muted)
        return false;
    return anySoloed() ? lane.soloed : true;
}

bool PatternData::operator== (const PatternData& other) const noexcept
{
    if (length != other.length || name != other.name)
        return false;

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto& a = lanes[(size_t) v];
        const auto& b = other.lanes[(size_t) v];
        if (a.length != b.length || a.muted != b.muted || a.soloed != b.soloed || a.locked != b.locked)
            return false;

        for (int s = 0; s < kMaxSteps; ++s)
        {
            const auto& x = a.steps[(size_t) s];
            const auto& y = b.steps[(size_t) s];
            if (x.on != y.on || x.locked != y.locked || x.ratchet != y.ratchet
                || x.velocity != y.velocity || x.probability != y.probability
                || x.microtiming != y.microtiming || x.pitchSemis != y.pitchSemis)
                return false;
        }
    }
    return true;
}

namespace
{
juce::String encodeFloatField (const Lane& lane, float Step::*member)
{
    juce::String out;
    out.preallocateBytes ((size_t) lane.length * 8);
    for (int s = 0; s < lane.length; ++s)
    {
        std::uint32_t bits = 0;
        const float value = lane.steps[(size_t) s].*member;
        static_assert (sizeof (float) == sizeof (std::uint32_t));
        std::memcpy (&bits, &value, sizeof (bits));
        out << juce::String::toHexString ((int) bits).paddedLeft ('0', 8);
    }
    return out;
}

void decodeFloatField (const juce::String& text, Lane& lane, float Step::*member)
{
    if (text.length() != lane.length * 8)
        return;

    for (int s = 0; s < lane.length; ++s)
    {
        const auto bits = (std::uint32_t) text.substring (s * 8, s * 8 + 8).getHexValue32();
        float value = 0.0f;
        std::memcpy (&value, &bits, sizeof (value));
        lane.steps[(size_t) s].*member = value;
    }
}

juce::String encodeBoolField (const Lane& lane, bool Step::*member)
{
    juce::String out;
    out.preallocateBytes ((size_t) lane.length);
    for (int s = 0; s < lane.length; ++s)
        out << (lane.steps[(size_t) s].*member ? '1' : '0');
    return out;
}

void decodeBoolField (const juce::String& text, Lane& lane, bool Step::*member)
{
    if (text.length() != lane.length)
        return;
    for (int s = 0; s < lane.length; ++s)
        lane.steps[(size_t) s].*member = text[s] == '1';
}

juce::String encodeRatchets (const Lane& lane)
{
    juce::String out;
    out.preallocateBytes ((size_t) lane.length);
    for (int s = 0; s < lane.length; ++s)
        out << (juce::jlimit (1, 4, lane.steps[(size_t) s].ratchet) + '0');
    return out;
}

void decodeRatchets (const juce::String& text, Lane& lane)
{
    if (text.length() != lane.length)
        return;
    for (int s = 0; s < lane.length; ++s)
        lane.steps[(size_t) s].ratchet = juce::jlimit (1, 4, (int) text[s] - '0');
}
} // namespace

juce::ValueTree PatternData::toValueTree() const
{
    juce::ValueTree tree ("PATTERN");
    tree.setProperty ("length", length, nullptr);
    tree.setProperty ("name", getName(), nullptr);

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto& lane = lanes[(size_t) v];
        juce::ValueTree laneTree ("LANE");
        laneTree.setProperty ("index", v, nullptr);
        laneTree.setProperty ("length", lane.length, nullptr);
        laneTree.setProperty ("mute", lane.muted, nullptr);
        laneTree.setProperty ("solo", lane.soloed, nullptr);
        laneTree.setProperty ("laneLock", lane.locked, nullptr);

        // Step data is encoded as compact strings so the whole state survives
        // JUCE's XML round-trip (XML cannot carry arrays/variant containers).
        laneTree.setProperty ("on",      encodeBoolField   (lane, &Step::on),      nullptr);
        laneTree.setProperty ("lock",    encodeBoolField   (lane, &Step::locked),  nullptr);
        laneTree.setProperty ("ratchet", encodeRatchets    (lane),                 nullptr);
        laneTree.setProperty ("vel",     encodeFloatField  (lane, &Step::velocity),    nullptr);
        laneTree.setProperty ("prob",    encodeFloatField  (lane, &Step::probability), nullptr);
        laneTree.setProperty ("micro",   encodeFloatField  (lane, &Step::microtiming), nullptr);
        laneTree.setProperty ("pitch",   encodeFloatField  (lane, &Step::pitchSemis),  nullptr);
        tree.addChild (laneTree, -1, nullptr);
    }

    return tree;
}

PatternData PatternData::fromValueTree (const juce::ValueTree& tree)
{
    PatternData out;
    out.length = juce::jlimit (kMinSteps, kMaxSteps, (int) tree.getProperty ("length", kDefaultSteps));
    out.setName (tree.getProperty ("name", "Init").toString());
    for (auto& lane : out.lanes)
        lane.length = out.length;

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto laneTree = tree.getChildWithProperty ("index", v);
        if (! laneTree.isValid())
            continue;

        auto& lane = out.lanes[(size_t) v];
        lane.length = juce::jlimit (1, out.length, (int) laneTree.getProperty ("length", out.length));
        lane.muted  = (bool) laneTree.getProperty ("mute", false);
        lane.soloed = (bool) laneTree.getProperty ("solo", false);
        lane.locked = (bool) laneTree.getProperty ("laneLock", false);

        decodeBoolField   (laneTree.getProperty ("on", juce::String()).toString(), lane, &Step::on);
        decodeBoolField   (laneTree.getProperty ("lock", juce::String()).toString(), lane, &Step::locked);
        decodeRatchets    (laneTree.getProperty ("ratchet", juce::String()).toString(), lane);
        decodeFloatField  (laneTree.getProperty ("vel", juce::String()).toString(), lane, &Step::velocity);
        decodeFloatField  (laneTree.getProperty ("prob", juce::String()).toString(), lane, &Step::probability);
        decodeFloatField  (laneTree.getProperty ("micro", juce::String()).toString(), lane, &Step::microtiming);
        decodeFloatField  (laneTree.getProperty ("pitch", juce::String()).toString(), lane, &Step::pitchSemis);
    }

    out.sanitise();
    return out;
}

} // namespace klopgeest
