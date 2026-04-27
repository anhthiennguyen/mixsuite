#pragma once
#include <JuceHeader.h>

class MixSuiteProcessor;

class InstanceListComponent : public juce::Component,
                              private juce::Timer
{
public:
    InstanceListComponent();
    ~InstanceListComponent() override;

    void paint     (juce::Graphics&) override;
    void resized   () override {}
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    int getPreferredHeight() const;

private:
    int hoveredRow_ = -1;
    static constexpr float kMinFreq   =  20.0f;
    static constexpr float kMaxFreq   = 20000.0f;
    static constexpr float kSpecMinDb = -80.0f;
    static constexpr float kSpecMaxDb =   6.0f;
    static constexpr float kEqRange   =  24.0f;
    static constexpr int   kRowH      =  72;
    static constexpr int   kLabelW    =  88;
    static constexpr int   kHeaderH   =  26;
    static constexpr int   kStripH    =  18;

    float freqToX (float freq, float eqW) const;

    void drawRow         (juce::Graphics&, int slot, int rowIndex, juce::Rectangle<float> bounds) const;
    void drawBottomStrip (juce::Graphics&) const;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstanceListComponent)
};

//==============================================================================
class InstanceListWindow : public juce::DocumentWindow
{
public:
    static void toggle();
    static bool isOpen();

    void closeButtonPressed() override;

private:
    InstanceListWindow();

    static InstanceListWindow* s_instance_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstanceListWindow)
};
