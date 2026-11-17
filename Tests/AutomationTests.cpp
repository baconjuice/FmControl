// DAW automation slots: host -> patch, patch -> host, voices, state.

#include "model/Session.h"
#include "plugin/Automation.h"

#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (! (c)) { std::printf ("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

struct DummyProcessor : juce::AudioProcessor
{
    const juce::String getName() const override { return "test"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};


static void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil (60); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    FmDevice device;
    Session session (device);
    session.setLive (false); // no MIDI port in tests
    DummyProcessor proc;
    Automation automation (proc, session);
    auto& params = proc.getParameters();
    CHECK (params.size() == Automation::kSlots);

    // Every default resolves to a real parameter.
    for (auto& a : Automation::defaults())
        CHECK (a.paramId.empty() || fm::voiceParams().find (a.paramId) != nullptr);

    const auto& T = fm::voiceParams();
    CHECK (params[0]->getName (100) == "1: V1 OP1 Volume");
    CHECK (params[4]->getName (100) == "5: V1 Feedback");
    CHECK (params[14]->getName (100) == "15: V1 WT Rate");

    // Host -> patch.
    params[0]->setValue (1.0f);
    pump();
    CHECK (session.getParam (T.get ("op1.volume")) == 63);
    params[0]->setValue (0.0f);
    pump();
    CHECK (session.getParam (T.get ("op1.volume")) == 0);

    // Patch -> host (editor edit).
    session.setParam (T.get ("feedback"), 7);
    CHECK (std::abs (params[4]->getValue() - 1.0f) < 1.0e-4f);

    // A slot on voice 3 drives voice 3 even while voice 1 is edited.
    automation.assign (5, 2, "algorithm", 0);
    CHECK (params[5]->getName (100) == "6: V3 Algorithm");
    CHECK (automation.slotFor (2, "algorithm") == 5);
    CHECK (automation.slotFor (0, "algorithm") == -1);
    CHECK (automation.slotFor (0, "op1.mult") == -1); // slot 6 was replaced
    params[5]->setValue (1.0f);
    pump();
    CHECK (session.getVoiceParam (2, T.get ("algorithm")) == 3);
    CHECK (session.getParam (T.get ("algorithm")) != 3 || session.getVoiceParam (0, T.get ("algorithm")) == 0);
    CHECK (session.getEditVoice() == 0);

    // State round trip.
    auto state = automation.toValueTree();
    automation.resetToDefaults (0);
    CHECK (automation.slotFor (2, "algorithm") == -1);
    automation.fromValueTree (state);
    CHECK (automation.slotFor (2, "algorithm") == 5);

    // Unassign.
    automation.unassign (2, "algorithm");
    CHECK (params[5]->getName (100) == "6: (unused)");

    // Drum mode: its own edit buffer and bank, voices untouched.
    {
        const auto& D = fm::drumParams();
        const auto voiceBefore = session.edit();
        CHECK (session.slotCount() == Session::kSlots);
        session.setDrumMode (true);
        CHECK (session.isDrumMode() && session.slotCount() == fm::kDrumSetsPerBank);
        CHECK (session.currentSlotName (2) == "A03");
        session.getDrumTarget().setParam (D.get ("bd.freq"), 77);
        CHECK (session.drums().getRaw (D.get ("bd.freq")) == 77);
        CHECK (session.edit() == voiceBefore);
        session.setBankNumber (3);
        CHECK (session.getBankNumber() == 3);
        session.setSlot (5, fm::makeInitDrums());
        CHECK (session.slot (5).has_value());

        session.randomize ("all", 1.0f);
        CHECK (session.canUndoRandom());
        session.undoRandom();
        CHECK (session.drums().getRaw (D.get ("bd.freq")) == 77);

        // State round trip keeps the kit, its bank and the mode.
        const auto sessionState = session.toValueTree();
        session.setEditVoice (1);
        CHECK (! session.isDrumMode() && session.getBankNumber() == 0 && ! session.slot (5).has_value());
        session.fromValueTree (sessionState);
        CHECK (session.isDrumMode() && session.getBankNumber() == 3 && session.slot (5).has_value());
        CHECK (session.drums().getRaw (D.get ("bd.freq")) == 77);

        // A drum-set file switches to drum mode and fills the drum list.
        juce::String msg;
        session.setEditVoice (0);
        CHECK (session.loadSyx (juce::File (FIXTURES_DIR "/midibox_fm_drumsets.syx"), msg));
        CHECK (session.isDrumMode());
        int loaded = 0;
        for (int i = 0; i < session.slotCount(); ++i)
            loaded += session.slot (i).has_value() ? 1 : 0;
        CHECK (loaded == 16);
        session.setEditVoice (0);
    }

    // Ensemble mode: own edit buffer and 32-slot bank; voices follow its channels.
    {
        const auto& E = fm::ensembleParams();
        const auto voiceBefore = session.edit();
        CHECK (session.voiceChannel (2) == 3);
        session.setEnsembleMode (true);
        CHECK (session.isEnsembleMode() && ! session.isDrumMode() && session.slotCount() == fm::kEnsemblesPerBank);
        CHECK (session.currentSlotName (31) == "A32");
        session.getEnsembleTarget().setParam (E.get ("v3.channel"), 9);
        CHECK (session.voiceChannel (2) == 10);
        session.getEnsembleTarget().setParam (E.get ("v2.patch"), 41);
        CHECK (fm::ensemblePatch (session.getEnsemble(), 1) == 41);
        CHECK (session.edit() == voiceBefore);
        CHECK (! session.canUndoRandom());
        session.randomize ("all", 1.0f); // no random ensembles
        CHECK (! session.canUndoRandom() && fm::ensemblePatch (session.getEnsemble(), 1) == 41);
        session.setBankNumber (2);
        session.setSlot (7, session.current());
        CHECK (session.slot (7).has_value() && session.editorSourceSlot() == -1);
        CHECK (session.storedPatchLabel (0, 4) == "A005");

        // State round trip keeps the ensemble, its bank and the mode.
        const auto sessionState = session.toValueTree();
        session.setEditVoice (0);
        CHECK (! session.isEnsembleMode() && session.getBankNumber() == 0);
        CHECK (session.voiceChannel (2) == 10); // still the ensemble's channel
        session.loadIntoEditor (fm::Patch::makeInit());
        session.setEnsembleMode (true);
        session.loadIntoEditor (fm::makeInitEnsemble());
        CHECK (session.voiceChannel (2) == 3);
        session.fromValueTree (sessionState);
        CHECK (session.isEnsembleMode() && session.getBankNumber() == 2 && session.slot (7).has_value());
        CHECK (session.voiceChannel (2) == 10);

        // An ensemble file switches to ensemble mode and fills the list.
        juce::String msg;
        session.setDrumMode (true);
        CHECK (session.loadSyx (juce::File (FIXTURES_DIR "/midibox_fm_ensembles.syx"), msg));
        CHECK (session.isEnsembleMode());
        int loaded = 0;
        for (int i = 0; i < session.slotCount(); ++i)
            loaded += session.slot (i).has_value() ? 1 : 0;
        CHECK (loaded == 32);
        // A drum-set file switches on to drum mode.
        CHECK (session.loadSyx (juce::File (FIXTURES_DIR "/midibox_fm_drumsets.syx"), msg) && session.isDrumMode());
        session.setEditVoice (0);
        CHECK (! session.isEnsembleMode() && ! session.isDrumMode());
    }

    std::printf (failures == 0 ? "All automation tests passed\n" : "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
