#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../dsp/ScopeBuffer.h"
#include "LookAndFeel1017.h"

namespace th::ui
{
    /**
     * Oscilloscope display. Pulls frames from ScopeBuffer in timerCallback,
     * paints a purple waveform centered vertically, with gold dashed ceiling
     * lines at +/- ceilingGain.
     */
    class ScopeDisplay : public juce::Component,
                        private juce::Timer
    {
    public:
        ScopeDisplay (const th::dsp::ScopeBuffer& bufIn,
                      const LookAndFeel1017& lnf)
            : buffer (bufIn), palette (lnf.getPalette())
        {
            startTimerHz (30);
        }

        ~ScopeDisplay() override { stopTimer(); }

        void setCeilingGain (float g) noexcept { ceilingGain = g; }

        void paint (juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat();

            // Background
            g.setColour (palette.bgDeep.darker (0.3f));
            g.fillRoundedRectangle (bounds, 8.0f);
            g.setColour (palette.gold.withAlpha (0.4f));
            g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

            // Faint grid
            g.setColour (palette.purpleLean.withAlpha (0.18f));
            for (int i = 1; i < 8; ++i)
            {
                const float x = bounds.getX() + bounds.getWidth() * (float) i / 8.0f;
                g.drawLine (x, bounds.getY() + 6.0f, x, bounds.getBottom() - 6.0f, 0.8f);
            }
            for (int i = 1; i < 4; ++i)
            {
                const float y = bounds.getY() + bounds.getHeight() * (float) i / 4.0f;
                g.drawLine (bounds.getX() + 6.0f, y, bounds.getRight() - 6.0f, y, 0.8f);
            }

            const float midY  = bounds.getCentreY();
            const float halfH = bounds.getHeight() * 0.42f;

            // Ceiling dashed lines at +/- ceilingGain
            const float ceilAbs = juce::jlimit (0.05f, 1.0f, ceilingGain);
            const float dash[] = { 6.0f, 4.0f };
            g.setColour (palette.gold.withAlpha (0.8f));
            g.drawDashedLine ({ bounds.getX() + 10.0f, midY - halfH * ceilAbs,
                                bounds.getRight() - 10.0f, midY - halfH * ceilAbs },
                              dash, 2, 1.5f);
            g.drawDashedLine ({ bounds.getX() + 10.0f, midY + halfH * ceilAbs,
                                bounds.getRight() - 10.0f, midY + halfH * ceilAbs },
                              dash, 2, 1.5f);

            // Waveform
            const int w = (int) bounds.getWidth() - 20;
            if (w <= 1) return;

            std::array<float, th::dsp::ScopeBuffer::frameSize> data {};
            buffer.readLatest (data.data(), w);

            juce::Path wave;
            wave.preallocateSpace (w * 3);

            const float x0 = bounds.getX() + 10.0f;
            bool started = false;

            for (int i = 0; i < w; ++i)
            {
                const float v = juce::jlimit (-1.0f, 1.0f, data[(size_t) i]);
                const float px = x0 + (float) i;
                const float py = midY - v * halfH;
                if (! started) { wave.startNewSubPath (px, py); started = true; }
                else            wave.lineTo (px, py);
            }

            // Glow layer (wide & translucent)
            g.setColour (palette.purpleHi.withAlpha (0.25f));
            g.strokePath (wave, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved));

            // Main waveform (bright purple)
            g.setColour (palette.purpleHi);
            g.strokePath (wave, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));

            // Center axis
            g.setColour (palette.cream.withAlpha (0.1f));
            g.drawLine (bounds.getX() + 8.0f, midY, bounds.getRight() - 8.0f, midY, 0.8f);

            // 🎮 CRT scanlines overlay — old arcade monitor feel
            if (scanlinesEnabled)
            {
                g.setColour (juce::Colours::black.withAlpha (0.22f));
                for (float sy = bounds.getY() + 2.0f; sy < bounds.getBottom() - 2.0f; sy += 3.0f)
                    g.drawHorizontalLine ((int) sy, bounds.getX() + 2.0f, bounds.getRight() - 2.0f);
            }
        }

        void setScanlinesEnabled (bool on) noexcept { scanlinesEnabled = on; repaint(); }

    private:
        void timerCallback() override { repaint(); }

        const th::dsp::ScopeBuffer& buffer;
        const LookAndFeel1017::Palette& palette;
        float ceilingGain { 0.97f };
        bool scanlinesEnabled { true }; // 🎮 CRT vibe, on by default
    };
}
