#include "ui/LookAndFeel.h"

namespace klopgeest::ui
{

KlopgeestLookAndFeel::KlopgeestLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);

    setColour (juce::PopupMenu::backgroundColourId, col::panelHi);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::amber.withAlpha (0.16f));
    setColour (juce::PopupMenu::highlightedTextColourId, col::amberHi);

    setColour (juce::ComboBox::backgroundColourId, col::panelLow);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::outlineColourId, col::line);
    setColour (juce::ComboBox::arrowColourId, col::amber);
    setColour (juce::ComboBox::buttonColourId, col::panelLow);

    setColour (juce::Label::textColourId, col::text);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    setColour (juce::TextEditor::backgroundColourId, col::panelLow);
    setColour (juce::TextEditor::textColourId, col::cyan);
    setColour (juce::TextEditor::outlineColourId, col::line);
    setColour (juce::TextEditor::focusedOutlineColourId, col::cyanDim);
    setColour (juce::TextEditor::highlightColourId, col::amber.withAlpha (0.22f));
    setColour (juce::TextEditor::highlightedTextColourId, col::text);

    setColour (juce::Slider::rotarySliderFillColourId, col::amber);
    setColour (juce::Slider::rotarySliderOutlineColourId, col::line);
    setColour (juce::Slider::thumbColourId, col::amber);

    setColour (juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, col::lineHi);
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    setColour (juce::TooltipWindow::backgroundColourId, col::panelHi);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::lineHi);
}

void KlopgeestLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPosProportional, float rotaryStartAngle,
                                             float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                            .reduced (2.0f);

    if (bounds.getWidth() < 8.0f || bounds.getHeight() < 8.0f)
        return;

    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto lineW = juce::jmax (1.9f, radius * 0.17f);
    const auto arcR = radius - lineW * 0.5f;

    auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

    if (! slider.isEnabled())
        accent = col::textFaint;
    else if (slider.isMouseOverOrDragging())
        accent = accent.brighter (0.16f);

    // Unfilled track.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (col::line);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    const auto pos = juce::jlimit (0.0f, 1.0f, sliderPosProportional);
    const bool bipolar = slider.getMinimum() < -0.0001 && slider.getMaximum() > 0.0001;

    juce::Path valueArc;

    if (bipolar)
    {
        const auto zeroProp = (float) slider.valueToProportionOfLength (0.0);
        const auto zeroAngle = rotaryStartAngle
                               + (rotaryEndAngle - rotaryStartAngle)
                                     * juce::jlimit (0.0f, 1.0f, zeroProp);
        const auto valueAngle = rotaryStartAngle
                                + (rotaryEndAngle - rotaryStartAngle) * pos;

        if (std::abs (valueAngle - zeroAngle) > 0.015f)
            valueArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                                    juce::jmin (zeroAngle, valueAngle),
                                    juce::jmax (zeroAngle, valueAngle), true);
    }
    else if (pos > 0.004f)
    {
        valueArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle,
                                rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * pos, true);
    }

    if (! valueArc.isEmpty())
    {
        g.setColour (accent.withAlpha (0.12f));
        g.strokePath (valueArc, juce::PathStrokeType (lineW * 2.6f,
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (valueArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
    }

    // Machined body.
    const auto bodyR = radius * 0.65f;
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);

    juce::ColourGradient bodyGrad (col::raised.brighter (0.14f),
                                   centre.x, centre.y - bodyR,
                                   col::panelLow,
                                   centre.x, centre.y + bodyR, false);
    g.setGradientFill (bodyGrad);
    g.fillEllipse (body);

    g.setColour (col::lineHi.withAlpha (0.9f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (body.reduced (2.0f).withTrimmedBottom (bodyR * 1.25f), 1.0f);

    // Pointer.
    const auto angle = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * pos;
    const auto dirX = std::sin (angle);
    const auto dirY = -std::cos (angle);
    const auto p1 = bodyR * 0.28f;
    const auto p2 = bodyR * 0.90f;

    g.setColour (accent.brighter (0.28f));
    g.drawLine ({ centre.x + dirX * p1, centre.y + dirY * p1,
                  centre.x + dirX * p2, centre.y + dirY * p2 },
                juce::jmax (1.7f, lineW * 0.62f));

    // Hub.
    g.setColour (col::panelLow);
    g.fillEllipse (juce::Rectangle<float> (lineW * 1.5f, lineW * 1.5f).withCentre (centre));
    g.setColour (accent.withAlpha (0.75f));
    g.fillEllipse (juce::Rectangle<float> (lineW * 0.7f, lineW * 0.7f).withCentre (centre));
}

void KlopgeestLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float minSliderPos, float maxSliderPos,
                                             juce::Slider::SliderStyle style, juce::Slider& slider)
{
    juce::ignoreUnused (minSliderPos, maxSliderPos);

    if (slider.isBar())
    {
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRect (juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height));
        return;
    }

    auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);

    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar
        || style == juce::Slider::LinearBarVertical)
    {
        auto track = bounds.withSizeKeepingCentre (bounds.getWidth(), 4.0f);
        g.setColour (col::line);
        g.fillRoundedRectangle (track, 2.0f);

        auto filled = track.withWidth (juce::jlimit (0.0f, track.getWidth(),
                                                     sliderPos - track.getX()));
        g.setColour (accent.withAlpha (0.85f));
        g.fillRoundedRectangle (filled, 2.0f);

        g.setColour (accent.brighter (0.25f));
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f)
                           .withCentre ({ sliderPos, track.getCentreY() }));
    }
    else
    {
        auto track = bounds.withSizeKeepingCentre (4.0f, bounds.getHeight());
        g.setColour (col::line);
        g.fillRoundedRectangle (track, 2.0f);

        auto filled = track.withTrimmedTop (sliderPos - track.getY());
        g.setColour (accent.withAlpha (0.85f));
        g.fillRoundedRectangle (filled, 2.0f);

        g.setColour (accent.brighter (0.25f));
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f)
                           .withCentre ({ track.getCentreX(), sliderPos }));
    }
}

void KlopgeestLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                 const juce::Colour& backgroundColour,
                                                 bool shouldDrawButtonAsHighlighted,
                                                 bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto radius = juce::jmin (5.0f, bounds.getHeight() * 0.35f);

    auto fill = backgroundColour;

    if (shouldDrawButtonAsDown)
        fill = fill.darker (0.2f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);

    g.setColour (col::lineHi);
    g.drawRoundedRectangle (bounds, radius, 1.0f);
}

void KlopgeestLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                           bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.findColour (button.getToggleState()
                                        ? juce::TextButton::textColourOnId
                                        : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));

    g.drawFittedText (button.getButtonText().toUpperCase(),
                      button.getLocalBounds().reduced (5, 0),
                      juce::Justification::centred, 1);
}

void KlopgeestLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                             bool shouldDrawButtonAsHighlighted,
                                             bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const auto radius = juce::jmin (5.0f, bounds.getHeight() * 0.4f);
    const auto on = button.getToggleState();

    g.setColour (on ? col::amber.withAlpha (0.85f)
                    : (shouldDrawButtonAsHighlighted ? col::offCellHi : col::offCell));
    g.fillRoundedRectangle (bounds, radius);

    g.setColour (on ? col::amberHi : (shouldDrawButtonAsDown ? col::lineHi : col::line));
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    g.setColour (on ? col::bgDeep : col::textDim.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.setFont (ty::sans (juce::jlimit (8.0f, 11.0f, bounds.getHeight() * 0.55f), true));
    g.drawFittedText (button.getButtonText().toUpperCase(), button.getLocalBounds().reduced (4, 0),
                      juce::Justification::centred, 1);
}

void KlopgeestLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                         int buttonX, int buttonY, int buttonW, int buttonH,
                                         juce::ComboBox& box)
{
    juce::ignoreUnused (buttonX, buttonY, buttonW, buttonH);

    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    const auto radius = juce::jmin (5.0f, height * 0.3f);

    g.setColour (isButtonDown ? col::raised : (box.isMouseOver() ? col::panelHi : col::panelLow));
    g.fillRoundedRectangle (bounds, radius);

    g.setColour (box.hasKeyboardFocus (false) ? col::amberDim : col::line);
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    // Caret.
    const auto cx = (float) width - 13.0f;
    const auto cy = (float) height * 0.5f;
    juce::Path caret;
    caret.startNewSubPath (cx - 4.0f, cy - 2.0f);
    caret.lineTo (cx, cy + 2.4f);
    caret.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (box.isMouseOver() ? col::amberHi : col::amber);
    g.strokePath (caret, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

juce::Font KlopgeestLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return ty::sans (10.5f, true);
}

juce::Font KlopgeestLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return ty::sans (juce::jlimit (9.0f, 12.0f, (float) buttonHeight * 0.42f), true);
}

juce::Font KlopgeestLookAndFeel::getLabelFont (juce::Label& label)
{
    return ty::sans (juce::jlimit (9.0f, 11.0f, (float) label.getHeight() * 0.55f));
}

juce::Font KlopgeestLookAndFeel::getPopupMenuFont()
{
    return ty::sans (11.0f, true);
}

void KlopgeestLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 0, box.getWidth() - 26, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

void KlopgeestLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                                     juce::TextEditor& editor)
{
    juce::ignoreUnused (editor);
    g.setColour (col::panelLow);
    g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height), 4.0f);
}

void KlopgeestLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                                  juce::TextEditor& editor)
{
    g.setColour (editor.hasKeyboardFocus (false) ? col::cyanDim : col::line);
    g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                                .reduced (0.5f),
                            4.0f, 1.0f);
}

void KlopgeestLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.setColour (col::panelHi);
    g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height), 6.0f);
    g.setColour (col::lineHi);
    g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                                .reduced (0.5f),
                            6.0f, 1.0f);
}

void KlopgeestLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                              bool isSeparator, bool isActive, bool isHighlighted,
                                              bool isTicked, bool hasSubMenu,
                                              const juce::String& text,
                                              const juce::String& shortcutKeyText,
                                              const juce::Drawable* icon,
                                              const juce::Colour* textColour)
{
    juce::ignoreUnused (hasSubMenu, shortcutKeyText, icon, textColour);

    if (isSeparator)
    {
        g.setColour (col::line);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (col::amber.withAlpha (0.14f));
        g.fillRoundedRectangle (area.toFloat().reduced (4.0f, 1.5f), 4.0f);
    }

    g.setColour (isActive ? (isHighlighted ? col::amberHi : col::text) : col::textFaint);
    g.setFont (getPopupMenuFont());
    g.drawText (text.toUpperCase(), area.reduced (12, 0), juce::Justification::centredLeft, true);

    if (isTicked)
    {
        g.setColour (col::cyan);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f)
                           .withCentre ({ (float) area.getRight() - 12.0f,
                                          (float) area.getCentreY() }));
    }
}

void KlopgeestLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& scrollbar,
                                          int x, int y, int width, int height,
                                          bool isScrollbarVertical, int thumbStartPosition,
                                          int thumbSize, bool isMouseOver, bool isMouseDown)
{
    juce::ignoreUnused (scrollbar);

    if (thumbSize <= 0)
        return;

    juce::Rectangle<float> thumb;

    if (isScrollbarVertical)
        thumb = { (float) x + 2.0f, (float) thumbStartPosition,
                  (float) width - 4.0f, (float) thumbSize };
    else
        thumb = { (float) thumbStartPosition, (float) y + 2.0f,
                  (float) thumbSize, (float) height - 4.0f };

    auto colour = col::lineHi;

    if (isMouseDown)
        colour = col::amber.withAlpha (0.75f);
    else if (isMouseOver)
        colour = col::textFaint;

    g.setColour (colour);
    g.fillRoundedRectangle (thumb, 3.0f);
}

void KlopgeestLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h,
                                              bool isMouseOver, bool isMouseDragging)
{
    juce::ignoreUnused (h);
    auto colour = col::lineHi;

    if (isMouseDragging)
        colour = col::amber;
    else if (isMouseOver)
        colour = col::textDim;

    g.setColour (colour);
    const auto right = (float) w - 2.0f;
    const auto bottom = (float) h - 2.0f;

    for (int i = 0; i < 3; ++i)
    {
        const auto offset = (float) (i * 4);
        g.drawLine (right - 12.0f + offset, bottom, right, bottom - 12.0f + offset, 1.0f);
    }
}

} // namespace klopgeest::ui
