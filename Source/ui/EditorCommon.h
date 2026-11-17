#pragma once

// Building blocks for the editor pages: scrolling pages and tabs.

#include "ParamControls.h"

namespace ui
{
juce::String signedText (int v);
juce::String noteText (int midiNote);
juce::String multText (int v); // OPL frequency multiplier register -> "x2"

// Black "scope" screen with a dotted grid, behind graphs and diagrams.
void drawScope (juce::Graphics& g, juce::Rectangle<float> r);
// A curve given as normalised points (0..1), filled underneath.
void drawCurve (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<juce::Point<float>>& pts, juce::Colour c);

std::unique_ptr<juce::ComboBox> makeSelector (std::initializer_list<const char*> items, int initial = 1);
std::unique_ptr<juce::Label> makeLabel (const juce::String& text, bool heading = false);

// Scrollable page whose content is laid out by `layout`, which returns the
// content height for a given width.
class Page : public juce::Component
{
public:
    Page (ParamTarget& target, const fm::ParamTable& table);

    void resized() override;

    template <typename C>
    C& add (std::unique_ptr<C> c)
    {
        auto& ref = *c;
        content.addAndMakeVisible (ref);
        owned.push_back (std::move (c));
        return ref;
    }

    ParamGroup& group (const juce::String& title, WriteOptionFn wopt = {});

    // Lays groups out in equal-width columns, row by row. Returns the bottom y.
    static int columns (const std::vector<ParamGroup*>& groups, int cols, juce::Rectangle<int> area, int gap = 8);

    std::function<int (int width)> layout = [] (int) { return 0; };
    int minContentWidth = 0;

    ParamTarget& target;
    const fm::ParamTable& table;

protected:
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<juce::Component>> owned;
};

// Tabs of the voice editor.
class EditorTabs : public juce::Component
{
public:
    EditorTabs();
    void addPage (const juce::String& name, std::unique_ptr<juce::Component> page);
    void resized() override { tabs.setBounds (getLocalBounds()); }
    juce::TabbedComponent& getTabs() { return tabs; }

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
};
} // namespace ui
