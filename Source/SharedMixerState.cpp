#include "SharedMixerState.h"
#include "PluginProcessor.h"

JUCE_IMPLEMENT_SINGLETON (SharedMixerState)

SharedMixerState::SharedMixerState() {}

//==============================================================================
int SharedMixerState::registerProcessor (MixSuiteProcessor* proc,
                                          const TrackState& initialState)
{
    int slot = -1;
    {
        juce::ScopedLock lock(lock_);
        for (int i = 0; i < kMaxTracks; ++i)
        {
            if (!slots_[i].occupied)
            {
                slots_[i].processor    = proc;
                slots_[i].state        = initialState;
                slots_[i].state.active = true;
                slots_[i].occupied     = true;
                slot = i;
                break;
            }
        }
    }
    if (slot >= 0) resolveAndPush();
    return slot;
}

void SharedMixerState::unregisterProcessor (int slot)
{
    if (slot < 0 || slot >= kMaxTracks) return;
    {
        juce::ScopedLock lock(lock_);
        slots_[slot].processor       = nullptr;
        slots_[slot].state.active    = false;
        slots_[slot].occupied        = false;
    }
    resolveAndPush();
}

//==============================================================================
std::array<TrackState, kMaxTracks> SharedMixerState::getAllStates() const
{
    juce::ScopedLock lock(lock_);
    std::array<TrackState, kMaxTracks> out;
    for (int i = 0; i < kMaxTracks; ++i)
        out[i] = slots_[i].state;
    return out;
}

//==============================================================================
void SharedMixerState::setPosition (int slot, float normX, float normY)
{
    if (slot < 0 || slot >= kMaxTracks) return;
    {
        juce::ScopedLock lock(lock_);
        if (!slots_[slot].occupied) return;
        slots_[slot].state.normX = normX;
        slots_[slot].state.normY = normY;
    }
    resolveAndPush();
}

void SharedMixerState::setHeight (int slot, float normHeight)
{
    if (slot < 0 || slot >= kMaxTracks) return;
    {
        juce::ScopedLock lock(lock_);
        if (!slots_[slot].occupied) return;
        slots_[slot].state.normHeight = normHeight;
    }
    resolveAndPush();
}

void SharedMixerState::setLabel (int slot, const juce::String& label)
{
    if (slot < 0 || slot >= kMaxTracks) return;
    MixSuiteProcessor* proc = nullptr;
    {
        juce::ScopedLock lock(lock_);
        if (!slots_[slot].occupied) return;
        slots_[slot].state.label = label;
        proc = slots_[slot].processor;
    }
    // Label change doesn't affect DSP — just update the processor's label
    if (proc) proc->setTrackLabel(label);
}

void SharedMixerState::setPriority (int slot, int priority)
{
    if (slot < 0 || slot >= kMaxTracks) return;
    {
        juce::ScopedLock lock(lock_);
        if (!slots_[slot].occupied) return;
        slots_[slot].state.priority = juce::jmax(0, priority);
    }
    resolveAndPush();
}

void SharedMixerState::setMode (int slot, TrackState::Mode mode)
{
    if (slot < 0 || slot >= kMaxTracks) return;
    MixSuiteProcessor* proc = nullptr;
    {
        juce::ScopedLock lock(lock_);
        if (!slots_[slot].occupied) return;
        slots_[slot].state.mode = mode;
        proc = slots_[slot].processor;
    }
    if (proc) proc->setTrackMode(mode);
    resolveAndPush();
}

void SharedMixerState::pushState (int slot, const TrackState& state)
{
    if (slot < 0 || slot >= kMaxTracks) return;
    {
        juce::ScopedLock lock(lock_);
        if (!slots_[slot].occupied) return;
        bool wasActive         = slots_[slot].state.active;
        slots_[slot].state     = state;
        slots_[slot].state.active = wasActive;
    }
    resolveAndPush();
}

//==============================================================================
int SharedMixerState::primaryBand (float normY)
{
    // Y=0 → High (band 4), Y=1 → Low (band 0)
    float frac = (1.0f - normY) * (kNumBands - 1);
    return juce::jlimit(0, kNumBands - 1, (int)std::round(frac));
}

void SharedMixerState::resolveAndPush()
{
    // 1. Snapshot all states and processor pointers under the lock
    std::array<TrackState, kMaxTracks>             states;
    std::array<MixSuiteProcessor*, kMaxTracks> procs;
    {
        juce::ScopedLock lock(lock_);
        for (int i = 0; i < kMaxTracks; ++i)
        {
            states[i] = slots_[i].state;
            procs[i]  = slots_[i].processor;
        }
    }

    // 2. Compute base DSP params for every active track
    std::array<TrackDSPParams, kMaxTracks> dsp;
    for (int i = 0; i < kMaxTracks; ++i)
    {
        if (!states[i].active) continue;
        states[i].computeDSP();
        dsp[i] = states[i].dsp;
    }

    // 3. Overlap resolution — group active tracks by band, then within each group:
    //    a) Find the highest-priority (lowest number) track → it wins at 0 dB
    //    b) Other tracks yield: -6 dB per priority step above the winner
    //    c) Narrow Q and shift centres apart for overlapping pairs
    for (int band = 0; band < kNumBands; ++band)
    {
        // Collect tracks in this band
        std::vector<int> group;
        for (int i = 0; i < kMaxTracks; ++i)
            if (states[i].active && primaryBand(states[i].normY) == band)
                group.push_back(i);

        if (group.size() < 2) continue;

        // Highest priority = lowest priority number
        int minPriority = states[group[0]].priority;
        for (int idx : group)
            minPriority = juce::jmin(minPriority, states[idx].priority);

        // Apply gain reduction and EQ narrowing
        for (int gi = 0; gi < (int)group.size(); ++gi)
        {
            int i = group[gi];
            int steps = states[i].priority - minPriority;
            dsp[i].gainDb = -(float)steps * 6.0f;  // 0, -6, -12, ...

            // Narrow Q for all overlapping tracks
            dsp[i].eqBandwidth *= 0.55f;

            // Shift each track's centre by a unique offset so they carve
            // distinct pockets. Space them 2 semitones apart within the group.
            float shiftSemitones = (float)(gi - (int)group.size() / 2) * 2.0f;
            float shiftFactor    = std::pow(2.0f, shiftSemitones / 12.0f);
            dsp[i].eqCenterHz   = juce::jlimit(20.0f, 20000.0f,
                                               dsp[i].eqCenterHz * shiftFactor);
        }
    }

    // 4. Push resolved params to each processor's DSP engine
    for (int i = 0; i < kMaxTracks; ++i)
        if (states[i].active && procs[i] != nullptr)
            procs[i]->applyResolvedDSP(dsp[i]);
}

//==============================================================================
SpectrumAnalyser* SharedMixerState::getSpectrumAnalyser (int slot)
{
    if (slot < 0 || slot >= kMaxTracks) return nullptr;
    juce::ScopedLock lock(lock_);
    if (!slots_[slot].occupied || slots_[slot].processor == nullptr) return nullptr;
    return &slots_[slot].processor->getSpectrumAnalyser();
}

StereoScope* SharedMixerState::getStereoScope (int slot)
{
    if (slot < 0 || slot >= kMaxTracks) return nullptr;
    juce::ScopedLock lock(lock_);
    if (!slots_[slot].occupied || slots_[slot].processor == nullptr) return nullptr;
    return &slots_[slot].processor->getStereoScope();
}

//==============================================================================
juce::Colour SharedMixerState::trackColour (int slot)
{
    static const juce::Colour colours[kMaxTracks] = {
        juce::Colour(0xffE53935),
        juce::Colour(0xff1E88E5),
        juce::Colour(0xff43A047),
        juce::Colour(0xffFB8C00),
        juce::Colour(0xff8E24AA),
        juce::Colour(0xff00ACC1),
        juce::Colour(0xffF9A825),
        juce::Colour(0xffE64A19),
    };
    return colours[juce::jlimit(0, kMaxTracks - 1, slot)];
}
