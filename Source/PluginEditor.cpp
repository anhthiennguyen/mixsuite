#include "PluginEditor.h"
#include "SharedAnalyserState.h"

static constexpr juce::uint32 kTabBarBg    = 0xff0a1220;
static constexpr juce::uint32 kEQColour    = 0xff2979ff;   // vivid blue
static constexpr juce::uint32 kSpatColour  = 0xffff6d00;   // vivid orange
static constexpr int          kTabW        = 110;

MixSuiteEditor::MixSuiteEditor (MixSuiteProcessor& p)
    : AudioProcessorEditor(&p), proc_(p),
      eqView_(p), canvasView_(p), spectrumView_(p)
{
    addAndMakeVisible(eqView_);
    addChildComponent(canvasView_);
    addChildComponent(spectrumView_);
    setSize(820, 660);
    setResizable(true, true);
    setResizeLimits(600, 480, 1600, 1000);
    startTimerHz(30);
    SharedAnalyserState::getInstance()->registerEditor(proc_.getSlotIndex(), this);
}

MixSuiteEditor::~MixSuiteEditor()
{
    stopTimer();
    SharedAnalyserState::getInstance()->unregisterEditor(proc_.getSlotIndex());
}

void MixSuiteEditor::timerCallback()
{
    repaint(0, 0, getWidth(), kTabBarH);
}

//==============================================================================
juce::Rectangle<int> MixSuiteEditor::eqTabRect() const
{
    int cx = getWidth() / 2 - kTabW - 4;
    return { cx, 4, kTabW, kTabBarH - 8 };
}

juce::Rectangle<int> MixSuiteEditor::spatTabRect() const
{
    int cx = getWidth() / 2 + 4;
    return { cx, 4, kTabW, kTabBarH - 8 };
}

juce::Rectangle<int> MixSuiteEditor::eqBypassRect() const
{
    auto r = eqTabRect();
    return { r.getRight() - 18, r.getY() + (r.getHeight() - 14) / 2, 14, 14 };
}

juce::Rectangle<int> MixSuiteEditor::spatBypassRect() const
{
    auto r = spatTabRect();
    return { r.getRight() - 18, r.getY() + (r.getHeight() - 14) / 2, 14, 14 };
}

juce::Rectangle<int> MixSuiteEditor::instanceListRect() const
{
    constexpr int kW = 62, kH = 20;
    return { getWidth() - kW - 10, (kTabBarH - kH) / 2, kW, kH };
}

juce::Rectangle<int> MixSuiteEditor::masterRect() const
{
    constexpr int kW = 58, kH = 20;
    auto ir = instanceListRect();
    return { ir.getX() - kW - 6, (kTabBarH - kH) / 2, kW, kH };
}

//==============================================================================
void MixSuiteEditor::drawTabBar (juce::Graphics& g) const
{
    const float w = (float)getWidth();

    // Background
    g.setColour(juce::Colour(kTabBarBg));
    g.fillRect(0, 0, getWidth(), kTabBarH);

    // Separator line at bottom
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawLine(0.0f, (float)kTabBarH - 0.5f, w, (float)kTabBarH - 0.5f, 1.0f);

    // App name
    g.setFont(juce::Font(juce::FontOptions().withHeight(14.0f).withStyle("Bold")));
    g.setColour(juce::Colours::white.withAlpha(0.90f));
    g.drawText("MIXSUITE", 14, 0, 110, kTabBarH, juce::Justification::centredLeft);

    // Signal-chain arrow hint
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.setColour(juce::Colours::white.withAlpha(0.25f));
    g.drawText("EQ  \xe2\x86\x92  Spatial", 130, 0, 120, kTabBarH, juce::Justification::centredLeft);

    // Draw a tab
    auto drawTab = [&](juce::Rectangle<int> rect, const char* label,
                       bool isActive, bool isEnabled, juce::uint32 accentRaw)
    {
        juce::Colour accent(accentRaw);
        auto rf = rect.toFloat();

        if (isActive)
        {
            g.setColour(accent.withAlpha(0.18f));
            g.fillRoundedRectangle(rf, 5.0f);
            g.setColour(accent.withAlpha(0.55f));
            g.drawRoundedRectangle(rf, 5.0f, 1.0f);
            // bottom glow line
            g.setColour(accent.withAlpha(0.80f));
            g.fillRect(rf.getX() + 6.0f, (float)kTabBarH - 2.5f,
                       rf.getWidth() - 12.0f, 2.5f);
        }
        else
        {
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.fillRoundedRectangle(rf, 5.0f);
        }

        // Bypass dot
        float dotX = (float)(rect.getRight() - 14);
        float dotY = (float)(rect.getCentreY() - 4);
        juce::Colour dotCol = isEnabled
            ? juce::Colour(0xff44dd66)
            : juce::Colours::white.withAlpha(0.22f);
        g.setColour(dotCol);
        g.fillEllipse(dotX, dotY, 8.0f, 8.0f);

        // Label
        juce::Colour labelCol = isActive
            ? juce::Colours::white.withAlpha(0.95f)
            : juce::Colours::white.withAlpha(0.42f);
        g.setColour(labelCol);
        g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f).withStyle("Bold")));
        g.drawText(label,
                   rect.getX() + 6, rect.getY(),
                   rect.getWidth() - 24, rect.getHeight(),
                   juce::Justification::centredLeft);
    };

    drawTab(eqTabRect(),   "EQ",      activeTab_ == ActiveTab::EQ,      proc_.eqEnabled_,      kEQColour);
    drawTab(spatTabRect(), "SPATIAL", activeTab_ == ActiveTab::Spatial,  proc_.spatialEnabled_, kSpatColour);

    // Master button
    bool isMaster = (proc_.getTrackState().mode == TrackState::Mode::Master);
    auto mr = masterRect().toFloat();
    g.setColour(isMaster ? juce::Colour(0xff3a2000) : juce::Colour(0xff0d1824));
    g.fillRoundedRectangle(mr, 4.0f);
    g.setColour(isMaster ? juce::Colour(0xffffaa00).withAlpha(0.70f)
                         : juce::Colours::white.withAlpha(0.13f));
    g.drawRoundedRectangle(mr, 4.0f, 0.8f);
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.5f).withStyle("Bold")));
    g.setColour(isMaster ? juce::Colour(0xffffcc44) : juce::Colours::white.withAlpha(0.38f));
    g.drawText("MASTER", mr.toNearestInt(), juce::Justification::centred);

    // Instance list button (top-right)
    auto ir = instanceListRect().toFloat();
    bool instOpen = InstanceListWindow::isOpen();
    g.setColour(instOpen ? juce::Colour(0xff1a2d3a) : juce::Colour(0xff0d1824));
    g.fillRoundedRectangle(ir, 4.0f);
    g.setColour(juce::Colours::white.withAlpha(instOpen ? 0.28f : 0.13f));
    g.drawRoundedRectangle(ir, 4.0f, 0.8f);
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.5f).withStyle("Bold")));
    g.setColour(juce::Colours::white.withAlpha(instOpen ? 0.75f : 0.38f));
    g.drawText("INSTANCES", ir.toNearestInt(), juce::Justification::centred);
}

void MixSuiteEditor::paint (juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff070b10));
    drawTabBar(g);
}

//==============================================================================
void MixSuiteEditor::mouseDown (const juce::MouseEvent& e)
{
    // Bypass-dot clicks
    if (eqBypassRect().contains(e.getPosition()))
    {
        proc_.eqEnabled_ = !proc_.eqEnabled_;
        repaint(0, 0, getWidth(), kTabBarH);
        return;
    }
    if (spatBypassRect().contains(e.getPosition()))
    {
        proc_.spatialEnabled_ = !proc_.spatialEnabled_;
        repaint(0, 0, getWidth(), kTabBarH);
        return;
    }

    // Master toggle
    if (masterRect().contains(e.getPosition()))
    {
        bool isMaster = (proc_.getTrackState().mode == TrackState::Mode::Master);
        proc_.setTrackMode(isMaster ? TrackState::Mode::Stereo : TrackState::Mode::Master);
        repaint(0, 0, getWidth(), kTabBarH);
        return;
    }

    // Instance list button
    if (instanceListRect().contains(e.getPosition()))
    {
        InstanceListWindow::toggle();
        repaint(0, 0, getWidth(), kTabBarH);
        return;
    }

    // Tab clicks
    if (eqTabRect().contains(e.getPosition()))   { switchTab(ActiveTab::EQ);      return; }
    if (spatTabRect().contains(e.getPosition()))  { switchTab(ActiveTab::Spatial); return; }
}

void MixSuiteEditor::switchTab (ActiveTab tab)
{
    activeTab_ = tab;
    resized();
    repaint();
}

//==============================================================================
void MixSuiteEditor::resized()
{
    const int tw = getWidth();
    const int contentY = kTabBarH;
    const int contentH = getHeight() - kTabBarH;

    if (activeTab_ == ActiveTab::EQ)
    {
        eqView_.setVisible(true);
        eqView_.setBounds(0, contentY, tw, contentH);
        canvasView_.setVisible(false);
        spectrumView_.setVisible(false);
    }
    else
    {
        eqView_.setVisible(false);
        canvasView_.setVisible(true);
        spectrumView_.setVisible(true);
        canvasView_.setBounds  (0, contentY,            tw, contentH - kSpecH);
        spectrumView_.setBounds(0, contentY + contentH - kSpecH, tw, kSpecH);
    }
}
