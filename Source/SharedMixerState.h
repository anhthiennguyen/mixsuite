#pragma once
#include <JuceHeader.h>
#include "TrackState.h"
#include "SpectrumAnalyser.h"
#include "StereoScope.h"

class MixSuiteProcessor;

// Singleton that holds all track positions so every plugin instance
// can read and draw the full picture. Each instance gets a slot (0-7).
// All methods called from the UI (message) thread only.
class SharedMixerState : public juce::DeletedAtShutdown
{
public:
    JUCE_DECLARE_SINGLETON (SharedMixerState, false)

    // Each PluginProcessor calls this on construction; returns its slot index.
    // Returns -1 if all 8 slots are taken.
    int  registerProcessor   (MixSuiteProcessor* proc, const TrackState& initialState);
    void unregisterProcessor (int slot);

    // Read snapshot of all states for canvas drawing (UI thread)
    std::array<TrackState, kMaxTracks> getAllStates() const;

    // Canvas calls these when the user drags any track.
    // Forwards the change to the owning processor (which pushes to its DSP engine)
    // and refreshes the cached state so all canvases repaint correctly.
    void setPosition (int slot, float normX, float normY);
    void setHeight   (int slot, float normHeight);
    void setLabel    (int slot, const juce::String& label);
    void setPriority (int slot, int priority);
    void setMode     (int slot, TrackState::Mode mode);

    // Called by a processor to push its full state (e.g. after setStateInformation)
    void pushState (int slot, const TrackState& state);

    // Returns nullptr if slot is unoccupied (message thread only)
    SpectrumAnalyser* getSpectrumAnalyser (int slot);
    StereoScope*      getStereoScope      (int slot);

    // Per-slot colours
    static juce::Colour trackColour (int slot);

private:
    SharedMixerState();

    struct SlotData
    {
        TrackState             state;
        MixSuiteProcessor* processor = nullptr;
        bool                   occupied  = false;
    };

    std::array<SlotData, kMaxTracks> slots_;
    mutable juce::CriticalSection    lock_;

    // Called after any position/height change.
    // Computes base DSP params for all tracks, resolves overlaps,
    // then pushes corrected params to each processor's DSP engine.
    // Must be called with lock_ NOT held (it acquires the lock internally).
    void resolveAndPush();

    // Returns the primary band index for a given normY
    static int primaryBand (float normY);
};
