#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Constants.h"

// -----------------------------------------------------------------------------
// KLOPGEEST visual language.
//
// One dark industrial panel, warm amber for structure, pale cyan for motion.
// Everything is drawn procedurally: no images, no external fonts, no assets.
// -----------------------------------------------------------------------------
namespace klopgeest::ui
{

namespace col
{
inline const juce::Colour bg          { 0xff0d0e10 };
inline const juce::Colour bgDeep      { 0xff08090a };
inline const juce::Colour panel       { 0xff15171a };
inline const juce::Colour panelHi     { 0xff1b1e22 };
inline const juce::Colour panelLow    { 0xff101215 };
inline const juce::Colour raised      { 0xff22262b };
inline const juce::Colour line        { 0xff282b31 };
inline const juce::Colour lineHi      { 0xff3b4048 };
inline const juce::Colour text        { 0xffe7e2d8 };
inline const juce::Colour textDim     { 0xff8b9099 };
inline const juce::Colour textFaint   { 0xff565c66 };
inline const juce::Colour amber       { 0xffe8a33d };
inline const juce::Colour amberHi     { 0xfff7c477 };
inline const juce::Colour amberDim    { 0xff6d4d1e };
inline const juce::Colour cyan        { 0xff6fd7e0 };
inline const juce::Colour cyanHi      { 0xffa9eef4 };
inline const juce::Colour cyanDim     { 0xff285a60 };
inline const juce::Colour red         { 0xffe0563f };
inline const juce::Colour redDim      { 0xff6e2a20 };
inline const juce::Colour green       { 0xff8fce6a };
inline const juce::Colour offCell     { 0xff191c20 };
inline const juce::Colour offCellHi   { 0xff21252a };
} // namespace col

namespace ty
{
inline juce::Font sans (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font mono (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace ty

/** Draws text with extra tracking between glyphs (uppercase label look). */
inline void drawSpacedText (juce::Graphics& g, const juce::String& text,
                            juce::Rectangle<float> area, const juce::Font& font,
                            juce::Colour colour,
                            juce::Justification justification = juce::Justification::centredLeft,
                            float extraSpacing = 0.0f)
{
    if (text.isEmpty() || area.isEmpty())
        return;

    g.setFont (font);
    g.setColour (colour);

    if (extraSpacing <= 0.0f)
    {
        g.drawText (text, area, justification, false);
        return;
    }

    juce::Array<float> widths;
    widths.ensureStorageAllocated (text.length());
    float total = 0.0f;

    for (int i = 0; i < text.length(); ++i)
    {
        const auto glyph = juce::String::charToString (text[i]);
        const auto w = juce::GlyphArrangement::getStringWidth (font, glyph);
        widths.add (w);
        total += w;
    }

    total += extraSpacing * (float) juce::jmax (0, text.length() - 1);

    float x = area.getX();

    if (justification.testFlags (juce::Justification::horizontallyCentred))
        x = area.getCentreX() - total * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        x = area.getRight() - total;

    for (int i = 0; i < text.length(); ++i)
    {
        const auto glyph = juce::String::charToString (text[i]);
        g.drawText (glyph, juce::Rectangle<float> (x, area.getY(), widths[i] + 1.0f, area.getHeight()),
                    juce::Justification::centredLeft, false);
        x += widths[i] + extraSpacing;
    }
}

/** A subtle small-caps section label. */
inline void drawSectionLabel (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                              juce::Colour colour = col::textDim, float height = 9.0f)
{
    drawSpacedText (g, text.toUpperCase(), area, ty::sans (height, true), colour,
                    juce::Justification::centredLeft, 1.1f);
}

inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r,
                       juce::Colour fill = col::panel,
                       juce::Colour border = col::line,
                       float radius = 7.0f)
{
    if (r.getWidth() <= 0.0f || r.getHeight() <= 0.0f)
        return;

    g.setColour (fill);
    g.fillRoundedRectangle (r, radius);

    g.setColour (border);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);

    // Faint top edge highlight so panels catch the light.
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    g.drawHorizontalLine ((int) r.getY() + 1,
                          r.getX() + radius, r.getRight() - radius);
}

inline void drawHairline (juce::Graphics& g, float x1, float x2, float y,
                          juce::Colour colour = col::line)
{
    g.setColour (colour);
    g.fillRect (juce::Rectangle<float> (x1, y, x2 - x1, 1.0f));
}

inline void drawVerticalHairline (juce::Graphics& g, float x, float y1, float y2,
                                  juce::Colour colour = col::line)
{
    g.setColour (colour);
    g.fillRect (juce::Rectangle<float> (x, y1, 1.0f, y2 - y1));
}

inline juce::String dbText (float db, int decimals = 1)
{
    if (db <= -99.0f)
        return juce::String ("-inf");
    return juce::String (db, decimals);
}

inline juce::String signedMs (float ms)
{
    const auto rounded = juce::roundToInt (ms * 10.0f) / 10.0f;
    return (rounded >= 0.0f ? "+" : "") + juce::String (rounded, 1) + " ms";
}

inline juce::String signedSemis (float semis)
{
    return (semis >= 0.0f ? "+" : "") + juce::String (semis, 1) + " st";
}

/** Voice role description used by the voice panel and groove copy. */
inline const char* voiceRole (int voice)
{
    static const char* roles[]
    {
        "ANCHOR - THE PULSE",   "ANCHOR - THE BACKBEAT",
        "COLOUR - DIFFUSION",   "COLOUR - TIMEKEEPER",
        "COLOUR - THE AIR",     "COLOUR - THE BODY",
        "COLOUR - THE SPICE",   "FERAL - THE WILDCARD"
    };
    return roles[juce::jlimit (0, 7, voice)];
}

inline bool voiceIsAnchor (int voice) noexcept { return voice <= VoiceSnare; }

} // namespace klopgeest::ui
