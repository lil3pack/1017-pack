#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace th::dsp
{
    /**
     * TRAP HOUSE core DSP.
     *
     * Signal chain (per sample, inside 4x oversampled block):
     *   x -> input gain -> character pre-shape -> clip(hard/soft blend) -> harmonics
     *        -> ceiling scale -> output
     *
     * The ceiling parameter is interpreted as the clip threshold in linear amplitude
     * (from the dB value set by the UI). Soft knee blends between a pure hard clip
     * and a tanh-based soft clip. Harmonics adds 2nd + 3rd harmonics via Chebyshev
     * polynomials, mixed in parallel at the post-clip stage.
     *
     * Character modes:
     *   HARD : raw hard clip, no pre-shape. Most aggressive, most transient brick-wall.
     *   TAPE : tanh pre-shape for gentle saturation before the clip. Smoother.
     *   TUBE : asymmetric soft-clip (biased tanh) for even harmonic emphasis.
     */
    class ClipperCore
    {
    public:
        enum class Character { Hard = 0, Tape, Tube };

        void prepare (double sampleRate, int maxBlockSize, int numChannels)
        {
            juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize,
                                          (juce::uint32) numChannels };

            oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
                numChannels,
                2, // factor 2^2 = 4x
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                true // max quality
            );

            oversampler->initProcessing ((size_t) maxBlockSize);
            oversampler->reset();

            latencySamples = (int) std::ceil (oversampler->getLatencyInSamples());

            inputGainSmoothed.reset (sampleRate * 4.0, 0.02);
            ceilingSmoothed.reset (sampleRate * 4.0, 0.02);
            kneeSmoothed.reset (sampleRate * 4.0, 0.02);
            harmonicsSmoothed.reset (sampleRate * 4.0, 0.02);
        }

        void reset()
        {
            if (oversampler) oversampler->reset();
        }

        int getLatencySamples() const noexcept { return latencySamples; }

        // Parameters in "UI units"
        void setInputGainDb (float db)      { inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (db)); }
        void setCeilingDb   (float db)      { ceilingSmoothed.setTargetValue   (juce::Decibels::decibelsToGain (db)); }
        void setKnee        (float norm01)  { kneeSmoothed.setTargetValue      (juce::jlimit (0.0f, 1.0f, norm01)); }
        void setHarmonics   (float norm01)  { harmonicsSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, norm01)); }
        void setCharacter   (Character c)   { character = c; }
        void setAutoGain    (bool on)       { autoGain = on; }

        void process (juce::AudioBuffer<float>& buffer)
        {
            juce::dsp::AudioBlock<float> block (buffer);
            auto osBlock = oversampler->processSamplesUp (block);

            const int numChannels = (int) osBlock.getNumChannels();
            const int numSamples  = (int) osBlock.getNumSamples();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = osBlock.getChannelPointer ((size_t) ch);

                // We keep a per-channel snapshot of smoothed values so left/right stay in lockstep.
                for (int n = 0; n < numSamples; ++n)
                {
                    const float inGain  = inputGainSmoothed.getNextValue();
                    const float ceiling = std::max (ceilingSmoothed.getNextValue(), 1.0e-6f);
                    const float knee    = kneeSmoothed.getNextValue();
                    const float harmAmt = harmonicsSmoothed.getNextValue();

                    float x = data[n] * inGain;

                    // Character pre-shape
                    switch (character)
                    {
                        case Character::Hard:
                            // no pre-shape
                            break;
                        case Character::Tape:
                            x = std::tanh (x * 1.2f) * 0.9f;
                            break;
                        case Character::Tube:
                            // asymmetric bias for even harmonics
                            x = std::tanh (x * 1.1f + 0.08f) - std::tanh (0.08f);
                            break;
                    }

                    // Normalize to ceiling domain for clipping: clip around +/- ceiling
                    const float xn = x / ceiling;

                    // Hard clip
                    const float hard = juce::jlimit (-1.0f, 1.0f, xn);

                    // Soft clip (tanh is smooth and cheap; scale so tanh(1) ~ 0.76 stays near ceiling)
                    const float soft = std::tanh (xn * 1.5f);

                    float y = (1.0f - knee) * hard + knee * soft;

                    // Harmonics: parallel generator. Chebyshev T2(y)=2y^2-1 gives 2nd harmonic,
                    // T3(y)=4y^3-3y gives 3rd. We mix a bit of both, DC-removed.
                    if (harmAmt > 0.0f)
                    {
                        const float h2 = 2.0f * y * y - 1.0f + 1.0f; // shift so no DC at y=0
                        const float h3 = 4.0f * y * y * y - 3.0f * y;
                        const float h  = 0.5f * (h2 * y) + 0.5f * h3; // y*h2 keeps sign for 2nd
                        y = y + harmAmt * 0.35f * h;
                    }

                    // Back to ceiling-scaled domain
                    y *= ceiling;

                    if (autoGain)
                    {
                        // crude makeup: undo the input gain partially so perceived level is stable
                        const float makeup = 1.0f / std::sqrt (std::max (inGain, 1.0f));
                        y *= makeup;
                    }

                    data[n] = y;
                }
            }

            oversampler->processSamplesDown (block);
        }

    private:
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
        int latencySamples { 0 };

        juce::LinearSmoothedValue<float> inputGainSmoothed { 1.0f };
        juce::LinearSmoothedValue<float> ceilingSmoothed   { 1.0f };
        juce::LinearSmoothedValue<float> kneeSmoothed      { 0.25f };
        juce::LinearSmoothedValue<float> harmonicsSmoothed { 0.40f };

        Character character { Character::Hard };
        bool autoGain { false };
    };
}
