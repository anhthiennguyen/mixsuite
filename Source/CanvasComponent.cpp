#include "CanvasComponent.h"
#include "PluginProcessor.h"
#include "SharedMixerState.h"
#include "SharedAnalyserState.h"

static juce::Colour bandColour (int band)
{
    static const juce::Colour colours[kNumBands] = {
        juce::Colour(0xff1a1040),
        juce::Colour(0xff0d2e50),
        juce::Colour(0xff0a3328),
        juce::Colour(0xff3d2800),
        juce::Colour(0xff3d1200),
    };
    return colours[juce::jlimit(0, kNumBands - 1, band)];
}

//==============================================================================
CanvasComponent::CanvasComponent (MixSuiteProcessor& proc)
    : proc_(proc)
{
    setSize(640, 500);
    startTimerHz(60);
}

CanvasComponent::~CanvasComponent() { stopTimer(); }

//==============================================================================
juce::Rectangle<float> CanvasComponent::boxRect (const TrackState& s) const
{
    float cx = s.normX * (float)getWidth();
    float cy = s.normY * (float)getHeight();
    float h  = juce::jmax(s.normHeight * (float)getHeight(), (float)kMinBoxPx);
    return { cx - kBoxW * 0.5f, cy - h * 0.5f, (float)kBoxW, h };
}

juce::Rectangle<float> CanvasComponent::mirrorBoxRect (const TrackState& s) const
{
    float mirrorNX = 1.0f - s.normX;
    float cx = mirrorNX * (float)getWidth();
    float cy = s.normY  * (float)getHeight();
    float h  = juce::jmax(s.normHeight * (float)getHeight(), (float)kMinBoxPx);
    return { cx - kBoxW * 0.5f, cy - h * 0.5f, (float)kBoxW, h };
}

CanvasComponent::DragMode CanvasComponent::hitZone (juce::Rectangle<float> rect,
                                                     juce::Point<float> pt) const
{
    if (!rect.contains(pt))                      return DragMode::None;
    if (pt.y <= rect.getY()      + kHandleZone)  return DragMode::ResizeTop;
    if (pt.y >= rect.getBottom() - kHandleZone)  return DragMode::ResizeBottom;
    return DragMode::Move;
}

int CanvasComponent::hitTest (const std::array<TrackState, kMaxTracks>& states,
                               juce::Point<float> pt, bool& isMirrorOut) const
{
    // Test top-most first; check both real and mirror box for each slot
    for (int i = kMaxTracks - 1; i >= 0; --i)
    {
        if (!states[i].active) continue;
        if (states[i].mode == TrackState::Mode::Master) continue;

        // Skip mirror test when centred or in Pan mode (no mirror exists)
        bool centred = std::abs(states[i].normX - 0.5f) < 0.01f;
        bool isPan   = (states[i].mode == TrackState::Mode::Pan);

        if (!centred && !isPan && mirrorBoxRect(states[i]).contains(pt))
        {
            isMirrorOut = true;
            return i;
        }
        if (boxRect(states[i]).contains(pt))
        {
            isMirrorOut = false;
            return i;
        }
    }
    return -1;
}

//==============================================================================
void CanvasComponent::drawBox (juce::Graphics& g,
                                juce::Rectangle<float> rect,
                                const juce::String& label,
                                juce::Colour col,
                                bool isDragged, bool isHovered, bool isOwned) const
{
    float r = 5.0f;

    // ── FROSTED GLASS FILL ───────────────────────────────────────────────────
    float fillAlpha = isDragged ? 0.45f : (isHovered ? 0.32f : (isOwned ? 0.24f : 0.15f));
    g.setColour(col.withAlpha(fillAlpha));
    g.fillRoundedRectangle(rect, r);

    // Top-half specular sheen
    juce::ColourGradient sheen (juce::Colours::white.withAlpha(0.09f), rect.getX(), rect.getY(),
                                juce::Colours::transparentWhite,       rect.getX(), rect.getCentreY(),
                                false);
    g.setGradientFill(sheen);
    g.fillRoundedRectangle(rect, r);

    // ── RESIZE HANDLE TINTS ──────────────────────────────────────────────────
    g.setColour(juce::Colours::white.withAlpha(0.09f));
    g.fillRoundedRectangle(rect.withHeight((float)kHandleZone), r);
    g.fillRoundedRectangle(rect.withY(rect.getBottom() - (float)kHandleZone)
                               .withHeight((float)kHandleZone), r);

    // ── BORDER ───────────────────────────────────────────────────────────────
    float borderAlpha = isDragged ? 1.0f : (isHovered ? 0.90f : (isOwned ? 0.75f : 0.55f));
    g.setColour(col.withAlpha(borderAlpha));
    g.drawRoundedRectangle(rect, r, isDragged ? 2.0f : 1.5f);

    if (isOwned)
    {
        g.setColour(juce::Colours::white.withAlpha(0.38f));
        g.drawRoundedRectangle(rect.expanded(1.5f), r + 1.5f, 1.0f);
    }

    // ── VERTICAL LABEL ───────────────────────────────────────────────────────
    if (rect.getHeight() >= 28.0f)
    {
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(
            -juce::MathConstants<float>::halfPi,
            rect.getCentreX(), rect.getCentreY()));

        juce::Rectangle<float> textRect (
            rect.getCentreX() - rect.getHeight() * 0.5f,
            rect.getCentreY() - rect.getWidth()  * 0.5f,
            rect.getHeight(), rect.getWidth());

        float fontSize = juce::jlimit(10.0f, 15.0f, rect.getHeight() * 0.14f);
        g.setFont(juce::Font(juce::FontOptions().withHeight(fontSize).withStyle("Bold")));
        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.drawText(label, textRect.toNearestInt(), juce::Justification::centred, true);
        g.restoreState();
    }

}

//==============================================================================
void CanvasComponent::drawSpreadBar (juce::Graphics& g, const TrackState& state,
                                      juce::Colour col, bool isDragged) const
{
    bool isPan   = (state.mode == TrackState::Mode::Pan);
    bool centred = std::abs(state.normX - 0.5f) < 0.01f;
    if (isPan || centred) return;

    float W  = (float)getWidth();
    float H  = (float)getHeight();
    float cx = state.normX * W;
    float mx = (1.0f - state.normX) * W;  // mirror centre X
    float cy = state.normY * H;
    float boxH = juce::jmax(state.normHeight * H, (float)kMinBoxPx);

    float barH = juce::jmax(6.0f, boxH * 0.28f);
    float x1   = juce::jmin(cx, mx);
    float x2   = juce::jmax(cx, mx);

    juce::Rectangle<float> bar (x1, cy - barH * 0.5f, x2 - x1, barH);

    // Gradient: strong at both ends (where the boxes are), fades toward centre
    juce::ColourGradient grad (col.withAlpha(isDragged ? 0.40f : 0.26f), x1, cy,
                               col.withAlpha(isDragged ? 0.40f : 0.26f), x2, cy, false);
    grad.addColour(0.5, col.withAlpha(0.07f));
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bar, barH * 0.5f);

    // Outline
    g.setColour(col.withAlpha(isDragged ? 0.50f : 0.22f));
    g.drawRoundedRectangle(bar, barH * 0.5f, 1.0f);

    // Centre-line tick at the stereo midpoint (canvas centre)
    float tickX = W * 0.5f;
    g.setColour(col.withAlpha(0.18f));
    g.drawLine(tickX, cy - barH, tickX, cy + barH, 1.0f);
}

//==============================================================================
void CanvasComponent::paint (juce::Graphics& g)
{
    const float W = (float)getWidth();
    const int   w = getWidth();
    const int   h = getHeight();

    // ── BACKGROUND ───────────────────────────────────────────────────────────
    g.fillAll(juce::Colour(0xff070b10));

    // Subtle per-band zone tints + 3-px coloured accent bar on the left edge
    for (int b = 0; b < kNumBands; ++b)
    {
        int   db = kNumBands - 1 - b;
        float y0 = (float)(b * h) / kNumBands;
        float y1 = (float)((b + 1) * h) / kNumBands;
        g.setColour(bandColour(db).withAlpha(0.07f));
        g.fillRect(juce::Rectangle<float>(0.f, y0, W, y1 - y0));
        g.setColour(bandColour(db).withAlpha(0.55f));
        g.fillRect(juce::Rectangle<float>(0.f, y0, 3.f, y1 - y0));
    }

    // ── BAND SEPARATORS + FREQUENCY LABELS ───────────────────────────────────
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f).withStyle("Italic")));
    for (int b = 0; b < kNumBands; ++b)
    {
        float y = (float)(b * h) / kNumBands;
        if (b > 0)
        {
            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.drawHorizontalLine((int)y, 3.f, W);
        }
        int   db     = kNumBands - 1 - b;
        float labelY = y + (float)h / kNumBands * 0.5f;
        g.setColour(juce::Colours::white.withAlpha(0.28f));
        g.drawText(juce::String(kBands[db].name) + " · "
                       + juce::String((int)kBands[db].centerHz) + "Hz",
                   w - 90, (int)(labelY - 7.f), 88, 14,
                   juce::Justification::centredRight, false);
    }

    // ── CENTRE LINE + ZONE LABELS ────────────────────────────────────────────
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.drawVerticalLine(w / 2, 0.f, (float)(h - 18));

    g.setColour(juce::Colours::white.withAlpha(0.30f));
    g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f).withStyle("Bold")));
    g.drawText("LEFT",  4,          4, w / 2 - 8, 14, juce::Justification::centredLeft);
    g.drawText("MONO",  w / 2 - 24, 4, 48,        14, juce::Justification::centred);
    g.drawText("RIGHT", w / 2 + 4,  4, w / 2 - 8, 14, juce::Justification::centredRight);

    // ── GONIOMETER / VECTORSCOPE ─────────────────────────────────────────────
    auto states  = SharedMixerState::getInstance()->getAllStates();
    drawGoniometer(g, states);

    // ── TRACK BOXES ──────────────────────────────────────────────────────────
    int  ownSlot = proc_.getSlotIndex();

    // Paint high-priority-number (yielding) tracks first so low-priority-number
    // (winning) tracks naturally render on top — no badge needed.
    std::array<int, kMaxTracks> drawOrder;
    std::iota(drawOrder.begin(), drawOrder.end(), 0);
    std::sort(drawOrder.begin(), drawOrder.end(), [&](int a, int b) {
        int pa = states[a].active ? states[a].priority : -1;
        int pb = states[b].active ? states[b].priority : -1;
        return pa > pb;   // higher number → drawn first → visually behind
    });

    // ── TRACK BOXES ──────────────────────────────────────────────────────────
    for (int i : drawOrder)
    {
        if (!states[i].active) continue;
        if (states[i].mode == TrackState::Mode::Master) continue;

        bool isOwned         = (i == ownSlot);
        bool isDraggedReal   = (i == draggedSlot_ && !dragIsMirror_);
        bool isDraggedMirror = (i == draggedSlot_ &&  dragIsMirror_);
        bool isHoveredReal   = (i == hoveredSlot_ && !hoverIsMirror_);
        bool isHoveredMirror = (i == hoveredSlot_ &&  hoverIsMirror_);

        juce::Colour col = SharedMixerState::trackColour(i);

        drawBox(g, boxRect(states[i]), states[i].label, col,
                isDraggedReal, isHoveredReal, isOwned);

        bool centred = std::abs(states[i].normX - 0.5f) < 0.01f;
        bool isPan   = (states[i].mode == TrackState::Mode::Pan);
        if (!centred && !isPan)
            drawBox(g, mirrorBoxRect(states[i]), states[i].label, col,
                    isDraggedMirror, isHoveredMirror, isOwned);
    }

    // ── STATUS BAR ───────────────────────────────────────────────────────────
    g.setColour(juce::Colours::black.withAlpha(0.65f));
    g.fillRect(0, h - 18, w, 18);
    g.setColour(juce::Colours::white.withAlpha(0.40f));
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));

    const auto&  ts        = proc_.getTrackState();
    bool         isMaster  = (ts.mode == TrackState::Mode::Master);
    bool         isPanMode = (ts.mode == TrackState::Mode::Pan);
    int ownSlotDisplay = ownSlot >= 0 ? ownSlot + 1 : 0;

    juce::String statusTxt = "Slot " + juce::String(ownSlotDisplay) + ": " + ts.label;
    if (isMaster)
    {
        statusTxt += "   [MASTER — spatial bypassed, not shown in other tracks]";
    }
    else
    {
        float        posWidth = std::abs(ts.normX * 2.0f - 1.0f);
        juce::String posStr   = isPanMode
            ? ((ts.normX < 0.5f ? "L " : "R ") + juce::String((int)(posWidth * 100)) + "%")
            : (juce::String((int)(posWidth * 100)) + "% wide");
        juce::String bandName = kBands[juce::jlimit(0, kNumBands - 1,
            (int)std::round(ts.getFractionalBand()))].name;
        statusTxt += "   [" + juce::String(isPanMode ? "Pan" : "Stereo") + "]"
                   + "   " + posStr + "   Band: " + bandName
                   + "   |  drag  |  edges: resize  |  dbl-click: rename  |  right-click: options";
    }
    g.drawText(statusTxt, 6, h - 17, w - 12, 16, juce::Justification::centredLeft, false);
}

void CanvasComponent::resized() {}

//==============================================================================
void CanvasComponent::timerCallback()
{
    auto* shared = SharedMixerState::getInstance();
    auto  states = shared->getAllStates();
    for (int i = 0; i < kMaxTracks; ++i)
    {
        if (!states[i].active) continue;
        StereoScope* scope = shared->getStereoScope(i);
        if (!scope) continue;

        std::array<float, StereoScope::kDisplaySize> tmpL{}, tmpR{};
        int got = scope->pullSamples(tmpL.data(), tmpR.data(), StereoScope::kDisplaySize);
        if (got <= 0) continue;

        int keep = juce::jmin(kScopeHistory - got, scopeCount_[i]);
        int shift = scopeCount_[i] - keep;
        for (int j = 0; j < keep; ++j)
            scopeHistory_[i][j] = scopeHistory_[i][shift + j];
        for (int j = 0; j < got; ++j)
            scopeHistory_[i][keep + j] = { tmpL[j], tmpR[j] };
        scopeCount_[i] = keep + got;
    }
    repaint();
}

//==============================================================================
void CanvasComponent::drawGoniometer (juce::Graphics& g,
                                       const std::array<TrackState, kMaxTracks>& states) const
{
    const float W = (float)getWidth();
    const float H = (float)getHeight();
    float cx     = W * 0.5f;
    float cy     = H * 0.5f;
    float radius = juce::jmin(W, H) * 0.28f;

    // Semi-transparent background circle
    g.setColour(juce::Colours::black.withAlpha(0.28f));
    g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

    // Circle outline + crosshairs
    g.setColour(juce::Colours::white.withAlpha(0.09f));
    g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 0.8f);
    g.drawLine(cx, cy - radius, cx, cy + radius, 0.5f);  // mono axis (vertical)
    g.drawLine(cx - radius, cy, cx + radius, cy, 0.5f);

    // Per-track dots
    const float sq2inv = 1.0f / std::sqrt(2.0f);
    for (int i = 0; i < kMaxTracks; ++i)
    {
        if (!states[i].active || scopeCount_[i] == 0) continue;
        juce::Colour col = SharedMixerState::trackColour(i);
        int count = scopeCount_[i];
        int split = count * 3 / 4;

        auto drawDots = [&](int start, int end, float alpha)
        {
            if (start >= end) return;
            juce::Path p;
            for (int s = start; s < end; ++s)
            {
                float l  = scopeHistory_[i][s].l;
                float r  = scopeHistory_[i][s].r;
                float px = cx + (r - l) * sq2inv * radius;
                float py = cy - (r + l) * sq2inv * radius;
                p.addEllipse(px - 1.0f, py - 1.0f, 2.0f, 2.0f);
            }
            g.setColour(col.withAlpha(alpha));
            g.fillPath(p);
        };

        drawDots(0,     split, 0.18f);
        drawDots(split, count, 0.60f);
    }
}

//==============================================================================
void CanvasComponent::mouseDown (const juce::MouseEvent& e)
{
    if (labelEditor_) { commitEdit(); return; }

    auto  states   = SharedMixerState::getInstance()->getAllStates();
    bool  isMirror = false;
    int   idx      = hitTest(states, e.position, isMirror);

    // Right-click → priority context menu
    if (e.mods.isRightButtonDown())
    {
        if (idx < 0) return;
        int currentPriority = states[idx].priority;

        bool isCurrentlyPan = (states[idx].mode == TrackState::Mode::Pan);

        juce::PopupMenu menu;
        menu.addSectionHeader(states[idx].label);
        menu.addItem(1, "Bring Forward",  currentPriority > 0);
        menu.addItem(2, "Send Back");
        if (currentPriority > 0)
            menu.addItem(3, "Reset Priority");
        menu.addSeparator();
        menu.addItem(4, isCurrentlyPan ? "Switch to Stereo Widening" : "Switch to Pan Mode");
        menu.addSeparator();
        menu.addItem(5, "Change Colour...");

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [this, idx, currentPriority, isCurrentlyPan] (int result)
            {
                if (result == 1)
                    SharedMixerState::getInstance()->setPriority(idx, currentPriority - 1);
                else if (result == 2)
                    SharedMixerState::getInstance()->setPriority(idx, currentPriority + 1);
                else if (result == 3)
                    SharedMixerState::getInstance()->setPriority(idx, 0);
                else if (result == 4)
                    SharedMixerState::getInstance()->setMode(
                        idx, isCurrentlyPan ? TrackState::Mode::Stereo : TrackState::Mode::Pan);
                else if (result == 5)
                {
                    auto picker = std::make_unique<TrackColourPicker>(idx, [this] { repaint(); });
                    juce::CallOutBox::launchAsynchronously(std::move(picker),
                        getScreenBounds(), nullptr);
                }
            });
        return;
    }

    if (idx < 0) return;

    draggedSlot_  = idx;
    dragIsMirror_ = isMirror;
    dragStartMouse_ = e.position;

    // Use whichever box was grabbed to seed drag start coords
    auto r = isMirror ? mirrorBoxRect(states[idx]) : boxRect(states[idx]);
    dragMode_         = hitZone(r, e.position);
    dragStartCentreX_ = r.getCentreX();
    dragStartCentreY_ = r.getCentreY();
    dragStartTopY_    = r.getY();
    dragStartBottomY_ = r.getBottom();
}

void CanvasComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedSlot_ < 0 || dragMode_ == DragMode::None) return;

    auto* shared = SharedMixerState::getInstance();
    const float W = (float)getWidth();
    const float H = (float)getHeight();
    auto delta = e.position - dragStartMouse_;

    // When dragging the mirror box, X movement is inverted so both boxes
    // track the mouse naturally from whichever side was grabbed.
    float xSign = dragIsMirror_ ? -1.0f : 1.0f;

    if (dragMode_ == DragMode::Move)
    {
        float rawCX = dragStartCentreX_ + delta.x * xSign;
        float nx = juce::jlimit(kBoxW * 0.5f / W, 1.0f - kBoxW * 0.5f / W, rawCX / W);
        // In Pan mode the single box can go anywhere; in Stereo mode keep primary
        // box on the left half so the mirror stays on the right half.
        bool isPan = (shared->getAllStates()[draggedSlot_].mode == TrackState::Mode::Pan);
        if (!dragIsMirror_ && !isPan)
            nx = juce::jlimit(0.0f, 0.5f, nx);
        float ny = juce::jlimit(0.0f, 1.0f, (dragStartCentreY_ + delta.y) / H);
        shared->setPosition(draggedSlot_, nx, ny);
    }
    else if (dragMode_ == DragMode::ResizeTop)
    {
        float newTop = juce::jlimit(0.0f, dragStartBottomY_ - kMinBoxPx,
                                    dragStartTopY_ + delta.y);
        float nx = shared->getAllStates()[draggedSlot_].normX;
        shared->setPosition(draggedSlot_, nx, (newTop + dragStartBottomY_) * 0.5f / H);
        shared->setHeight  (draggedSlot_, (dragStartBottomY_ - newTop) / H);
    }
    else if (dragMode_ == DragMode::ResizeBottom)
    {
        float newBot = juce::jlimit(dragStartTopY_ + kMinBoxPx, H,
                                    dragStartBottomY_ + delta.y);
        float nx = shared->getAllStates()[draggedSlot_].normX;
        shared->setPosition(draggedSlot_, nx, (dragStartTopY_ + newBot) * 0.5f / H);
        shared->setHeight  (draggedSlot_, (newBot - dragStartTopY_) / H);
    }
}

void CanvasComponent::mouseUp (const juce::MouseEvent&)
{
    draggedSlot_  = -1;
    dragIsMirror_ = false;
    dragMode_     = DragMode::None;
}

void CanvasComponent::mouseMove (const juce::MouseEvent& e)
{
    auto  states = SharedMixerState::getInstance()->getAllStates();
    bool  isMirror = false;
    int   slot  = hitTest(states, e.position, isMirror);

    bool changed = (slot != hoveredSlot_ || isMirror != hoverIsMirror_);
    hoveredSlot_   = slot;
    hoverIsMirror_ = isMirror;
    if (changed) repaint();

    if (slot >= 0)
    {
        auto r    = isMirror ? mirrorBoxRect(states[slot]) : boxRect(states[slot]);
        auto zone = hitZone(r, e.position);
        if (zone == DragMode::ResizeTop || zone == DragMode::ResizeBottom)
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        else
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }
    else
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void CanvasComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto  states = SharedMixerState::getInstance()->getAllStates();
    bool  isMirror = false;
    int   idx = hitTest(states, e.position, isMirror);
    if (idx >= 0) startEditing(idx);
}

//==============================================================================
void CanvasComponent::startEditing (int slot)
{
    commitEdit();
    editingSlot_ = slot;

    auto states = SharedMixerState::getInstance()->getAllStates();
    auto rect   = boxRect(states[slot]).toNearestInt();
    int  edW    = juce::jmax(130, rect.getWidth() + 50);
    int  edH    = 28;

    labelEditor_ = std::make_unique<juce::TextEditor>();
    labelEditor_->setBounds(rect.getCentreX() - edW / 2, rect.getCentreY() - edH / 2, edW, edH);
    labelEditor_->setText(states[slot].label, false);
    labelEditor_->selectAll();
    labelEditor_->setFont(juce::Font(juce::FontOptions().withHeight(14.0f)));
    labelEditor_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha(0.9f));
    labelEditor_->setColour(juce::TextEditor::textColourId,       juce::Colours::white);
    labelEditor_->setColour(juce::TextEditor::outlineColourId,    SharedMixerState::trackColour(slot));
    labelEditor_->onReturnKey = [this] { commitEdit(); };
    labelEditor_->onEscapeKey = [this] { labelEditor_.reset(); editingSlot_ = -1; };
    labelEditor_->onFocusLost = [this] { commitEdit(); };

    addAndMakeVisible(*labelEditor_);
    labelEditor_->grabKeyboardFocus();
}

void CanvasComponent::commitEdit()
{
    if (!labelEditor_ || editingSlot_ < 0) return;
    juce::String txt = labelEditor_->getText().trim();
    if (txt.isNotEmpty())
        SharedMixerState::getInstance()->setLabel(editingSlot_, txt);
    labelEditor_.reset();
    editingSlot_ = -1;
    repaint();
}
