#include "SharedAnalyserState.h"
#include "PluginProcessor.h"

JUCE_IMPLEMENT_SINGLETON (SharedAnalyserState)

static constexpr juce::uint32 kDefaultColours[SharedAnalyserState::kMaxTracks] = {
    0xff00BCD4, 0xff8BC34A, 0xffFFC107, 0xff9C27B0,
    0xffE91E63, 0xff009688, 0xff3F51B5, 0xffFF5722,
};

SharedAnalyserState::SharedAnalyserState()
{
    for (int i = 0; i < kMaxTracks; ++i)
        colours_[i] = juce::Colour(kDefaultColours[i]);
}

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
    if (auto* inst = getInstance())
        return inst->colours_[juce::jlimit(0, kMaxTracks - 1, slot)];
    return juce::Colour(kDefaultColours[juce::jlimit(0, kMaxTracks - 1, slot)]);
}

void SharedAnalyserState::setTrackColour (int slot, juce::Colour colour)
{
    if (slot >= 0 && slot < kMaxTracks)
        colours_[slot] = colour;
}
