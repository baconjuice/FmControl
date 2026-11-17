#pragma once

#include "model/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
// Top bar: MIDI ports, device number, connection test and edit buffer actions.
class ConnectionBar : public juce::Component,
                      private FmDevice::Listener,
                      private Session::Listener,
                      private juce::Timer
{
public:
    explicit ConnectionBar (Session& s);
    ~ConnectionBar() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void refreshPorts();
    void refreshKeys();
    void fmActivity (bool outgoing) override;
    void fmPortsChanged() override;
    void editReplaced() override { refreshPart(); }
    void refreshPart();             // Voice or drum set wording on the buttons
    void timerCallback() override;

    Session& session;
    juce::Label inLabel, outLabel, devLabel;
    juce::ComboBox inBox, outBox, devBox, keysBox;
    juce::TextButton pingButton { "Test" }, liveButton { "Live" }, getButton { "Get Voice" },
        sendButton { "Send Voice" }, playButton { "Play" }, panicButton { "Panic" };

    juce::Array<juce::MidiDeviceInfo> inputs, outputs;
    juce::MidiDeviceListConnection deviceListConnection;
    juce::uint32 lastIn = 0, lastOut = 0;
    bool playHeld = false;
    juce::Rectangle<int> inLed, outLed;
};
} // namespace ui
