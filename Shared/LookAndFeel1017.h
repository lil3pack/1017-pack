#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Shared visual identity for the 1017 Pack.
 * Purple lean body, gold knobs/text, cream value readouts.
 * Matches the Nano Banana mockups.
 */
class LookAndFeel1017 : public juce::LookAndFeel_V4
{
public:
    // 3PACK CLIP · VOLT edition palette (default)
    // Acid green + lime + electric yellow — trap meme aesthetic.
    // (Keeps the member names 'purpleLean' / 'purpleHi' / 'gold' / 'goldHi' for
    //  backward compat — they now carry the VOLT palette tones.)
    struct Palette
    {
        juce::Colour bgDeep     { 0xFF0A1000 };  // near-black with green tint
        juce::Colour bgMid      { 0xFF1A2500 };  // dark forest
        juce::Colour purpleLean { 0xFF9CCC65 };  // acid green (primary)
        juce::Colour purpleHi   { 0xFFC6FF00 };  // bright electric green highlight
        juce::Colour gold       { 0xFFFFEB3B };  // acid yellow
        juce::Colour goldHi     { 0xFFCDDC39 };  // lime yellow
        juce::Colour cream      { 0xFFF1F8E9 };  // light pale green-cream
        juce::Colour danger     { 0xFFE74C3C };
    };

    LookAndFeel1017()
    {
        setColour (juce::ResizableWindow::backgroundColourId,      palette.bgDeep);
        setColour (juce::Slider::textBoxTextColourId,              palette.cream);
        setColour (juce::Slider::textBoxBackgroundColourId,        palette.bgDeep.brighter (0.1f));
        setColour (juce::Slider::textBoxOutlineColourId,           palette.gold.withAlpha (0.4f));
        setColour (juce::Slider::rotarySliderFillColourId,         palette.gold);
        setColour (juce::Slider::rotarySliderOutlineColourId,      palette.purpleLean);
        setColour (juce::Slider::thumbColourId,                    palette.purpleHi);
        setColour (juce::Label::textColourId,                      palette.gold);
        setColour (juce::ComboBox::backgroundColourId,             palette.gold);
        setColour (juce::ComboBox::textColourId,                   palette.bgDeep);
        setColour (juce::ComboBox::arrowColourId,                  palette.bgDeep);
        setColour (juce::ComboBox::outlineColourId,                palette.goldHi);
        setColour (juce::PopupMenu::backgroundColourId,            palette.bgMid);
        setColour (juce::PopupMenu::textColourId,                  palette.cream);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, palette.purpleLean);
        setColour (juce::PopupMenu::highlightedTextColourId,       palette.goldHi);
        setColour (juce::ToggleButton::textColourId,               palette.cream);
        setColour (juce::ToggleButton::tickColourId,               palette.goldHi);
        setColour (juce::ToggleButton::tickDisabledColourId,       palette.purpleLean.withAlpha (0.4f));
    }

    // ---- Rotary knob ------------------------------------------------------
    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        auto centre = bounds.getCentre();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 6.0f;
        const float angle  = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // Outer purple track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (palette.purpleLean.withAlpha (0.35f));
        g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Gold value arc
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                rotaryStartAngle, angle, true);
        g.setColour (palette.gold);
        g.strokePath (valueArc, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

        // Glow on hover / drag
        if (slider.isMouseOverOrDragging())
        {
            g.setColour (palette.purpleHi.withAlpha (0.25f));
            g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
        }

        // Gold disc with radial gradient
        const float discRadius = radius - 10.0f;
        const auto discBounds = juce::Rectangle<float> (
            centre.x - discRadius, centre.y - discRadius, discRadius * 2.0f, discRadius * 2.0f);

        juce::ColourGradient disc (
            palette.goldHi,
            centre.x - discRadius * 0.45f, centre.y - discRadius * 0.45f,
            palette.gold.darker (0.35f),
            centre.x + discRadius, centre.y + discRadius,
            true);
        g.setGradientFill (disc);
        g.fillEllipse (discBounds);

        // Dark rim
        g.setColour (palette.bgDeep.withAlpha (0.7f));
        g.drawEllipse (discBounds, 1.5f);

        // Purple indicator line
        juce::Path indicator;
        indicator.addRoundedRectangle (-2.0f, -discRadius + 3.0f, 4.0f, discRadius * 0.55f, 2.0f);
        g.setColour (palette.purpleLean.darker (0.6f));
        g.fillPath (indicator, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

        // Value readout INSIDE the disc — dark text on gold (FabFilter style)
        const juce::String text = slider.getTextFromValue (slider.getValue());
        const float fontSize = juce::jmin (discRadius * 0.42f, 20.0f);
        g.setFont (juce::Font (fontSize, juce::Font::bold));
        g.setColour (palette.bgDeep.withAlpha (0.92f));
        g.drawText (text,
                    juce::Rectangle<float> (centre.x - discRadius,
                                            centre.y - fontSize * 0.5f - 1.0f,
                                            discRadius * 2.0f,
                                            fontSize + 2.0f),
                    juce::Justification::centred, false);
    }

    // ---- Slider text box (value readout under the knob) -------------------
    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox (slider);
        l->setColour (juce::Label::outlineWhenEditingColourId, palette.goldHi);
        l->setColour (juce::Label::backgroundWhenEditingColourId, palette.bgDeep);
        l->setColour (juce::Label::textWhenEditingColourId, palette.cream);
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::Font (13.0f, juce::Font::bold));
        return l;
    }

    // ---- ComboBox styled as gold nameplate --------------------------------
    void drawComboBox (juce::Graphics& g, int width, int height,
                       bool /*isButtonDown*/, int /*buttonX*/, int /*buttonY*/,
                       int /*buttonW*/, int /*buttonH*/, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

        // Gold plate gradient
        juce::ColourGradient grad (palette.goldHi, bounds.getTopLeft(),
                                   palette.gold.darker (0.2f), bounds.getBottomRight(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bounds, 4.0f);

        // Dark edge
        g.setColour (palette.bgDeep.withAlpha (0.5f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

        // Arrow triangle on the right
        juce::Path arrow;
        const float cx = bounds.getRight() - 12.0f;
        const float cy = bounds.getCentreY();
        arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (palette.bgDeep);
        g.fillPath (arrow);

        juce::ignoreUnused (box);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::Font (12.0f, juce::Font::bold);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 0, box.getWidth() - 24, box.getHeight());
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (juce::Justification::centredLeft);
        label.setColour (juce::Label::textColourId, palette.bgDeep);
    }

    // ---- Toggle button styled as gem LED ----------------------------------
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                           bool /*shouldDrawButtonAsHighlighted*/,
                           bool /*shouldDrawButtonAsDown*/) override
    {
        auto bounds = b.getLocalBounds().toFloat();
        auto gemArea = bounds.removeFromLeft (bounds.getHeight()).reduced (3.0f);

        const bool on = b.getToggleState();

        // Gem body (diamond shape)
        juce::Path gem;
        const auto c = gemArea.getCentre();
        const float s = gemArea.getWidth() * 0.45f;
        gem.addQuadrilateral (c.x, c.y - s, c.x + s, c.y, c.x, c.y + s, c.x - s, c.y);

        if (on)
        {
            juce::ColourGradient gg (palette.goldHi, c.x - s * 0.5f, c.y - s * 0.5f,
                                     palette.gold.darker (0.3f), c.x + s, c.y + s, true);
            g.setGradientFill (gg);
            g.fillPath (gem);
            // Glow halo
            g.setColour (palette.goldHi.withAlpha (0.25f));
            g.fillEllipse (gemArea.expanded (4.0f));
        }
        else
        {
            g.setColour (palette.bgDeep.brighter (0.15f));
            g.fillPath (gem);
        }
        g.setColour (palette.bgDeep);
        g.strokePath (gem, juce::PathStrokeType (1.0f));

        // Label
        g.setColour (on ? palette.goldHi : palette.cream.withAlpha (0.7f));
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawText (b.getButtonText(), bounds.translated (6.0f, 0.0f),
                    juce::Justification::centredLeft);
    }

    // ---- Button (for POWER, used with juce::TextButton) -------------------
    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour& /*bg*/, bool /*over*/, bool /*down*/) override
    {
        auto bounds = b.getLocalBounds().toFloat().reduced (2.0f);
        const bool on = b.getToggleState();

        juce::ColourGradient grad (on ? palette.goldHi : palette.gold.darker (0.4f),
                                   bounds.getCentreX() - bounds.getWidth() * 0.3f,
                                   bounds.getCentreY() - bounds.getHeight() * 0.3f,
                                   on ? palette.gold.darker (0.3f) : palette.bgDeep,
                                   bounds.getRight(), bounds.getBottom(), true);
        g.setGradientFill (grad);
        g.fillEllipse (bounds);

        g.setColour (palette.bgDeep.withAlpha (0.7f));
        g.drawEllipse (bounds, 1.5f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override
    {
        return juce::Font (10.0f, juce::Font::bold);
    }

    const Palette& getPalette() const noexcept { return palette; }

private:
    Palette palette;
};
