#pragma once
#include <JuceHeader.h>
#include "SpectrumAnalyser.h"
#include "SharedAnalyserState.h"

class MixSuiteProcessor;

class EQComponent : public juce::Component,
                    private juce::Timer
{
public:
    explicit EQComponent (MixSuiteProcessor& proc);
    ~EQComponent() override;

    void paint    (juce::Graphics&) override;
    void resized  () override;

    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseUp          (const juce::MouseEvent&) override;
    void mouseMove        (const juce::MouseEvent&) override;
    void mouseWheelMove   (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    MixSuiteProcessor& proc_;

    static constexpr float kMinFreq   =    20.0f;
    static constexpr float kMaxFreq   = 20000.0f;
    static constexpr float kSpecMinDb =   -80.0f;
    static constexpr float kSpecMaxDb =     6.0f;
    static constexpr float kEqRange   =    24.0f;

    int draggedBand_ = -1;
    int hoveredBand_ = -1;

    float freqToX   (float freq, float w) const;
    float xToFreq   (float x,    float w) const;
    float specDbToY (float db,   float h) const;
    float eqDbToY   (float db,   float h) const;
    float yToEqDb   (float y,    float h) const;

    // Interpolated spectrum dB at a given frequency from a SpectrumAnalyser
    static float getDbAtFreq (const SpectrumAnalyser& sa, float freq);

    // Combined EQ magnitude response (current track only) in dB
    float computeMagnitudeAt (float freq) const;

    int bandAtPoint (juce::Point<float> pt, float w, float h) const;

    void drawGrid       (juce::Graphics&, float w, float h) const;
    void drawMudOverlay (juce::Graphics&, float w, float h) const;
    void drawAllSpectra (juce::Graphics&, float w, float h) const;
    void drawEQCurve    (juce::Graphics&, float w, float h) const;
    void drawNodes      (juce::Graphics&, float w, float h) const;

    void timerCallback() override;

    juce::String computeAnalysisHints() const;
    void drawAnalysisPanel (juce::Graphics&, float w, float h) const;

    juce::TextButton autoEqBtn_;
    void runAutoEQ();

    // Extra-clean: surgical notch results shown in the analysis panel
    struct NotchResult { float freq = 0.0f; float gainDb = 0.0f; bool active = false; };
    std::array<NotchResult, 3> lastNotches_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQComponent)
};
