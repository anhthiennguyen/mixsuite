#pragma once
#include <JuceHeader.h>

class MixSuiteProcessor;

// Singleton — every VisualEQ instance registers here so all editors can
// see every track's spectrum and compute inter-track muddiness.
class SharedAnalyserState : public juce::DeletedAtShutdown
{
public:
    JUCE_DECLARE_SINGLETON (SharedAnalyserState, false)

    static constexpr int kMaxTracks = 8;

    // Called from MixSuiteProcessor constructor / destructor (message thread)
    int  registerProcessor   (MixSuiteProcessor* proc);
    void unregisterProcessor (int slot);

    // Returns a snapshot of all processor pointers (null = empty slot).
    // Safe to call from the message thread.
    std::array<MixSuiteProcessor*, kMaxTracks> getProcessors() const;

    static juce::Colour trackColour (int slot);

private:
    SharedAnalyserState();

    struct Slot { MixSuiteProcessor* proc = nullptr; bool occupied = false; };
    std::array<Slot, kMaxTracks> slots_;
    mutable juce::CriticalSection lock_;
};
