#include "SharedAnalyserState.h"
#include "PluginProcessor.h"

JUCE_IMPLEMENT_SINGLETON (SharedAnalyserState)

SharedAnalyserState::SharedAnalyserState() {}

int SharedAnalyserState::registerProcessor (MixSuiteProcessor* proc)
{
    juce::ScopedLock lock(lock_);
    for (int i = 0; i < kMaxTracks; ++i)
        if (!slots_[i].occupied)
        {
            slots_[i] = { proc, true };
            return i;
        }
    return -1;
}

void SharedAnalyserState::unregisterProcessor (int slot)
{
    if (slot < 0 || slot >= kMaxTracks) return;
    juce::ScopedLock lock(lock_);
    slots_[slot] = {};
}

std::array<MixSuiteProcessor*, SharedAnalyserState::kMaxTracks>
SharedAnalyserState::getProcessors() const
{
    juce::ScopedLock lock(lock_);
    std::array<MixSuiteProcessor*, kMaxTracks> out{};
    for (int i = 0; i < kMaxTracks; ++i)
        out[i] = slots_[i].occupied ? slots_[i].proc : nullptr;
    return out;
}

juce::Colour SharedAnalyserState::trackColour (int slot)
{
    static const juce::Colour colours[kMaxTracks] = {
        juce::Colour(0xff00BCD4),  // cyan
        juce::Colour(0xff8BC34A),  // lime
        juce::Colour(0xffFFC107),  // amber
        juce::Colour(0xff9C27B0),  // purple
        juce::Colour(0xffE91E63),  // pink
        juce::Colour(0xff009688),  // teal
        juce::Colour(0xff3F51B5),  // indigo
        juce::Colour(0xffFF5722),  // deep orange
    };
    return colours[juce::jlimit(0, kMaxTracks - 1, slot)];
}
