#include "PluginProcessor.h"
#include "PluginEditor.h"

FmControlProcessor::FmControlProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

FmControlProcessor::~FmControlProcessor()
{
    aliveFlag.reset();
}

bool FmControlProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void FmControlProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Keyboard (standalone "Active MIDI Inputs") or DAW track -> MIDIbox FM.
    device.forwardMidi (midi);
    buffer.clear();
    midi.clear();
}

juce::AudioProcessorEditor* FmControlProcessor::createEditor()
{
    return new FmControlEditor (*this);
}

void FmControlProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto v = session.toValueTree();
    v.appendChild (automation.toValueTree(), nullptr);
    v.setProperty ("editorWidth", editorWidth, nullptr);
    v.setProperty ("editorHeight", editorHeight, nullptr);
    if (auto xml = v.createXml())
        copyXmlToBinary (*xml, destData);
}

void FmControlProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;
    auto v = juce::ValueTree::fromXml (*xml);
    editorWidth = v.getProperty ("editorWidth", editorWidth);
    editorHeight = v.getProperty ("editorHeight", editorHeight);

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        session.fromValueTree (v);
        automation.fromValueTree (v.getChildWithName ("Automation"));
        return;
    }
    juce::MessageManager::callAsync ([this, v, alive = std::weak_ptr<bool> (aliveFlag)]
    {
        if (! alive.expired())
        {
            session.fromValueTree (v);
            automation.fromValueTree (v.getChildWithName ("Automation"));
        }
    });
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FmControlProcessor();
}
