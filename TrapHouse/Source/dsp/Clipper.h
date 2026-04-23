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

        // v4.1 FIX: plain tanh soft-clip.
        //
        // PREVIOUS BUG (v3 level-matched soft clip = tanh(g*x)/tanh(g)):
        //   At high sharpness g (low knee), the slope at x=0 is g/tanh(g)
        //   which is >> 1. E.g. at knee=0, g=5.5 → slope near 0 is 5.5.
        //   That AMPLIFIED small signals by +15 dB.
        //   Blended with hard clip, this produced audible crackle at DRIVE=0
        //   (signals below ceiling got compressed UPWARD → distortion).
        //
        // FIX: plain tanh(x). Slope at 0 = 1 (unity gain). Saturates at ±1.
        // KNEE now controls only the hard/soft blend, not the soft sharpness.
        // Clean, predictable, matches Saturn 2 / Decapitator behavior.
        inline float softClip (float x) noexcept
        {
            return std::tanh (x);
        }

        // Antiderivative: F(x) = log(cosh(x)), numerically stable for large |x|.
        inline float F_softClip (float x) noexcept
        {
            const float ax = std::abs (x);
            return (ax > 20.0f) ? (ax - 0.693147181f /* log(2) */)
                                : std::log (std::cosh (x));
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
            // Slower fast-attack (4 ms) avoids hyperactivity on dense mixes.
            aFastAtt = a (4.0f);    aFastRel = a (100.0f);
            aSlowAtt = a (60.0f);   aSlowRel = a (400.0f);
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
    // Shelving filter wrapper (2-channel, JUCE IIR + ProcessorDuplicator).
    // Used for pre-EQ low-shelf boost and post-EQ high-shelf "air" boost —
    // the key ingredient that makes one-knob maximizers (Sausage Fattener,
    // OneKnob Louder) sound "fat" rather than just "distorted".
    //==========================================================================
    class ShelvingFilter
    {
    public:
        enum class Type { LowShelf, HighShelf };

        void prepare (double sr, int numChannels, int maxBlockSize, Type t)
        {
            sampleRate = sr;
            type = t;
            juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlockSize,
                                          (juce::uint32) juce::jmax (1, numChannels) };
            filter.prepare (spec);
            setParameters (t == Type::LowShelf ? 120.0f : 6000.0f, 0.0f);
        }

        void reset() { filter.reset(); }

        void setParameters (float freqHz, float gainDb)
        {
            const float gainLin = std::pow (10.0f, gainDb / 20.0f);
            auto coeffs = (type == Type::LowShelf)
                ? juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sampleRate, freqHz, 0.707f, gainLin)
                : juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, freqHz, 0.707f, gainLin);
            *filter.state = *coeffs;
        }

        void process (juce::AudioBuffer<float>& buffer)
        {
            juce::dsp::AudioBlock<float> block (buffer);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            filter.process (ctx);
        }

    private:
        juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                        juce::dsp::IIR::Coefficients<float>> filter;
        double sampleRate { 48000.0 };
        Type type { Type::LowShelf };
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
            // v4.1: slower attack (2 ms vs 0.5 ms) — avoids "clicks" on small
            // transient overshoots that were a source of audible crackle.
            // v4.2: attack 5 ms (was 2 ms) — gentler on dense program material.
            attCoeff = std::exp (-1.0f / (sr * 0.005f));  // 5 ms attack
            relCoeff = std::exp (-1.0f / (sr * 0.120f));  // 120 ms release (smoother)
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
        enum class Character { Hard = 0, Tape, Tube, Ice };

        static constexpr float subGuardCrossoverHz = 120.0f;

        void prepare (double sampleRate, int maxBlockSize, int numChannels)
        {
            // v4.2 FIX: FIR linear-phase oversampler eliminates IIR ringing
            // artifacts on transients (a hidden source of "crackle" on dense
            // material). FIR adds ~2ms more latency than IIR but auto-compensated.
            oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
                numChannels,
                2, // factor 2^2 = 4x
                juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
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

            // v3.2: smoother on transient amount (5 ms) — kills modulation clicks.
            trAmtSmoother.reset (osRate, 0.005);
            trAmtSmoother.setCurrentAndTargetValue (0.0f);

            // DSP v3: TAPE character HF damping — very subtle LP above 10 kHz
            // gives the "bandwidth-limited tape" feel on TAPE mode only.
            tapeHfDamp.resize ((size_t) nCh);
            for (auto& f : tapeHfDamp) f.setCutoff (10000.0f, osRate);

            // Cached per-sample transient amount, one slot per native sample
            cachedTransient.fill (0.0f);

            // DSP v3.1: post-processing True-Peak limiter (master-ready)
            tpLimiter.prepare (sampleRate, nCh, maxBlockSize);

            // DSP v4: Shelving EQ pre + post (Sausage Fattener style)
            cachedSampleRate = sampleRate;
            lowShelf .prepare (sampleRate, nCh, maxBlockSize, ShelvingFilter::Type::LowShelf);
            highShelf.prepare (sampleRate, nCh, maxBlockSize, ShelvingFilter::Type::HighShelf);
            lowShelfAmount01  = 0.0f;
            highShelfAmount01 = 0.0f;
            lowShelf .setParameters (120.0f, 0.0f);
            highShelf.setParameters (6500.0f, 0.0f);

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
            trAmtSmoother  .setCurrentAndTargetValue (0.0f);
            subLowpass.reset();
            tpLimiter.reset();
            lowShelf .reset();
            highShelf.reset();
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

        // v4: Sausage Fattener-style shelving EQ amount (0..1, driven by DRIVE)
        // Pre: low-shelf boost @ 120 Hz up to +4 dB (thump / weight)
        // Post: high-shelf boost @ 6.5 kHz up to +2.5 dB (air / presence)
        //
        // v5.1 MASTER-READY: low and high shelves can now be set independently
        // so the processor can taper the HIGH shelf past drive=0.7 (prevents
        // HF harshness when the clipper is already generating upper harmonics)
        // while keeping the LOW shelf rising for warmth.
        void setShelfAmount (float norm01)
        {
            // Back-compat API: both shelves move together.
            setLowShelfAmount  (norm01);
            setHighShelfAmount (norm01);
        }

        void setLowShelfAmount (float norm01)
        {
            const float v = juce::jlimit (0.0f, 1.0f, norm01);
            if (std::abs (v - lowShelfAmount01) < 5.0e-3f) return;
            lowShelfAmount01 = v;
            lowShelf.setParameters (120.0f, v * 4.0f);
        }

        void setHighShelfAmount (float norm01)
        {
            const float v = juce::jlimit (0.0f, 1.0f, norm01);
            if (std::abs (v - highShelfAmount01) < 5.0e-3f) return;
            highShelfAmount01 = v;
            highShelf.setParameters (6500.0f, v * 2.5f);
        }

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

            // --- 1.1 PRE-EQ: Low-shelf boost (Sausage Fattener-style weight/thump) ---
            // Runs at native rate on the DRY input before oversampling +
            // clipping, so the boosted lows feed into the saturator
            // and generate fat harmonics. This is the #1 thing that makes
            // maximizers sound "fat" instead of "distorted".
            if (lowShelfAmount01 > 1.0e-4f)
                lowShelf.process (buffer);

            // --- 1.25 Transient detection (native rate, pre-oversample) ---
            // v4.2: trigger on max(|L|, |R|) so stereo transients always register.
            // (Was channel 0 only — could desync modulation on wide stereo material.)
            const int transientCacheSize = juce::jmin (numSamples, (int) cachedTransient.size());
            {
                const auto* l = buffer.getReadPointer (0);
                const auto* r = (numChannels > 1) ? buffer.getReadPointer (1) : l;
                auto& det = transients[0];
                for (int i = 0; i < transientCacheSize; ++i)
                {
                    const float mono = std::max (std::abs (l[i]), std::abs (r[i]));
                    cachedTransient[(size_t) i] = det.process (mono);
                }
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

            // v4.3: precompute smoothed transient amount ONCE per os-sample
            // so L/R see identical values in the per-channel loop below.
            // (Was called inside the loop → advanced 2× per sample in stereo.)
            for (int n = 0; n < effSamples; ++n)
            {
                const int nativeIdx = juce::jmin (n >> 2, transientCacheSize - 1);
                trAmtSmoother.setTargetValue (cachedTransient[(size_t) nativeIdx]);
                cachedTrAmtPerSample[(size_t) n] = trAmtSmoother.getNextValue();
            }

            // --- 3. Per-sample processing at 4x rate ---
            for (int ch = 0; ch < osChannels; ++ch)
            {
                auto* d = osBlock.getChannelPointer ((size_t) ch);
                auto& st = adaaState[(size_t) juce::jmin (ch, (int) adaaState.size() - 1)];

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

                    // v4.3 CRITICAL FIX: trAmt is now cached per os-sample (not
                    // per channel-sample). In stereo, the previous code advanced
                    // trAmtSmoother TWICE per sample (once for L, once for R),
                    // giving L and R DIFFERENT trAmt values → inter-channel
                    // modulation mismatch = crackle. Now L and R read the same
                    // pre-computed trAmt from the cache.
                    const float trAmt   = cachedTrAmtPerSample[(size_t) n];

                    // Reduced modulation depth for cleaner sound:
                    //   gain -8%, knee +15%, harm -25%.
                    // Still preserves transient punch without clicking.
                    const float effGain = inGain  * (1.0f - trAmt * 0.08f);
                    const float effKnee = juce::jlimit (0.0f, 1.0f, knee + trAmt * 0.15f);
                    const float effHarm = harmAmt * (1.0f - trAmt * 0.25f);

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

                        case Character::Ice:
                            // v4.3: ICE — crystalline saturation (unlocked by
                            // owning a LABEL in 1017 TYCOON). Smooth asinh +
                            // slight HF emphasis for a bright, glassy texture.
                            // Different from TAPE's warm roll-off, this gives
                            // top-end clarity while keeping the saturation
                            // musical. Plus 3rd-harmonic dominant bias.
                            x = std::asinh (x * 1.0f) / std::asinh (1.0f);
                            x *= 1.03f; // very slight gain for crystal zing
                            break;

                        case Character::Tube:
                            // SMOOTH tube model (Decapitator / Saturn 2 style):
                            //   y = tanh(x * 1.1 + bias) - tanh(bias)
                            // Symmetric tanh with DC offset → the offset introduces
                            // 2nd-harmonic content, then we subtract the DC so no
                            // offset remains. Derivative is continuous everywhere
                            // (unlike an asymmetric-slope model which has a kink
                            // at x=0 that aliases heavily — the ORIGIN of the
                            // crackle reported in v3.1).
                            {
                                const float bias = 0.08f;
                                x = std::tanh (x * 1.1f + bias) - std::tanh (bias);
                                x *= 0.95f; // slight static compression for "tubey" thickness
                            }
                            break;
                    }

                    // Normalize into clip domain
                    const float xn = x / ceiling;

                    // ADAA: share xPrev, separate FPrev per curve.
                    // v4.1: soft-clip is plain tanh (no sharpness parameter).
                    // KNEE blend alone controls hard/soft balance — no
                    // more "slope at 0" amplification bug.
                    const float dx     = xn - st.xPrev;
                    const float FhardN = shape::F_hardClip (xn);
                    const float FsoftN = shape::F_softClip (xn);

                    float hard, soft;
                    if (std::abs (dx) < 1.0e-5f)
                    {
                        const float mid = 0.5f * (xn + st.xPrev);
                        hard = shape::hardClip (mid);
                        soft = shape::softClip (mid);
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

                    // v4.1: SMOOTH harmonic injection (no 2nd-derivative kink → no crackle).
                    //
                    // PREVIOUS BUG: h2 = 2*yn²*sign(yn) has a 2nd-derivative
                    // discontinuity at yn=0 (jumps by ±8 on each zero-crossing).
                    // That produces constant aliasing audible as "crackle"
                    // at low-to-mid DRIVE (masked by saturation at high DRIVE).
                    //
                    // FIX: the 2nd harmonic now comes from a smooth
                    // "tanh with DC offset, minus DC" formula — the exact
                    // technique real tube models (UAD, Waves, FabFilter) use.
                    // This function has continuous derivatives of ALL orders.
                    if (effHarm > 0.0f)
                    {
                        float biasEven;
                        float biasOffset;
                        switch (character)
                        {
                            case Character::Hard: biasEven = 0.20f; biasOffset = 0.05f; break;
                            case Character::Tape: biasEven = 0.65f; biasOffset = 0.10f; break;
                            case Character::Tube: biasEven = 0.85f; biasOffset = 0.15f; break;
                            case Character::Ice:  biasEven = 0.25f; biasOffset = 0.04f; break; // 3rd dominant + subtle
                            default:              biasEven = 0.50f; biasOffset = 0.10f; break;
                        }

                        // Smooth 2nd-harmonic generator (tanh with DC offset trick):
                        //   h2s = (tanh(yn + b) - tanh(b)) - tanh(yn)
                        //   => asymmetric saturation component, DC-removed, smooth
                        const float tanhBiased = std::tanh (yn + biasOffset);
                        const float tanhBase   = std::tanh (yn);
                        const float tanhB0     = std::tanh (biasOffset);
                        const float h2_smooth  = (tanhBiased - tanhB0) - tanhBase;

                        // 3rd harmonic (Chebyshev T3) — polynomial, already smooth
                        const float h3 = 4.0f * yn * yn * yn - 3.0f * yn;

                        const float h = biasEven * h2_smooth + (1.0f - biasEven) * h3;
                        yn += effHarm * 0.40f * h;
                    }

                    // Soft safety clamp in normalised domain (cheap overshoot control)
                    yn = juce::jlimit (-1.0f, 1.0f, yn);

                    // v4.2: DC blocker REMOVED from per-sample loop.
                    // The smooth TUBE model (tanh + bias - tanh(bias)) is already
                    // DC-free by construction. Removing the DC blocker eliminates
                    // its transient overshoot (HPF ringing) which was one of the
                    // remaining crackle sources.
                    float y = yn * ceiling;

                    // Hard ceiling guarantee (no overshoot)
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

            // --- 4.7 POST-EQ: High-shelf air / presence boost ---
            // After clipping/harmonic injection: adds a gentle "sparkle" to
            // the top end. Combined with the pre low-shelf boost, this gives
            // the Sausage Fattener-style smile curve that makes beats pop.
            if (highShelfAmount01 > 1.0e-4f)
                highShelf.process (buffer);

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
                    // v4.2: tighter clamp (0.5..2.0 = ±6 dB) prevents pumping
                    // artifacts that came from the old 0.1..3.0 range (±30 dB!).
                    const float ratio = (float) juce::jlimit (0.5, 2.0, dryAvg / wetAvg);
                    autoGainSmoother.setTargetValue (ratio);
                }

                // v4.3 FIX: advance autoGain smoother ONCE per sample (apply to
                // all channels with the same gain). Previous loop advanced the
                // smoother numChannels× per sample → stereo L/R mismatch.
                for (int i = 0; i < numSamples; ++i)
                {
                    const float g = autoGainSmoother.getNextValue();
                    for (int ch = 0; ch < numChannels; ++ch)
                        buffer.getWritePointer (ch)[i] *= g;
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
        std::array<float, 8192> cachedInGain         {};
        std::array<float, 8192> cachedCeiling        {};
        std::array<float, 8192> cachedKnee           {};
        std::array<float, 8192> cachedHarm           {};
        std::array<float, 8192> cachedTrAmtPerSample {}; // v4.3: stereo-consistent trAmt

        // DSP v3 additions
        std::vector<TransientDetector> transients;      // per-channel (native rate)
        std::vector<OnePoleLP>         tapeHfDamp;      // per-channel (oversampled rate, TAPE only)
        std::array<float, 2048>        cachedTransient {}; // per-native-sample transient amount

        // DSP v3.2: smoother on transient amount to prevent click-on-modulation
        juce::LinearSmoothedValue<float> trAmtSmoother { 0.0f };

        // DSP v3.1: post-processing True-Peak limiter
        LookAheadTPLimiter tpLimiter;

        // DSP v4: Sausage-Fattener-style shelving EQ (pre-low, post-high)
        ShelvingFilter lowShelf;
        ShelvingFilter highShelf;
        float lowShelfAmount01  { 0.0f };
        float highShelfAmount01 { 0.0f };
        double cachedSampleRate { 48000.0 };
    };
}
