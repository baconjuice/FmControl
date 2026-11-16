#include "PacedSysEx.h"

#include <chrono>
#include <thread>

#if defined(__APPLE__)
 #include <CoreMIDI/CoreMIDI.h>

struct PacedSysEx::Impl
{
    MIDIClientRef client = 0;
    MIDIPortRef port = 0;
    MIDIEndpointRef endpoint = 0;
};

PacedSysEx::PacedSysEx() : impl (std::make_unique<Impl>()) {}
PacedSysEx::~PacedSysEx() { close(); }

bool PacedSysEx::open (const std::string& identifier)
{
    close();
    MIDIUniqueID uid = 0;
    try { uid = (MIDIUniqueID) std::stol (identifier); }
    catch (...) { return false; }

    MIDIObjectRef obj = 0;
    MIDIObjectType type {};
    if (MIDIObjectFindByUniqueID (uid, &obj, &type) != noErr || type != kMIDIObjectType_Destination)
        return false;
    impl->endpoint = (MIDIEndpointRef) obj;

    if (MIDIClientCreate (CFSTR ("PacedSysEx"), nullptr, nullptr, &impl->client) != noErr)
        return false;
    if (MIDIOutputPortCreate (impl->client, CFSTR ("PacedSysEx out"), &impl->port) != noErr)
    {
        close();
        return false;
    }
    return true;
}

void PacedSysEx::close()
{
    if (impl->port != 0) MIDIPortDispose (impl->port);
    if (impl->client != 0) MIDIClientDispose (impl->client);
    impl->port = 0;
    impl->client = 0;
    impl->endpoint = 0;
}

bool PacedSysEx::isOpen() const { return impl->port != 0 && impl->endpoint != 0; }

bool PacedSysEx::send (const std::vector<uint8_t>& bytes, int chunkSize, int gapMs)
{
    if (! isOpen() || bytes.empty())
        return false;
    if (chunkSize <= 0)
        chunkSize = (int) bytes.size();

    for (size_t i = 0; i < bytes.size(); i += (size_t) chunkSize)
    {
        const auto n = std::min ((size_t) chunkSize, bytes.size() - i);
        alignas (MIDIPacketList) uint8_t storage[1024];
        auto* list = reinterpret_cast<MIDIPacketList*> (storage);
        auto* packet = MIDIPacketListInit (list);
        packet = MIDIPacketListAdd (list, sizeof (storage), packet, 0, (ByteCount) n, bytes.data() + i);
        if (packet == nullptr || MIDISend (impl->port, impl->endpoint, list) != noErr)
            return false;
        if (gapMs > 0 && i + n < bytes.size())
            std::this_thread::sleep_for (std::chrono::milliseconds (gapMs));
    }
    return true;
}

#else

struct PacedSysEx::Impl {};
PacedSysEx::PacedSysEx() : impl (std::make_unique<Impl>()) {}
PacedSysEx::~PacedSysEx() = default;
bool PacedSysEx::open (const std::string&) { return false; }
void PacedSysEx::close() {}
bool PacedSysEx::isOpen() const { return false; }
bool PacedSysEx::send (const std::vector<uint8_t>&, int, int) { return false; }

#endif
