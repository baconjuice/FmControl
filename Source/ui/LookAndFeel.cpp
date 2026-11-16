#include "LookAndFeel.h"
#include "PixelFont.h"

namespace ui
{
juce::Font font (float height, bool bold)
{
    // A monospaced face for the text-mode feel. Menlo runs wide, so it is
    // drawn a little smaller than the requested height.
    return juce::Font (juce::FontOptions()
                           .withName ("Menlo")
                           .withStyle (bold ? "Bold" : "Regular")
                           .withHeight (height * 0.92f));
}

void drawWindow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, juce::Colour edge, float shadow)
{
    if (shadow > 0.0f)
    {
        g.setColour (colours::shadow.withAlpha (0.85f));
        g.fillRect (r.translated (shadow, shadow));
    }
    g.setColour (fill);
    g.fillRect (r);
    g.setColour (edge);
    g.drawRect (r, 1.0f);
}

void drawWindowTitle (juce::Graphics& g, juce::Rectangle<float> window, const juce::String& title, juce::Colour colour)
{
    if (title.isEmpty())
        return;
    // "TITLE ===========" across the top of the window, in the pixel font.
    auto strip = juce::Rectangle<float> (window.getX() + 8.0f, window.getY() + 2.0f, window.getWidth() - 16.0f, 16.0f);
    const float px = pixel::fit (title, strip.getWidth() - 16.0f, 1.5f, 1.0f);
    const float w = pixel::width (title, px);
    g.setColour (colour);
    pixel::draw (g, title, strip, px);
    const float x0 = std::round (strip.getX() + w + 6.0f), x1 = strip.getRight();
    if (x1 - x0 > 6.0f)
    {
        const float cy = std::round (strip.getCentreY());
        g.setColour (colours::panelEdge);
        g.fillRect (x0, cy - 2.0f, x1 - x0, 1.0f);
        g.fillRect (x0, cy + 1.0f, x1 - x0, 1.0f);
    }
}

LookAndFeel::LookAndFeel()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, panelEdge);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, onAccent);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ToggleButton::tickDisabledColourId, dimText);
    setColour (juce::ComboBox::backgroundColourId, background);
    setColour (juce::ComboBox::outlineColourId, panelEdge);
    setColour (juce::ComboBox::textColourId, accent);
    setColour (juce::ComboBox::arrowColourId, text);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, text);
    setColour (juce::PopupMenu::highlightedTextColourId, onAccent);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, accent2);
    setColour (juce::TextEditor::backgroundColourId, background);
    setColour (juce::TextEditor::outlineColourId, panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::textColourId, accent);
    setColour (juce::TextEditor::highlightColourId, text.withAlpha (0.4f));
    setColour (juce::TextEditor::highlightedTextColourId, onAccent);
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::ListBox::backgroundColourId, background);
    setColour (juce::ListBox::outlineColourId, panelEdge);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, panelEdge);
    setColour (juce::Slider::textBoxTextColourId, accent);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, text.withAlpha (0.4f));
    setColour (juce::ScrollBar::thumbColourId, panelEdge);
    setColour (juce::ScrollBar::trackColourId, background);
    setColour (juce::TabbedComponent::backgroundColourId, background);
    setColour (juce::TabbedComponent::outlineColourId, panelEdge);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, panelEdge);
    setColour (juce::ProgressBar::backgroundColourId, background);
    setColour (juce::ProgressBar::foregroundColourId, accent);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, panelEdge);
    setColour (juce::TooltipWindow::backgroundColourId, text);
    setColour (juce::TooltipWindow::textColourId, onAccent);
    setColour (juce::TooltipWindow::outlineColourId, onAccent);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                    juce::Slider& s)
{
    // A ring of square segments, lit up to the value (from the centre for
    // bipolar parameters), around a pointer made of blocks.
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const int segments = radius > 18.0f ? 15 : 11;
    const float seg = juce::jmax (2.5f, radius * 0.22f);
    const float ringR = radius - seg * 0.6f;

    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zero = bipolar ? (float) ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) : 0.0f;
    const auto lit = s.isEnabled() ? s.findColour (juce::Slider::rotarySliderFillColourId) : colours::dimText;

    for (int i = 0; i < segments; ++i)
    {
        const float t = (float) i / (float) (segments - 1);
        const float a = start + t * (end - start);
        const bool on = bipolar ? (t >= juce::jmin (zero, pos) - 0.001f && t <= juce::jmax (zero, pos) + 0.001f)
                                : (t <= pos + 0.001f && pos > 0.0f);
        g.setColour (on ? lit : colours::panelEdge);
        g.fillRect (juce::Rectangle<float> (seg, seg)
                        .withCentre ({ centre.x + ringR * std::sin (a), centre.y - ringR * std::cos (a) }));
    }

    // Knob body and pointer.
    const float bodyR = ringR - seg * 1.1f;
    g.setColour (colours::background);
    g.fillRect (juce::Rectangle<float> (bodyR * 1.6f, bodyR * 1.6f).withCentre (centre));
    g.setColour (colours::panelEdge);
    g.drawRect (juce::Rectangle<float> (bodyR * 1.6f, bodyR * 1.6f).withCentre (centre), 1.0f);
    const float angle = start + pos * (end - start);
    const float dot = juce::jmax (2.0f, seg * 0.7f);
    g.setColour (s.isEnabled() ? colours::text : colours::dimText);
    for (float d = 0.15f; d <= 0.8f; d += 0.32f)
        g.fillRect (juce::Rectangle<float> (dot, dot)
                        .withCentre ({ centre.x + bodyR * d * std::sin (angle), centre.y - bodyR * d * std::cos (angle) }));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    // Text-mode button with a hard shadow; pressing it pushes it into the shadow.
    auto r = b.getLocalBounds().toFloat().withTrimmedRight (2.0f).withTrimmedBottom (2.0f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
    if (over && ! down) c = c.brighter (0.15f);
    if (! b.isEnabled()) c = c.withMultipliedAlpha (0.45f);
    if (down)
        r.translate (2.0f, 2.0f);
    else
    {
        g.setColour (colours::shadow);
        g.fillRect (r.translated (2.0f, 2.0f));
    }
    g.setColour (c);
    g.fillRect (r);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    auto r = b.getLocalBounds().withTrimmedRight (2).withTrimmedBottom (2);
    if (down) r.translate (2, 2);
    const bool on = b.getToggleState();
    auto c = b.findColour (on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    if (! b.isEnabled()) c = c.withMultipliedAlpha (0.5f);
    g.setColour (c);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText(), r.reduced (3, 1), juce::Justification::centred, 1, 0.8f);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRect (r);
    g.setColour (box.hasKeyboardFocus (true) ? colours::accent : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRect (r, 1.0f);

    // A solid "scroll" block with a down arrow, like a text-mode drop-down.
    auto arrowBox = r.removeFromRight (juce::jmin (16.0f, (float) h)).reduced (2.0f);
    g.setColour (colours::panelEdge);
    g.fillRect (arrowBox);
    juce::Path p;
    const auto c = arrowBox.getCentre();
    p.addTriangle (c.x - 3.5f, c.y - 2.0f, c.x + 3.5f, c.y - 2.0f, c.x, c.y + 2.5f);
    g.setColour (box.isEnabled() ? box.findColour (juce::ComboBox::arrowColourId) : colours::dimText);
    g.fillPath (p);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    // [X] checkbox.
    const float s = juce::jmin (14.0f, (float) b.getHeight() - 4.0f);
    auto box = juce::Rectangle<float> (2.0f, ((float) b.getHeight() - s) * 0.5f, s, s);
    g.setColour (colours::background);
    g.fillRect (box);
    g.setColour (over ? colours::text : colours::panelEdge);
    g.drawRect (box, 1.0f);
    if (b.getToggleState())
    {
        g.setColour (b.isEnabled() ? colours::accent : colours::dimText);
        g.fillRect (box.reduced (3.0f));
    }
    g.setColour (b.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.setFont (font (12.5f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft ((int) s + 7), juce::Justification::centredLeft, 1);
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbPos,
                                 int thumbSize, bool over, bool down)
{
    // Dithered track (the text-mode ░ shade) with a solid thumb.
    g.setColour (colours::background);
    g.fillRect (x, y, w, h);
    g.setColour (colours::panelEdge.withAlpha (0.45f));
    for (int yy = y; yy < y + h; yy += 2)
        for (int xx = x + ((yy / 2) % 2); xx < x + w; xx += 2)
            g.fillRect (xx, yy, 1, 1);
    auto thumb = vertical ? juce::Rectangle<int> (x + 1, thumbPos, w - 2, thumbSize)
                          : juce::Rectangle<int> (thumbPos, y + 1, thumbSize, h - 2);
    g.setColour (down ? colours::accent : (over ? colours::text : colours::dimText));
    g.fillRect (thumb);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (colours::text);
    g.drawRect (0, 0, w, h, 1);
}

void LookAndFeel::drawCallOutBoxBackground (juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    juce::ignoreUnused (box);
    g.setColour (colours::panel);
    g.fillPath (path);
    g.setColour (colours::text);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    // A row of blocks, lit up to the value, like a text-mode level meter.
    const int block = 6, gap = 2, bh = juce::jmin (12, h - 4), by = y + (h - bh) / 2;
    for (int bx = x; bx + block <= x + w; bx += block + gap)
    {
        const bool lit = (float) bx + block * 0.5f <= pos;
        g.setColour (lit ? colours::accent : colours::panelEdge);
        g.fillRect (bx, by, block, bh);
    }
}

void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
    g.fillRect (0, 0, w, h);
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    if (! e.isEnabled()) return;
    g.setColour (e.hasKeyboardFocus (true) && ! e.isReadOnly() ? e.findColour (juce::TextEditor::focusedOutlineColourId)
                                                               : e.findColour (juce::TextEditor::outlineColourId));
    g.drawRect (0, 0, w, h, 1);
}

void LookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar& bar, int w, int h, double progress,
                                   const juce::String& text)
{
    // A row of █ blocks.
    g.setColour (bar.findColour (juce::ProgressBar::backgroundColourId));
    g.fillRect (0, 0, w, h);
    g.setColour (colours::panelEdge);
    g.drawRect (0, 0, w, h, 1);
    const int block = juce::jmax (4, h - 6), gap = 2;
    const int n = (w - 4) / (block + gap);
    const int lit = progress >= 0.0 ? (int) std::round (progress * n) : 0;
    g.setColour (bar.findColour (juce::ProgressBar::foregroundColourId));
    for (int i = 0; i < lit; ++i)
        g.fillRect (3 + i * (block + gap), 3, block, h - 6);
    if (text.isNotEmpty())
    {
        g.setColour (colours::text);
        g.setFont (font (12.0f));
        g.drawText (text, 0, 0, w, h, juce::Justification::centred);
    }
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return font (juce::jmin (13.0f, (float) h * 0.6f)); }
juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (12.5f); }
juce::Font LookAndFeel::getLabelFont (juce::Label& l) { return l.getFont().withHeight (juce::jmin (l.getFont().getHeight(), 14.0f)); }
juce::Font LookAndFeel::getPopupMenuFont() { return font (13.0f); }
juce::Font LookAndFeel::getAlertWindowTitleFont() { return font (15.0f, true); }
juce::Font LookAndFeel::getAlertWindowMessageFont() { return font (13.0f); }
juce::Font LookAndFeel::getAlertWindowFont() { return font (13.0f); }

int LookAndFeel::getTabButtonBestWidth (juce::TabBarButton& b, int)
{
    return (int) pixel::width (b.getButtonText(), 1.5f) + 28;
}

void LookAndFeel::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    // Like a text-mode menu bar: the front tab is an inverse (cyan) bar.
    auto r = b.getLocalBounds().toFloat().reduced (2.0f, 3.0f);
    const bool front = b.isFrontTab();
    if (front)
    {
        g.setColour (colours::shadow);
        g.fillRect (r.translated (2.0f, 2.0f));
    }
    g.setColour (front ? colours::text : (over ? colours::panelEdge : colours::panel));
    g.fillRect (r);
    g.setColour (front ? colours::onAccent : colours::text);
    pixel::draw (g, b.getButtonText(), r, 1.5f, juce::Justification::centred);
}

void LookAndFeel::drawTabAreaBehindFrontButton (juce::TabbedButtonBar& bar, juce::Graphics& g, int w, int h)
{
    juce::ignoreUnused (bar);
    g.setColour (colours::panelEdge);
    g.fillRect (0, h - 1, w, 1);
}
} // namespace ui
