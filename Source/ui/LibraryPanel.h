#pragma once

#include "model/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
// Librarian: one bank of 128 voice patches (or 16 drum sets in drum mode),
// transfers to/from the box and .syx files.
class LibraryPanel : public juce::Component,
                     private juce::ListBoxModel,
                     private Session::Listener
{
public:
    explicit LibraryPanel (Session& s);
    ~LibraryPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    void openSyx();
    void saveBank();

private:
    int getNumRows() override { return session.slotCount(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    void bankChanged() override;
    void editReplaced() override { list.repaint(); }
    void updateTooltips();

    void openSlotInEditor (int row);
    void showSlotMenu (int row);
    int selectedRow() const { return list.getSelectedRow(); }
    void showStoreDialog (int row);
    void confirmThen (const juce::String& title, const juce::String& text, std::function<void()> action);

    Session& session;
    juce::ComboBox bankBox;
    juce::ListBox list { "Patches", this };
    juce::TextButton fetchBankButton { "Fetch Bank" }, writeBankButton { "Write Bank" },
        fetchButton { "Fetch" }, storeButton { "Store Here" }, openButton { "Open .syx" }, saveButton { "Save Bank" };

    std::optional<fm::Patch> clipboard;
    bool clipboardIsDrums = false;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastDir;
};
} // namespace ui
