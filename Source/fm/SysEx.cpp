#include "SysEx.h"

namespace fm::sysex
{
namespace
{
constexpr uint8_t kHeader[] = { 0xf0, 0x00, 0x00, 0x7e, 0x49 };

Bytes header (uint8_t device, uint8_t command)
{
    Bytes b (std::begin (kHeader), std::end (kHeader));
    b.push_back (device & 0x7f);
    b.push_back (command);
    return b;
}

Bytes dump (uint8_t device, uint8_t t, uint8_t bank, uint8_t patch, const Patch& p)
{
    auto b = header (device, 0x02);
    b.insert (b.end(), { t, (uint8_t) (bank & 0x07), (uint8_t) (patch & 0x7f) });
    for (auto v : p.data)
        b.push_back (v & 0x7f);
    b.push_back (checksum (p.data.data(), p.data.size()));
    b.push_back (0xf7);
    return b;
}
} // namespace

uint8_t checksum (const uint8_t* data, size_t count)
{
    unsigned sum = 0;
    for (size_t i = 0; i < count; ++i)
        sum += data[i] & 0x7f;
    return (uint8_t) ((0x80 - (sum & 0x7f)) & 0x7f);
}

Bytes requestStored (uint8_t device, uint8_t bank, uint8_t patch)
{
    auto b = header (device, 0x01);
    b.insert (b.end(), { type::Stored, (uint8_t) (bank & 0x07), (uint8_t) (patch & 0x7f), 0xf7 });
    return b;
}

Bytes requestVoice (uint8_t device, int voice)
{
    auto b = header (device, 0x01);
    b.insert (b.end(), { (uint8_t) (type::VoiceRam | (voice & 3)), 0x00, 0x00, 0xf7 });
    return b;
}

Bytes writeStored (uint8_t device, uint8_t bank, uint8_t patch, const Patch& p)
{
    return dump (device, type::Stored, bank, patch, p);
}

Bytes writeVoice (uint8_t device, int voice, const Patch& p)
{
    return dump (device, (uint8_t) (type::VoiceRam | (voice & 3)), 0, 0, p);
}

Bytes parameterWrite (uint8_t device, int voice, uint8_t addr, uint8_t value)
{
    auto b = header (device, 0x06);
    b.insert (b.end(), { (uint8_t) (type::VoiceRam | (voice & 3)), (uint8_t) (addr >> 7), (uint8_t) (addr & 0x7f),
                         (uint8_t) (value & 0x7f), 0xf7 });
    return b;
}

Bytes storeVoice (uint8_t device, int voice)
{
    auto b = header (device, 0x0a);
    b.insert (b.end(), { (uint8_t) (voice & 3), 0xf7 });
    return b;
}

Bytes ping (uint8_t device)
{
    auto b = header (device, 0x0f);
    b.push_back (0xf7);
    return b;
}

std::optional<Message> parse (const uint8_t* data, size_t size)
{
    if (size < 8 || data[0] != 0xf0 || data[size - 1] != 0xf7)
        return std::nullopt;
    for (size_t i = 1; i < 5; ++i)
        if (data[i] != kHeader[i])
            return std::nullopt;

    Message m;
    m.device = data[5];
    m.command = data[6];

    switch (m.command)
    {
        case 0x0f:
            m.kind = Message::Kind::Ack;
            return m;
        case 0x0e:
            m.kind = Message::Kind::Error;
            m.errorCode = size > 8 ? data[7] : 0;
            return m;
        case 0x02:
        {
            // 02 <type> <bank> <patch> <256 bytes> <checksum> F7
            if (size != 7 + 3 + kPatchSize + 2)
                return m;
            m.type = data[7];
            m.bank = data[8];
            m.patchNumber = data[9];
            if ((m.type & 0xf4) != 0x00) // voice types 00-03 / 08-0B only
                return m;
            const uint8_t* body = data + 10;
            for (int i = 0; i < kPatchSize; ++i)
                m.patch.data[(size_t) i] = body[i] & 0x7f;
            m.checksumOk = checksum (body, kPatchSize) == data[10 + kPatchSize];
            m.kind = Message::Kind::PatchDump;
            return m;
        }
        default:
            return m;
    }
}

const char* errorText (uint8_t code)
{
    switch (code)
    {
        case 0x01: return "received fewer bytes than expected";
        case 0x03: return "wrong checksum";
        case 0x0a: return "BankStick or patch not available";
        case 0x0b: return "parameter not available";
        default:   return "unknown error";
    }
}
} // namespace fm::sysex
