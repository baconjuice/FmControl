#pragma once

#include "model/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
// The random generator, shown in a call-out from the patch header: one
// button per section (like the box's RND menu), an amount slider for gentle
// variations, and undo. It works on the voice being edited.
class RandomPanel : public juce::Component,
                    private Session::Listener
{
public:
    explicit RandomPanel (Session& s);
    ~RandomPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void editReplaced() override { refresh(); }
    void refresh();
    void updateAmountText();

    Session& session;
    juce::OwnedArray<juce::TextButton> sectionButtons;
    juce::Label amountLabel, hint;
    juce::Slider amount;
    juce::TextButton undoButton { "Undo" };
};
} // namespace ui
