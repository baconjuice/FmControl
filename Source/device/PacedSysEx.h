#pragma once

// Sends long SysEx messages in small pieces with a pause between them.
// Some USB MIDI interfaces (and the MIDIbox FM's input) lose bytes when a
// 268-byte patch dump arrives at full speed; pacing it fixes that.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class PacedSysEx
{
public:
    PacedSysEx();
    ~PacedSysEx();

    // identifier: the JUCE MIDI output identifier (the CoreMIDI unique ID on macOS).
    bool open (const std::string& identifier);
    void close();
    bool isOpen() const;

    // Blocking: sends the bytes in chunks of chunkSize, sleeping gapMs between them.
    bool send (const std::vector<uint8_t>& bytes, int chunkSize, int gapMs);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
