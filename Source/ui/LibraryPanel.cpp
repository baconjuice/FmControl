#include "LibraryPanel.h"
#include "LookAndFeel.h"

namespace ui
{
namespace
{
juce::String bankLetter (int b) { return juce::String::charToString ((juce::juce_wchar) ('A' + b)); }
}

LibraryPanel::LibraryPanel (Session& s) : session (s)
{
    for (int b = 0; b < 8; ++b)
        bankBox.addItem ("Bank " + bankLetter (b), b + 1);
    bankBox.setSelectedId (session.getBankNumber() + 1, juce::dontSendNotification);
    bankBox.setTooltip ("BankStick bank on the MIDIbox FM used for fetch/store");
    bankBox.onChange = [this] { session.setBankNumber (bankBox.getSelectedId() - 1); };
    addAndMakeVisible (bankBox);

    list.setRowHeight (20);
    list.setColour (juce::ListBox::backgroundColourId, colours::background);
    list.setMultipleSelectionEnabled (false);
    addAndMakeVisible (list);

    fetchBankButton.setTooltip ("Read all 128 patches of this bank from the MIDIbox FM");
    fetchBankButton.onClick = [this]
    {
        if (! session.isEnsembleMode())
        {
            session.fetchBank();
            return;
        }
        // Reading a stored ensemble switches the box to it.
        auto opts = juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle ("Fetch ensembles")
                        .withMessage ("The MIDIbox FM switches to each ensemble it sends, so the voices reload their stored "
                                      "patches and the drum set its stored set. FmControl puts the ensemble that plays now "
                                      "back at the end, but unsaved edits of the voices and the drum set are lost.\n\n"
                                      "Fetch all 32 ensembles of bank " + bankLetter (session.getBankNumber()) + "?")
                        .withButton ("Fetch", 1)
                        .withButton ("Cancel", 0)
                        .withAssociatedComponent (this);
        juce::AlertWindow::showAsync (opts, [this] (int result) { if (result == 1) session.fetchBank(); });
    };
    writeBankButton.setTooltip ("Write every patch in this list to the MIDIbox FM's BankStick");
    writeBankButton.onClick = [this]
    {
        confirmThen ("Write bank", "Overwrite bank " + bankLetter (session.getBankNumber()) + " on the MIDIbox FM with the "
                                       + Session::partNoun (session.getPart(), true) + " in this list?",
                     [this] { session.writeBankToDevice(); });
    };
    fetchButton.setTooltip ("Read the selected patch from the MIDIbox FM and open it");
    fetchButton.onClick = [this] { if (selectedRow() >= 0) session.fetchSlot (selectedRow(), true); };
    storeButton.setTooltip ("Store the patch in the editor into the selected slot on the MIDIbox FM");
    storeButton.onClick = [this]
    {
        if (const int row = selectedRow(); row >= 0)
            showStoreDialog (row);
    };
    openButton.onClick = [this] { openSyx(); };
    saveButton.onClick = [this] { saveBank(); };

    for (auto* b : { &fetchBankButton, &writeBankButton, &fetchButton, &storeButton, &openButton, &saveButton })
        addAndMakeVisible (b);

    lastDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    session.addListener (this);
    updateTooltips();
}

void LibraryPanel::updateTooltips()
{
    const auto part = session.getPart();
    const bool e = part == Session::Part::Ensemble;
    const juce::String what = Session::partNoun (part);
    fetchBankButton.setTooltip ("Read all " + juce::String (session.slotCount()) + " " + Session::partNoun (part, true)
                                + " of this bank from the MIDIbox FM"
                                + (e ? juce::String (" (the box switches to each one, then back)") : juce::String()));
    writeBankButton.setTooltip ("Write every " + what + " in this list to the MIDIbox FM's BankStick");
    fetchButton.setTooltip (e ? juce::String ("Load the selected ensemble on the MIDIbox FM and open it (the voices reload "
                                              "their stored patches)")
                              : "Read the selected " + what + " from the MIDIbox FM and open it");
    storeButton.setTooltip ("Store the " + what + " in the editor into the selected slot on the MIDIbox FM");
}

LibraryPanel::~LibraryPanel()
{
    session.removeListener (this);
}

void LibraryPanel::bankChanged()
{
    bankBox.setSelectedId (session.getBankNumber() + 1, juce::dontSendNotification);
    if (list.getSelectedRow() >= session.slotCount())
        list.deselectAllRows();
    updateTooltips();
    list.updateContent();
    list.repaint();
}

void LibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (selected)
        g.fillAll (colours::text);
    else if (row == session.editorSourceSlot())
        g.fillAll (colours::panelEdge.withAlpha (0.6f));
    else if (row % 2)
        g.fillAll (colours::panel.withAlpha (0.4f));

    const auto& p = session.slot (row);
    g.setFont (font (12.5f));
    g.setColour (selected ? colours::onAccent : colours::dimText);
    g.drawText (session.currentSlotName (row), 6, 0, 40, h, juce::Justification::centredLeft);
    if (p)
    {
        // Drum sets have no name: show the program that selects it on
        // channel 16. Ensembles: the four patches they play.
        juce::String text;
        switch (session.getPart())
        {
            case Session::Part::Drums:
                text = "Drum Set   (PC " + juce::String (session.getBankNumber() * fm::kDrumSetsPerBank + row + 1) + ")";
                break;
            case Session::Part::Ensemble:
                for (int v = 0; v < fm::kVoices; ++v)
                    text << (v > 0 ? " " : "") << Session::slotName (fm::ensembleBank (*p, v), fm::ensemblePatch (*p, v));
                break;
            case Session::Part::Voice:
                text = p->name();
                break;
        }
        g.setColour (selected ? colours::onAccent : colours::accent);
        g.drawText (text, 52, 0, w - 56, h, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (selected ? colours::onAccent : colours::panelEdge);
        g.drawText ("(not loaded)", 52, 0, w - 56, h, juce::Justification::centredLeft);
    }
}

void LibraryPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    openSlotInEditor (row);
}

void LibraryPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showSlotMenu (row);
}

void LibraryPanel::openSlotInEditor (int row)
{
    if (const auto& p = session.slot (row))
        session.loadIntoEditor (*p, row);
    else
        session.fetchSlot (row, true);
}

void LibraryPanel::showSlotMenu (int row)
{
    list.selectRow (row);
    const auto& p = session.slot (row);
    const auto d = session.getPart();
    const auto slotLabel = session.currentSlotName (row);
    const juce::String what = Session::partNoun (d);

    juce::PopupMenu m;
    m.addSectionHeader (slotLabel + (p && d == Session::Part::Voice ? "  " + juce::String (p->name()) : juce::String()));
    m.addItem (1, "Open in editor", p.has_value());
    m.addItem (2, "Fetch from MIDIbox FM");
    m.addItem (3, "Store editor " + what + " here (on MIDIbox FM)");
    m.addSeparator();
    m.addItem (4, "Copy", p.has_value());
    m.addItem (5, "Paste (list only)", clipboard.has_value() && clipboardPart == d);
    m.addItem (6, "Put editor " + what + " here (list only)");
    m.addItem (10, d == Session::Part::Drums      ? juce::String ("Init kit here (list only)")
                   : d == Session::Part::Ensemble ? juce::String ("Init ensemble here (list only)")
                                                  : juce::String ("Init patch here (list only)"));
    m.addItem (7, "Clear (list only)", p.has_value());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&list).withMousePosition(),
                     [this, row] (int result)
                     {
                         switch (result)
                         {
                             case 1: openSlotInEditor (row); break;
                             case 2: session.fetchSlot (row, false); break;
                             case 3: list.selectRow (row); storeButton.triggerClick(); break;
                             case 4: clipboard = session.slot (row); clipboardPart = session.getPart(); break;
                             case 5: if (clipboard) session.setSlot (row, clipboard); break;
                             case 6: session.setSlot (row, session.current()); break;
                             case 7: session.setSlot (row, std::nullopt); break;
                             case 10:
                                 session.setSlot (row, session.isDrumMode()       ? fm::makeInitDrums()
                                                       : session.isEnsembleMode() ? fm::makeInitEnsemble()
                                                                                  : fm::Patch::makeInit());
                                 break;
                             default: break;
                         }
                     });
}

// Store asks for the name (a random patch or a new one usually needs one)
// and warns when the slot already holds a different patch.
void LibraryPanel::showStoreDialog (int row)
{
    const auto slotName = session.currentSlotName (row);
    const auto& existing = session.slot (row);
    if (session.getPart() != Session::Part::Voice)
    {
        // No name to ask for: just confirm when it replaces another one.
        const auto what = Session::partNoun (session.getPart());
        if (existing && *existing != session.current())
            confirmThen ("Store " + what + " in " + slotName,
                         "Replace the " + what + " in " + slotName + " on the MIDIbox FM with the one in the editor?",
                         [this, row] { session.storeEditToSlot (row); });
        else
            session.storeEditToSlot (row);
        return;
    }
    juce::String message = "Name the patch and store it in " + slotName + " on the MIDIbox FM.";
    if (existing && *existing != session.edit())
        message << "\n\nThis replaces \"" << juce::String (existing->name()).trimEnd() << "\" in " << slotName << ".";

    auto* w = new juce::AlertWindow ("Store patch in " + slotName, message,
                                     existing ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", juce::String (session.edit().name()).trimEnd(), "Name");
    if (auto* ed = w->getTextEditor ("name"))
    {
        ed->setInputRestrictions (fm::kNameLength);
        ed->setSelectAllWhenFocused (true);
    }
    w->addButton (existing && *existing != session.edit() ? "Overwrite" : "Store", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<LibraryPanel> self (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([self, w, row] (int result)
                        {
                            if (result != 1 || self == nullptr)
                                return;
                            auto name = w->getTextEditorContents ("name").trim();
                            if (name.isNotEmpty())
                                self->session.setName (name);
                            self->session.storeEditToSlot (row);
                        }),
                        true);
    if (auto* ed = w->getTextEditor ("name"))
        ed->grabKeyboardFocus();
}

void LibraryPanel::confirmThen (const juce::String& title, const juce::String& text, std::function<void()> action)
{
    auto opts = juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::WarningIcon)
                    .withTitle (title)
                    .withMessage (text)
                    .withButton ("Overwrite", 1)
                    .withButton ("Cancel", 0)
                    .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (opts, [fn = std::move (action)] (int result) { if (result == 1) fn(); });
}

void LibraryPanel::openSyx()
{
    chooser = std::make_unique<juce::FileChooser> ("Open MIDIbox FM patches", lastDir, "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              lastDir = f.getParentDirectory();
                              juce::String msg;
                              if (! session.loadSyx (f, msg))
                                  juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (
                                                                    juce::MessageBoxIconType::WarningIcon, "Open .syx", msg),
                                                                nullptr);
                          });
}

void LibraryPanel::saveBank()
{
    auto start = lastDir.getChildFile ((session.isDrumMode() ? "FM Drum Sets " : session.isEnsembleMode() ? "FM Ensembles " : "FM Bank ")
                                       + bankLetter (session.getBankNumber()) + ".syx");
    chooser = std::make_unique<juce::FileChooser> ("Save bank", start, "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              lastDir = f.getParentDirectory();
                              juce::String msg;
                              if (! session.saveBankSyx (f.withFileExtension ("syx"), msg))
                                  juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (
                                                                    juce::MessageBoxIconType::WarningIcon, "Save bank", msg),
                                                                nullptr);
                          });
}

void LibraryPanel::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds());
    g.setColour (colours::panelEdge);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
    g.fillRect (getWidth() - 3, 0, 1, getHeight());
}

void LibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (8).withTrimmedRight (1);
    bankBox.setBounds (r.removeFromTop (26));
    r.removeFromTop (6);

    auto bottom = r.removeFromBottom (26 * 3 + 8);
    auto row = [&bottom]
    {
        auto x = bottom.removeFromTop (26);
        bottom.removeFromTop (4);
        return x;
    };
    auto pair = [] (juce::Rectangle<int> rr, juce::Button& a, juce::Button& b)
    {
        a.setBounds (rr.removeFromLeft (rr.getWidth() / 2 - 2));
        b.setBounds (rr.withTrimmedLeft (4));
    };
    pair (row(), fetchButton, storeButton);
    pair (row(), fetchBankButton, writeBankButton);
    pair (row(), openButton, saveButton);

    r.removeFromBottom (6);
    list.setBounds (r);
}
} // namespace ui
