#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <array>
#include <vector>

namespace th::dsp
{
    //==========================================================================
    // Shape functions (namespace-level, branchless where possible)
    //==========================================================================
    namespace shape
    {
        // Hard clip f(x) = clamp(x, -1, 1)
        inline float hardClip (float x) noexcept
        {
            return juce::jlimit (-1.0f, 1.0f, x);
        }

        // Antiderivative of hard clip:
        //   F(x) = x²/2            if |x| <= 1
        //   F(x) = |x| - 1/2       otherwise
        inline float F_hardClip (float x) noexcept
        {
            const float ax = std::abs (x);
            return ax <= 1.0f ? x * x * 0.5f : ax - 0.5f;
        }

        // Level-matched soft clip: tanh(g*x) / tanh(g)
        // Always maps |x|=1 to |y|=1 exactly, regardless of sharpness.
        inline float softClip (float x, float g) noexcept
        {
            const float ty = std::tanh (g * x);
            const float tg = std::tanh (g);
            return ty / tg;
        }

        // Antiderivative of the level-matched soft clip:
        //   F(x) = log(cosh(g*x)) / (g * tanh(g))
        // Numerically stable for large |g*x| (cosh overflows float32 > ~88).
        // Uses the asymptote log(cosh(z)) ≈ |z| - log(2) for |z| > 20.
        inline float F_softClip (float x, float g) noexcept
        {
            const float gx    = g * x;
            const float absgx = std::abs (gx);
            const float logCosh = (absgx > 20.0f)
                ? (absgx - 0.693147181f /* log(2) */)
                : std::log (std::cosh (gx));
            const float tg = std::tanh (g);
            return logCosh / (g * tg);
        }
    }

    //==========================================================================
    // One-pole DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1]
    // Cutoff: 15 Hz
    //==========================================================================
    class DCBlocker
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            const double target = 15.0;
            R = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * target / sampleRate);
            reset();
        }

        void reset() noexcept { xPrev = 0.0f; yPrev = 0.0f; }

        float process (float x) noexcept
        {
            const float y = x - xPrev + R * yPrev;
            xPrev = x;
            yPrev = y;
            return y;
        }

    private:
        float R     { 0.9975f };
        float xPrev { 0.0f };
        float yPrev { 0.0f };
    };

    //==========================================================================
    // RMS envelope follower
    //==========================================================================
    class RmsFollower
    {
    public:
        void prepare (double sampleRate, float timeConstantMs) noexcept
        {
            const float t = timeConstantMs * 0.001f;
            a = std::exp (-1.0f / (float) (sampleRate * t));
            sq = 0.0f;
        }

        void reset() noexcept { sq = 0.0f; }

        float process (float x) noexcept
        {
            const float x2 = x * x;
            sq = a * sq + (1.0f - a) * x2;
            return std::sqrt (sq + 1.0e-20f);
        }

    private:
        float a  { 0.999f };
        float sq { 0.0f };
    };

    //==========================================================================
    // ClipperCore — the TRAP HOUSE DSP engine.
    //
    // Signal chain (per sample, inside 4x oversampled block):
    //   x
    //   -> input gain
    //   -> pre-shape (character: HARD bypass, TAPE asinh, TUBE asymmetric)
    //   -> ADAA-blended clip (hard ADAA | soft ADAA, blended by SOFT KNEE)
    //   -> parallel harmonic injection (Chebyshev 2nd + 3rd, bias by character)
    //   -> final normalized-ceiling clamp (safety)
    //   -> back to output domain (× ceiling)
    //   -> DC blocker
    //   -> (optional) RMS auto-gain makeup post-downsample
    //
    // Key DSP techniques:
    //   * ADAA 1st order on both clip curves (20+ dB extra aliasing attenuation)
    //   * Level-matched soft clip so SOFT KNEE morphs without loudness jumps
    //   * Chebyshev harmonic injection with character-dependent even/odd bias
    //   * RMS auto-gain via dual envelope followers
    //==========================================================================
    class ClipperCore
    {
    public:
        enum class Character { Hard = 0, Tape, Tube };

        void prepare (double sampleRate, int maxBlockSize, int numChannels)
        {
            oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
                numChannels,
                2, // factor 2^2 = 4x
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                true);

            oversampler->initProcessing ((size_t) maxBlockSize);
            oversampler->reset();

            latencySamples = (int) std::ceil (oversampler->getLatencyInSamples());

            const double osRate = sampleRate * 4.0;
            inputGainSmoothed .reset (osRate, 0.04);
            ceilingSmoothed   .reset (osRate, 0.04);
            kneeSmoothed      .reset (osRate, 0.04);
            harmonicsSmoothed .reset (osRate, 0.04);

            const int nCh = juce::jmax (1, numChannels);
            dcBlocker.resize ((size_t) nCh);
            for (auto& d : dcBlocker) d.prepare (osRate);

            adaaState.assign ((size_t) nCh, AdaaState{});

            inRms .resize ((size_t) nCh);
            outRms.resize ((size_t) nCh);
            for (auto& r : inRms)  r.prepare (sampleRate, 80.0f);
            for (auto& r : outRms) r.prepare (sampleRate, 80.0f);

            autoGainSmoother.reset (sampleRate, 0.200);
            autoGainSmoother.setCurrentAndTargetValue (1.0f);
        }

        void reset()
        {
            if (oversampler) oversampler->reset();
            for (auto& d : dcBlocker) d.reset();
            for (auto& s : adaaState) s = {};
            for (auto& r : inRms)  r.reset();
            for (auto& r : outRms) r.reset();
            autoGainSmoother.setCurrentAndTargetValue (1.0f);
        }

        int getLatencySamples() const noexcept { return latencySamples; }

        // Parameter setters (UI units)
        void setInputGainDb (float db)      { inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (db)); }
        void setCeilingDb   (float db)      { ceilingSmoothed  .setTargetValue (juce::Decibels::decibelsToGain (db)); }
        void setKnee        (float norm01)  { kneeSmoothed     .setTargetValue (juce::jlimit (0.0f, 1.0f, norm01)); }
        void setHarmonics   (float norm01)  { harmonicsSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, norm01)); }
        void setCharacter   (Character c)   { character = c; }
        void setAutoGain    (bool on)       { autoGain = on; }

        void process (juce::AudioBuffer<float>& buffer)
        {
            const int numSamples  = buffer.getNumSamples();
            const int numChannels = buffer.getNumChannels();
            if (numSamples <= 0 || numChannels <= 0) return;

            // --- 1. Track dry input RMS (at native rate) for auto-gain ---
            double dryRmsSum = 0.0;
            if (autoGain)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto* d = buffer.getReadPointer (ch);
                    auto& follower = inRms[(size_t) juce::jmin (ch, (int) inRms.size() - 1)];
                    for (int i = 0; i < numSamples; ++i)
                        dryRmsSum += follower.process (d[i]);
                }
            }

            // --- 2. Upsample ---
            juce::dsp::AudioBlock<float> block (buffer);
            auto osBlock = oversampler->processSamplesUp (block);

            const int osChannels = (int) osBlock.getNumChannels();
            const int osSamples  = (int) osBlock.getNumSamples();

            // Guard: growing beyond our parameter cache would be a bug.
            jassert (osSamples <= (int) cachedInGain.size());
            const int effSamples = juce::jmin (osSamples, (int) cachedInGain.size());

            // --- 3. Per-sample processing at 4x rate ---
            for (int ch = 0; ch < osChannels; ++ch)
            {
                auto* d = osBlock.getChannelPointer ((size_t) ch);
                auto& st = adaaState[(size_t) juce::jmin (ch, (int) adaaState.size() - 1)];
                auto& dc = dcBlocker[(size_t) juce::jmin (ch, (int) dcBlocker.size() - 1)];

                const bool primaryChannel = (ch == 0);

                for (int n = 0; n < effSamples; ++n)
                {
                    float inGain, ceiling, knee, harmAmt;

                    if (primaryChannel)
                    {
                        inGain  = inputGainSmoothed.getNextValue();
                        ceiling = std::max (ceilingSmoothed.getNextValue(), 1.0e-6f);
                        knee    = kneeSmoothed.getNextValue();
                        harmAmt = harmonicsSmoothed.getNextValue();

                        cachedInGain [n] = inGain;
                        cachedCeiling[n] = ceiling;
                        cachedKnee   [n] = knee;
                        cachedHarm   [n] = harmAmt;
                    }
                    else
                    {
                        inGain  = cachedInGain [n];
                        ceiling = cachedCeiling[n];
                        knee    = cachedKnee   [n];
                        harmAmt = cachedHarm   [n];
                    }

                    // Input gain
                    float x = d[n] * inGain;

                    // Character pre-shape
                    switch (character)
                    {
                        case Character::Hard:
                            break;

                        case Character::Tape:
                            // asinh is smoother than tanh — compression-like
                            x = std::asinh (x * 0.9f) / std::asinh (0.9f);
                            break;

                        case Character::Tube:
                            // Asymmetric soft saturation: positive lobe pushed, negative less.
                            // Generates even harmonics ahead of the clip.
                            x = std::tanh (x * 1.05f)
                                + 0.08f * (x * x * (x > 0.0f ? 1.0f : -1.0f));
                            break;
                    }

                    // Normalize into clip domain
                    const float xn = x / ceiling;

                    // ADAA: share xPrev, separate FPrev per curve.
                    const float dx     = xn - st.xPrev;
                    const float FhardN = shape::F_hardClip (xn);
                    const float softG  = 0.5f + (1.0f - knee) * 5.0f; // knee=0 → 5.5 (near hard), knee=1 → 0.5 (very soft)
                    const float FsoftN = shape::F_softClip  (xn, softG);

                    float hard, soft;
                    if (std::abs (dx) < 1.0e-5f)
                    {
                        const float mid = 0.5f * (xn + st.xPrev);
                        hard = shape::hardClip (mid);
                        soft = shape::softClip  (mid, softG);
                    }
                    else
                    {
                        hard = (FhardN - st.FhardPrev) / dx;
                        soft = (FsoftN - st.FsoftPrev) / dx;
                    }

                    st.xPrev     = xn;
                    st.FhardPrev = FhardN;
                    st.FsoftPrev = FsoftN;

                    // Blend: both curves clip at ±1 so no level mismatch.
                    float yn = (1.0f - knee) * hard + knee * soft;

                    // Parallel harmonic injection
                    if (harmAmt > 0.0f)
                    {
                        float biasEven;
                        switch (character)
                        {
                            case Character::Hard: biasEven = 0.30f; break;
                            case Character::Tape: biasEven = 0.60f; break;
                            case Character::Tube: biasEven = 0.80f; break;
                            default:              biasEven = 0.50f; break;
                        }

                        // 2nd harmonic (sign-preserved, centred at 0)
                        const float h2 = (2.0f * yn * yn) * (yn >= 0.0f ? 1.0f : -1.0f);
                        // 3rd harmonic (Chebyshev T3)
                        const float h3 = 4.0f * yn * yn * yn - 3.0f * yn;

                        const float h = biasEven * h2 + (1.0f - biasEven) * h3;
                        yn += harmAmt * 0.30f * h;
                    }

                    // Soft safety clamp in normalised domain (cheap overshoot control)
                    yn = juce::jlimit (-1.0f, 1.0f, yn);

                    // Back to output domain
                    float y = yn * ceiling;

                    // DC blocker (removes asymmetric processing DC)
                    y = dc.process (y);

                    // *** HARD ceiling guarantee ***
                    // The DC blocker is a 1st-order recursive HPF and can overshoot
                    // by ~1-2 dB on transients. Final clamp at the REAL ceiling
                    // guarantees no peak escapes.
                    y = juce::jlimit (-ceiling, ceiling, y);

                    d[n] = y;
                }
            }

            // --- 4. Downsample ---
            oversampler->processSamplesDown (block);

            // --- 5. Auto-gain RMS makeup ---
            if (autoGain)
            {
                double wetRmsSum = 0.0;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto* d = buffer.getReadPointer (ch);
                    auto& follower = outRms[(size_t) juce::jmin (ch, (int) outRms.size() - 1)];
                    for (int i = 0; i < numSamples; ++i)
                        wetRmsSum += follower.process (d[i]);
                }

                const double total = (double) juce::jmax (1, numSamples * numChannels);
                const double dryAvg = dryRmsSum / total;
                const double wetAvg = wetRmsSum / total;

                if (wetAvg > 1.0e-6 && dryAvg > 1.0e-6)
                {
                    const float ratio = (float) juce::jlimit (0.1, 3.0, dryAvg / wetAvg);
                    autoGainSmoother.setTargetValue (ratio);
                }

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto* d = buffer.getWritePointer (ch);
                    for (int i = 0; i < numSamples; ++i)
                        d[i] *= autoGainSmoother.getNextValue();
                }
            }
            else
            {
                // Bleed back to 1.0 so toggling doesn't pop
                autoGainSmoother.setTargetValue (1.0f);
                for (int i = 0; i < numSamples; ++i)
                    (void) autoGainSmoother.getNextValue();
            }
        }

    private:
        struct AdaaState
        {
            float xPrev     { 0.0f };
            float FhardPrev { 0.0f };
            float FsoftPrev { 0.0f };
        };

        std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
        int latencySamples { 0 };

        juce::LinearSmoothedValue<float> inputGainSmoothed { 1.0f };
        juce::LinearSmoothedValue<float> ceilingSmoothed   { 1.0f };
        juce::LinearSmoothedValue<float> kneeSmoothed      { 0.25f };
        juce::LinearSmoothedValue<float> harmonicsSmoothed { 0.40f };

        Character character { Character::Hard };
        bool autoGain { false };

        std::vector<AdaaState> adaaState;
        std::vector<DCBlocker> dcBlocker;
        std::vector<RmsFollower> inRms, outRms;
        juce::LinearSmoothedValue<float> autoGainSmoother { 1.0f };

        // Per-sample cache so L/R share smoothed values (oversampled block).
        std::array<float, 8192> cachedInGain  {};
        std::array<float, 8192> cachedCeiling {};
        std::array<float, 8192> cachedKnee    {};
        std::array<float, 8192> cachedHarm    {};
    };
}
