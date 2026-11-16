#pragma once

// A small 5x7 pixel font for headings, tabs and titles: the DOS-tracker look.
// Lowercase is drawn as uppercase. Unknown characters draw as '?'.

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui::pixel
{
// Width in logical pixels of text drawn with the given pixel size.
float width (const juce::String& text, float px);
float height (float px); // 7 * px

// Draws the text into r (vertically centred) with the given horizontal justification.
void draw (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float px,
           juce::Justification just = juce::Justification::centredLeft);

// Pixel size that fits text into maxWidth, between minPx and maxPx (steps of 0.5).
float fit (const juce::String& text, float maxWidth, float maxPx, float minPx = 1.0f);
} // namespace ui::pixel
