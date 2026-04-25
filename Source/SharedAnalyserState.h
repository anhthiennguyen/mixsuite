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

    // Track colours — readable as static, mutable via setTrackColour (message thread)
    static juce::Colour trackColour    (int slot);
    void                setTrackColour (int slot, juce::Colour colour);

private:
    SharedAnalyserState();

    struct Slot { MixSuiteProcessor* proc = nullptr; bool occupied = false; };
    std::array<Slot, kMaxTracks> slots_;
    mutable juce::CriticalSection lock_;

    juce::Colour colours_[kMaxTracks];
};

//==============================================================================
// Reusable colour-picker popup — include this wherever you need colour editing.
class TrackColourPicker : public juce::Component,
                          private juce::ChangeListener
{
public:
    TrackColourPicker (int slot, std::function<void()> onChanged)
        : slot_ (slot), onChanged_ (std::move (onChanged))
    {
        addAndMakeVisible (selector_);
        selector_.setCurrentColour (SharedAnalyserState::trackColour (slot));
        selector_.addChangeListener (this);
        setSize (300, 280);
    }
    void resized() override { selector_.setBounds (getLocalBounds()); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        SharedAnalyserState::getInstance()->setTrackColour (slot_, selector_.getCurrentColour());
        if (onChanged_) onChanged_();
    }

    int slot_;
    std::function<void()> onChanged_;
    juce::ColourSelector selector_ { juce::ColourSelector::showColourAtTop
                                   | juce::ColourSelector::editableColour
                                   | juce::ColourSelector::showColourspace };
};
