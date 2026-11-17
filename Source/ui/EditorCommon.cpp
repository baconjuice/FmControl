#include "EditorCommon.h"
#include "LookAndFeel.h"

#include "fm/Params.h"

namespace ui
{
juce::String signedText (int v) { return v > 0 ? "+" + juce::String (v) : juce::String (v); }
juce::String noteText (int midiNote) { return juce::String (fm::noteName (midiNote)); }

// OPL frequency multiplier register -> factor.
juce::String multText (int v)
{
    static const char* f[] = { "0.5", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "10", "12", "12", "15", "15" };
    return juce::String ("x") + f[v & 15];
}

// Draws a small picture of a curve given as normalised points.
void drawCurve (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<juce::Point<float>>& pts, juce::Colour c)
{
    juce::Path path;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const float x = r.getX() + r.getWidth() * pts[i].x, y = r.getBottom() - r.getHeight() * pts[i].y;
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    juce::Path fill (path);
    fill.lineTo (r.getRight(), r.getBottom());
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();
    g.setColour (c.withAlpha (0.18f));
    g.fillPath (fill);
    g.setColour (c);
    g.strokePath (path, juce::PathStrokeType (2.0f));
}

// Black "scope" screen with a dotted grid, behind graphs and diagrams.
void drawScope (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (colours::shadow);
    g.fillRect (r);
    g.setColour (colours::panelEdge.withAlpha (0.55f));
    g.drawRect (r, 1.0f);
    // Dots every 4 px along rows that are 8 px apart.
    const auto inner = r.toNearestInt();
    const juce::Point<int> first (inner.getX() + 8, inner.getY() + 8);
    const auto area = juce::Rectangle<int>::leftTopRightBottom (first.x, first.y, inner.getRight() - 1, inner.getBottom() - 1);
    fillDots (g, area, first, colours::panelEdge.withAlpha (0.4f), 4, 8, { { 0, 0 } });
}

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
