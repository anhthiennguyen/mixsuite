#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SharedMixerState.h"
#include "SpectrumAnalyser.h"

class SpectrumComponent : public juce::Component,
                           public juce::Timer
{
public:
    explicit SpectrumComponent (MixSuiteProcessor& proc);
    ~SpectrumComponent() override;

    void paint   (juce::Graphics&) override;
    void resized () override {}
    void timerCallback () override;

private:
    MixSuiteProcessor& proc_;

    // Frequency ↔ pixel mapping (log scale)
    float freqToX  (float freq, float width)  const;
    float dbToY    (float db,   float height) const;

    void drawGrid        (juce::Graphics&, float w, float h) const;
    void drawBandOverlay (juce::Graphics&, const TrackState&, juce::Colour, float w, float h) const;
    void drawCurve       (juce::Graphics&, int slot, float w, float h) const;

    static constexpr float kMinFreq = 20.0f;
    static constexpr float kMaxFreq = 20000.0f;
    static constexpr float kMinDb   = -80.0f;
    static constexpr float kMaxDb   =   6.0f;
};
