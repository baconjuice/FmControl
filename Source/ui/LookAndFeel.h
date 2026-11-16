#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// FmControl's look: a DOS music tracker on an early-'90s OPL3 sound card.
// VGA blues, cyan text, yellow values, magenta headings, square corners,
// drop shadows and a 5x7 pixel font for titles (see PixelFont.h).

namespace ui
{
namespace colours
{
    const juce::Colour background { 0xff0a0a2a }; // desktop
    const juce::Colour panel      { 0xff10104a }; // windows
    const juce::Colour panelEdge  { 0xff3a3ab8 }; // borders, unlit segments
    const juce::Colour shadow     { 0xff000008 }; // drop shadows
    const juce::Colour text       { 0xff55ffff }; // VGA bright cyan
    const juce::Colour dimText    { 0xff00a8a8 }; // VGA dark cyan
    const juce::Colour accent     { 0xffffff55 }; // VGA yellow: values, active things
    const juce::Colour accent2    { 0xffff55ff }; // VGA magenta: headings
    const juce::Colour onAccent   { 0xff000000 }; // text on yellow/cyan
    const juce::Colour good       { 0xff55ff55 };
    const juce::Colour bad        { 0xffff5555 };
} // namespace colours

juce::Font font (float height, bool bold = false);

// A DOS-style window: square panel with a hard drop shadow and a border.
void drawWindow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill = colours::panel,
                 juce::Colour edge = colours::panelEdge, float shadow = 3.0f);
// A title in the pixel font, sitting in a gap of the window's top border.
void drawWindowTitle (juce::Graphics& g, juce::Rectangle<float> window, const juce::String& title,
                      juce::Colour colour = colours::accent2);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbPos,
                        int thumbSize, bool over, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int w, int h, double progress, const juce::String&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;
};
} // namespace ui
