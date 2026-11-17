#include "SyxFile.h"

namespace fm::syx
{
ReadResult read (const uint8_t* data, size_t size)
{
    ReadResult r;
    size_t start = size;
    for (size_t i = 0; i < size; ++i)
    {
        if (data[i] == 0xf0)
            start = i;
        else if (data[i] == 0xf7 && start < i)
        {
            const auto m = sysex::parse (data + start, i - start + 1);
            if (m && m->kind == sysex::Message::Kind::PatchDump)
            {
                Entry e;
                e.bank = m->bank;
                e.patchNumber = m->patchNumber;
                e.voiceRam = m->isVoiceRam();
                e.drums = m->isDrums();
                e.drumsRam = m->isDrumsRam();
                e.checksumOk = m->checksumOk;
                e.patch = m->patch;
                r.patches.push_back (e);
            }
            else
            {
                ++r.otherMessages;
            }
            start = size;
        }
    }
    return r;
}

sysex::Bytes write (const std::vector<Entry>& entries, uint8_t device)
{
    sysex::Bytes out;
    for (auto& e : entries)
    {
        auto m = e.drums ? sysex::writeDrums (device, e.bank, e.patchNumber, e.patch)
                         : sysex::writeStored (device, e.bank, e.patchNumber, e.patch);
        out.insert (out.end(), m.begin(), m.end());
    }
    return out;
}

sysex::Bytes writeVoicePatch (const Patch& p, uint8_t device)
{
    return sysex::writeVoice (device, 0, p);
}

sysex::Bytes writeDrumSet (const Patch& p, uint8_t device)
{
    return sysex::writeDrumsRam (device, p);
}
} // namespace fm::syx
