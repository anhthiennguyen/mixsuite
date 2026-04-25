#pragma once
#include <JuceHeader.h>

// Thread-safe spectrum analyser.
// Audio thread calls pushSamples(); message thread calls pullSpectrum().
class SpectrumAnalyser
{
public:
    static constexpr int fftOrder = 11;       // 2048-point FFT
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int numBins  = fftSize / 2;
    static constexpr int fifoSize = fftSize * 4;

    explicit SpectrumAnalyser (float smoothing = 0.75f)
        : fft_(fftOrder),
          window_(fftSize, juce::dsp::WindowingFunction<float>::hann),
          abstractFifo_(fifoSize),
          smoothing_(smoothing)
    {
        fifoBuffer_.fill(0.0f);
        fftData_.fill(0.0f);
        accumulator_.fill(0.0f);
        spectrum_.fill(-100.0f);
    }

    void prepare(double sampleRate) { sampleRate_ = sampleRate; }
    double getSampleRate() const    { return sampleRate_; }

    // Called from audio thread
    void pushSamples(const float* L, const float* R, int numSamples)
    {
        int s1, n1, s2, n2;
        abstractFifo_.prepareToWrite(numSamples, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i)
            fifoBuffer_[s1 + i] = (L[i] + R[i]) * 0.5f;
        for (int i = 0; i < n2; ++i)
            fifoBuffer_[s2 + i] = (L[n1 + i] + R[n1 + i]) * 0.5f;
        abstractFifo_.finishedWrite(n1 + n2);
    }

    // Called from message thread — returns true if spectrum was updated
    bool pullSpectrum()
    {
        int available = abstractFifo_.getNumReady();
        if (available < 1) return false;

        bool updated = false;
        int s1, n1, s2, n2;
        abstractFifo_.prepareToRead(available, s1, n1, s2, n2);

        for (int i = 0; i < n1; ++i)
        {
            accumulator_[accPos_] = fifoBuffer_[s1 + i];
            if (++accPos_ == fftSize) { runFFT(); accPos_ = 0; updated = true; }
        }
        for (int i = 0; i < n2; ++i)
        {
            accumulator_[accPos_] = fifoBuffer_[s2 + i];
            if (++accPos_ == fftSize) { runFFT(); accPos_ = 0; updated = true; }
        }
        abstractFifo_.finishedRead(n1 + n2);
        return updated;
    }

    const std::array<float, numBins>& getSpectrum() const { return spectrum_; }

private:
    void runFFT()
    {
        std::copy(accumulator_.begin(), accumulator_.end(), fftData_.begin());
        std::fill(fftData_.begin() + fftSize, fftData_.end(), 0.0f);
        window_.multiplyWithWindowingTable(fftData_.data(), fftSize);
        fft_.performFrequencyOnlyForwardTransform(fftData_.data());

        for (int i = 0; i < numBins; ++i)
        {
            float db = juce::Decibels::gainToDecibels(fftData_[i] / (float)fftSize, -100.0f);
            spectrum_[i] = spectrum_[i] * smoothing_ + db * (1.0f - smoothing_);
        }
    }

    juce::dsp::FFT fft_;
    juce::dsp::WindowingFunction<float> window_;
    juce::AbstractFifo abstractFifo_;

    std::array<float, fifoSize>       fifoBuffer_{};
    std::array<float, fftSize>        accumulator_{};
    std::array<float, fftSize * 2>    fftData_{};
    std::array<float, numBins>        spectrum_{};

    int    accPos_     = 0;
    double sampleRate_ = 44100.0;
    float  smoothing_  = 0.75f;
};
