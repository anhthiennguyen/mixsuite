#include "DSPEngine.h"

DSPEngine::DSPEngine() {}

void DSPEngine::prepare (double sampleRate, int samplesPerBlock)
{
    sampleRate_   = sampleRate;
    maxBlockSize_ = samplesPerBlock;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = 1;

    filterL_.prepare(spec);
    filterR_.prepare(spec);
    filterL_.reset();
    filterR_.reset();

    spectrumAnalyser_.prepare(sampleRate);
    widthSmoother_.reset(sampleRate, 0.02);
    widthSmoother_.setCurrentAndTargetValue(0.0f);

    gainSmoother_.reset(sampleRate, 0.05);  // 50ms ramp — slow enough to avoid clicks
    gainSmoother_.setCurrentAndTargetValue(1.0f);
}

void DSPEngine::reset()
{
    filterL_.reset();
    filterR_.reset();
    lastCenterHz_ = -1.0f;
}

void DSPEngine::updateParams (const TrackDSPParams& params)
{
    juce::SpinLock::ScopedLockType lock(pendingLock_);
    pendingParams_ = params;
    paramsDirty_.store(true, std::memory_order_release);
}

void DSPEngine::initParams (const TrackDSPParams& params)
{
    activeParams_  = params;
    pendingParams_ = params;
    paramsDirty_.store(false, std::memory_order_release);
    rebuildFilter(params.eqCenterHz, params.eqBandwidth, params.eqGainDb);
    lastCenterHz_  = params.eqCenterHz;
    lastBandwidth_ = params.eqBandwidth;
    lastEqGainDb_  = params.eqGainDb;

    widthSmoother_.setCurrentAndTargetValue(std::abs(params.panNormalized));
    gainSmoother_.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(params.gainDb));
}

void DSPEngine::syncParams()
{
    if (!paramsDirty_.load(std::memory_order_acquire)) return;

    {
        juce::SpinLock::ScopedTryLockType lock(pendingLock_);
        if (!lock.isLocked()) return;
        activeParams_ = pendingParams_;
        paramsDirty_.store(false, std::memory_order_release);
    }

    float center = juce::jlimit(20.0f, 20000.0f, activeParams_.eqCenterHz);
    if (std::abs(center - lastCenterHz_)                    > 0.5f  ||
        std::abs(activeParams_.eqBandwidth - lastBandwidth_) > 0.01f ||
        std::abs(activeParams_.eqGainDb    - lastEqGainDb_)  > 0.1f)
    {
        rebuildFilter(center, activeParams_.eqBandwidth, activeParams_.eqGainDb);
        lastCenterHz_  = center;
        lastBandwidth_ = activeParams_.eqBandwidth;
        lastEqGainDb_  = activeParams_.eqGainDb;
    }

    // In pan mode use the raw pan value; in stereo mode use distance from centre
    float targetWidth = activeParams_.isPanMode ? 0.0f
                                                : std::abs(activeParams_.panNormalized);
    widthSmoother_.setTargetValue(targetWidth);
    gainSmoother_.setTargetValue(juce::Decibels::decibelsToGain(activeParams_.gainDb));
}

void DSPEngine::processBlock (float* L, float* R, int numSamples)
{
    spectrumAnalyser_.pushSamples(L, R, numSamples);

    for (int n = 0; n < numSamples; ++n)
    {
        // Apply EQ to both channels independently
        float inL = filterL_.processSample(L[n]);
        float inR = filterR_.processSample(R[n]);

        float gain = gainSmoother_.getNextValue();

        if (activeParams_.isPanMode)
        {
            // Hard pan: sum to mono, then apply constant-power pan law
            float pan   = juce::jlimit(-1.0f, 1.0f, activeParams_.panNormalized);
            float angle = (pan + 1.0f) * 0.5f * juce::MathConstants<float>::halfPi;
            float gainL = std::cos(angle);
            float gainR = std::sin(angle);
            float mono  = (inL + inR) * 0.5f;
            L[n] = mono * gainL * gain;
            R[n] = mono * gainR * gain;
        }
        else
        {
            // MS stereo widening: 0 = mono, 1 = original width, 2 = double wide
            float M        = (inL + inR) * 0.5f;
            float S        = (inL - inR) * 0.5f;
            float width    = widthSmoother_.getNextValue();
            float widenedS = S * (width * 2.0f);
            L[n] = (M + widenedS) * gain;
            R[n] = (M - widenedS) * gain;
        }
    }

    stereoScope_.pushSamples(L, R, numSamples);
}

void DSPEngine::rebuildFilter (float centerHz, float bandwidthOct, float gainDb)
{
    float bw   = juce::jlimit(0.1f, 4.0f, bandwidthOct);
    float root = std::pow(2.0f, bw);
    float Q    = juce::jlimit(0.1f, 30.0f, std::sqrt(root) / (root - 1.0f));

    auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate_, (double)centerHz, (double)Q,
        (double)juce::Decibels::decibelsToGain(gainDb));

    *filterL_.coefficients = *coeffs;
    *filterR_.coefficients = *coeffs;
}
