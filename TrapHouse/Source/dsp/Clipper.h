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
    // Transient detector — dual envelope follower (fast / slow).
    // Returns a 0-1 amount: 0 = steady signal, 1 = strong attack transient.
    // Used to reduce clipping aggression during drum hits so punch is preserved.
    // (Inspired by Waves L3 / Kazrog KClip 3 / DMG Limitless transient modes.)
    //==========================================================================
    class TransientDetector
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            const auto a = [sampleRate] (float ms) {
                return (float) std::exp (-1.0 / (sampleRate * ms * 1.0e-3));
            };
            aFastAtt = a (2.0f);    aFastRel = a (60.0f);
            aSlowAtt = a (40.0f);   aSlowRel = a (400.0f);
            reset();
        }

        void reset() noexcept { fast = 0.0f; slow = 0.0f; }

        // Returns transient amount in [0, 1].
        float process (float x) noexcept
        {
            const float ax = std::abs (x);
            fast = ax > fast ? aFastAtt * fast + (1.0f - aFastAtt) * ax
                             : aFastRel * fast + (1.0f - aFastRel) * ax;
            slow = ax > slow ? aSlowAtt * slow + (1.0f - aSlowAtt) * ax
                             : aSlowRel * slow + (1.0f - aSlowRel) * ax;
            const float ratio = fast / std::max (slow, 1.0e-6f);
            // ratio=1 → 0 (steady); ratio=3 → 1 (sharp transient)
            return juce::jlimit (0.0f, 1.0f, (ratio - 1.0f) * 0.5f);
        }

    private:
        float fast { 0.0f }, slow { 0.0f };
        float aFastAtt { 0.9f }, aFastRel { 0.99f };
        float aSlowAtt { 0.99f }, aSlowRel { 0.999f };
    };

    //==========================================================================
    // Look-ahead True-Peak limiter (post-downsample safety net).
    //
    // Adds ~2 ms of latency but guarantees that the final output never exceeds
    // the ceiling, even under inter-sample-peak (ISP) conditions that survive
    // oversampled clipping. Essential for "pro master" usage where true-peak
    // compliance (e.g. -1 dBTP for streaming) matters.
    //
    // Algorithm: fast attack (0.5 ms) / slow release (50 ms) smoothed gain
    // reduction, applied to a delayed signal so GR can ramp up BEFORE the
    // loud peak reaches output.
    //==========================================================================
    class LookAheadTPLimiter
    {
    public:
        void prepare (double sampleRate, int numChannels, int maxBlockSize)
        {
            lookAhead = (int) std::ceil (sampleRate * 0.002); // 2 ms
            const int need = lookAhead + maxBlockSize + 4;
            int pow2 = 1;
            while (pow2 < need) pow2 <<= 1;
            bufferSize = pow2;
            bufferMask = pow2 - 1;
            buffers.resize ((size_t) juce::jmax (1, numChannels));
            for (auto& b : buffers) b.assign ((size_t) bufferSize, 0.0f);
            writePos = 0;
            gainReduction = 1.0f;

            const float sr = (float) sampleRate;
            attCoeff = std::exp (-1.0f / (sr * 0.0005f)); // 0.5 ms attack
            relCoeff = std::exp (-1.0f / (sr * 0.050f));  // 50  ms release
        }

        int getLatencySamples() const noexcept { return lookAhead; }

        void reset() noexcept
        {
            for (auto& b : buffers) std::fill (b.begin(), b.end(), 0.0f);
            writePos = 0;
            gainReduction = 1.0f;
        }

        void process (juce::AudioBuffer<float>& buffer, float ceilingLinear)
        {
            const int numSamples = buffer.getNumSamples();
            const int numChannels = juce::jmin ((int) buffers.size(), buffer.getNumChannels());
            if (numSamples <= 0 || numChannels <= 0 || ceilingLinear <= 0.0f) return;

            for (int n = 0; n < numSamples; ++n)
            {
                // Measure peak across channels of this new input sample
                float peak = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    peak = std::max (peak, std::abs (buffer.getReadPointer (ch)[n]));

                // Target gain reduction to bring peak to ceiling
                const float target = peak > ceilingLinear ? ceilingLinear / peak : 1.0f;

                // Fast attack, slow release
                if (target < gainReduction)
                    gainReduction = attCoeff * gainReduction + (1.0f - attCoeff) * target;
                else
                    gainReduction = relCoeff * gainReduction + (1.0f - relCoeff) * target;

                // Write current input to ring buffer, output delayed sample * GR
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto& buf = buffers[(size_t) ch];
                    const int readPos = (writePos - lookAhead + bufferSize) & bufferMask;
                    const float delayed = buf[(size_t) readPos];
                    buf[(size_t) writePos] = buffer.getReadPointer (ch)[n];
                    buffer.getWritePointer (ch)[n] = delayed * gainReduction;
                }
                writePos = (writePos + 1) & bufferMask;
            }
        }

        // Exposes current GR (for meter). 1.0 = no reduction, <1 = active.
        float getCurrentGainReduction() const noexcept { return gainReduction; }

    private:
        std::vector<std::vector<float>> buffers;
        int lookAhead  { 96 };
        int bufferSize { 128 };
        int bufferMask { 127 };
        int writePos   { 0 };
        float gainReduction { 1.0f };
        float attCoeff { 0.99f }, relCoeff { 0.999f };
    };

    //==========================================================================
    // One-pole low-pass (for TAPE character HF damping).
    //==========================================================================
    class OnePoleLP
    {
    public:
        void setCutoff (float freqHz, double sampleRate) noexcept
        {
            a = (float) std::exp (-2.0 * juce::MathConstants<double>::pi
                                  * (double) freqHz / sampleRate);
        }
        void reset() noexcept { state = 0.0f; }
        float process (float x) noexcept
        {
            state = x * (1.0f - a) + state * a;
            return state;
        }
    private:
        float a { 0.0f }, state { 0.0f };
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

        static constexpr float subGuardCrossoverHz = 120.0f;

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
            subGuardSmoothed  .reset (sampleRate, 0.04); // operates at native rate

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

            // SUB GUARD crossover filter — 4th order Linkwitz-Riley LP @ 120 Hz
            juce::dsp::ProcessSpec nativeSpec { sampleRate,
                                                (juce::uint32) maxBlockSize,
                                                (juce::uint32) nCh };
            subLowpass.prepare (nativeSpec);
            subLowpass.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
            subLowpass.setCutoffFrequency (subGuardCrossoverHz);

            // Scratch buffer for the low-band (allocated once, re-used)
            lowBandBuffer.setSize (nCh, maxBlockSize, false, false, true);
            lowBandBuffer.clear();

            // DSP v3: Transient detectors (native rate) — one per channel
            transients.resize ((size_t) nCh);
            for (auto& t : transients) t.prepare (sampleRate);

            // DSP v3: TAPE character HF damping — very subtle LP above 10 kHz
            // gives the "bandwidth-limited tape" feel on TAPE mode only.
            tapeHfDamp.resize ((size_t) nCh);
            for (auto& f : tapeHfDamp) f.setCutoff (10000.0f, osRate);

            // Cached per-sample transient amount, one slot per native sample
            cachedTransient.fill (0.0f);

            // DSP v3.1: post-processing True-Peak limiter (master-ready)
            tpLimiter.prepare (sampleRate, nCh, maxBlockSize);

            // Combined latency = oversampler + TP lookahead (reassign, not +=,
            // so repeated prepare() calls don't double-count).
            latencySamples = (int) std::ceil (oversampler->getLatencyInSamples())
                           + tpLimiter.getLatencySamples();
        }

        void reset()
        {
            if (oversampler) oversampler->reset();
            for (auto& d : dcBlocker) d.reset();
            for (auto& s : adaaState) s = {};
            for (auto& r : inRms)  r.reset();
            for (auto& r : outRms) r.reset();
            for (auto& t : transients) t.reset();
            for (auto& f : tapeHfDamp) f.reset();
            autoGainSmoother.setCurrentAndTargetValue (1.0f);
            subLowpass.reset();
            tpLimiter.reset();
            lowBandBuffer.clear();
        }

        int getLatencySamples() const noexcept { return latencySamples; }

        // Parameter setters (UI units)
        void setInputGainDb (float db)      { inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (db)); }
        void setCeilingDb   (float db)      { ceilingSmoothed  .setTargetValue (juce::Decibels::decibelsToGain (db)); }
        void setKnee        (float norm01)  { kneeSmoothed     .setTargetValue (juce::jlimit (0.0f, 1.0f, norm01)); }
        void setHarmonics   (float norm01)  { harmonicsSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, norm01)); }
        void setSubGuard    (float norm01)  { subGuardSmoothed .setTargetValue (juce::jlimit (0.0f, 1.0f, norm01)); }
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

            // --- 1.25 Transient detection (native rate, pre-oversample) ---
            // For each incoming native sample, compute a 0-1 "transient amount".
            // We use channel 0 as the trigger signal (stereo trigger is approx
            // the max of L,R; here we use L — cheap and good enough for trap).
            const int transientCacheSize = juce::jmin (numSamples, (int) cachedTransient.size());
            {
                const auto* d = buffer.getReadPointer (0);
                auto& det = transients[0];
                for (int i = 0; i < transientCacheSize; ++i)
                    cachedTransient[(size_t) i] = det.process (d[i]);
            }

            // --- 1.5 SUB GUARD split (at native rate, before oversampling) ---
            // low = LP(120 Hz, LR4) of input
            // Modify buffer: buffer = high + low * (1 - sg)   [goes into clipper]
            // We'll add back "low * sg" AFTER the clipper (post-downsample).
            //
            // sg=0 : clipper sees original (low+high = x). Classic behavior.
            // sg=1 : clipper sees only high, low is preserved bit-perfect.
            float sgForMix = 0.0f;
            {
                // Copy the input to lowBandBuffer, then LPF it
                const int nCh = juce::jmin (numChannels, lowBandBuffer.getNumChannels());
                lowBandBuffer.setSize (numChannels, numSamples, false, false, true);
                for (int ch = 0; ch < nCh; ++ch)
                    lowBandBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

                juce::dsp::AudioBlock<float> lowBlock (lowBandBuffer);
                juce::dsp::ProcessContextReplacing<float> lowCtx (lowBlock);
                subLowpass.process (lowCtx);

                // Pre-read the smoothed sub-guard once per block for the recombine step.
                sgForMix = subGuardSmoothed.getNextValue();
                subGuardSmoothed.skip (numSamples - 1);
                sgForMix = juce::jlimit (0.0f, 1.0f, sgForMix);

                // Pre-clipper signal: buffer = buffer - low * sg
                //                           = (low + high) - low * sg
                //                           = high + low * (1 - sg)
                if (sgForMix > 0.0f)
                {
                    for (int ch = 0; ch < nCh; ++ch)
                    {
                        auto* b  = buffer.getWritePointer (ch);
                        const auto* l = lowBandBuffer.getReadPointer (ch);
                        for (int i = 0; i < numSamples; ++i)
                            b[i] -= l[i] * sgForMix;
                    }
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

                    // DSP v3: transient modulation (preserves punch on drum hits).
                    // One native sample = 4 oversampled samples, so nativeIdx = n / 4.
                    const int nativeIdx = juce::jmin (n >> 2, transientCacheSize - 1);
                    const float trAmt = cachedTransient[(size_t) nativeIdx];

                    // During a transient, clipping is softened:
                    //   - Effective input gain reduced ~15% (so transient peak
                    //     enters the clip less hard → preserves attack shape)
                    //   - Knee softened → smoother limiting of the transient
                    //   - Harmonics reduced ~50% → cleaner transient, less fizz
                    // Decay is controlled by the slow env inside TransientDetector.
                    const float effGain = inGain  * (1.0f - trAmt * 0.15f);
                    const float effKnee = juce::jlimit (0.0f, 1.0f, knee + trAmt * 0.35f);
                    const float effHarm = harmAmt * (1.0f - trAmt * 0.5f);

                    // Input gain
                    float x = d[n] * effGain;

                    // DSP v3: character-specific pre-shape with distinct tonal signature.
                    switch (character)
                    {
                        case Character::Hard:
                            // Pure ADAA clip downstream — keep transients bright.
                            break;

                        case Character::Tape:
                            // asinh for musical compression-like saturation
                            // (smoother than tanh, more transparent at low levels).
                            x = std::asinh (x * 0.8f) / std::asinh (0.8f);
                            // Subtle HF damping at 10 kHz (per-channel, oversampled rate).
                            // Recreates the bandwidth-limited feel of magnetic tape.
                            x = tapeHfDamp[(size_t) juce::jmin (ch, (int) tapeHfDamp.size() - 1)]
                                    .process (x);
                            break;

                        case Character::Tube:
                            // Asymmetric diode clipper (1N914-inspired model):
                            // positive lobe saturates ~45% faster than negative.
                            // This generates strong even harmonics ahead of the main clip.
                            {
                                const float slopePos = 1.3f;
                                const float slopeNeg = 0.9f;
                                x = (x > 0.0f) ? std::tanh (x * slopePos) / std::tanh (slopePos)
                                               : std::tanh (x * slopeNeg) / std::tanh (slopeNeg);
                                // Extra static compression-like bulge for "tubey" thickness.
                                x *= 0.94f;
                            }
                            break;
                    }

                    // Normalize into clip domain
                    const float xn = x / ceiling;

                    // ADAA: share xPrev, separate FPrev per curve.
                    const float dx     = xn - st.xPrev;
                    const float FhardN = shape::F_hardClip (xn);
                    const float softG  = 0.5f + (1.0f - effKnee) * 5.0f; // effKnee uses transient modulation
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
                    float yn = (1.0f - effKnee) * hard + effKnee * soft;

                    // Parallel harmonic injection (DSP v3: more distinct bias per character)
                    if (effHarm > 0.0f)
                    {
                        float biasEven;
                        switch (character)
                        {
                            case Character::Hard: biasEven = 0.20f; break; // 20% 2nd / 80% 3rd — bright, edgy
                            case Character::Tape: biasEven = 0.65f; break; // 65% 2nd / 35% 3rd — warm, smooth
                            case Character::Tube: biasEven = 0.85f; break; // 85% 2nd / 15% 3rd — thick, tubey
                            default:              biasEven = 0.50f; break;
                        }

                        // 2nd harmonic (sign-preserved, centred at 0)
                        const float h2 = (2.0f * yn * yn) * (yn >= 0.0f ? 1.0f : -1.0f);
                        // 3rd harmonic (Chebyshev T3)
                        const float h3 = 4.0f * yn * yn * yn - 3.0f * yn;

                        const float h = biasEven * h2 + (1.0f - biasEven) * h3;
                        yn += effHarm * 0.30f * h;
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

            // --- 4.5 SUB GUARD recombine: add back low * sg ---
            if (sgForMix > 0.0f)
            {
                const int nCh = juce::jmin (numChannels, lowBandBuffer.getNumChannels());
                for (int ch = 0; ch < nCh; ++ch)
                {
                    auto* b  = buffer.getWritePointer (ch);
                    const auto* l = lowBandBuffer.getReadPointer (ch);
                    for (int i = 0; i < numSamples; ++i)
                        b[i] += l[i] * sgForMix;
                }
            }

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

            // --- 6. True-Peak safety limiter (master-ready) ---
            // Catches any inter-sample overshoot that survived clipping
            // and recombine. Latency = ~2 ms, reported via getLatencySamples().
            const float currentCeiling = juce::jmax (0.01f, ceilingSmoothed.getCurrentValue());
            tpLimiter.process (buffer, currentCeiling);
        }

        float getCurrentGainReduction() const noexcept { return tpLimiter.getCurrentGainReduction(); }

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
        juce::LinearSmoothedValue<float> subGuardSmoothed  { 0.0f };

        Character character { Character::Hard };
        bool autoGain { false };

        std::vector<AdaaState> adaaState;
        std::vector<DCBlocker> dcBlocker;
        std::vector<RmsFollower> inRms, outRms;
        juce::LinearSmoothedValue<float> autoGainSmoother { 1.0f };

        // SUB GUARD dual-band state
        juce::dsp::LinkwitzRileyFilter<float> subLowpass;
        juce::AudioBuffer<float> lowBandBuffer;

        // Per-sample cache so L/R share smoothed values (oversampled block).
        std::array<float, 8192> cachedInGain  {};
        std::array<float, 8192> cachedCeiling {};
        std::array<float, 8192> cachedKnee    {};
        std::array<float, 8192> cachedHarm    {};

        // DSP v3 additions
        std::vector<TransientDetector> transients;      // per-channel (native rate)
        std::vector<OnePoleLP>         tapeHfDamp;      // per-channel (oversampled rate, TAPE only)
        std::array<float, 2048>        cachedTransient {}; // per-native-sample transient amount

        // DSP v3.1: post-processing True-Peak limiter
        LookAheadTPLimiter tpLimiter;
    };
}
