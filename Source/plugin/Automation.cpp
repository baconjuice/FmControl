#include "Automation.h"
#include "fm/Drums.h"
#include "fm/Params.h"

#include <cmath>

namespace
{
const fm::ParamTable& tableFor (int voice) { return voice == Automation::kDrums ? fm::drumParams() : fm::voiceParams(); }
} // namespace

// ---------------------------------------------------------------------------
class Automation::SlotParameter : public juce::AudioParameterFloat
{
public:
    SlotParameter (Automation& a, int i)
        : juce::AudioParameterFloat (juce::ParameterID { "slot" + juce::String (i + 1), 1 },
                                     "Slot " + juce::String (i + 1), juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                     juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                         [&a, i] (float v, int) { return a.valueText (i, v); })),
          owner (a), index (i)
    {
    }

    juce::String getName (int maximumLength) const override
    {
        return owner.slotLabel (index).substring (0, maximumLength);
    }

    std::atomic<bool> dirty { false };

private:
    void valueChanged (float) override { dirty = true; }

    Automation& owner;
    int index;
};

// ---------------------------------------------------------------------------
Automation::Automation (juce::AudioProcessor& p, Session& s) : processor (p), session (s), slots (defaults())
{
    for (int i = 0; i < kSlots; ++i)
    {
        auto param = std::make_unique<SlotParameter> (*this, i);
        params[(size_t) i] = param.get();
        processor.addParameter (param.release());
    }
    session.addListener (this);
    session.getDrumTarget().addTargetListener (this);
    session.setAutomation (this);
    editReplaced();
    startTimer (15);
}

Automation::~Automation()
{
    stopTimer();
    session.setAutomation (nullptr);
    session.getDrumTarget().removeTargetListener (this);
    session.removeListener (this);
}

std::array<Automation::Assignment, Automation::kSlots> Automation::defaults()
{
    return { { { 0, "op1.volume" }, { 0, "op2.volume" }, { 0, "op3.volume" }, { 0, "op4.volume" },
               { 0, "feedback" }, { 0, "op1.mult" }, { 0, "op3.mult" }, { 0, "op1.decay" },
               { 0, "op3.decay" }, { 0, "lfo1.rate" }, { 0, "lfo1.pitch" }, { 0, "lfo2.rate" },
               { 0, "eg5.attack" }, { 0, "eg5.decay1" }, { 0, "wt.rate" }, { 0, "finetune" },
               { kDrums, "bd.freq" }, { kDrums, "bd.decr" }, { kDrums, "bdc.decay" }, { kDrums, "sd.decay" },
               { kDrums, "hhc.decay" }, { kDrums, "hh.freq" }, { kDrums, "tom.freq" }, { kDrums, "cym.decay" } } };
}

Automation::Assignment Automation::assignment (int slot) const
{
    const juce::ScopedLock sl (lock);
    return slots[(size_t) slot];
}

const fm::ParamDef* Automation::defFor (int slot) const
{
    const auto a = assignment (slot);
    return a.paramId.empty() ? nullptr : tableFor (a.voice).find (a.paramId);
}

int Automation::currentRaw (const Assignment& a, const fm::ParamDef& d) const
{
    return a.voice == kDrums ? session.drums().getRaw (d) : session.getVoiceParam (a.voice, d);
}

int Automation::slotFor (int voice, const std::string& paramId) const
{
    const juce::ScopedLock sl (lock);
    for (int i = 0; i < kSlots; ++i)
        if (slots[(size_t) i].voice == voice && slots[(size_t) i].paramId == paramId)
            return i;
    return -1;
}

juce::String Automation::slotLabel (int slot) const
{
    const auto a = assignment (slot);
    const auto* d = defFor (slot);
    juce::String label = juce::String (slot + 1) + ": ";
    if (d == nullptr)
        return label + (isDrumSlot (slot) ? "(unused drum slot)" : "(unused)");
    if (a.voice == kDrums)
    {
        // "bd.freq" -> "DR BD Pitch", "sd.decay" -> "DR SD Decay".
        const juce::String id (d->id);
        return label + "DR " + id.upToFirstOccurrenceOf (".", false, false).toUpperCase() + " " + juce::String (d->name);
    }

    // "op3.volume" on voice 1 -> "V1 OP3 Volume", "wt.rate" -> "V1 WT Rate".
    const juce::String id (d->id), name (d->name);
    juce::String prefix;
    if (id.contains ("."))
    {
        const auto head = id.upToFirstOccurrenceOf (".", false, false);
        if (head == "vel") prefix = "Velocity ";
        else if (head == "at") prefix = "Aftertouch ";
        else if (head == "mw") prefix = "ModWheel ";
        else if (! name.startsWithIgnoreCase (head)) prefix = head.toUpperCase() + " ";
    }
    return label + "V" + juce::String (a.voice + 1) + " " + prefix + name;
}

juce::String Automation::valueText (int slot, float v) const
{
    const auto* d = defFor (slot);
    if (d == nullptr)
        return "-";
    const int raw = juce::roundToInt (v * (float) d->maxRaw());
    if (! d->choices.empty() && raw < (int) d->choices.size())
        return d->choices[(size_t) raw];
    return juce::String (raw - d->offset);
}

void Automation::assign (int slot, int voice, const std::string& paramId, uint8_t)
{
    if (slot < 0 || slot >= kSlots || voice < 0 || voice > kDrums || ! slotAccepts (slot, voice))
        return;
    {
        const juce::ScopedLock sl (lock);
        for (auto& a : slots)
            if (a.voice == voice && a.paramId == paramId)
                a.paramId.clear(); // the slot stays a voice or drum slot
        slots[(size_t) slot] = { voice, paramId };
    }
    assignmentsChanged();
}

void Automation::unassign (int voice, const std::string& paramId)
{
    {
        const juce::ScopedLock sl (lock);
        for (auto& a : slots)
            if (a.voice == voice && a.paramId == paramId)
                a.paramId.clear(); // the slot stays a voice or drum slot
    }
    assignmentsChanged();
}

void Automation::resetToDefaults (int voice)
{
    {
        const juce::ScopedLock sl (lock);
        const auto d = defaults();
        for (int i = 0; i < kSlots; ++i)
            if (slotAccepts (i, voice))
                slots[(size_t) i] = d[(size_t) i];
    }
    assignmentsChanged();
}

void Automation::assignmentsChanged()
{
    for (int i = 0; i < kSlots; ++i)
        pushToHost (i);
    processor.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withParameterInfoChanged (true));
    notify();
}

void Automation::beginGesture (int voice, const std::string& paramId)
{
    if (const int slot = slotFor (voice, paramId); slot >= 0 && params[(size_t) slot] != nullptr)
        params[(size_t) slot]->beginChangeGesture();
}

void Automation::endGesture (int voice, const std::string& paramId)
{
    if (const int slot = slotFor (voice, paramId); slot >= 0 && params[(size_t) slot] != nullptr)
        params[(size_t) slot]->endChangeGesture();
}

void Automation::pushToHost (int slot)
{
    auto* p = params[(size_t) slot];
    const auto a = assignment (slot);
    const auto* d = defFor (slot);
    if (p == nullptr || d == nullptr || d->maxRaw() <= 0)
        return;
    const float v = (float) currentRaw (a, *d) / (float) d->maxRaw();
    if (std::abs (p->get() - v) > 1.0e-6f)
    {
        p->setValueNotifyingHost (v);
        p->dirty = false; // our own value, nothing to apply
    }
}

void Automation::editReplaced()
{
    // A voice got a new patch (or the editor switched voices): report all.
    for (int i = 0; i < kSlots; ++i)
        pushToHost (i);
}

void Automation::editChanged (const std::vector<uint16_t>& addresses)
{
    changedFor (session.getEditVoice(), addresses);
}

void Automation::targetChanged (const std::vector<uint16_t>& addresses)
{
    changedFor (kDrums, addresses);
}

void Automation::changedFor (int voice, const std::vector<uint16_t>& addresses)
{
    if (applyingFromHost)
        return;
    for (int i = 0; i < kSlots; ++i)
    {
        const auto a = assignment (i);
        if (a.voice != voice)
            continue;
        if (const auto* d = defFor (i))
            if (std::find (addresses.begin(), addresses.end(), d->addr) != addresses.end())
                pushToHost (i);
    }
}

void Automation::timerCallback()
{
    for (int i = 0; i < kSlots; ++i)
    {
        auto* p = params[(size_t) i];
        if (p == nullptr || ! p->dirty.exchange (false))
            continue;
        const auto a = assignment (i);
        const auto* d = defFor (i);
        if (d == nullptr)
            continue;
        const int raw = juce::jlimit (0, d->maxRaw(), juce::roundToInt (p->get() * (float) d->maxRaw()));
        if (raw == currentRaw (a, *d))
            continue;
        const juce::ScopedValueSetter<bool> svs (applyingFromHost, true);
        if (a.voice == kDrums)
            session.setDrumParam (*d, raw);
        else
            session.setVoiceParam (a.voice, *d, raw);
    }
}

juce::ValueTree Automation::toValueTree() const
{
    juce::ValueTree v ("Automation");
    const juce::ScopedLock sl (lock);
    for (int i = 0; i < kSlots; ++i)
    {
        juce::ValueTree c ("Slot");
        c.setProperty ("slot", i, nullptr);
        c.setProperty ("voice", slots[(size_t) i].voice, nullptr);
        c.setProperty ("param", juce::String (slots[(size_t) i].paramId), nullptr);
        v.appendChild (c, nullptr);
    }
    return v;
}

void Automation::fromValueTree (const juce::ValueTree& v)
{
    if (! v.isValid())
        return;
    {
        const juce::ScopedLock sl (lock);
        for (const auto& c : v)
        {
            const int i = c.getProperty ("slot", -1), voice = c.getProperty ("voice", 0);
            if (i < 0 || i >= kSlots || voice < 0 || voice > kDrums)
                continue;
            const auto id = c.getProperty ("param").toString().toStdString();
            if (! id.empty() && tableFor (voice).find (id) == nullptr)
                continue;
            if (id.empty())
            {
                slots[(size_t) i] = { isDrumSlot (i) ? kDrums : 0, {} };
                continue;
            }
            if (! slotAccepts (i, voice))
                continue;
            slots[(size_t) i] = { voice, id };
        }
    }
    assignmentsChanged();
}
