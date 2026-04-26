#pragma once
#include <JuceHeader.h>
#include "SpectrumAnalyser.h"
#include "TrackState.h"
#include "DSPEngine.h"

static constexpr int kNumEQBands = 5;

enum class BandType { LowShelf, Peak, HighShelf };
static constexpr BandType kEQBandTypes[kNumEQBands] = {
    BandType::LowShelf, BandType::Peak, BandType::Peak, BandType::Peak, BandType::HighShelf
};
static constexpr float kEQDefaultFreqs[kNumEQBands] = { 80.0f, 250.0f, 1000.0f, 4000.0f, 12000.0f };
static constexpr float kEQDefaultQs[kNumEQBands]    = { 0.7f,  1.0f,   1.0f,    1.0f,    0.7f };

class MixSuiteProcessor : public juce::AudioProcessor
{
public:
    MixSuiteProcessor();
    ~MixSuiteProcessor() override;

    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock   (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override  { return "MixSuite"; }
    bool acceptsMidi()  const override           { return false; }
    bool producesMidi() const override           { return false; }
    bool isMidiEffect() const override           { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override                               { return 1; }
    int getCurrentProgram() override                            { return 0; }
    void setCurrentProgram (int) override                       {}
    const juce::String getProgramName (int) override            { return "Default"; }
    void changeProgramName (int, const juce::String&) override  {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // === EQ interface (used by EQComponent) ===
    juce::AudioProcessorValueTreeState& getAPVTS()    { return apvts_; }
    double           getPluginSampleRate() const       { return sampleRate_; }
    SpectrumAnalyser& getEQAnalyser()                  { return eqAnalyser_; }
    SpectrumAnalyser& getHintsAnalyser()               { return hintsAnalyser_; }

    // === Spatial interface (used by CanvasComponent, SpectrumComponent, SharedMixerState) ===
    SpectrumAnalyser& getSpectrumAnalyser()  { return spatialDSP_.getSpectrumAnalyser(); }
    StereoScope&      getStereoScope()       { return spatialDSP_.getStereoScope(); }
    const TrackState& getTrackState() const  { return trackState_; }
    int               getSlotIndex()  const  { return slotIndex_; }

    void setTrackPosition (float normX, float normY);
    void setTrackHeight   (float normHeight);
    void setTrackLabel    (const juce::String& label);
    void setTrackPriority (int priority);
    void setTrackMode     (TrackState::Mode mode);
    void applyResolvedDSP (const TrackDSPParams& params);

    // === Module bypass flags (toggled from tab bar) ===
    bool eqEnabled_      = true;
    bool spatialEnabled_ = true;

private:
    double sampleRate_ = 44100.0;
    int    slotIndex_  = -1;

    // --- EQ module ---
    juce::AudioProcessorValueTreeState apvts_;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParams();

    using Filter      = juce::dsp::IIR::Filter<float>;
    using FilterCoefs = juce::dsp::IIR::Coefficients<float>;
    std::array<Filter, kNumEQBands> filtersL_, filtersR_;
    void updateEQFilters();
    SpectrumAnalyser eqAnalyser_;                    // display spectrum (smooth)
    SpectrumAnalyser hintsAnalyser_ { 0.2f };        // hints spectrum (fast response)

    // --- Spatial module ---
    TrackState trackState_;
    DSPEngine  spatialDSP_;
    void pushSpatialParams();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixSuiteProcessor)
};
