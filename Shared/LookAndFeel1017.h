#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Shared visual identity for the 1017 Pack.
 *
 * This is a starting skeleton. On next session we'll override drawRotarySlider,
 * drawButton, drawToggleButton, drawComboBox, etc., so both plugins look like
 * the Nano Banana mockups (gold knobs, purple glass body, gem LEDs).
 */
class LookAndFeel1017 : public juce::LookAndFeel_V4
{
public:
    // Palette — keep in sync with the mockup
    struct Palette
    {
        juce::Colour bgDeep     { 0xFF1A0F2B };
        juce::Colour bgMid      { 0xFF2B1A3D };
        juce::Colour purpleLean { 0xFF6B3FA0 };
        juce::Colour purpleHi   { 0xFF9B6FD9 };
        juce::Colour gold       { 0xFFD4AF37 };
        juce::Colour goldHi     { 0xFFF4D03F };
        juce::Colour cream      { 0xFFF5E9D1 };
        juce::Colour danger     { 0xFFE74C3C };
    };

    LookAndFeel1017()
    {
        // Sensible defaults so the whole UI uses the palette.
        setColour (juce::ResizableWindow::backgroundColourId,      palette.bgDeep);
        setColour (juce::Slider::textBoxTextColourId,              palette.cream);
        setColour (juce::Slider::textBoxBackgroundColourId,        palette.bgDeep);
        setColour (juce::Slider::textBoxOutlineColourId,           palette.gold.withAlpha (0.4f));
        setColour (juce::Slider::rotarySliderFillColourId,         palette.gold);
        setColour (juce::Slider::rotarySliderOutlineColourId,      palette.purpleLean);
        setColour (juce::Slider::thumbColourId,                    palette.purpleHi);
        setColour (juce::Label::textColourId,                      palette.gold);
        setColour (juce::ComboBox::backgroundColourId,             palette.bgDeep);
        setColour (juce::ComboBox::textColourId,                   palette.gold);
        setColour (juce::ComboBox::arrowColourId,                  palette.gold);
        setColour (juce::ComboBox::outlineColourId,                palette.gold.withAlpha (0.5f));
        setColour (juce::PopupMenu::backgroundColourId,            palette.bgMid);
        setColour (juce::PopupMenu::textColourId,                  palette.cream);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, palette.purpleLean);
        setColour (juce::PopupMenu::highlightedTextColourId,       palette.goldHi);
    }

    // --- Rotary knob: gold disc with purple indicator, matches mockup ---
    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        juce::ignoreUnused (slider);

        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        auto centre = bounds.getCentre();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 4.0f;
        const float angle  = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // Outer purple arc track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (palette.purpleLean.withAlpha (0.5f));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Gold value arc
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                rotaryStartAngle, angle, true);
        g.setColour (palette.gold);
        g.strokePath (valueArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

        // Gold disc with subtle radial gradient
        const float discRadius = radius - 8.0f;
        juce::ColourGradient disc (palette.goldHi, centre.x - discRadius * 0.4f, centre.y - discRadius * 0.4f,
                                   palette.gold.darker (0.3f), centre.x + discRadius, centre.y + discRadius, true);
        g.setGradientFill (disc);
        g.fillEllipse (centre.x - discRadius, centre.y - discRadius, discRadius * 2.0f, discRadius * 2.0f);

        g.setColour (palette.bgDeep.withAlpha (0.6f));
        g.drawEllipse (centre.x - discRadius, centre.y - discRadius, discRadius * 2.0f, discRadius * 2.0f, 1.2f);

        // Purple indicator line
        juce::Path indicator;
        const float indLen = discRadius * 0.8f;
        indicator.addRectangle (-1.5f, -discRadius + 2.0f, 3.0f, indLen * 0.55f);
        g.setColour (palette.purpleHi);
        g.fillPath (indicator, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    }

    const Palette& getPalette() const noexcept { return palette; }

private:
    Palette palette;
};
