#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SharedAnalyserState.h"
#include "SharedMixerState.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MixSuiteProcessor::createParams()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < kNumEQBands; ++i)
    {
        juce::String p = "band" + juce::String(i) + "_";
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "freq", 1),
            "Band " + juce::String(i + 1) + " Freq",
            juce::NormalisableRange<float>(20.0f, 20000.0f, 0.1f, 0.25f),
            kEQDefaultFreqs[i]));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "gain", 1),
            "Band " + juce::String(i + 1) + " Gain",
            juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
            0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "q", 1),
            "Band " + juce::String(i + 1) + " Q",
            juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f, 0.5f),
            kEQDefaultQs[i]));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(p + "enabled", 1),
            "Band " + juce::String(i + 1) + " On",
            i < kDefaultEQBands));
    }
    return layout;
}

MixSuiteProcessor::MixSuiteProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "MixSuite", createParams())
{
    trackState_.computeDSP();
    int eqSlot   = SharedAnalyserState::getInstance()->registerProcessor(this);
    int spatSlot = SharedMixerState::getInstance()->registerProcessor(this, trackState_);
    slotIndex_   = eqSlot >= 0 ? eqSlot : spatSlot;
    if (slotIndex_ >= 0)
        trackState_.label = "Track " + juce::String(slotIndex_ + 1);
}

MixSuiteProcessor::~MixSuiteProcessor()
{
    if (auto* s = SharedAnalyserState::getInstance())
        s->unregisterProcessor(slotIndex_);
    SharedMixerState::getInstance()->unregisterProcessor(slotIndex_);
}

//==============================================================================
void MixSuiteProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)samplesPerBlock, 1 };
    for (auto& f : filtersL_) { f.prepare(spec); f.reset(); }
    for (auto& f : filtersR_) { f.prepare(spec); f.reset(); }
    eqAnalyser_.prepare(sampleRate);
    hintsAnalyser_.prepare(sampleRate);
    updateEQFilters();
    spatialDSP_.prepare(sampleRate, samplesPerBlock);
    trackState_.computeDSP();
    spatialDSP_.initParams(trackState_.dsp);
}

void MixSuiteProcessor::releaseResources()
{
    spatialDSP_.reset();
}

void MixSuiteProcessor::updateEQFilters()
{
    for (int i = 0; i < kNumEQBands; ++i)
    {
        juce::String p    = "band" + juce::String(i) + "_";
        float freq = *apvts_.getRawParameterValue(p + "freq");
        float gain = *apvts_.getRawParameterValue(p + "gain");
        float q    = *apvts_.getRawParameterValue(p + "q");
        bool  on   = *apvts_.getRawParameterValue(p + "enabled") > 0.5f;
        float gl   = (on && eqEnabled_) ? juce::Decibels::decibelsToGain(gain) : 1.0f;

        juce::ReferenceCountedObjectPtr<FilterCoefs> c;
        if      (kEQBandTypes[i] == BandType::LowShelf)
            c = FilterCoefs::makeLowShelf  (sampleRate_, (double)freq, (double)q, (double)gl);
        else if (kEQBandTypes[i] == BandType::HighShelf)
            c = FilterCoefs::makeHighShelf (sampleRate_, (double)freq, (double)q, (double)gl);
        else
            c = FilterCoefs::makePeakFilter(sampleRate_, (double)freq, (double)q, (double)gl);

        *filtersL_[i].coefficients = *c;
        *filtersR_[i].coefficients = *c;
    }
}

void MixSuiteProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    int    n = buffer.getNumSamples();

    // --- EQ stage ---
    updateEQFilters();
    for (int i = 0; i < kNumEQBands; ++i)
        for (int s = 0; s < n; ++s)
        {
            L[s] = filtersL_[i].processSample(L[s]);
            if (R) R[s] = filtersR_[i].processSample(R[s]);
        }
    eqAnalyser_.pushSamples(L, R ? R : L, n);
    hintsAnalyser_.pushSamples(L, R ? R : L, n);

    // --- Spatial stage ---
    if (spatialEnabled_)
    {
        spatialDSP_.syncParams();
        spatialDSP_.processBlock(L, R ? R : L, n);
    }
}

//==============================================================================
void MixSuiteProcessor::setTrackPosition (float normX, float normY)
{
    trackState_.normX = normX;
    trackState_.normY = normY;
}

void MixSuiteProcessor::setTrackHeight (float normHeight)
{
    trackState_.normHeight = juce::jlimit(0.05f, 0.95f, normHeight);
}

void MixSuiteProcessor::setTrackLabel (const juce::String& label)
{
    trackState_.label = label;
    SharedMixerState::getInstance()->pushState(slotIndex_, trackState_);
}

void MixSuiteProcessor::setTrackPriority (int priority)
{
    trackState_.priority = juce::jmax(0, priority);
    SharedMixerState::getInstance()->setPriority(slotIndex_, trackState_.priority);
}

void MixSuiteProcessor::setTrackMode (TrackState::Mode mode)
{
    trackState_.mode = mode;
    pushSpatialParams();
}

void MixSuiteProcessor::pushSpatialParams()
{
    trackState_.computeDSP();
    SharedMixerState::getInstance()->pushState(slotIndex_, trackState_);
}

void MixSuiteProcessor::applyResolvedDSP (const TrackDSPParams& params)
{
    trackState_.dsp = params;
    spatialDSP_.updateParams(params);
}

//==============================================================================
void MixSuiteProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement root("MixSuiteState");

    // EQ state
    auto eqState = apvts_.copyState();
    if (auto* eqXml = eqState.createXml().release())
        root.addChildElement(eqXml);

    // Spatial state
    auto* spatXml = root.createNewChildElement("Spatial");
    spatXml->setAttribute("x",         trackState_.normX);
    spatXml->setAttribute("y",         trackState_.normY);
    spatXml->setAttribute("height",    trackState_.normHeight);
    spatXml->setAttribute("label",     trackState_.label);
    spatXml->setAttribute("priority",  trackState_.priority);
    spatXml->setAttribute("isPanMode", trackState_.mode == TrackState::Mode::Pan ? 1 : 0);
    spatXml->setAttribute("eqEnabled",      eqEnabled_      ? 1 : 0);
    spatXml->setAttribute("spatialEnabled", spatialEnabled_ ? 1 : 0);
    spatXml->setAttribute("trackColourARGB", (int)SharedAnalyserState::trackColour(slotIndex_).getARGB());

    copyXmlToBinary(root, dest);
}

void MixSuiteProcessor::setStateInformation (const void* data, int size)
{
    auto root = getXmlFromBinary(data, size);
    if (!root || !root->hasTagName("MixSuiteState")) return;

    // EQ state
    if (auto* eqXml = root->getFirstChildElement())
    {
        auto vt = juce::ValueTree::fromXml(*eqXml);
        if (vt.isValid()) apvts_.replaceState(vt);
    }

    // Spatial state
    if (auto* s = root->getChildByName("Spatial"))
    {
        trackState_.normX      = (float)s->getDoubleAttribute("x",        0.5);
        trackState_.normY      = (float)s->getDoubleAttribute("y",        0.5);
        trackState_.normHeight = (float)s->getDoubleAttribute("height",   0.25);
        trackState_.label      =        s->getStringAttribute("label",    trackState_.label);
        trackState_.priority   =        s->getIntAttribute   ("priority", 0);
        trackState_.mode       = s->getIntAttribute("isPanMode", 0)
                               ? TrackState::Mode::Pan : TrackState::Mode::Stereo;
        eqEnabled_      = s->getIntAttribute("eqEnabled",      1) != 0;
        spatialEnabled_ = s->getIntAttribute("spatialEnabled", 1) != 0;
        int colourARGB  = s->getIntAttribute("trackColourARGB", -1);
        if (colourARGB != -1 && slotIndex_ >= 0)
            SharedAnalyserState::getInstance()->setTrackColour(slotIndex_, juce::Colour((juce::uint32)colourARGB));
        pushSpatialParams();
    }
}

//==============================================================================
juce::AudioProcessorEditor* MixSuiteProcessor::createEditor()
{
    return new MixSuiteEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixSuiteProcessor();
}
