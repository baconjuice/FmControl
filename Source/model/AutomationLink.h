#pragma once

// DAW automation slots as seen by the editor (implemented by the plugin's
// Automation class). A slot is a host parameter that drives one patch
// parameter of the voice being edited.

#include <juce_core/juce_core.h>

#include <string>

class AutomationLink
{
public:
    virtual ~AutomationLink() = default;

    virtual int numSlots() const = 0;
    virtual int slotFor (int scope, const std::string& paramId) const = 0; // -1 if none
    virtual juce::String slotLabel (int slot) const = 0;                     // "3: OP1 Volume"
    virtual void assign (int slot, int scope, const std::string& paramId, uint8_t writeOption) = 0;
    virtual void unassign (int scope, const std::string& paramId) = 0;
    virtual void resetToDefaults (int scope) = 0;
    // A knob drag on an assigned control: the host sees one gesture (one undo
    // step, clean automation recording) instead of a stream of separate edits.
    virtual void beginGesture (int scope, const std::string& paramId) = 0;
    virtual void endGesture (int scope, const std::string& paramId) = 0;

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void automationChanged() = 0;
    };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

protected:
    void notify() { listeners.call ([] (Listener& l) { l.automationChanged(); }); }
    juce::ListenerList<Listener> listeners;
};
