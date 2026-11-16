#include "FmDevice.h"

using namespace fm;

namespace
{
constexpr int kTimerMs = 10;
constexpr int kParamsPerTick = 1; // one 12-byte write per 10 ms, gentle on the FM's input
// Long SysEx goes out in small pieces: the FM loses bytes of a patch dump
// that arrives faster than about one byte per 2.5 ms (tested on hardware).
constexpr int kPaceChunk = 4;
constexpr int kPaceGapMs = 10;
} // namespace

FmDevice::FmDevice()
{
    senderThread = std::thread ([this] { senderLoop(); });
    startTimer (kTimerMs);
}

FmDevice::~FmDevice()
{
    stopTimer();
    closeInput(); // stop callbacks before members go away
    {
        const std::lock_guard<std::mutex> lock (queueMutex);
        stopSender = true;
    }
    queueCv.notify_all();
    senderThread.join();
    closeOutput();
}

void FmDevice::writeOut (std::vector<uint8_t> bytes)
{
    if (bytes.empty())
        return;
    {
        const std::lock_guard<std::mutex> lock (queueMutex);
        senderBusy = true;
        outQueue.push_back (std::move (bytes));
    }
    queueCv.notify_one();
}

void FmDevice::senderLoop()
{
    for (;;)
    {
        std::vector<uint8_t> bytes;
        {
            std::unique_lock<std::mutex> lock (queueMutex);
            queueCv.wait (lock, [this] { return stopSender.load() || ! outQueue.empty(); });
            if (stopSender)
                return;
            bytes = std::move (outQueue.front());
            outQueue.pop_front();
        }

        const bool longSysEx = bytes.size() > 16 && bytes[0] == 0xf0;
        bool sent = false;
        {
            const std::lock_guard<std::mutex> lock (pacedMutex);
            if (paced.isOpen())
                sent = paced.send (bytes, longSysEx ? kPaceChunk : 0, longSysEx ? kPaceGapMs : 0);
        }
        if (! sent)
        {
            // No CoreMIDI sender (other platforms): fall back to JUCE.
            const juce::SpinLock::ScopedLockType lock (outputLock);
            if (output != nullptr)
                output->sendMessageNow (juce::MidiMessage (bytes.data(), (int) bytes.size()));
        }

        lastSendDoneAt = juce::Time::getMillisecondCounter();
        const std::lock_guard<std::mutex> lock (queueMutex);
        if (outQueue.empty())
            senderBusy = false;
    }
}

// ---------------------------------------------------------------------------
// Ports
// ---------------------------------------------------------------------------
bool FmDevice::openInput (const juce::String& identifier)
{
    closeInput();
    if (identifier.isEmpty())
        return false;

    input = juce::MidiInput::openDevice (identifier, this);
    if (input == nullptr)
    {
        log ("Could not open MIDI input");
        listeners.call ([] (Listener& l) { l.fmPortsChanged(); });
        return false;
    }
    input->start();
    inputId = identifier;
    log ("MIDI in: " + input->getName());
    listeners.call ([] (Listener& l) { l.fmPortsChanged(); });
    return true;
}

bool FmDevice::openOutput (const juce::String& identifier)
{
    closeOutput();
    if (identifier.isEmpty())
        return false;

    auto opened = juce::MidiOutput::openDevice (identifier);
    {
        const juce::SpinLock::ScopedLockType lock (outputLock);
        output = std::move (opened);
    }
    if (output == nullptr)
    {
        log ("Could not open MIDI output");
        listeners.call ([] (Listener& l) { l.fmPortsChanged(); });
        return false;
    }
    {
        const std::lock_guard<std::mutex> lock (pacedMutex);
        if (! paced.open (identifier.toStdString()))
            log ("Paced SysEx sender unavailable; sending dumps at full speed");
    }
    outputId = identifier;
    log ("MIDI out: " + output->getName());
    listeners.call ([] (Listener& l) { l.fmPortsChanged(); });
    return true;
}

void FmDevice::closeInput()
{
    if (input != nullptr)
        input->stop();
    input.reset();
    inputId = {};
}

void FmDevice::closeOutput()
{
    {
        const std::lock_guard<std::mutex> lock (pacedMutex);
        paced.close();
    }
    std::unique_ptr<juce::MidiOutput> old;
    {
        const juce::SpinLock::ScopedLockType lock (outputLock);
        old = std::move (output);
    }
    old.reset();
    outputId = {};
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------
void FmDevice::send (const sysex::Bytes& message)
{
    if (output == nullptr || message.size() < 2)
        return;

    // Remember outgoing dumps so an echo from the box's MIDI merger is ignored.
    if (auto m = sysex::parse (message.data(), message.size()); m && m->kind == sysex::Message::Kind::PatchDump)
    {
        recentlySentDumps.push_back ({ m->patch, juce::Time::getMillisecondCounter() });
        while (recentlySentDumps.size() > 4)
            recentlySentDumps.pop_front();
    }

    writeOut (message);
    listeners.call ([] (Listener& l) { l.fmActivity (true); });
}

void FmDevice::sendMidi (const juce::MidiMessage& m)
{
    if (output != nullptr)
        writeOut ({ m.getRawData(), m.getRawData() + m.getRawDataSize() });
}

void FmDevice::forwardMidi (const juce::MidiBuffer& buffer)
{
    if (buffer.isEmpty())
        return;

    const int channel = keyboardChannel.load();
    const bool learnAll = learning.load();
    bool sent = false;
    for (const auto meta : buffer)
    {
        auto m = meta.getMessage();
        if (m.isSysEx() || m.isActiveSense() || m.isMetaEvent())
            continue;

        // Controllers used by MIDI learn edit the patch instead of reaching
        // the FM directly. Channel mode messages (120..127) always pass.
        if (m.isController() && m.getControllerNumber() < 120)
        {
            const int ch = m.getChannel(), cc = m.getControllerNumber();
            if (learnAll || mappedControllers[(size_t) ((ch - 1) * 128 + cc)].load())
            {
                pushController (ch, cc, m.getControllerValue());
                continue;
            }
        }

        if (channel > 0 && m.getChannel() > 0)
            m.setChannel (channel);
        if (output == nullptr)
            continue;
        if (senderBusy.load())
        {
            // A dump is going out: queue behind it so the note can't break it.
            writeOut ({ m.getRawData(), m.getRawData() + m.getRawDataSize() });
        }
        else
        {
            const juce::SpinLock::ScopedLockType lock (outputLock);
            if (output == nullptr)
                continue;
            output->sendMessageNow (m);
        }
        sent = true;
    }
    if (sent)
        lastOutputActivity = juce::Time::getMillisecondCounter();
}

void FmDevice::setControllerMapped (int channel, int cc, bool mapped)
{
    if (channel >= 1 && channel <= 16 && cc >= 0 && cc < 128)
        mappedControllers[(size_t) ((channel - 1) * 128 + cc)] = mapped;
}

void FmDevice::clearControllerMappings()
{
    for (auto& m : mappedControllers)
        m = false;
}

void FmDevice::pushController (int channel, int cc, int value)
{
    // Audio thread (or MIDI thread): single producer.
    const auto scope = controllerFifo.write (1);
    if (scope.blockSize1 > 0)
        controllerEvents[(size_t) scope.startIndex1] = (uint32_t) (channel << 16 | cc << 8 | value);
}

void FmDevice::drainControllers()
{
    // Only the latest value per controller matters.
    std::map<int, int> latest; // channel << 8 | cc -> value
    std::vector<int> order;
    const auto scope = controllerFifo.read (controllerFifo.getNumReady());
    auto take = [&] (int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto e = controllerEvents[(size_t) i];
            const int key = (int) (e >> 8);
            if (latest.find (key) == latest.end())
                order.push_back (key);
            latest[key] = (int) (e & 0x7f);
        }
    };
    take (scope.startIndex1, scope.blockSize1);
    take (scope.startIndex2, scope.blockSize2);
    for (auto key : order)
        listeners.call ([&] (Listener& l) { l.fmController (key >> 8, key & 0x7f, latest[key]); });
}

void FmDevice::queueParameter (int voice, uint16_t addr, uint8_t value)
{
    pendingParams[{ voice, addr }] = value;
}

void FmDevice::enqueue (sysex::Bytes message, Expect expect, int timeoutMs, Done done)
{
    Job j;
    j.message = std::move (message);
    j.expect = expect;
    j.timeoutMs = timeoutMs;
    j.done = std::move (done);
    jobs.push_back (std::move (j));
    if (jobs.size() == 1)
        startNextJob();
}

void FmDevice::cancelAll()
{
    auto old = std::move (jobs);
    jobs.clear();
    Result r;
    r.error = "Cancelled";
    for (auto& j : old)
        if (j.done) j.done (r);
}

void FmDevice::startNextJob()
{
    while (! jobs.empty())
    {
        auto& j = jobs.front();
        if (j.started)
            return; // already in flight (e.g. queued from a completion callback)

        // Let acks of recent parameter writes arrive first, so they can't be
        // mistaken for the reply to this request. The timer retries.
        if (j.expect != Expect::Nothing
            && (! pendingParams.empty() || juce::Time::getMillisecondCounter() - lastParamSendAt < 60))
            return;

        if (output == nullptr)
        {
            Result r;
            r.error = "No MIDI output selected";
            finishJob (r);
            continue;
        }

        j.started = true;
        j.startedAt = juce::Time::getMillisecondCounter();
        send (j.message);

        if (j.expect == Expect::Nothing)
        {
            Result r;
            r.ok = true;
            finishJob (r);
            continue;
        }
        return; // wait for reply or timeout
    }
}

void FmDevice::finishJob (const Result& r)
{
    if (jobs.empty())
        return;
    auto done = std::move (jobs.front().done);
    jobs.pop_front();
    if (done)
        done (r);
}

// ---------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------
void FmDevice::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (! m.isActiveSense())
        lastInputActivity = juce::Time::getMillisecondCounter();

    if (! m.isSysEx())
        return;

    // Copy the full message (F0..F7) and continue on the message thread.
    std::vector<uint8_t> raw (m.getRawData(), m.getRawData() + m.getRawDataSize());
    juce::WeakReference<FmDevice> self (this);
    juce::MessageManager::callAsync ([self, bytes = std::move (raw)]
    {
        if (auto* d = self.get())
            d->handleSysEx (bytes);
    });
}

bool FmDevice::isOwnEcho (const sysex::Message& m) const
{
    // A merger echo arrives right after we sent it; the same patch coming
    // back later is a genuine reply.
    if (m.kind != fm::sysex::Message::Kind::PatchDump)
        return false;
    const auto now = juce::Time::getMillisecondCounter();
    for (auto& [patch, sentAt] : recentlySentDumps)
        if (patch == m.patch && now - sentAt < 1000)
            return true;
    return false;
}

void FmDevice::handleSysEx (const std::vector<uint8_t>& bytes)
{
    auto msg = sysex::parse (bytes.data(), bytes.size());
    if (! msg)
        return;

    listeners.call ([] (Listener& l) { l.fmActivity (false); });

    using Kind = sysex::Message::Kind;
    const bool waiting = ! jobs.empty() && jobs.front().started && jobs.front().expect != Expect::Nothing;

    // A request in flight claims replies from our device number.
    if (waiting && msg->device == deviceNumber)
    {
        const auto expect = jobs.front().expect;
        Result r;
        r.reply = &*msg;

        if (msg->kind == Kind::Error)
        {
            r.error = juce::String ("MIDIbox FM: ") + sysex::errorText (msg->errorCode);
            finishJob (r);
            startNextJob();
            return;
        }
        const bool isDump = msg->kind == Kind::PatchDump;
        if (expect == Expect::Dump && isDump && ! isOwnEcho (*msg))
        {
            r.ok = msg->checksumOk;
            if (! r.ok) r.error = "Received dump with bad checksum";
            finishJob (r);
            startNextJob();
            return;
        }
        if (expect == Expect::Ack && msg->kind == Kind::Ack)
        {
            r.ok = true;
            finishJob (r);
            startNextJob();
            return;
        }
    }

    if (msg->kind == Kind::PatchDump && ! isOwnEcho (*msg))
    {
        if (msg->checksumOk)
            listeners.call ([&] (Listener& l) { l.fmUnsolicitedDump (*msg); });
        else
            log ("Ignored a dump with bad checksum");
    }
    else if (msg->kind == Kind::Error)
    {
        log (juce::String ("Error from MIDIbox FM: ") + sysex::errorText (msg->errorCode));
    }
}

void FmDevice::timerCallback()
{
    drainControllers();

    const bool requestInFlight = ! jobs.empty() && jobs.front().started && jobs.front().expect != Expect::Nothing;

    // Rate limited live parameter writes (held back while a request waits for its reply).
    for (int i = 0; i < kParamsPerTick && ! requestInFlight && ! senderBusy.load() && ! pendingParams.empty(); ++i)
    {
        auto it = pendingParams.begin();
        const auto [voice, addr] = it->first;
        const auto value = it->second;
        pendingParams.erase (it);
        send (sysex::parameterWrite (deviceNumber, voice, (uint8_t) addr, value));
        lastParamSendAt = juce::Time::getMillisecondCounter();
    }

    if (! jobs.empty() && ! jobs.front().started)
        startNextJob();

    // Request timeouts.
    if (! jobs.empty() && jobs.front().started && jobs.front().expect != Expect::Nothing)
    {
        // The timeout starts once the message has actually gone out
        // (a paced patch dump takes most of a second to send).
        const auto from = juce::jmax (jobs.front().startedAt, lastSendDoneAt.load());
        const auto elapsed = juce::Time::getMillisecondCounter() - from;
        if (! senderBusy.load() && (int) elapsed > jobs.front().timeoutMs)
        {
            Result r;
            r.error = "No reply from MIDIbox FM (timeout)";
            finishJob (r);
            startNextJob();
        }
    }
}

void FmDevice::log (const juce::String& text)
{
    listeners.call ([&] (Listener& l) { l.fmLog (text); });
}
