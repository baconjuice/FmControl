#include "EditorCommon.h"
#include "LookAndFeel.h"

#include "fm/Params.h"

namespace ui
{
juce::String signedText (int v) { return v > 0 ? "+" + juce::String (v) : juce::String (v); }
juce::String noteText (int midiNote) { return juce::String (fm::noteName (midiNote)); }

std::unique_ptr<juce::ComboBox> makeSelector (std::initializer_list<const char*> items, int initial)
{
    auto c = std::make_unique<juce::ComboBox>();
    int id = 1;
    for (auto* i : items)
        c->addItem (i, id++);
    c->setSelectedId (initial, juce::dontSendNotification);
    return c;
}

std::unique_ptr<juce::Label> makeLabel (const juce::String& text, bool heading)
{
    auto l = std::make_unique<juce::Label> (juce::String(), text);
    l->setFont (font (heading ? 13.0f : 12.0f, heading));
    l->setColour (juce::Label::textColourId, heading ? colours::accent2 : colours::dimText);
    return l;
}

// ---------------------------------------------------------------------------
// Page
// ---------------------------------------------------------------------------
Page::Page (ParamTarget& t, const fm::ParamTable& tbl) : target (t), table (tbl)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);
}

void Page::resized()
{
    viewport.setBounds (getLocalBounds());
    layout (viewport.getMaximumVisibleWidth()); // measures minContentWidth
    viewport.setScrollBarsShown (true, minContentWidth > viewport.getMaximumVisibleWidth());
    const int w = juce::jmax (viewport.getMaximumVisibleWidth(), minContentWidth);
    content.setSize (w, juce::jmax (layout (w), viewport.getMaximumVisibleHeight()));
    layout (w);
}

ParamGroup& Page::group (const juce::String& title, WriteOptionFn wopt)
{
    return add (std::make_unique<ParamGroup> (target, table, title, std::move (wopt)));
}

int Page::columns (const std::vector<ParamGroup*>& groups, int cols, juce::Rectangle<int> area, int gap)
{
    const int colW = (area.getWidth() - (cols - 1) * gap) / cols;
    int y = area.getY();
    for (size_t i = 0; i < groups.size(); i += (size_t) cols)
    {
        int rowH = 0;
        for (int c = 0; c < cols && i + (size_t) c < groups.size(); ++c)
            rowH = juce::jmax (rowH, groups[i + (size_t) c]->preferredHeight (colW));
        for (int c = 0; c < cols && i + (size_t) c < groups.size(); ++c)
            groups[i + (size_t) c]->setBounds (area.getX() + c * (colW + gap), y, colW, rowH);
        y += rowH + gap;
    }
    return y;
}

EditorTabs::EditorTabs()
{
    tabs.setTabBarDepth (30);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);
}

void EditorTabs::addPage (const juce::String& name, std::unique_ptr<juce::Component> page)
{
    tabs.addTab (name, colours::background, page.release(), true);
}
} // namespace ui
