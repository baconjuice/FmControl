#pragma once

#include "PluginProcessor.h"
#include "ui/ConnectionBar.h"
#include "ui/LibraryPanel.h"
#include "ui/LookAndFeel.h"
#include "ui/VoiceEditor.h"

#include <juce_audio_processors/juce_audio_processors.h>

class FmControlEditor : public juce::AudioProcessorEditor,
                        private Session::Listener,
                        private juce::Timer
{
public:
    explicit FmControlEditor (FmControlProcessor&);
    ~FmControlEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void editReplaced() override { refreshHeader(); }
    void editChanged (const std::vector<uint16_t>& addresses) override;
    void statusChanged() override;
    void timerCallback() override;

    void refreshHeader();
    void savePatch();
    void showLog();
    void writeSnapshots (const juce::File& dir);

    FmControlProcessor& proc;
    Session& session;
    ui::LookAndFeel lnf;

    ui::ConnectionBar connectionBar;
    ui::LibraryPanel library;

    juce::Label nameLabel, voiceLabel, sourceLabel;
    juce::TextEditor nameEditor;
    juce::OwnedArray<juce::TextButton> voiceButtons;
    juce::TextButton newButton { "Init Patch" }, openButton { "Open .syx" }, savePatchButton { "Save Patch .syx" };

    std::unique_ptr<ui::VoiceEditor> voiceEditor;

    juce::Label statusLabel;
    double progressValue = -1.0;
    juce::ProgressBar progressBar { progressValue };
    juce::TextButton cancelButton { "Cancel" }, logButton { "Log" };

    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FmControlEditor)
};
