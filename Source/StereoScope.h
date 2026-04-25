#pragma once
#include <JuceHeader.h>

// Thread-safe stereo L/R sample capture for goniometer display.
// Audio thread pushes; message thread pulls.
class StereoScope
{
public:
    static constexpr int kFifoSize    = 4096;
    static constexpr int kDisplaySize = 512;   // samples drawn per frame

    StereoScope() : fifo_(kFifoSize) { bufL_.fill(0.0f); bufR_.fill(0.0f); }

    // Audio thread
    void pushSamples (const float* L, const float* R, int numSamples)
    {
        int s1, n1, s2, n2;
        fifo_.prepareToWrite(numSamples, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { bufL_[s1 + i] = L[i]; bufR_[s1 + i] = R[i]; }
        for (int i = 0; i < n2; ++i) { bufL_[s2 + i] = L[n1 + i]; bufR_[s2 + i] = R[n1 + i]; }
        fifo_.finishedWrite(n1 + n2);
    }

    // Message thread — fills outL/outR with up to maxPairs samples, returns count
    int pullSamples (float* outL, float* outR, int maxPairs)
    {
        int available = juce::jmin(fifo_.getNumReady(), maxPairs);
        if (available <= 0) return 0;
        int s1, n1, s2, n2;
        fifo_.prepareToRead(available, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { outL[i]      = bufL_[s1 + i]; outR[i]      = bufR_[s1 + i]; }
        for (int i = 0; i < n2; ++i) { outL[n1 + i] = bufL_[s2 + i]; outR[n1 + i] = bufR_[s2 + i]; }
        fifo_.finishedRead(n1 + n2);
        return n1 + n2;
    }

private:
    juce::AbstractFifo fifo_;
    std::array<float, kFifoSize> bufL_{}, bufR_{};
};
