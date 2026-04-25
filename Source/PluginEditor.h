#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EQComponent.h"
#include "CanvasComponent.h"
#include "SpectrumComponent.h"

class MixSuiteEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit MixSuiteEditor (MixSuiteProcessor&);
    ~MixSuiteEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    MixSuiteProcessor& proc_;

    enum class ActiveTab { EQ, Spatial };
    ActiveTab activeTab_ = ActiveTab::EQ;

    EQComponent       eqView_;
    CanvasComponent   canvasView_;
    SpectrumComponent spectrumView_;

    static constexpr int kTabBarH = 38;
    static constexpr int kSpecH   = 140;

    void switchTab (ActiveTab);
    void drawTabBar (juce::Graphics&) const;

    juce::Rectangle<int> eqTabRect()   const;
    juce::Rectangle<int> spatTabRect() const;
    juce::Rectangle<int> eqBypassRect()   const;
    juce::Rectangle<int> spatBypassRect() const;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixSuiteEditor)
};
