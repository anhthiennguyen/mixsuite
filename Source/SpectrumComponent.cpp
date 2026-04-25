#include "SpectrumComponent.h"

SpectrumComponent::SpectrumComponent (MixSuiteProcessor& proc)
    : proc_(proc)
{
    startTimerHz(30);
}

SpectrumComponent::~SpectrumComponent()
{
    stopTimer();
}

void SpectrumComponent::timerCallback()
{
    bool needsRepaint = false;
    auto* shared = SharedMixerState::getInstance();
    for (int i = 0; i < kMaxTracks; ++i)
    {
        auto* analyser = shared->getSpectrumAnalyser(i);
        if (analyser && analyser->pullSpectrum())
            needsRepaint = true;
    }
    if (needsRepaint)
        repaint();
}

//==============================================================================
float SpectrumComponent::freqToX (float freq, float width) const
{
    return width * std::log10(freq / kMinFreq) / std::log10(kMaxFreq / kMinFreq);
}

float SpectrumComponent::dbToY (float db, float height) const
{
    float norm = (db - kMinDb) / (kMaxDb - kMinDb);
    return height * (1.0f - juce::jlimit(0.0f, 1.0f, norm));
}

//==============================================================================
void SpectrumComponent::drawGrid (juce::Graphics& g, float w, float h) const
{
    // Frequency grid lines
    static constexpr float kGridFreqs[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    static constexpr const char* kGridLabels[] = { "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
    static constexpr int kNumGrid = 9;

    g.setColour(juce::Colours::white.withAlpha(0.06f));
    for (int i = 0; i < kNumGrid; ++i)
    {
        float x = freqToX(kGridFreqs[i], w);
        g.drawLine(x, 0.0f, x, h, 1.0f);
    }

    // dB grid lines
    for (float db = -60.0f; db <= 0.0f; db += 20.0f)
    {
        float y = dbToY(db, h);
        g.drawLine(0.0f, y, w, y, 1.0f);
    }

    // Frequency labels
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.setColour(juce::Colours::white.withAlpha(0.28f));
    for (int i = 0; i < kNumGrid; ++i)
    {
        float x = freqToX(kGridFreqs[i], w);
        g.drawText(kGridLabels[i], (int)x - 12, (int)h - 14, 24, 12,
                   juce::Justification::centred);
    }

    // dB labels on left
    const char* dbLabels[] = { "0", "-20", "-40", "-60" };
    float       dbVals[]   = { 0.0f, -20.0f, -40.0f, -60.0f };
    for (int i = 0; i < 4; ++i)
    {
        float y = dbToY(dbVals[i], h);
        g.drawText(dbLabels[i], 2, (int)y - 5, 22, 10, juce::Justification::centredLeft);
    }
}

void SpectrumComponent::drawBandOverlay (juce::Graphics& g, const TrackState& state,
                                          juce::Colour colour, float w, float h) const
{
    float centerHz = state.dsp.eqCenterHz;
    float bwOct    = state.dsp.eqBandwidth;

    float loHz = centerHz * std::pow(2.0f, -bwOct * 0.5f);
    float hiHz = centerHz * std::pow(2.0f,  bwOct * 0.5f);
    loHz = juce::jlimit(kMinFreq, kMaxFreq, loHz);
    hiHz = juce::jlimit(kMinFreq, kMaxFreq, hiHz);

    float x1 = freqToX(loHz,    w);
    float x2 = freqToX(hiHz,    w);
    float xc = freqToX(centerHz, w);

    // Band fill
    g.setColour(colour.withAlpha(0.10f));
    g.fillRect(x1, 0.0f, x2 - x1, h);

    // Left/right band edge
    g.setColour(colour.withAlpha(0.35f));
    g.drawLine(x1, 0.0f, x1, h, 1.0f);
    g.drawLine(x2, 0.0f, x2, h, 1.0f);

    // Centre frequency marker
    g.setColour(colour.withAlpha(0.65f));
    g.drawLine(xc, 0.0f, xc, h, 1.5f);

    // Label: track name near top
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.setColour(colour.withAlpha(0.85f));
    g.drawText(state.label, (int)xc - 28, 4, 56, 10, juce::Justification::centred);
}

void SpectrumComponent::drawCurve (juce::Graphics& g, int slot, float w, float h) const
{
    auto* analyser = SharedMixerState::getInstance()->getSpectrumAnalyser(slot);
    if (!analyser) return;

    const auto& spectrum = analyser->getSpectrum();
    const double sr = analyser->getSampleRate();

    juce::Path path;
    bool started = false;

    for (int i = 1; i < SpectrumAnalyser::numBins; ++i)
    {
        float freq = (float)i * (float)sr / (float)SpectrumAnalyser::fftSize;
        if (freq < kMinFreq || freq > kMaxFreq) continue;

        float x = freqToX(freq, w);
        float y = juce::jlimit(0.0f, h, dbToY(spectrum[i], h));

        if (!started) { path.startNewSubPath(x, y); started = true; }
        else          path.lineTo(x, y);
    }

    if (!started) return;

    // Filled area under the curve
    path.lineTo(freqToX(kMaxFreq, w), h);
    path.lineTo(freqToX(kMinFreq, w), h);
    path.closeSubPath();

    auto colour = SharedMixerState::trackColour(slot);
    g.setColour(colour.withAlpha(0.12f));
    g.fillPath(path);

    // Stroke on top
    juce::Path strokePath;
    started = false;
    for (int i = 1; i < SpectrumAnalyser::numBins; ++i)
    {
        float freq = (float)i * (float)sr / (float)SpectrumAnalyser::fftSize;
        if (freq < kMinFreq || freq > kMaxFreq) continue;
        float x = freqToX(freq, w);
        float y = juce::jlimit(0.0f, h, dbToY(spectrum[i], h));
        if (!started) { strokePath.startNewSubPath(x, y); started = true; }
        else          strokePath.lineTo(x, y);
    }
    g.setColour(colour.withAlpha(0.75f));
    g.strokePath(strokePath, juce::PathStrokeType(1.5f));
}

//==============================================================================
void SpectrumComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float w = bounds.getWidth();
    float h = bounds.getHeight() - 16.0f;  // bottom 16px for freq labels

    // Background
    g.fillAll(juce::Colour(0xff070b10));

    // Top separator
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawLine(0.0f, 0.0f, w, 0.0f, 1.0f);

    drawGrid(g, w, h);

    auto* shared = SharedMixerState::getInstance();
    auto states  = shared->getAllStates();

    // Draw spectrum curves
    for (int i = 0; i < kMaxTracks; ++i)
        if (states[i].active)
            drawCurve(g, i, w, h);

    // "SPECTRUM" label
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.setColour(juce::Colours::white.withAlpha(0.22f));
    g.drawText("SPECTRUM", 4, 3, 60, 10, juce::Justification::centredLeft);
}
