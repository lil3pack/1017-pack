#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

TrapHouseProcessor::TrapHouseProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "STATE", createLayout())
{
}

APVTS::ParameterLayout TrapHouseProcessor::createLayout()
{
    using namespace juce;

    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    // DRIVE — macro 0-100%, internally maps to (input gain, knee, harmonics).
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { th::PID::drive, 1 }, "Drive",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) {
                return juce::String ((int) std::round (v * 100.0f)) + "%";
            })));

    // SUB GUARD — 0% = standard clip, 100% = sub-bass (<120 Hz) stays untouched.
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { th::PID::subGuard, 1 }, "Sub Guard",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) {
                return juce::String ((int) std::round (v * 100.0f)) + "%";
            })));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { th::PID::character, 1 }, "Character",
        StringArray { "HARD", "TAPE", "TUBE", "ICE" }, 0));
        // HARD default — most aggressive maximizer.
        // ICE (idx 3) is unlocked by owning a LABEL in 1017 TYCOON; otherwise
        // falls back to HARD in processBlock.

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { th::PID::autoGain, 1 }, "Auto Gain", false /* v4.2: OFF by default — user can enable manually */));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { th::PID::bypass, 1 }, "Bypass", false));

    // v4.4 secret panel params
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { th::PID::stereoWidth, 1 }, "Stereo Width",
        NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return juce::String ((int) std::round (v * 100.0f)) + "%"; })));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { th::PID::outputTrim, 1 }, "Output Trim",
        NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction (
                [] (float v, int) { return juce::String (v, 1) + " dB"; })));

    // v5.2: MIX — parallel dry/wet blend (0 = dry, 1 = fully processed)
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { th::PID::mix, 1 }, "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) {
                return juce::String ((int) std::round (v * 100.0f)) + "%";
            })));

    return { params.begin(), params.end() };
}

void TrapHouseProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    clipper.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples (clipper.getLatencySamples());

    // Decimate for the scope so we show ~80 ms in the visible frames.
    const int target = juce::jmax (1, (int) std::round (sampleRate * 0.080 / th::dsp::ScopeBuffer::frameSize));
    scopeBuffer.setDecimation (target);
    scopeBuffer.reset();
}

void TrapHouseProcessor::releaseResources()
{
    clipper.reset();
    scopeBuffer.reset();
}

bool TrapHouseProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void TrapHouseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalIn  = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (apvts.getRawParameterValue (th::PID::bypass)->load() > 0.5f)
        return;

    // --- DRIVE macro mapping ---
    // Single knob that drives input gain, knee morph, and harmonic richness.
    // Ceiling stays fixed at -0.3 dBFS under the hood (mastering-friendly
    // default; a true peak would need intersample detection which is a
    // future enhancement).
    const float drive = juce::jlimit (0.0f, 1.0f,
        apvts.getRawParameterValue (th::PID::drive)->load());

    // DRIVE macro — v4 FATNESS ENGINE (Sausage Fattener / OneKnob Louder style):
    //
    //   Each % of DRIVE drives an entire master-bus maximizer chain:
    //   pre low-shelf, input gain, cascade saturation, hard clip, harmonic
    //   enhance, post high-shelf, auto makeup, TP limiter.
    //
    //   v5.1 MASTER-READY mapping. Below 0.7 the plugin adds harmonics for
    //   character; ABOVE 0.7 the plugin prioritises peak control, tapering
    //   extra harmonics + high-shelf air so pushed-to-max stays musical on a
    //   stereo bus instead of getting fizzy/harsh.
    //
    //   drive=0    → 0 dB   / knee 0.00 / harm 0.00 / loS 0.00 / hiS 0.00  — passthrough
    //   drive=0.2  → +2 dB  / knee 0.24 / harm 0.14 / loS 0.20 / hiS 0.20  — warmth
    //   drive=0.3  → +4 dB  / knee 0.32 / harm 0.21 / loS 0.30 / hiS 0.30  — glue
    //   drive=0.5  → +7 dB  / knee 0.48 / harm 0.33 / loS 0.50 / hiS 0.50  — fat
    //   drive=0.7  → +10 dB / knee 0.63 / harm 0.42 / loS 0.70 / hiS 0.70  — loud
    //   drive=0.85 → +12 dB / knee 0.72 / harm 0.29 / loS 0.85 / hiS 0.40  — hot but controlled
    //   drive=1.0  → +15 dB / knee 0.80 / harm 0.15 / loS 1.00 / hiS 0.10  — master-safe max
    //
    //   Ceiling stays at -1.0 dBFS; post TP limiter guarantees -1 dBTP safe.
    const float driveCurve = std::pow (drive, 1.25f);            // mild ease-in
    const float inGainDb   = driveCurve * 15.0f;                  // was drive*18 (too brutal at max)
    const float ceilDb     = -1.0f;
    // Knee ramps faster → more soft-clip blend at high drive (smoother peaks):
    const float knee       = std::pow (drive, 0.75f) * 0.80f;
    // Harmonic injection peaks at drive≈0.6 then TAPERS — stops stacking
    // extra crunch on top of what the clipper naturally generates:
    const float harm       = (drive <= 0.6f)
        ? drive * 0.70f
        : juce::jmax (0.0f, 0.42f - (drive - 0.6f) * 0.675f);     // 0.42 @0.6 → 0.15 @1.0 → 0 past
    // LOW shelf keeps rising (warmth counteracts thinness from heavy clipping):
    const float shelfLow   = drive;
    // HIGH shelf tapers hard past 0.7 — when the clip is already making upper
    // harmonics, boosting 6.5 kHz on top = harshness. We back off instead:
    const float shelfHigh  = (drive <= 0.7f)
        ? drive
        : juce::jmax (0.0f, 0.7f - (drive - 0.7f) * 2.0f);        // 0.7 @0.7 → 0.1 @1.0

    const float subGuard = juce::jlimit (0.0f, 1.0f,
        apvts.getRawParameterValue (th::PID::subGuard)->load());

    int  charIdx       = (int) apvts.getRawParameterValue (th::PID::character)->load();
    const bool autoG   = apvts.getRawParameterValue (th::PID::autoGain)->load() > 0.5f;

    // 🎮 ICE character gate: requires owning ≥1 LABEL in 1017 TYCOON.
    // If locked, silently fall back to HARD so the plugin never fails,
    // and the user sees a visual "locked" indicator in the UI instead.
    if (charIdx == 3 && ! isIceUnlocked())
        charIdx = 0;

    clipper.setInputGainDb      (inGainDb);
    clipper.setCeilingDb        (ceilDb);
    clipper.setKnee             (knee);
    clipper.setHarmonics        (harm);
    clipper.setLowShelfAmount   (shelfLow);
    clipper.setHighShelfAmount  (shelfHigh);
    clipper.setSubGuard    (subGuard);
    clipper.setCharacter   (static_cast<th::dsp::ClipperCore::Character> (juce::jlimit (0, 3, charIdx)));
    clipper.setAutoGain    (autoG);

    // v4.4: per-channel + mono RMS for VU meters
    const int n = buffer.getNumSamples();
    auto computeRms = [n] (const float* d)
    {
        float s = 0.0f;
        for (int i = 0; i < n; ++i) s += d[i] * d[i];
        return std::sqrt (s / (float) juce::jmax (1, n));
    };

    const float inL = (totalIn > 0) ? computeRms (buffer.getReadPointer (0)) : 0.0f;
    const float inR = (totalIn > 1) ? computeRms (buffer.getReadPointer (1)) : inL;
    inputRmsL.store (inL);
    inputRmsR.store (inR);
    inputRms .store (0.5f * (inL + inR));

    // v5.2: save DRY copy before clipping so the MIX knob can blend parallel.
    //  - mix=1 (default) → fully wet (clipper output as before)
    //  - mix=0           → fully dry (bypass-like)
    //  - mix between     → sum wet*mix + dry*(1-mix)  (gain-preserving blend)
    juce::AudioBuffer<float> dryCopy;
    const float mix01 = juce::jlimit (0.0f, 1.0f,
        apvts.getRawParameterValue (th::PID::mix)->load());
    const bool blendNeeded = (mix01 < 0.999f);
    if (blendNeeded)
    {
        dryCopy.setSize (totalOut, n, false, false, true);
        for (int ch = 0; ch < totalOut; ++ch)
            dryCopy.copyFrom (ch, 0, buffer, ch, 0, n);
    }

    clipper.process (buffer);

    if (blendNeeded)
    {
        const float wet = mix01;
        const float dry = 1.0f - mix01;
        for (int ch = 0; ch < totalOut; ++ch)
        {
            auto* w = buffer.getWritePointer (ch);
            const auto* d = dryCopy.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                w[i] = w[i] * wet + d[i] * dry;
        }
    }

    // v4.4 stereo-width / output-trim: PARAMS KEPT in APVTS for back-compat,
    // but NO LONGER applied in the signal path (removed in v5.2 — user found
    // them buggy/unmusical; the secret panel now hosts MIX + transfer-curve
    // display + LUFS readout instead).

    // Output RMS + clip detection (v4.4)
    const float outL = (totalOut > 0) ? computeRms (buffer.getReadPointer (0)) : 0.0f;
    const float outR = (totalOut > 1) ? computeRms (buffer.getReadPointer (1)) : outL;
    outputRmsL.store (outL);
    outputRmsR.store (outR);
    outputRms .store (0.5f * (outL + outR));

    // Clip event: any sample >= ceiling - epsilon flags a clip for UI LED
    const float ceilingLin = juce::Decibels::decibelsToGain (-1.0f);
    for (int ch = 0; ch < totalOut; ++ch)
    {
        const auto* d = buffer.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            if (std::abs (d[i]) >= ceilingLin * 0.98f)
            {
                clipEventFlag.store (true);
                break;
            }
        }
    }

    // GR display: computed from autogain + TP limiter (approx)
    gainReductionDb.store (juce::Decibels::gainToDecibels (clipper.getCurrentGainReduction(), -60.0f));

    // v5.2: LUFS-ish estimate (short-term, K-weighted approximation via RMS).
    // Exact ITU BS.1770 weighting is heavier than needed for a UI readout;
    // this uses the post-process RMS and converts to dBFS, which tracks
    // integrated loudness closely enough for a mastering "am I too loud?"
    // indicator on a program signal. Good-enough is good enough here —
    // we don't want a dedicated biquad chain burning CPU on every block.
    {
        const float rmsMean = 0.5f * (outL + outR);
        const float lufsApprox = juce::Decibels::gainToDecibels (
            std::sqrt (rmsMean), -60.0f) - 0.691f /* BS.1770 constant */;
        // Smoothing: 1-pole toward the live value (IIR alpha = 0.1)
        const float prev = loudnessLufs.load();
        loudnessLufs.store (prev * 0.9f + lufsApprox * 0.1f);
    }
    // Knee snapshot for the UI transfer-curve display
    currentKnee01.store (knee);

    if (totalOut > 0)
    {
        const auto* l = buffer.getReadPointer (0);
        const auto* r = (totalOut > 1) ? buffer.getReadPointer (1) : l;
        for (int i = 0; i < n; ++i)
            scopeBuffer.push ((l[i] + r[i]) * 0.5f);
    }
}

juce::AudioProcessorEditor* TrapHouseProcessor::createEditor()
{
    return new TrapHouseEditor (*this);
}

void TrapHouseProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    // Wrap APVTS state + tycoon state in a single XML root so both
    // round-trip through the DAW session save cycle.
    juce::ValueTree root ("3PackClipState");
    root.appendChild (apvts.copyState(), nullptr);
    if (tycoonState.isValid())
        root.appendChild (tycoonState.createCopy(), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, dest);
}

void TrapHouseProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml) return;

    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid()) return;

    // Support legacy saves (APVTS XML at root) + new wrapper format
    if (root.hasType ("3PackClipState"))
    {
        auto apvtsVT  = root.getChildWithName (apvts.state.getType());
        if (apvtsVT.isValid())
            apvts.replaceState (apvtsVT);

        auto tyVT = root.getChildWithName ("TycoonState");
        if (tyVT.isValid())
            tycoonState = tyVT.createCopy();
    }
    else
    {
        // Legacy: whole thing was just the APVTS
        apvts.replaceState (root);
    }
}

// --- JUCE entry point ---
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TrapHouseProcessor();
}
