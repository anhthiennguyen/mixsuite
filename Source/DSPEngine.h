#pragma once
#include <JuceHeader.h>
#include "TrackState.h"
#include "SpectrumAnalyser.h"
#include "StereoScope.h"

// Single-track stereo DSP engine.
// UI thread writes params via updateParams().
// Audio thread reads via syncParams() + processBlock().
class DSPEngine
{
public:
    DSPEngine();

    void prepare (double sampleRate, int samplesPerBlock);
    void reset();

    // UI thread: stage new params
    void updateParams (const TrackDSPParams& params);

    // Audio thread: pull staged params if dirty (call at top of processBlock)
    void syncParams();

    // Audio thread: process one stereo block in-place.
    // Uses MS (Mid-Side) widening: X position → stereo width, not hard pan.
    void processBlock (float* L, float* R, int numSamples);

    // Safe to call before audio starts (no thread contention yet)
    void initParams (const TrackDSPParams& params);

    double getSampleRate() const { return sampleRate_; }
    SpectrumAnalyser& getSpectrumAnalyser() { return spectrumAnalyser_; }
    StereoScope&      getStereoScope()      { return stereoScope_; }

private:
    double sampleRate_ = 44100.0;
    int    maxBlockSize_ = 0;

    // Staging (UI → audio thread)
    TrackDSPParams pendingParams_;
    TrackDSPParams activeParams_;
    std::atomic<bool> paramsDirty_ { false };
    juce::SpinLock pendingLock_;

    // Filters (stereo — one per channel)
    juce::dsp::IIR::Filter<float> filterL_, filterR_;

    // widthSmoother_: 0=mono (box at centre), 1=full stereo (box at edge)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoother_;
    // gainSmoother_: linear gain from priority-based reduction
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainSmoother_;

    float lastCenterHz_   = -1.0f;
    float lastBandwidth_  = -1.0f;
    float lastEqGainDb_   = -1.0f;

    void rebuildFilter (float centerHz, float bandwidthOct, float gainDb);

    SpectrumAnalyser spectrumAnalyser_;
    StereoScope      stereoScope_;
};
