#include "Patch.h"
#include "Params.h"

#include <algorithm>
#include <cstring>

namespace fm
{
extern const uint8_t kInitPatch[kPatchSize];

Patch Patch::makeInit()
{
    Patch p;
    std::memcpy (p.data.data(), kInitPatch, kPatchSize);
    return p;
}

std::string Patch::name() const
{
    std::string s;
    for (int i = 0; i < kNameLength; ++i)
    {
        auto c = (char) data[(size_t) i];
        s += (c >= 0x20 && c < 0x7f) ? c : ' ';
    }
    while (! s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

void Patch::setName (std::string_view newName)
{
    for (int i = 0; i < kNameLength; ++i)
    {
        char c = i < (int) newName.size() ? newName[(size_t) i] : ' ';
        if (c < 0x20 || c >= 0x7f) c = ' ';
        data[(size_t) i] = (uint8_t) c;
    }
}

int getRawValue (const uint8_t* data, size_t size, const ParamDef& p)
{
    const int mask = (1 << p.bits) - 1;
    return (data[(size_t) p.addr % size] >> p.shift) & mask;
}

std::vector<uint16_t> setRawValue (uint8_t* data, size_t size, const ParamDef& p, int raw)
{
    raw = std::clamp (raw, 0, p.maxRaw());
    std::vector<uint16_t> changed;
    auto& b = data[(size_t) p.addr % size];
    const int mask = ((1 << p.bits) - 1) << p.shift;
    const auto v = (uint8_t) (((b & ~mask) | ((raw << p.shift) & mask)) & 0x7f);
    if (v != b)
    {
        b = v;
        changed.push_back (p.addr);
    }
    return changed;
}
} // namespace fm
