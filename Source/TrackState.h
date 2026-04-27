#pragma once
#include <JuceHeader.h>

static constexpr int kMaxTracks = 8;
static constexpr int kNumBands  = 5;

struct BandDef { const char* name; float centerHz; };

static constexpr BandDef kBands[kNumBands] = {
    { "Low",      80.0f    },
    { "Low-Mid",  300.0f   },
    { "Mid",      1000.0f  },
    { "High-Mid", 3500.0f  },
    { "High",     10000.0f },
};

struct TrackDSPParams
{
    float panNormalized = 0.0f;
    float eqCenterHz   = 1000.0f;
    float eqBandwidth  = 1.5f;
    float eqGainDb     = 6.0f;
    float gainDb       = 0.0f;   // priority-based gain reduction (0, -6, -12, ...)
    bool  isPanMode    = false;  // true = hard mono pan, false = MS stereo widening
};

struct TrackState
{
    enum class Mode { Stereo, Pan, Master };

    juce::String label      = "Track";
    float normX             = 0.5f;
    float normY             = 0.5f;
    float normHeight        = 0.25f;
    int   priority          = 0;
    Mode  mode              = Mode::Stereo;
    bool  active            = false;
    TrackDSPParams dsp;

    float getPan() const { return normX * 2.0f - 1.0f; }

    float getFractionalBand() const
    {
        float frac = (1.0f - normY) * (kNumBands - 1);
        return juce::jlimit(0.0f, float(kNumBands - 1), frac);
    }

    float getBandwidthOctaves() const
    {
        return juce::jlimit(0.3f, 4.0f, normHeight * 5.0f);
    }

    void computeDSP()
    {
        if (mode == Mode::Master) { dsp = TrackDSPParams{}; return; }
        dsp.isPanMode     = (mode == Mode::Pan);
        dsp.panNormalized = getPan();
        float frac  = getFractionalBand();
        int   lo    = juce::jlimit(0, kNumBands - 1, (int)std::floor(frac));
        int   hi    = juce::jlimit(0, kNumBands - 1, lo + 1);
        float blend = frac - (float)lo;
        dsp.eqCenterHz  = kBands[lo].centerHz
                        * std::pow(kBands[hi].centerHz / kBands[lo].centerHz, blend);
        dsp.eqBandwidth = getBandwidthOctaves();
        dsp.eqGainDb    = 6.0f;
    }
};
