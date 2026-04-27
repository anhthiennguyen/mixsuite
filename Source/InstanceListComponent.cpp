#include "InstanceListComponent.h"
#include "PluginProcessor.h"
#include "SharedAnalyserState.h"
#include "SpectrumAnalyser.h"

//==============================================================================
// Free helper: EQ magnitude at a frequency for a given processor
static float computeEQMagnitude (MixSuiteProcessor& proc, float freq)
{
    double sr   = proc.getPluginSampleRate();
    double mag  = 1.0;
    auto&  apvts = proc.getAPVTS();

    for (int i = 0; i < kNumEQBands; ++i)
    {
        juce::String p = "band" + juce::String(i) + "_";
        bool on = *apvts.getRawParameterValue(p + "enabled") > 0.5f;
        if (!on) continue;
        float f    = *apvts.getRawParameterValue(p + "freq");
        float gain = *apvts.getRawParameterValue(p + "gain");
        float q    = *apvts.getRawParameterValue(p + "q");

        int typeIdx = juce::jlimit(0, 6, (int)*apvts.getRawParameterValue(p + "type"));
        auto bandType = (BandType)typeIdx;

        double gl = (double)juce::Decibels::decibelsToGain(gain);
        juce::ReferenceCountedObjectPtr<juce::dsp::IIR::Coefficients<float>> c;
        switch (bandType)
        {
            case BandType::LowShelf:  c = juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sr, (double)f, (double)q, gl); break;
            case BandType::HighShelf: c = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, (double)f, (double)q, gl); break;
            case BandType::LowCut:    c = juce::dsp::IIR::Coefficients<float>::makeHighPass  (sr, (double)f, (double)q); break;
            case BandType::HighCut:   c = juce::dsp::IIR::Coefficients<float>::makeLowPass   (sr, (double)f, (double)q); break;
            case BandType::Notch:     c = juce::dsp::IIR::Coefficients<float>::makeNotch     (sr, (double)f, (double)q); break;
            case BandType::BandPass:  c = juce::dsp::IIR::Coefficients<float>::makeBandPass  (sr, (double)f, (double)q); break;
            default:                  c = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, (double)f, (double)q, gl); break;
        }
        mag *= c->getMagnitudeForFrequency((double)freq, sr);
    }
    return (float)juce::Decibels::gainToDecibels(mag);
}

//==============================================================================
InstanceListComponent::InstanceListComponent()
{
    startTimerHz(30);
}

InstanceListComponent::~InstanceListComponent()
{
    stopTimer();
}

void InstanceListComponent::timerCallback()
{
    bool updated = false;
    auto procs = SharedAnalyserState::getInstance()->getProcessors();
    for (auto* p : procs)
        if (p && p->getEQAnalyser().pullSpectrum())
            updated = true;
    if (updated) repaint();
}

int InstanceListComponent::getPreferredHeight() const
{
    int n = 0;
    auto procs = SharedAnalyserState::getInstance()->getProcessors();
    for (auto* p : procs) if (p) ++n;
    return kHeaderH + juce::jmax(1, n) * kRowH + kStripH + 4;
}

float InstanceListComponent::freqToX (float freq, float eqW) const
{
    return eqW * std::log10(freq / kMinFreq) / std::log10(kMaxFreq / kMinFreq);
}

//==============================================================================
void InstanceListComponent::drawRow (juce::Graphics& g, int slot, int rowIndex,
                                      juce::Rectangle<float> bounds) const
{
    auto procs = SharedAnalyserState::getInstance()->getProcessors();
    auto* proc = procs[slot];
    if (!proc) return;

    juce::Colour col = SharedAnalyserState::trackColour(slot);
    float rx = bounds.getX(), ry = bounds.getY();
    float rw = bounds.getWidth(), rh = bounds.getHeight();

    // Row background (alternating + hover highlight)
    bool hovered = (rowIndex == hoveredRow_);
    juce::Colour rowBg = hovered
        ? juce::Colour(0xff14243a)
        : juce::Colour(slot % 2 == 0 ? 0xff0a1018u : 0xff0d1622u);
    g.setColour(rowBg);
    g.fillRect(bounds);

    // Left colour accent strip
    g.setColour(col.withAlpha(0.88f));
    g.fillRect(rx, ry, 4.0f, rh);

    // Bottom separator
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawLine(rx, ry + rh - 0.5f, rx + rw, ry + rh - 0.5f, 0.5f);

    // Track label
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.5f).withStyle("Bold")));
    g.setColour(col.withAlpha(0.90f));
    g.drawText(proc->getTrackState().label,
               (int)rx + 8, (int)ry + 5, kLabelW - 12, 15,
               juce::Justification::centredLeft);

    // EQ enabled label
    bool eqOn = proc->eqEnabled_;
    g.setFont(juce::Font(juce::FontOptions().withHeight(8.0f)));
    g.setColour(juce::Colours::white.withAlpha(eqOn ? 0.35f : 0.18f));
    g.drawText(eqOn ? "EQ ON" : "BYPASS",
               (int)rx + 8, (int)ry + 22, kLabelW - 12, 12,
               juce::Justification::centredLeft);

    // EQ display area (right of label column)
    float ex = rx + (float)kLabelW;
    float ew = rw - (float)kLabelW;
    float ey = ry + 3.0f;
    float eh = rh - 6.0f;

    auto& sa  = proc->getEQAnalyser();
    double sr = sa.getSampleRate();
    if (sr < 1.0) return;

    // 0dB centre line
    float midY = ey + eh * 0.5f;
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawLine(ex, midY, ex + ew, midY, 0.5f);

    // Spectrum fill
    {
        float binHz = (float)sr / (float)SpectrumAnalyser::fftSize;
        juce::Path fill;
        bool started = false;
        for (int i = 1; i < SpectrumAnalyser::numBins; ++i)
        {
            float freq = (float)i * binHz;
            if (freq < kMinFreq || freq > kMaxFreq) continue;
            float px   = ex + freqToX(freq, ew);
            float norm = (sa.getSpectrum()[i] - kSpecMinDb) / (kSpecMaxDb - kSpecMinDb);
            float py   = ey + eh * (1.0f - juce::jlimit(0.0f, 1.0f, norm));
            if (!started) { fill.startNewSubPath(px, py); started = true; }
            else          { fill.lineTo(px, py); }
        }
        if (started)
        {
            fill.lineTo(ex + ew, ey + eh);
            fill.lineTo(ex, ey + eh);
            fill.closeSubPath();
            g.setColour(col.withAlpha(0.20f));
            g.fillPath(fill);
        }
    }

    // EQ curve
    {
        constexpr int kSteps = 300;
        juce::Path curve;
        bool started = false;
        for (int i = 0; i <= kSteps; ++i)
        {
            float t    = (float)i / (float)kSteps;
            float freq = kMinFreq * std::pow(kMaxFreq / kMinFreq, t);
            float db   = computeEQMagnitude(*proc, freq);
            float norm = (db + kEqRange) / (2.0f * kEqRange);
            float px   = ex + freqToX(freq, ew);
            float py   = juce::jlimit(ey, ey + eh, ey + eh * (1.0f - juce::jlimit(0.0f, 1.0f, norm)));
            if (!started) { curve.startNewSubPath(px, py); started = true; }
            else          { curve.lineTo(px, py); }
        }
        g.setColour(juce::Colours::white.withAlpha(0.80f));
        g.strokePath(curve, juce::PathStrokeType(1.3f));
    }
}

// Returns the processor slot for a given row index (-1 if out of range)
static int slotForRow (int row)
{
    auto procs = SharedAnalyserState::getInstance()->getProcessors();
    int current = 0;
    for (int slot = 0; slot < SharedAnalyserState::kMaxTracks; ++slot)
    {
        if (!procs[slot]) continue;
        if (current == row) return slot;
        ++current;
    }
    return -1;
}

static int rowAtY (int y, int headerH, int rowH, int numRows)
{
    int row = (y - headerH) / rowH;
    return (row >= 0 && row < numRows) ? row : -1;
}

static int activeRowCount()
{
    int n = 0;
    auto procs = SharedAnalyserState::getInstance()->getProcessors();
    for (auto* p : procs) if (p) ++n;
    return n;
}

void InstanceListComponent::mouseDown (const juce::MouseEvent& e)
{
    int n   = activeRowCount();
    int row = rowAtY(e.y, kHeaderH, kRowH, n);
    if (row < 0) return;

    int slot = slotForRow(row);
    if (slot < 0) return;

    auto procs = SharedAnalyserState::getInstance()->getProcessors();
    auto* proc = procs[slot];
    if (!proc) return;

    if (auto* editor = proc->getActiveEditor())
    {
        if (auto* top = editor->getTopLevelComponent())
        {
            top->toFront(true);
            if (auto* peer = top->getPeer())
                peer->toFront(true);
        }
    }
}

void InstanceListComponent::mouseMove (const juce::MouseEvent& e)
{
    int n      = activeRowCount();
    int newRow = rowAtY(e.y, kHeaderH, kRowH, n);
    setMouseCursor(newRow >= 0 ? juce::MouseCursor::PointingHandCursor
                               : juce::MouseCursor::NormalCursor);
    if (newRow != hoveredRow_)
    {
        hoveredRow_ = newRow;
        repaint();
    }
}

void InstanceListComponent::drawBottomStrip (juce::Graphics& g) const
{
    auto procs = SharedAnalyserState::getInstance()->getProcessors();
    int n = 0;
    for (auto* p : procs) if (p) ++n;
    if (n == 0) return;

    float stripY = (float)(kHeaderH + juce::jmax(1, n) * kRowH) + 2.0f;
    float totalW = (float)getWidth();
    float slotW  = totalW / (float)n;

    int col = 0;
    for (int slot = 0; slot < SharedAnalyserState::kMaxTracks; ++slot)
    {
        auto* p = procs[slot];
        if (!p) continue;
        float sx = (float)col * slotW;
        juce::Colour c = SharedAnalyserState::trackColour(slot);
        g.setColour(c.withAlpha(0.70f));
        g.fillRect(sx, stripY, slotW - 1.0f, (float)kStripH - 2.0f);
        g.setFont(juce::Font(juce::FontOptions().withHeight(8.5f).withStyle("Bold")));
        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.drawText(p->getTrackState().label,
                   (int)sx, (int)stripY, (int)(slotW - 1.0f), kStripH - 2,
                   juce::Justification::centred);
        ++col;
    }
}

//==============================================================================
void InstanceListComponent::paint (juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff080c12));

    // Header bar
    g.setColour(juce::Colour(0xff0a1220));
    g.fillRect(0, 0, getWidth(), kHeaderH);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawLine(0.0f, (float)kHeaderH - 0.5f, (float)getWidth(), (float)kHeaderH - 0.5f, 0.5f);

    g.setFont(juce::Font(juce::FontOptions().withHeight(10.5f).withStyle("Bold")));
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.drawText("MIXSUITE - INSTANCE LIST", 10, 0, getWidth() - 20, kHeaderH,
               juce::Justification::centredLeft);

    // Instance rows
    auto procs = SharedAnalyserState::getInstance()->getProcessors();
    int row = 0;
    for (int slot = 0; slot < SharedAnalyserState::kMaxTracks; ++slot)
    {
        if (!procs[slot]) continue;
        drawRow(g, slot, row,
                { 0.0f, (float)(kHeaderH + row * kRowH), (float)getWidth(), (float)kRowH });
        ++row;
    }

    if (row == 0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        g.drawText("No MixSuite instances open",
                   0, kHeaderH, getWidth(), kRowH, juce::Justification::centred);
    }

    drawBottomStrip(g);
}

//==============================================================================
// InstanceListWindow

InstanceListWindow* InstanceListWindow::s_instance_ = nullptr;

InstanceListWindow::InstanceListWindow()
    : DocumentWindow("MixSuite - Instance List",
                     juce::Colour(0xff0a1220),
                     DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    auto* comp = new InstanceListComponent();
    setContentOwned(comp, true);
    setResizable(false, false);
    centreWithSize(580, comp->getPreferredHeight());
    setVisible(true);
    toFront(true);
}

void InstanceListWindow::closeButtonPressed()
{
    s_instance_ = nullptr;
    delete this;
}

void InstanceListWindow::toggle()
{
    if (s_instance_)
        s_instance_->closeButtonPressed();
    else
        s_instance_ = new InstanceListWindow();
}

bool InstanceListWindow::isOpen()
{
    return s_instance_ != nullptr;
}
