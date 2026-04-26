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

        randomBtn_.setButtonText ("Pick Random Colour");
        randomBtn_.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff1a2d3a));
        randomBtn_.setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.80f));
        randomBtn_.onClick = [this]
        {
            juce::Random rng;
            auto col = juce::Colour::fromHSV (rng.nextFloat(),
                                              0.55f + rng.nextFloat() * 0.35f,
                                              0.75f + rng.nextFloat() * 0.20f,
                                              1.0f);
            selector_.setCurrentColour (col);
        };
        addAndMakeVisible (randomBtn_);

        setSize (300, 310);
    }

    void resized() override
    {
        constexpr int kBtnH = 28;
        selector_ .setBounds (0, 0, getWidth(), getHeight() - kBtnH - 4);
        randomBtn_.setBounds (4, getHeight() - kBtnH - 2, getWidth() - 8, kBtnH);
    }

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
    juce::TextButton randomBtn_;
};
