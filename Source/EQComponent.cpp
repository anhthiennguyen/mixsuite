#include "EQComponent.h"
#include "PluginProcessor.h"

static const juce::Colour kBandColours[kNumEQBands] = {
    // Original 5 bands
    juce::Colour(0xff4CAF50), juce::Colour(0xff2196F3),
    juce::Colour(0xffFF9800), juce::Colour(0xff9C27B0), juce::Colour(0xffF44336),
    // User-added bands 5-15
    juce::Colour(0xff00BCD4), juce::Colour(0xffFFEB3B), juce::Colour(0xffE91E63),
    juce::Colour(0xff8BC34A), juce::Colour(0xffFF5722), juce::Colour(0xff009688),
    juce::Colour(0xff673AB7), juce::Colour(0xffFFC107), juce::Colour(0xff03A9F4),
    juce::Colour(0xffCDDC39), juce::Colour(0xffF06292),
};

EQComponent::EQComponent (MixSuiteProcessor& proc) : proc_(proc)
{
    auto setupBtn = [](juce::TextButton& btn, const char* label, juce::Colour accent)
    {
        btn.setButtonText(label);
        btn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff0f1e2d));
        btn.setColour(juce::TextButton::buttonOnColourId, accent);
        btn.setColour(juce::TextButton::textColourOffId,  accent.withAlpha(0.85f));
        btn.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
    };

    setupBtn(autoEqBtn_,    "CLEAN",       juce::Colour(0xff29b6f6));
    setupBtn(extraCleanBtn_, "EXTRA CLEAN", juce::Colour(0xff26c6da));

    autoEqBtn_.onClick    = [this] { runAutoEQ();    };
    extraCleanBtn_.onClick = [this] { runExtraClean(); };

    addAndMakeVisible(autoEqBtn_);
    addAndMakeVisible(extraCleanBtn_);

    startTimerHz(30);
}

EQComponent::~EQComponent() { stopTimer(); }

void EQComponent::timerCallback()
{
    bool updated = false;
    auto procs = SharedAnalyserState::getInstance()->getProcessors();
    for (auto* p : procs)
    {
        if (p && p->getEQAnalyser().pullSpectrum())
            updated = true;
        if (p)
            p->getHintsAnalyser().pullSpectrum();
    }
    if (updated || draggedBand_ >= 0)
        repaint();
}

//==============================================================================
float EQComponent::freqToX (float freq, float w) const
{
    return w * std::log10(freq / kMinFreq) / std::log10(kMaxFreq / kMinFreq);
}

float EQComponent::xToFreq (float x, float w) const
{
    return kMinFreq * std::pow(kMaxFreq / kMinFreq, x / w);
}

float EQComponent::specDbToY (float db, float h) const
{
    float norm = (db - kSpecMinDb) / (kSpecMaxDb - kSpecMinDb);
    return h * (1.0f - juce::jlimit(0.0f, 1.0f, norm));
}

float EQComponent::eqDbToY (float db, float h) const
{
    float norm = (db + kEqRange) / (2.0f * kEqRange);
    return h * (1.0f - juce::jlimit(0.0f, 1.0f, norm));
}

float EQComponent::yToEqDb (float y, float h) const
{
    return -kEqRange + (1.0f - y / h) * (2.0f * kEqRange);
}

float EQComponent::getDbAtFreq (const SpectrumAnalyser& sa, float freq)
{
    double sr = sa.getSampleRate();
    float  binWidth = (float)sr / (float)SpectrumAnalyser::fftSize;
    float  idx = freq / binWidth;
    int    lo  = juce::jlimit(0, SpectrumAnalyser::numBins - 1, (int)idx);
    int    hi  = juce::jlimit(0, SpectrumAnalyser::numBins - 1, lo + 1);
    float  t   = idx - (float)lo;
    const auto& sp = sa.getSpectrum();
    return sp[lo] * (1.0f - t) + sp[hi] * t;
}

float EQComponent::computeMagnitudeAt (float freq) const
{
    double sr  = proc_.getPluginSampleRate();
    double mag = 1.0;
    auto& apvts = proc_.getAPVTS();

    for (int i = 0; i < kNumEQBands; ++i)
    {
        juce::String p = "band" + juce::String(i) + "_";
        bool  on   = *apvts.getRawParameterValue(p + "enabled") > 0.5f;
        if (!on) continue;
        float f    = *apvts.getRawParameterValue(p + "freq");
        float gain = *apvts.getRawParameterValue(p + "gain");
        float q    = *apvts.getRawParameterValue(p + "q");
        float gl   = juce::Decibels::decibelsToGain(gain);

        juce::ReferenceCountedObjectPtr<juce::dsp::IIR::Coefficients<float>> c;
        if      (kEQBandTypes[i] == BandType::LowShelf)
            c = juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sr, (double)f, (double)q, (double)gl);
        else if (kEQBandTypes[i] == BandType::HighShelf)
            c = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, (double)f, (double)q, (double)gl);
        else
            c = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, (double)f, (double)q, (double)gl);

        mag *= c->getMagnitudeForFrequency((double)freq, sr);
    }

    return (float)juce::Decibels::gainToDecibels(mag);
}

//==============================================================================
void EQComponent::drawGrid (juce::Graphics& g, float w, float h) const
{
    static constexpr float       kFreqs[]  = { 50,100,200,500,1000,2000,5000,10000,20000 };
    static constexpr const char* kLabels[] = { "50","100","200","500","1k","2k","5k","10k","20k" };
    static constexpr int kN = 9;

    g.setColour(juce::Colours::white.withAlpha(0.06f));
    for (int i = 0; i < kN; ++i)
        g.drawLine(freqToX(kFreqs[i], w), 0.0f, freqToX(kFreqs[i], w), h, 1.0f);

    g.setColour(juce::Colours::white.withAlpha(0.05f));
    for (float db : { -24.0f, -12.0f, -6.0f, 6.0f, 12.0f, 24.0f })
        g.drawLine(0.0f, eqDbToY(db, h), w, eqDbToY(db, h), 0.5f);

    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.drawLine(0.0f, eqDbToY(0.0f, h), w, eqDbToY(0.0f, h), 1.0f);

    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.setColour(juce::Colours::white.withAlpha(0.28f));
    for (int i = 0; i < kN; ++i)
        g.drawText(kLabels[i], (int)freqToX(kFreqs[i], w) - 12, (int)h - 14, 24, 12,
                   juce::Justification::centred);

    g.setColour(juce::Colours::white.withAlpha(0.22f));
    for (float db : { 24.0f, 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -24.0f })
    {
        juce::String lbl = (db > 0 ? "+" : "") + juce::String((int)db);
        g.drawText(lbl, (int)w - 28, (int)eqDbToY(db, h) - 5, 26, 10,
                   juce::Justification::centredRight);
    }
}

//==============================================================================
// Heat map: sweeps orange → red where 2+ tracks have energy in the same band.
void EQComponent::drawMudOverlay (juce::Graphics& g, float w, float h) const
{
    auto procs = SharedAnalyserState::getInstance()->getProcessors();

    // Count active tracks
    int totalActive = 0;
    for (auto* p : procs) if (p) ++totalActive;
    if (totalActive < 2) return;

    constexpr float kThreshDb = -48.0f;
    constexpr int   kStep     = 2;

    for (int px = 0; px < (int)w; px += kStep)
    {
        float freq = xToFreq((float)px, w);

        int   tracksWithEnergy = 0;
        for (auto* p : procs)
        {
            if (!p) continue;
            if (getDbAtFreq(p->getEQAnalyser(), freq) > kThreshDb)
                ++tracksWithEnergy;
        }

        if (tracksWithEnergy < 2) continue;

        // muddiness: 0 = 2 tracks just touching, 1 = 5+ tracks piling up
        float mud = juce::jlimit(0.0f, 1.0f, (float)(tracksWithEnergy - 1) / 4.0f);

        // Hue: 0.10 (amber) → 0.0 (red) as muddiness rises
        float hue   = 0.10f * (1.0f - mud);
        float alpha = 0.12f + mud * 0.35f;

        g.setColour(juce::Colour::fromHSV(hue, 0.90f, 0.95f, alpha));
        g.fillRect((float)px, 0.0f, (float)kStep, h);
    }
}

//==============================================================================
void EQComponent::drawAllSpectra (juce::Graphics& g, float w, float h) const
{
    int ownSlot = proc_.getSlotIndex();
    auto procs  = SharedAnalyserState::getInstance()->getProcessors();

    for (int slot = 0; slot < SharedAnalyserState::kMaxTracks; ++slot)
    {
        auto* p = procs[slot];
        if (!p) continue;

        bool isOwn = (slot == ownSlot);
        auto& sa   = p->getEQAnalyser();
        double sr  = sa.getSampleRate();
        juce::Colour col = SharedAnalyserState::trackColour(slot);

        juce::Path fill, stroke;
        bool started = false;

        for (int i = 1; i < SpectrumAnalyser::numBins; ++i)
        {
            float freq = (float)i * (float)sr / (float)SpectrumAnalyser::fftSize;
            if (freq < kMinFreq || freq > kMaxFreq) continue;
            float x = freqToX(freq, w);
            float y = juce::jlimit(0.0f, h, specDbToY(sa.getSpectrum()[i], h));
            if (!started) { fill.startNewSubPath(x, y); stroke.startNewSubPath(x, y); started = true; }
            else          { fill.lineTo(x, y);          stroke.lineTo(x, y); }
        }
        if (!started) continue;

        fill.lineTo(freqToX(kMaxFreq, w), h);
        fill.lineTo(freqToX(kMinFreq, w), h);
        fill.closeSubPath();

        float fillAlpha   = isOwn ? 0.20f : 0.08f;
        float strokeAlpha = isOwn ? 0.75f : 0.40f;

        g.setColour(col.withAlpha(fillAlpha));
        g.fillPath(fill);
        g.setColour(col.withAlpha(strokeAlpha));
        g.strokePath(stroke, juce::PathStrokeType(isOwn ? 1.4f : 0.9f));

        // Track label near the peak of the spectrum
        if (!isOwn)
        {
            // Find loudest bin for label placement
            float peakX = w * 0.5f, peakDb = -80.0f;
            for (int i = 1; i < SpectrumAnalyser::numBins; ++i)
            {
                float freq = (float)i * (float)sr / (float)SpectrumAnalyser::fftSize;
                if (freq < kMinFreq || freq > kMaxFreq) continue;
                float db = sa.getSpectrum()[i];
                if (db > peakDb) { peakDb = db; peakX = freqToX(freq, w); }
            }
            if (peakDb > -60.0f)
            {
                float peakY = specDbToY(peakDb, h) - 12.0f;
                g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f).withStyle("Bold")));
                g.setColour(col.withAlpha(0.65f));
                g.drawText("T" + juce::String(slot + 1),
                           (int)peakX - 14, (int)peakY, 28, 10,
                           juce::Justification::centred);
            }
        }
    }
}

//==============================================================================
void EQComponent::drawEQCurve (juce::Graphics& g, float w, float h) const
{
    constexpr int kSteps = 600;
    juce::Path fill, stroke;
    bool started = false;
    float y0 = eqDbToY(0.0f, h);

    for (int i = 0; i <= kSteps; ++i)
    {
        float t    = (float)i / kSteps;
        float freq = kMinFreq * std::pow(kMaxFreq / kMinFreq, t);
        float db   = computeMagnitudeAt(freq);
        float x    = freqToX(freq, w);
        float y    = juce::jlimit(0.0f, h, eqDbToY(db, h));
        if (!started) { fill.startNewSubPath(x, y); stroke.startNewSubPath(x, y); started = true; }
        else          { fill.lineTo(x, y);          stroke.lineTo(x, y); }
    }

    fill.lineTo(freqToX(kMaxFreq, w), y0);
    fill.lineTo(freqToX(kMinFreq, w), y0);
    fill.closeSubPath();

    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.fillPath(fill);
    g.setColour(juce::Colours::white.withAlpha(0.90f));
    g.strokePath(stroke, juce::PathStrokeType(1.8f));
}

//==============================================================================
void EQComponent::drawNodes (juce::Graphics& g, float w, float h) const
{
    auto& apvts = proc_.getAPVTS();
    float y0    = eqDbToY(0.0f, h);

    for (int i = 0; i < kNumEQBands; ++i)
    {
        juce::String p = "band" + juce::String(i) + "_";
        bool  on   = *apvts.getRawParameterValue(p + "enabled") > 0.5f;
        float freq = *apvts.getRawParameterValue(p + "freq");
        float gain = *apvts.getRawParameterValue(p + "gain");
        float nx   = freqToX(freq, w);
        float ny   = eqDbToY(gain, h);
        juce::Colour col = kBandColours[i];

        g.setColour(col.withAlpha(on ? 0.22f : 0.07f));
        g.drawLine(nx, y0, nx, ny, 1.0f);

        float r = (i == hoveredBand_ || i == draggedBand_) ? 7.5f : 6.0f;
        g.setColour(col.withAlpha(on ? (i == draggedBand_ ? 1.0f : 0.82f) : 0.30f));
        g.fillEllipse(nx - r, ny - r, r * 2.0f, r * 2.0f);
        g.setColour(juce::Colours::white.withAlpha(on ? 0.65f : 0.18f));
        g.drawEllipse(nx - r, ny - r, r * 2.0f, r * 2.0f, 1.0f);

        g.setFont(juce::Font(juce::FontOptions().withHeight(8.0f).withStyle("Bold")));
        g.setColour(juce::Colours::white.withAlpha(on ? 0.90f : 0.35f));
        g.drawText(juce::String(i + 1), (int)(nx - r), (int)(ny - r),
                   (int)(r * 2), (int)(r * 2), juce::Justification::centred);
    }
}

//==============================================================================
int EQComponent::bandAtPoint (juce::Point<float> pt, float w, float h) const
{
    auto& apvts = proc_.getAPVTS();
    for (int i = 0; i < kNumEQBands; ++i)
    {
        juce::String p = "band" + juce::String(i) + "_";
        float freq = *apvts.getRawParameterValue(p + "freq");
        float gain = *apvts.getRawParameterValue(p + "gain");
        float d = std::hypot(pt.x - freqToX(freq, w), pt.y - eqDbToY(gain, h));
        if (d < 12.0f) return i;
    }
    return -1;
}

void EQComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        float w = (float)getWidth(), h = (float)(getHeight() - 20);
        int band = bandAtPoint(e.position, w, h);

        if (band >= 0)
        {
            // Right-click on a node → band options
            auto& apvts = proc_.getAPVTS();
            juce::String p = "band" + juce::String(band) + "_";
            bool isEnabled = *apvts.getRawParameterValue(p + "enabled") > 0.5f;
            bool isUserBand = (band >= kDefaultEQBands);

            juce::PopupMenu menu;
            menu.addSectionHeader("Band " + juce::String(band + 1));
            menu.addItem(1, isEnabled ? "Bypass Band" : "Enable Band");
            if (isUserBand)
                menu.addItem(2, "Remove Band");

            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                [this, band, isEnabled, isUserBand](int result)
                {
                    auto& apvts = proc_.getAPVTS();
                    juce::String p = "band" + juce::String(band) + "_";
                    if (result == 1)
                    {
                        if (auto* ep = apvts.getParameter(p + "enabled"))
                            ep->setValueNotifyingHost(isEnabled ? 0.0f : 1.0f);
                    }
                    else if (result == 2 && isUserBand)
                    {
                        if (auto* ep = apvts.getParameter(p + "enabled"))
                            ep->setValueNotifyingHost(0.0f);
                        if (auto* gp = apvts.getParameter(p + "gain"))
                            gp->setValueNotifyingHost(gp->convertTo0to1(0.0f));
                    }
                    repaint();
                });
            return;
        }

        // Right-click on empty space → track colour picker
        int slot = proc_.getSlotIndex();
        if (slot < 0) return;
        auto picker = std::make_unique<TrackColourPicker>(slot, [this] { repaint(); });
        auto anchor = juce::Rectangle<int>(localPointToGlobal(e.getPosition()), juce::Point<int>());
        juce::CallOutBox::launchAsynchronously(std::move(picker), anchor, nullptr);
        return;
    }
    draggedBand_ = bandAtPoint(e.position, (float)getWidth(), (float)(getHeight() - 20));
    repaint();
}

void EQComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    float w = (float)getWidth(), h = (float)(getHeight() - 20);

    // Ignore double-clicks on existing nodes
    if (bandAtPoint(e.position, w, h) >= 0) return;

    // Find first free slot in the user-band range (5-15)
    auto& apvts = proc_.getAPVTS();
    int freeSlot = -1;
    for (int i = kDefaultEQBands; i < kNumEQBands; ++i)
    {
        juce::String p = "band" + juce::String(i) + "_";
        if (*apvts.getRawParameterValue(p + "enabled") < 0.5f) { freeSlot = i; break; }
    }
    if (freeSlot < 0) return;  // all 16 slots used

    float freq = juce::jlimit(kMinFreq, kMaxFreq, xToFreq(e.position.x, w));
    float gain = juce::jlimit(-kEqRange, kEqRange, yToEqDb(e.position.y, h));
    juce::String p = "band" + juce::String(freeSlot) + "_";

    if (auto* fp = apvts.getParameter(p + "freq"))
        fp->setValueNotifyingHost(fp->convertTo0to1(freq));
    if (auto* gp = apvts.getParameter(p + "gain"))
        gp->setValueNotifyingHost(gp->convertTo0to1(gain));
    if (auto* qp = apvts.getParameter(p + "q"))
        qp->setValueNotifyingHost(qp->convertTo0to1(1.0f));
    if (auto* ep = apvts.getParameter(p + "enabled"))
        ep->setValueNotifyingHost(1.0f);

    repaint();
}

void EQComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedBand_ < 0) return;
    float w = (float)getWidth(), h = (float)(getHeight() - 20);
    juce::String p = "band" + juce::String(draggedBand_) + "_";
    float newFreq = juce::jlimit(kMinFreq, kMaxFreq, xToFreq(juce::jlimit(0.0f, w, e.position.x), w));
    float newGain = juce::jlimit(-kEqRange, kEqRange, yToEqDb(juce::jlimit(0.0f, h, e.position.y), h));
    if (auto* fp = proc_.getAPVTS().getParameter(p + "freq"))
        fp->setValueNotifyingHost(fp->convertTo0to1(newFreq));
    if (auto* gp = proc_.getAPVTS().getParameter(p + "gain"))
        gp->setValueNotifyingHost(gp->convertTo0to1(newGain));
    repaint();
}

void EQComponent::mouseUp (const juce::MouseEvent&)    { draggedBand_ = -1; repaint(); }

void EQComponent::mouseMove (const juce::MouseEvent& e)
{
    int band = bandAtPoint(e.position, (float)getWidth(), (float)(getHeight() - 20));
    if (band != hoveredBand_) { hoveredBand_ = band; repaint(); }
    setMouseCursor(band >= 0 ? juce::MouseCursor::DraggingHandCursor
                             : juce::MouseCursor::CrosshairCursor);
}

void EQComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int band = bandAtPoint(e.position, (float)getWidth(), (float)(getHeight() - 20));
    if (band < 0) return;
    juce::String p = "band" + juce::String(band) + "_";
    auto* qp = proc_.getAPVTS().getParameter(p + "q");
    if (!qp) return;
    float q    = *proc_.getAPVTS().getRawParameterValue(p + "q");
    float newQ = juce::jlimit(0.1f, 10.0f, q * (1.0f + wheel.deltaY * 0.15f));
    qp->setValueNotifyingHost(qp->convertTo0to1(newQ));
    repaint();
}

//==============================================================================
void EQComponent::paint (juce::Graphics& g)
{
    float w = (float)getWidth();
    float h = (float)(getHeight() - 20);

    g.fillAll(juce::Colour(0xff070b10));

    drawGrid          (g, w, h);
    drawMudOverlay    (g, w, h);
    drawAllSpectra    (g, w, h);
    drawEQCurve       (g, w, h);
    drawNodes         (g, w, h);
    drawAnalysisPanel (g, w, h);

    // Status bar
    g.setColour(juce::Colours::black.withAlpha(0.70f));
    g.fillRect(0.0f, h, w, 20.0f);
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
    g.setColour(juce::Colours::white.withAlpha(0.40f));

    auto& apvts = proc_.getAPVTS();
    int   show  = draggedBand_ >= 0 ? draggedBand_ : hoveredBand_;
    juce::String txt;

    if (show >= 0)
    {
        juce::String p = "band" + juce::String(show) + "_";
        float freq = *apvts.getRawParameterValue(p + "freq");
        float gain = *apvts.getRawParameterValue(p + "gain");
        float q    = *apvts.getRawParameterValue(p + "q");
        static const char* kTypes[] = { "Low Shelf","Peak","Peak","Peak","High Shelf" };
        txt = "Band " + juce::String(show + 1) + " [" + kTypes[show] + "]"
            + "   Freq: " + juce::String((int)freq) + " Hz"
            + "   Gain: " + (gain >= 0 ? "+" : "") + juce::String(gain, 1) + " dB"
            + "   Q: " + juce::String(q, 2)
            + "   (scroll to adjust Q)";
    }
    else
    {
        int n = 0;
        for (auto* p2 : SharedAnalyserState::getInstance()->getProcessors())
            if (p2) ++n;
        txt = "MixSuite  |  Track " + juce::String(proc_.getSlotIndex() + 1)
            + " of " + juce::String(n)
            + "   |  drag: freq+gain   |   scroll: Q"
            + "   |  double-click: add band   |   right-click band: options"
            + (n >= 2 ? "   |  orange/red = clash" : "");
    }
    g.drawText(txt, 6, (int)h + 1, (int)w - 12, 18, juce::Justification::centredLeft);
}

void EQComponent::resized()
{
    // Two buttons side by side, below the analysis panel (max height ~85px at y=10)
    constexpr int kPanelW = 230;
    constexpr int kBtnH   = 22;
    constexpr int kBtnY   = 10 + 90;  // below worst-case 3-hint panel
    constexpr int kGap = 4;
    int btnX  = getWidth() - kPanelW - 10;
    int halfW = (kPanelW - kGap) / 2;
    autoEqBtn_   .setBounds(btnX,                  kBtnY, halfW,                    kBtnH);
    extraCleanBtn_.setBounds(btnX + halfW + kGap,  kBtnY, kPanelW - halfW - kGap,  kBtnH);
}

//==============================================================================
void EQComponent::drawAnalysisPanel (juce::Graphics& g, float w, float h) const
{
    juce::String hints = computeAnalysisHints();
    if (hints.isEmpty()) return;

    bool noSignal = hints.startsWith("Play");
    bool isGood   = hints.startsWith("Balance");

    // Split into individual lines for multi-hint display
    juce::StringArray lines;
    if (!noSignal && !isGood)
        lines.addTokens(hints, "|", "");
    else
        lines.add(hints);

    // Remove leading/trailing whitespace from each token
    for (auto& line : lines)
        line = line.trim();

    constexpr float kPad    = 8.0f;
    constexpr float kLineH  = 17.0f;
    constexpr float kTitleH = 16.0f;
    float panelW = 230.0f;
    int activeNotches = 0;
    for (auto& n : lastNotches_) if (n.active) ++activeNotches;
    float notchSection = activeNotches > 0
        ? (8.0f + 14.0f + (float)activeNotches * kLineH)  // separator + header + rows
        : 0.0f;
    float panelH = kTitleH + kPad * 1.5f + (float)lines.size() * kLineH + kPad * 0.5f + notchSection;

    float px = w - panelW - 10.0f;
    float py = 10.0f;

    // Panel background
    juce::Rectangle<float> panel(px, py, panelW, panelH);
    g.setColour(juce::Colour(0xff0b1520).withAlpha(0.92f));
    g.fillRoundedRectangle(panel, 5.0f);

    // Panel border
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawRoundedRectangle(panel, 5.0f, 1.0f);

    // Title bar "EQ ANALYSIS"
    g.setColour(juce::Colour(0xff2a3a50));
    g.fillRoundedRectangle(px, py, panelW, kTitleH, 5.0f);
    g.fillRect(px, py + 8.0f, panelW, 8.0f);

    g.setFont(juce::Font(juce::FontOptions().withHeight(9.5f).withStyle("Bold")));
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.drawText("EQ ANALYSIS", (int)px + 8, (int)py, (int)panelW - 16, (int)kTitleH,
               juce::Justification::centredLeft);

    // Hint lines
    g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));

    float ty = py + kTitleH + kPad * 0.75f;
    for (const auto& line : lines)
    {
        juce::Colour col = noSignal  ? juce::Colours::white.withAlpha(0.45f)
                         : isGood   ? juce::Colour(0xff66dd77)
                                    : juce::Colour(0xffffc44f);
        g.setColour(col.withAlpha(col.getAlpha() * 0.7f));
        g.fillEllipse(px + kPad, ty + 4.5f, 5.0f, 5.0f);
        g.setColour(col);
        g.drawText(line, (int)(px + kPad + 10.0f), (int)ty,
                   (int)(panelW - kPad * 2.0f - 10.0f), (int)kLineH,
                   juce::Justification::centredLeft);
        ty += kLineH;
    }

    // Show surgical notches applied by extra-clean (if any)
    bool anyNotch = false;
    for (auto& n : lastNotches_) if (n.active) { anyNotch = true; break; }

    if (anyNotch)
    {
        // Thin separator
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawLine(px + kPad, ty + 2.0f, px + panelW - kPad, ty + 2.0f, 0.5f);
        ty += 6.0f;

        g.setFont(juce::Font(juce::FontOptions().withHeight(9.5f).withStyle("Bold")));
        g.setColour(juce::Colour(0xff29b6f6).withAlpha(0.70f));
        g.drawText("NOTCHES APPLIED", (int)(px + kPad), (int)ty,
                   (int)(panelW - kPad * 2.0f), 13, juce::Justification::centredLeft);
        ty += 14.0f;

        g.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
        for (auto& n : lastNotches_)
        {
            if (!n.active) continue;
            juce::String freqStr = n.freq < 1000.0f
                ? juce::String((int)n.freq) + " Hz"
                : juce::String(n.freq / 1000.0f, 1) + " kHz";
            juce::String txt = freqStr + "   "
                + juce::String(n.gainDb, 1) + " dB  (Q 2.5)";
            g.setColour(juce::Colour(0xff29b6f6).withAlpha(0.55f));
            g.fillEllipse(px + kPad, ty + 4.5f, 5.0f, 5.0f);
            g.setColour(juce::Colour(0xff29b6f6).withAlpha(0.85f));
            g.drawText(txt, (int)(px + kPad + 10.0f), (int)ty,
                       (int)(panelW - kPad * 2.0f - 10.0f), (int)kLineH,
                       juce::Justification::centredLeft);
            ty += kLineH;
        }
    }
}

//==============================================================================
juce::String EQComponent::computeAnalysisHints() const
{
    auto& sa  = proc_.getHintsAnalyser();
    double sr = sa.getSampleRate();
    if (sr < 1.0) return {};

    const auto& sp  = sa.getSpectrum();
    float       binHz = (float)sr / (float)SpectrumAnalyser::fftSize;

    // Average power (linear → dB) across a frequency range
    auto avgDb = [&](float lo, float hi) -> float
    {
        int binLo = juce::jmax(1, (int)(lo / binHz));
        int binHi = juce::jmin(SpectrumAnalyser::numBins - 1, (int)(hi / binHz));
        if (binLo >= binHi) return -100.0f;
        double sum = 0.0;
        int    cnt = 0;
        for (int i = binLo; i <= binHi; ++i)
        {
            sum += (double)juce::Decibels::decibelsToGain(sp[i], -100.0f);
            ++cnt;
        }
        return cnt > 0 ? juce::Decibels::gainToDecibels((float)(sum / cnt), -100.0f) : -100.0f;
    };

    float subBass  = avgDb (20.0f,    80.0f);
    float bass     = avgDb (80.0f,   250.0f);
    float lowMid   = avgDb (250.0f,  500.0f);
    float mid      = avgDb (500.0f,  2000.0f);
    float hiMid    = avgDb (2000.0f, 5000.0f);
    float presence = avgDb (5000.0f, 10000.0f);
    float air      = avgDb (10000.0f, 20000.0f);

    float maxLevel = std::max ({ subBass, bass, lowMid, mid, hiMid, presence, air });
    if (maxLevel < -55.0f)
        return "Play audio to get EQ suggestions";

    juce::StringArray hints;

    // Low-end problems
    if ((bass - mid) > 12.0f)
        hints.add ("Boomy: cut low shelf ~100-150 Hz");
    else if ((subBass - bass) > 8.0f)
        hints.add ("Sub rumble: high-pass below 80 Hz");

    // Mid muddiness
    if ((lowMid - mid) > 9.0f)
        hints.add ("Muddy: cut 250-400 Hz");

    // Mid-range balance
    if ((mid - hiMid) > 12.0f)
        hints.add ("Lacks presence: boost 2-5 kHz");
    else if ((hiMid - mid) > 10.0f)
        hints.add ("Harsh upper mids: cut 2-4 kHz");

    // High-end balance
    float lowAvg  = (bass + lowMid) * 0.5f;
    float highAvg = (hiMid + presence) * 0.5f;
    if ((lowAvg - highAvg) > 14.0f)
        hints.add ("Dull/dark: boost high shelf 10 kHz+");
    else if ((lowAvg - highAvg) > 8.0f && hints.isEmpty())
        hints.add ("Dark: add highs 8-12 kHz");

    // Thin / lacks body
    if ((mid - bass) > 12.0f)
        hints.add ("Thin: boost lows 80-150 Hz");

    // Air
    if ((presence - air) > 14.0f && presence > -55.0f)
        hints.add ("Lacks air: boost 12-16 kHz");

    if (hints.isEmpty())
        return "Balance sounds good";

    // Show up to 3 most critical hints
    juce::StringArray top;
    for (int i = 0; i < juce::jmin (3, hints.size()); ++i)
        top.add (hints[i]);
    return top.joinIntoString ("   |   ");
}

//==============================================================================
void EQComponent::runAutoEQ()
{
    auto& sa  = proc_.getHintsAnalyser();
    double sr = sa.getSampleRate();
    if (sr < 1.0) return;

    const auto& sp  = sa.getSpectrum();
    float binHz = (float)sr / (float)SpectrumAnalyser::fftSize;

    // Average linear power over a range, returned in dB
    auto avgDb = [&](float lo, float hi) -> float
    {
        int binLo = juce::jmax(1, (int)(lo / binHz));
        int binHi = juce::jmin(SpectrumAnalyser::numBins - 1, (int)(hi / binHz));
        if (binLo >= binHi) return -100.0f;
        double sum = 0.0; int cnt = 0;
        for (int i = binLo; i <= binHi; ++i)
        { sum += (double)juce::Decibels::decibelsToGain(sp[i], -100.0f); ++cnt; }
        return cnt > 0 ? juce::Decibels::gainToDecibels((float)(sum / cnt), -100.0f) : -100.0f;
    };

    float ref = avgDb(500.0f, 5000.0f);
    if (ref < -50.0f) return;

    float noiseFloor = ref - 18.0f;

    auto& apvts = proc_.getAPVTS();
    auto setParam = [&](const juce::String& id, float val) {
        if (auto* p = apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(val));
    };

    // ── Band 0: low-shelf HP ────────────────────────────────────────────────
    float hpFreq = -1.0f;
    for (float f = 20.0f; f < 500.0f; f *= 1.2f)
        if (avgDb(f, f * 1.2f) > noiseFloor) { hpFreq = f; break; }

    if (hpFreq > 40.0f)
    {
        setParam("band0_freq",    juce::jlimit(20.0f, 400.0f, hpFreq * 0.8f));
        setParam("band0_gain",    -15.0f);
        setParam("band0_enabled", 1.0f);
    }

    // ── Band 4: high-shelf LP ────────────────────────────────────────────────
    float lpFreq = -1.0f;
    for (float f = 20000.0f; f > 2000.0f; f /= 1.2f)
        if (avgDb(f / 1.2f, f) > noiseFloor) { lpFreq = f; break; }

    if (lpFreq > 0.0f && lpFreq < 17000.0f)
    {
        setParam("band4_freq",    juce::jlimit(3000.0f, 20000.0f, lpFreq * 1.2f));
        setParam("band4_gain",    -15.0f);
        setParam("band4_enabled", 1.0f);
    }

    repaint();
}

//==============================================================================
void EQComponent::runExtraClean()
{
    // Step 1: run the standard HP/LP clean first
    runAutoEQ();

    // Step 2: surgical notches in the "thin spaces between main content"
    //
    // We compare each 1/3-octave band against the 2-octave neighbourhood.
    // A bump that is > 3.5 dB above its surroundings but NOT in the main
    // body of the signal (excluded if within 6 dB of the loudest level)
    // is an unnecessary resonance.  A tight notch (Q 2.5, ≤ –8 dB) clips
    // only the tip — perceptually transparent, spectrally cleaner.

    auto& sa  = proc_.getHintsAnalyser();
    double sr = sa.getSampleRate();
    if (sr < 1.0) return;

    const auto& sp  = sa.getSpectrum();
    float binHz = (float)sr / (float)SpectrumAnalyser::fftSize;

    auto avgDb = [&](float lo, float hi) -> float
    {
        int binLo = juce::jmax(1, (int)(lo / binHz));
        int binHi = juce::jmin(SpectrumAnalyser::numBins - 1, (int)(hi / binHz));
        if (binLo >= binHi) return -100.0f;
        double sum = 0.0; int cnt = 0;
        for (int i = binLo; i <= binHi; ++i)
        { sum += (double)juce::Decibels::decibelsToGain(sp[i], -100.0f); ++cnt; }
        return cnt > 0 ? juce::Decibels::gainToDecibels((float)(sum / cnt), -100.0f) : -100.0f;
    };

    float ref        = avgDb(500.0f, 5000.0f);
    float noiseFloor = ref - 18.0f;

    auto& apvts = proc_.getAPVTS();
    auto setParam = [&](const juce::String& id, float val) {
        if (auto* p = apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(val));
    };

    struct Candidate { float freq; float excess; };
    std::vector<Candidate> candidates;

    for (float fc = 80.0f; fc < 12000.0f; fc *= 1.122f)
    {
        float narrow = avgDb(fc / 1.15f, fc * 1.15f);
        float wide   = avgDb(fc / 2.83f, fc * 2.83f);
        if (narrow < noiseFloor + 4.0f) continue;
        if (narrow > ref - 6.0f) continue;
        float excess = narrow - wide;
        if (excess > 3.5f)
            candidates.push_back({ fc, excess });
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b){ return a.excess > b.excess; });

    std::vector<Candidate> chosen;
    for (auto& c : candidates)
    {
        bool tooClose = false;
        for (auto& sel : chosen)
            if (std::abs(std::log2(c.freq / sel.freq)) < 0.5f) { tooClose = true; break; }
        if (!tooClose) chosen.push_back(c);
        if ((int)chosen.size() == 3) break;
    }

    for (auto& n : lastNotches_) n = {};

    for (int i = 0; i < (int)chosen.size(); ++i)
    {
        int   band   = i + 1;
        float gainDb = juce::jlimit(-8.0f, -2.0f, -chosen[i].excess * 0.65f);
        juce::String b = "band" + juce::String(band) + "_";
        setParam(b + "freq",    chosen[i].freq);
        setParam(b + "gain",    gainDb);
        setParam(b + "q",       2.5f);
        setParam(b + "enabled", 1.0f);
        lastNotches_[i] = { chosen[i].freq, gainDb, true };
    }

    repaint();
}

