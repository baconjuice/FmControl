#include "PluginEditor.h"
#include "ui/RandomPanel.h"

using namespace ui;

FmControlEditor::FmControlEditor (FmControlProcessor& p)
    : AudioProcessorEditor (p), proc (p), session (p.session), connectionBar (p.session), library (p.session)
{
    setLookAndFeel (&lnf);
    setOpaque (true); // paint() fills every pixel, so the host need not draw behind it
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);

    addAndMakeVisible (connectionBar);
    addAndMakeVisible (library);

    nameLabel.setText ("Patch", juce::dontSendNotification);
    nameLabel.setFont (font (12.5f));
    nameLabel.setColour (juce::Label::textColourId, colours::dimText);
    addAndMakeVisible (nameLabel);

    nameEditor.setFont (font (16.0f, true));
    nameEditor.setInputRestrictions (fm::kNameLength);
    nameEditor.setTooltip ("Patch name (16 characters)");
    nameEditor.onTextChange = [this] { session.setName (nameEditor.getText()); };
    addAndMakeVisible (nameEditor);

    voiceLabel.setText ("Voice", juce::dontSendNotification);
    voiceLabel.setFont (font (12.5f));
    voiceLabel.setColour (juce::Label::textColourId, colours::dimText);
    voiceLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (voiceLabel);
    for (int v = 0; v < fm::kVoices; ++v)
    {
        auto* b = voiceButtons.add (new juce::TextButton (juce::String (v + 1)));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4711);
        b->setTooltip ("Edit voice " + juce::String (v + 1) + " (MIDI channel " + juce::String (session.voiceChannel (v))
                       + " in the default ensemble)");
        b->setConnectedEdges ((v > 0 ? juce::Button::ConnectedOnLeft : 0) | (v < 3 ? juce::Button::ConnectedOnRight : 0));
        b->onClick = [this, v] { if (voiceButtons[v]->getToggleState()) session.setEditVoice (v); };
        addAndMakeVisible (b);
    }
    {
        // The drum set, like a fifth "voice".
        auto* b = voiceButtons.add (new juce::TextButton ("DR"));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4711);
        b->setTooltip ("Edit the drum set (MIDI channel " + juce::String (fm::kDrumChannel)
                       + "); the library then shows the bank's 16 drum sets");
        b->setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        b->onClick = [this, b] { if (b->getToggleState()) session.setDrumMode (true); };
        addAndMakeVisible (b);
        voiceButtons[fm::kVoices - 1]->setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    }
    {
        // The ensemble: which patch, channel and key range each voice plays.
        auto* b = voiceButtons.add (new juce::TextButton ("EN"));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4711);
        b->setTooltip ("Edit the ensemble: the stored patch, MIDI channel, key range, volume and outputs of each voice; "
                       "the library then shows the bank's 32 ensembles");
        b->setConnectedEdges (juce::Button::ConnectedOnLeft);
        b->onClick = [this, b] { if (b->getToggleState()) session.setEnsembleMode (true); };
        addAndMakeVisible (b);
    }

    sourceLabel.setFont (font (12.0f));
    sourceLabel.setColour (juce::Label::textColourId, colours::dimText);
    addAndMakeVisible (sourceLabel);

    newButton.onClick = [this]
    {
        session.loadIntoEditor (session.isDrumMode()       ? fm::makeInitDrums()
                                : session.isEnsembleMode() ? fm::makeInitEnsemble()
                                                           : fm::Patch::makeInit());
    };
    openButton.onClick = [this] { library.openSyx(); };
    savePatchButton.onClick = [this] { savePatch(); };
    randomButton.setTooltip ("Random generator: roll new operators, LFOs, EG5 or wavetable (like RND on the box)");
    randomButton.setColour (juce::TextButton::buttonColourId, colours::accent2);
    randomButton.setColour (juce::TextButton::textColourOffId, colours::onAccent);
    randomButton.onClick = [this] { showRandomPanel(); };
    for (auto* b : { &newButton, &openButton, &savePatchButton, &randomButton })
        addAndMakeVisible (b);

    voiceEditor = std::make_unique<VoiceEditor> (session);
    addAndMakeVisible (*voiceEditor);
    drumEditor = std::make_unique<DrumEditor> (session);
    addChildComponent (*drumEditor);
    ensembleEditor = std::make_unique<EnsembleEditor> (session);
    addChildComponent (*ensembleEditor);

    statusLabel.setFont (font (12.5f));
    statusLabel.setColour (juce::Label::textColourId, colours::onAccent);
    addAndMakeVisible (statusLabel);
    addChildComponent (progressBar);
    cancelButton.onClick = [this] { session.cancelTransfers(); };
    addChildComponent (cancelButton);
    logButton.onClick = [this] { showLog(); };
    addAndMakeVisible (logButton);

    session.addListener (this);
    refreshHeader();
    statusChanged();

    setResizable (true, true);
    setResizeLimits (1200, 700, 2400, 1600);
    setSize (proc.editorWidth, proc.editorHeight);
    startTimerHz (10);

    // Developer aid: FMCONTROL_SNAPSHOT_DIR=/some/dir renders every page to PNG and quits.
    // Optionally load a bank first: FMCONTROL_SNAPSHOT_SYX=/file.syx FMCONTROL_SNAPSHOT_SLOT=n
    if (auto dir = juce::SystemStats::getEnvironmentVariable ("FMCONTROL_SNAPSHOT_DIR", {}); dir.isNotEmpty())
    {
        session.setLive (false);
        if (auto syx = juce::SystemStats::getEnvironmentVariable ("FMCONTROL_SNAPSHOT_SYX", {}); syx.isNotEmpty())
        {
            juce::String msg;
            session.loadSyx (juce::File (syx), msg);
            const int slot = juce::SystemStats::getEnvironmentVariable ("FMCONTROL_SNAPSHOT_SLOT", "0").getIntValue();
            if (const auto& patch = session.slot (slot))
                session.loadIntoEditor (*patch, slot);
        }
        juce::Timer::callAfterDelay (1500, [this, dir] { writeSnapshots (juce::File (dir)); });
    }
}

FmControlEditor::~FmControlEditor()
{
    session.removeListener (this);
    voiceEditor.reset();
    drumEditor.reset();
    ensembleEditor.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void FmControlEditor::writeSnapshots (const juce::File& dir)
{
    dir.createDirectory();
    auto& tabs = voiceEditor->getTabs();
    for (int i = 0; i < tabs.getNumTabs(); ++i)
    {
        tabs.setCurrentTabIndex (i);
        auto img = createComponentSnapshot (getLocalBounds(), true, 1.0f);
        {
            // Paint timing at Retina scale, for spotting slow drawing code.
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int k = 0; k < 10; ++k)
                createComponentSnapshot (getLocalBounds(), true, 2.0f);
            juce::Logger::outputDebugString ("paint " + tabs.getTabNames()[i] + ": "
                                             + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 10.0, 2) + " ms");
        }
        juce::FileOutputStream out (dir.getChildFile (juce::String (i) + "_" + tabs.getTabNames()[i].removeCharacters (" &") + ".png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (img, out);
    }
    {
        // Random generator panel, and a fully random patch.
        ui::RandomPanel panel (session);
        panel.setLookAndFeel (&lnf);
        auto img = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out (dir.getChildFile ("RandomPanel.png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (img, out);
        panel.setLookAndFeel (nullptr);
    }
    session.randomize ("all", 1.0f);
    for (int i = 0; i < tabs.getNumTabs(); ++i)
    {
        tabs.setCurrentTabIndex (i);
        auto img = createComponentSnapshot (getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out (dir.getChildFile ("Random_" + juce::String (i) + ".png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (img, out);
    }
    {
        // Drum set page and its random panel.
        session.setDrumMode (true);
        auto save = [&dir] (const juce::Image& img, const juce::String& file)
        {
            juce::FileOutputStream out (dir.getChildFile (file));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (img, out);
        };
        save (createComponentSnapshot (getLocalBounds(), true, 1.0f), "Drums.png");
        ui::RandomPanel panel (session);
        panel.setLookAndFeel (&lnf);
        save (panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f), "RandomPanel_Drums.png");
        panel.setLookAndFeel (nullptr);
        session.randomize ("all", 1.0f);
        save (createComponentSnapshot (getLocalBounds(), true, 1.0f), "Drums_Random.png");
        session.setEnsembleMode (true);
        save (createComponentSnapshot (getLocalBounds(), true, 1.0f), "Ensemble.png");
        session.setEditVoice (0);
    }
    if (auto* app = juce::JUCEApplicationBase::getInstance())
        app->systemRequestedQuit();
}

void FmControlEditor::showRandomPanel()
{
    auto panel = std::make_unique<ui::RandomPanel> (session);
    juce::CallOutBox::launchAsynchronously (std::move (panel), randomButton.getBounds(), this);
}

void FmControlEditor::refreshHeader()
{
    const bool drums = session.isDrumMode(), ens = session.isEnsembleMode();
    const juce::String name = drums ? juce::String ("Drum Set") : ens ? juce::String ("Ensemble") : juce::String (session.edit().name());
    if (nameEditor.getText().trimEnd() != name)
        nameEditor.setText (name, juce::dontSendNotification);
    nameEditor.setReadOnly (drums || ens);
    nameEditor.setTooltip (drums ? "Drum sets have no name on the MIDIbox FM"
                           : ens ? "Ensembles have no name on the MIDIbox FM"
                                 : "Patch name (16 characters)");
    nameLabel.setText (drums ? "Kit" : ens ? "Ens." : "Patch", juce::dontSendNotification);
    const int selected = drums ? fm::kDrumPart : ens ? fm::kEnsemblePart : session.getEditVoice();
    for (int v = 0; v < voiceButtons.size(); ++v)
        voiceButtons[v]->setToggleState (v == selected, juce::dontSendNotification);
    for (int v = 0; v < fm::kVoices; ++v)
        voiceButtons[v]->setTooltip ("Edit voice " + juce::String (v + 1) + " (MIDI channel "
                                     + juce::String (session.voiceChannel (v)) + " in the ensemble)");
    const int s = session.editorSourceSlot();
    sourceLabel.setText (s >= 0 ? "from " + session.currentSlotName (s) : juce::String(), juce::dontSendNotification);

    newButton.setButtonText (drums ? "Init Kit" : ens ? "Init Ens." : "Init Patch");
    newButton.setTooltip (drums ? "Replace the drum set with the init kit (preset drum set 1)"
                          : ens ? "Replace the ensemble with the init ensemble (voices on channels 1-4, full keyboard)"
                                : "Replace the edited voice's patch with the init patch");
    savePatchButton.setButtonText (drums ? "Save Kit .syx" : ens ? "Save Ens. .syx" : "Save Patch .syx");
    randomButton.setTooltip (drums ? "Random generator: roll a new kit, or just the bass drum, hi-hats, envelopes, ..."
                                   : "Random generator: roll new operators, LFOs, EG5 or wavetable (like RND on the box)");
    randomButton.setVisible (! ens);
    if (drumEditor != nullptr && voiceEditor != nullptr && ensembleEditor != nullptr)
    {
        drumEditor->setVisible (drums);
        ensembleEditor->setVisible (ens);
        voiceEditor->setVisible (! drums && ! ens);
    }
}

void FmControlEditor::editChanged (const std::vector<uint16_t>& addresses)
{
    for (auto a : addresses)
        if (a < fm::kNameLength && ! nameEditor.hasKeyboardFocus (true)) { refreshHeader(); return; }
}

void FmControlEditor::statusChanged()
{
    statusLabel.setText (session.getStatus(), juce::dontSendNotification);
    progressValue = session.getProgress();
    const bool busy = session.isBusy();
    progressBar.setVisible (busy && progressValue >= 0.0);
    cancelButton.setVisible (busy);
}

void FmControlEditor::timerCallback()
{
    proc.editorWidth = getWidth();
    proc.editorHeight = getHeight();
    if (cancelButton.isVisible() != session.isBusy())
        statusChanged();
}

void FmControlEditor::savePatch()
{
    auto name = juce::File::createLegalFileName (session.isDrumMode()       ? juce::String ("Drum Set")
                                                 : session.isEnsembleMode() ? juce::String ("Ensemble")
                                                                            : juce::String (session.edit().name()).trim());
    if (name.isEmpty()) name = "Patch";
    chooser = std::make_unique<juce::FileChooser> (
        "Save patch", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name + ".syx"), "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              juce::String msg;
                              session.savePatchSyx (f.withFileExtension ("syx"), msg);
                              statusLabel.setText (msg, juce::dontSendNotification);
                          });
}

void FmControlEditor::showLog()
{
    auto* text = new juce::TextEditor();
    text->setMultiLine (true);
    text->setReadOnly (true);
    text->setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    text->setText (session.getLog().joinIntoString ("\n"));
    text->setSize (620, 360);
    text->moveCaretToEnd();
    juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (text), logButton.getScreenBounds(), nullptr);
}

void FmControlEditor::paint (juce::Graphics& g)
{
    // Text-mode desktop: a dithered ░ shade behind the windows.
    g.fillAll (colours::background);
    fillDots (g, getLocalBounds(), {}, colours::panelEdge.withAlpha (0.22f), 4, 4, { { 0, 0 }, { 2, 2 } });

    // Status line: the classic cyan bar at the bottom of the screen.
    auto status = getLocalBounds().removeFromBottom (28);
    g.setColour (colours::text);
    g.fillRect (status);
}

void FmControlEditor::resized()
{
    auto r = getLocalBounds();
    connectionBar.setBounds (r.removeFromTop (40));

    auto status = r.removeFromBottom (28).reduced (8, 4);
    logButton.setBounds (status.removeFromRight (44));
    status.removeFromRight (6);
    cancelButton.setBounds (status.removeFromRight (64));
    status.removeFromRight (6);
    progressBar.setBounds (status.removeFromRight (200));
    statusLabel.setBounds (status);

    library.setBounds (r.removeFromLeft (280));

    auto main = r.reduced (8, 6);
    auto header = main.removeFromTop (30);
    nameLabel.setBounds (header.removeFromLeft (44));
    nameEditor.setBounds (header.removeFromLeft (200));
    header.removeFromLeft (10);
    voiceLabel.setBounds (header.removeFromLeft (46));
    header.removeFromLeft (6);
    for (auto* b : voiceButtons)
        b->setBounds (header.removeFromLeft (b->getButtonText().length() > 1 ? 40 : 34).reduced (0, 2));
    header.removeFromLeft (12);
    sourceLabel.setBounds (header.removeFromLeft (90));
    savePatchButton.setBounds (header.removeFromRight (120).reduced (0, 2));
    header.removeFromRight (6);
    openButton.setBounds (header.removeFromRight (84).reduced (0, 2));
    header.removeFromRight (6);
    newButton.setBounds (header.removeFromRight (90).reduced (0, 2));
    header.removeFromRight (6);
    randomButton.setBounds (header.removeFromRight (90).reduced (0, 2));
    main.removeFromTop (6);
    voiceEditor->setBounds (main);
    drumEditor->setBounds (main);
    ensembleEditor->setBounds (main);
}
