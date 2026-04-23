#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include "LookAndFeel1017.h"

// =============================================================================
// 1017 TYCOON — pixel-art trap mogul tycoon, embedded in 3PACK CLIP.
//
// Gameplay loop (Cookie Clicker / AdVenture Capitalist school):
//   - Click the BEAT PAD for instant cash ($1 + bonus per MPC owned)
//   - Buy upgrades (MPC → BOOTH → LABEL → TOUR → GRILL) that generate
//     passive income per second
//   - Cost of each upgrade scales exponentially (1.10–1.22× per unit)
//   - Unlocks are gated on totalEarned ($50 → $500 → $5K → $50K)
//   - Random events every 60–180 s: viral hits, label deals, bootleg loss
//   - Audio-coupled bonus: +50% click reward when audio passes through
//   - Progress persists in plugin state (saves with project)
//   - Offline earnings: up to 1 h of passive income when plugin reopens
//
// Visuals:
//   - Animated studio scene with tiered producer character
//   - Equipment sprites fill the floor as you upgrade
//   - Floating "+$" popups, beat-pad pulse, gold-flash money counter
//   - Random-event banner slides in at the top of the scene
//
// Save state via juce::ValueTree — round-trips through plugin serialization.
// =============================================================================
namespace th::game
{
    //==========================================================================
    // Upgrade catalog
    //==========================================================================
    struct UpgradeDef
    {
        const char* longName;
        const char* shortName;
        double      baseCost;
        double      costMultiplier;
        double      incomePerSec;
        double      unlockThreshold; // totalEarned required to unlock
    };

    static constexpr int NUM_UPGRADES = 5;

    static const std::array<UpgradeDef, NUM_UPGRADES>& getUpgrades()
    {
        static const std::array<UpgradeDef, NUM_UPGRADES> UP = {{
            { "MPC Sampler",   "MPC", 10.0,       1.10,    1.0,      0.0     },
            { "Booth",          "BTH", 100.0,      1.13,    8.0,      50.0    },
            { "Label",          "LBL", 1000.0,     1.16,    55.0,     500.0   },
            { "World Tour",     "TOR", 10000.0,    1.19,    420.0,    5000.0  },
            { "Diamond Grill",  "GRL", 100000.0,   1.22,    3200.0,   50000.0 },
        }};
        return UP;
    }

    //==========================================================================
    // Game state
    //==========================================================================
    struct GameState
    {
        double  money       { 0.0 };
        double  totalEarned { 0.0 };
        std::array<int, NUM_UPGRADES> counts { {0, 0, 0, 0, 0} };
        int64_t lastSavedMs { 0 };

        // Temporary event multiplier + end timestamp
        double  eventMultiplier { 1.0 };
        int64_t eventEndMs      { 0 };

        double getNextCost (int idx) const noexcept
        {
            const auto& u = getUpgrades()[(size_t) idx];
            return u.baseCost * std::pow (u.costMultiplier, counts[(size_t) idx]);
        }

        double getIncomePerSec() const noexcept
        {
            double total = 0.0;
            for (int i = 0; i < NUM_UPGRADES; ++i)
                total += counts[(size_t) i] * getUpgrades()[(size_t) i].incomePerSec;
            return total * eventMultiplier;
        }

        double getClickReward() const noexcept
        {
            // Base click + 10% of one MPC's per-sec income per MPC owned
            return 1.0 + counts[0] * 0.15;
        }

        bool isUpgradeUnlocked (int idx) const noexcept
        {
            return totalEarned >= getUpgrades()[(size_t) idx].unlockThreshold;
        }

        int getProducerTier() const noexcept
        {
            return totalEarned >= 500000.0 ? 3 :
                   totalEarned >= 50000.0  ? 2 :
                   totalEarned >= 5000.0   ? 1 : 0;
        }

        //----- serialize -----
        juce::ValueTree toValueTree() const
        {
            juce::ValueTree vt ("TycoonState");
            vt.setProperty ("money",         money,         nullptr);
            vt.setProperty ("totalEarned",   totalEarned,   nullptr);
            vt.setProperty ("lastSavedMs",   (juce::int64) lastSavedMs, nullptr);
            for (int i = 0; i < NUM_UPGRADES; ++i)
                vt.setProperty (juce::String ("count_") + juce::String (i),
                                counts[(size_t) i], nullptr);
            return vt;
        }

        void fromValueTree (const juce::ValueTree& vt)
        {
            if (! vt.isValid()) return;
            money       = (double) vt.getProperty ("money",       0.0);
            totalEarned = (double) vt.getProperty ("totalEarned", 0.0);
            lastSavedMs = (int64_t) (juce::int64) vt.getProperty ("lastSavedMs", 0);
            for (int i = 0; i < NUM_UPGRADES; ++i)
                counts[(size_t) i] = (int) vt.getProperty (
                    juce::String ("count_") + juce::String (i), 0);
        }
    };

    //==========================================================================
    // Floating "+$" popup
    //==========================================================================
    struct FloatingText
    {
        float x, y, vy;
        float life;
        juce::String text;
        juce::Colour colour;
        float scale { 1.0f };
    };

    //==========================================================================
    // Main game component
    //==========================================================================
    class TycoonGame : public juce::Component,
                        private juce::Timer
    {
    public:
        TycoonGame()
        {
            setOpaque (false);
            setInterceptsMouseClicks (true, false);
            lastTickMs = juce::Time::currentTimeMillis();
            startTimerHz (30);
        }

        ~TycoonGame() override { stopTimer(); }

        //----- component -----
        void paint (juce::Graphics& g) override
        {
            drawBackground     (g);
            drawTitleBar       (g);
            drawHUD            (g);
            drawScene          (g);
            drawShop           (g);
            drawFloatingTexts  (g);
            drawEventBanner    (g);
        }

        void resized() override { layoutRects(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (beatPadBounds.contains (e.getPosition()))
            {
                clickBeatPad (e.getPosition());
                return;
            }
            for (int i = 0; i < NUM_UPGRADES; ++i)
                if (shopRects[(size_t) i].contains (e.getPosition()))
                {
                    buyUpgrade (i);
                    return;
                }
        }

        //----- audio coupling -----
        void setAudioActivity (float rms01) noexcept
        {
            // Exponential smoothing so activity fluctuates but doesn't jitter
            audioActivity = 0.92f * audioActivity + 0.08f * juce::jlimit (0.0f, 1.0f, rms01);
        }

        //----- save/load -----
        juce::ValueTree getSaveState() const
        {
            state.lastSavedMs = juce::Time::currentTimeMillis();
            return state.toValueTree();
        }

        void loadSaveState (const juce::ValueTree& vt)
        {
            state.fromValueTree (vt);

            // Offline progress: compute earnings from lastSavedMs to now (cap 1h)
            if (state.lastSavedMs > 0)
            {
                const int64_t nowMs = juce::Time::currentTimeMillis();
                const double  elapsedS = juce::jlimit (
                    0.0, 3600.0, (double) (nowMs - state.lastSavedMs) / 1000.0);
                const double earned = state.getIncomePerSec() * elapsedS;
                if (earned > 0.0)
                {
                    state.money       += earned;
                    state.totalEarned += earned;
                    offlineBannerTimer = 90; // show banner for 3 sec
                    offlineBannerAmount = earned;
                }
                state.lastSavedMs = nowMs;
            }
        }

    private:
        //======================================================================
        // State
        //======================================================================
        mutable GameState state;

        float  audioActivity     { 0.0f };
        float  beatPadPulse      { 1.0f };
        float  moneyFlash        { 0.0f };
        int    offlineBannerTimer  { 0 };
        double offlineBannerAmount { 0.0 };
        int    framesSinceEvent  { 0 };
        int64_t lastTickMs { 0 };

        std::vector<FloatingText> floaters;
        juce::Random rng;

        // Bounds
        juce::Rectangle<int> titleRect;
        juce::Rectangle<int> hudRect;
        juce::Rectangle<int> sceneRect;
        juce::Rectangle<int> shopRect;
        juce::Rectangle<int> beatPadBounds;
        std::array<juce::Rectangle<int>, NUM_UPGRADES> shopRects;

        //======================================================================
        // Layout
        //======================================================================
        void layoutRects()
        {
            const auto b = getLocalBounds();
            titleRect = b.withHeight (16);
            hudRect   = b.withY (16).withHeight (14);
            shopRect  = b.withY (b.getBottom() - 46).withHeight (46);
            sceneRect = b.withTrimmedTop (titleRect.getHeight() + hudRect.getHeight() + 2)
                         .withTrimmedBottom (shopRect.getHeight() + 2);

            // Centered beat pad button (42×28)
            const int bpW = 48, bpH = 28;
            beatPadBounds = juce::Rectangle<int> (
                sceneRect.getCentreX() - bpW / 2,
                sceneRect.getCentreY() - 2,
                bpW, bpH);

            const int btnW = shopRect.getWidth() / NUM_UPGRADES;
            for (int i = 0; i < NUM_UPGRADES; ++i)
                shopRects[(size_t) i] = juce::Rectangle<int> (
                    shopRect.getX() + i * btnW + 1,
                    shopRect.getY() + 2,
                    btnW - 2,
                    shopRect.getHeight() - 4);
        }

        //======================================================================
        // Game actions
        //======================================================================
        void clickBeatPad (juce::Point<int> /*clickPos*/)
        {
            const double audioBonus = 1.0 + audioActivity * 0.5;
            const double reward = state.getClickReward() * audioBonus;

            state.money       += reward;
            state.totalEarned += reward;

            beatPadPulse = 1.5f;
            moneyFlash   = 1.0f;

            spawnFloater (juce::String::formatted ("+$%.0f", reward),
                          (float) beatPadBounds.getCentreX(),
                          (float) beatPadBounds.getY(),
                          juce::Colour (0xFFFFEE58),
                          1.2f);
        }

        void buyUpgrade (int idx)
        {
            if (! state.isUpgradeUnlocked (idx)) return;
            const double cost = state.getNextCost (idx);
            if (state.money < cost) return;

            state.money -= cost;
            state.counts[(size_t) idx]++;

            const auto r = shopRects[(size_t) idx];
            spawnFloater (juce::String (getUpgrades()[(size_t) idx].shortName) + " +1",
                          (float) r.getCentreX(),
                          (float) r.getY(),
                          juce::Colours::white,
                          1.0f);
        }

        void spawnFloater (const juce::String& text, float x, float y,
                           juce::Colour col, float scale = 1.0f)
        {
            floaters.push_back ({ x, y, -1.3f, 1.0f, text, col, scale });
            if (floaters.size() > 50)
                floaters.erase (floaters.begin());
        }

        void triggerRandomEvent()
        {
            struct Ev { juce::String text; double mul; int durationMs; double flat; };
            const std::array<Ev, 6> events = {{
                { "VIRAL ON TIKTOK x2",     2.0, 25000, 0.0 },
                { "GUCCI CO-SIGN +$1K",     1.0, 0,     1000.0 },
                { "PLATINUM PLAQUE x3",     3.0, 15000, 0.0 },
                { "LABEL DEAL +$5K",        1.0, 0,     5000.0 },
                { "RADIO ROTATION x1.5",    1.5, 60000, 0.0 },
                { "FEAT. ON MIXTAPE +$500", 1.0, 0,     500.0 },
            }};
            const auto& e = events[(size_t) rng.nextInt ((int) events.size())];
            if (e.durationMs > 0)
            {
                state.eventMultiplier = e.mul;
                state.eventEndMs      = juce::Time::currentTimeMillis() + e.durationMs;
            }
            if (e.flat > 0.0)
            {
                state.money       += e.flat;
                state.totalEarned += e.flat;
            }
            spawnFloater (e.text,
                          (float) sceneRect.getCentreX(),
                          (float) sceneRect.getY() + 6.0f,
                          juce::Colour (0xFFFFEE58), 1.0f);
            framesSinceEvent = 0;
        }

        //======================================================================
        // Timer — game tick + animation update
        //======================================================================
        void timerCallback() override
        {
            const int64_t nowMs = juce::Time::currentTimeMillis();
            double dtS = (double) (nowMs - lastTickMs) / 1000.0;
            if (dtS > 0.25) dtS = 0.25;
            lastTickMs = nowMs;

            // Passive income
            const double earned = state.getIncomePerSec() * dtS;
            if (earned > 0.0)
            {
                state.money       += earned;
                state.totalEarned += earned;
            }

            // Event expiration
            if (state.eventEndMs > 0 && nowMs > state.eventEndMs)
            {
                state.eventMultiplier = 1.0;
                state.eventEndMs      = 0;
            }

            // Random events: check every ~60 frames once past 30 sec
            framesSinceEvent++;
            if (framesSinceEvent > 30 * 60 && rng.nextFloat() < 0.01f)
                triggerRandomEvent();

            // Animation decays
            beatPadPulse = 1.0f + (beatPadPulse - 1.0f) * 0.82f;
            moneyFlash  *= 0.90f;
            if (offlineBannerTimer > 0) --offlineBannerTimer;

            // Update floaters
            for (auto& f : floaters)
            {
                f.y   += f.vy;
                f.life -= 1.0f / 45.0f;  // ~1.5 sec lifetime
            }
            floaters.erase (std::remove_if (floaters.begin(), floaters.end(),
                              [] (const FloatingText& f) { return f.life <= 0.0f; }),
                            floaters.end());

            state.lastSavedMs = nowMs;
            repaint();
        }

        //======================================================================
        // Rendering
        //======================================================================
        static juce::String formatMoney (double amount)
        {
            if (amount < 1000.0)        return juce::String (amount, 0);
            if (amount < 1000000.0)     return juce::String (amount / 1000.0, 1) + "K";
            if (amount < 1000000000.0)  return juce::String (amount / 1000000.0, 2) + "M";
            return juce::String (amount / 1000000000.0, 2) + "B";
        }

        void drawBackground (juce::Graphics& g)
        {
            // Red–black gradient (matches plugin FIRE palette)
            juce::ColourGradient bg (
                juce::Colour (0xFF1A0504), 0.0f, 0.0f,
                juce::Colour (0xFF2E0A07), (float) getWidth(), (float) getHeight(), false);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

            // Gold border (pulses with audio)
            const float pulse = 0.35f + audioActivity * 0.5f;
            g.setColour (juce::Colour (0xFFFFC107).withAlpha (pulse));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.2f);

            // Subtle floor grid inside scene area for depth
            g.setColour (juce::Colour (0xFFB71C1C).withAlpha (0.15f));
            const int gridStep = 6;
            for (int gy = sceneRect.getY() + gridStep; gy < sceneRect.getBottom(); gy += gridStep)
                g.drawHorizontalLine (gy, (float) sceneRect.getX() + 2, (float) sceneRect.getRight() - 2);
        }

        void drawTitleBar (juce::Graphics& g)
        {
            g.setColour (juce::Colour (0xFFFFC107));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                   10.0f, juce::Font::bold));
            g.drawFittedText ("1017 TYCOON", titleRect, juce::Justification::centred, 1);
        }

        void drawHUD (juce::Graphics& g)
        {
            // Money (gold flashes on earn)
            const juce::Colour moneyCol = juce::Colour (0xFFFFEE58).interpolatedWith (
                juce::Colours::white, moneyFlash);
            g.setColour (moneyCol);
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                   11.0f, juce::Font::bold));
            g.drawText ("$" + formatMoney (state.money),
                        hudRect.withTrimmedLeft (4).withTrimmedRight (hudRect.getWidth() / 2),
                        juce::Justification::centredLeft);

            // Income rate
            g.setColour (juce::Colour (0xFFFFA726));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                   9.0f, juce::Font::plain));
            g.drawText ("+$" + formatMoney (state.getIncomePerSec()) + "/s",
                        hudRect.withTrimmedRight (4).withTrimmedLeft (hudRect.getWidth() / 2),
                        juce::Justification::centredRight);

            // Event multiplier indicator
            if (state.eventEndMs > juce::Time::currentTimeMillis() && state.eventMultiplier > 1.0)
            {
                g.setColour (juce::Colour (0xFFFF5252));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                       9.0f, juce::Font::bold));
                g.drawText ("x" + juce::String (state.eventMultiplier, 1),
                            hudRect.withWidth (28).withX (hudRect.getCentreX() - 14),
                            juce::Justification::centred);
            }
        }

        void drawScene (juce::Graphics& g)
        {
            // Producer character (evolves with tier)
            const int tier = state.getProducerTier();
            drawProducer (g,
                          sceneRect.getX() + 12,
                          sceneRect.getBottom() - 32,
                          tier);

            // Equipment sprites (floor, fills as upgrades bought)
            drawEquipment (g);

            // Beat pad button (centered)
            drawBeatPad (g);
        }

        void drawProducer (juce::Graphics& g, int x, int y, int tier)
        {
            // Subtle head-bob
            const float bob = std::sin ((float) (juce::Time::currentTimeMillis() / 250.0)) * 1.0f;
            y += (int) bob;

            const juce::Colour skin      (0xFFEAC6A9);
            const juce::Colour hoodie    = tier >= 1 ? juce::Colour (0xFF3D0A0A)
                                                      : juce::Colour (0xFF663333);
            const juce::Colour hoodieTrim = juce::Colour (0xFF882020);
            const juce::Colour eyes      (0xFF0E0714);
            const juce::Colour chain     (0xFFFFD700);
            const juce::Colour grill     (0xFFFFF8DC);
            const juce::Colour cap       (0xFF0E0714);

            // Body (hoodie)
            g.setColour (hoodie);
            g.fillRect (x + 4, y,      12, 14);   // torso
            g.fillRect (x + 1, y + 2,   4,  8);   // left arm
            g.fillRect (x + 15, y + 2,  4,  8);   // right arm
            // Hoodie trim
            g.setColour (hoodieTrim);
            g.fillRect (x + 4, y + 13, 12, 1);

            // Head
            g.setColour (skin);
            g.fillRect (x + 6, y - 8,  8, 8);

            // Hood top (tier 0)
            if (tier == 0)
            {
                g.setColour (hoodie);
                g.fillRect (x + 5, y - 10, 10, 3);
            }
            // Cap (tier 1+)
            if (tier >= 1)
            {
                g.setColour (cap);
                g.fillRect (x + 5, y - 10, 10, 3);
                g.fillRect (x + 13, y - 9,  4, 1);  // brim
            }
            // Chain (tier 2+)
            if (tier >= 2)
            {
                g.setColour (chain);
                g.fillRect (x + 7, y + 1, 6, 1);
                g.fillRect (x + 9, y + 2, 2, 2);
                g.setColour (juce::Colours::white);
                g.fillRect (x + 9, y + 2, 1, 1); // diamond glint
            }
            // Grill / smile (tier 3)
            if (tier >= 3)
            {
                g.setColour (grill);
                g.fillRect (x + 8, y - 2, 4, 1);
            }

            // Eyes
            g.setColour (eyes);
            g.fillRect (x + 7, y - 5,  1, 1);
            g.fillRect (x + 12, y - 5, 1, 1);
        }

        void drawEquipment (juce::Graphics& g)
        {
            // MPCs (up to 8 shown on the floor, bottom-left)
            const int mpcs = std::min (state.counts[0], 8);
            for (int i = 0; i < mpcs; ++i)
            {
                const int x = sceneRect.getX() + 6 + (i % 4) * 7;
                const int y = sceneRect.getBottom() - 8 - (i / 4) * 7;
                g.setColour (juce::Colour (0xFF0E0714));
                g.fillRect (x, y, 6, 6);
                g.setColour (juce::Colour (0xFFFFC107));
                g.fillRect (x + 1, y + 1, 1, 1);
                g.fillRect (x + 4, y + 1, 1, 1);
                g.fillRect (x + 1, y + 4, 1, 1);
                g.fillRect (x + 4, y + 4, 1, 1);
            }

            // Booths (mic stands, right side)
            const int booths = std::min (state.counts[1], 5);
            for (int i = 0; i < booths; ++i)
            {
                const int x = sceneRect.getRight() - 10 - i * 8;
                const int y = sceneRect.getBottom() - 16;
                g.setColour (juce::Colour (0xFF888888));
                g.fillRect (x + 3, y + 4, 1, 8);
                g.fillRect (x + 1, y + 11, 5, 1);
                g.setColour (juce::Colour (0xFFFFE082));
                g.fillEllipse ((float) (x + 2), (float) y, 3.5f, 5.0f);
            }

            // Labels (gold chains hanging from top, up to 3)
            const int labels = std::min (state.counts[2], 3);
            for (int i = 0; i < labels; ++i)
            {
                const int x = sceneRect.getX() + 24 + i * 18;
                const int y = sceneRect.getY() + 4;
                g.setColour (juce::Colour (0xFFFFD700));
                g.fillEllipse ((float) x, (float) y, 3.0f, 3.0f);
                g.fillRect (x - 1, y + 3, 5, 1);
            }

            // Tour planes (tiny plane icons top-right, max 2)
            const int tours = std::min (state.counts[3], 2);
            for (int i = 0; i < tours; ++i)
            {
                const int x = sceneRect.getRight() - 14 - i * 12;
                const int y = sceneRect.getY() + 4;
                g.setColour (juce::Colours::white);
                g.fillRect (x, y + 2, 8, 1);
                g.fillRect (x + 3, y, 2, 4);
            }

            // Grills (tiny diamond icons, center top)
            const int grills = std::min (state.counts[4], 3);
            for (int i = 0; i < grills; ++i)
            {
                const int x = sceneRect.getCentreX() - 6 + i * 4;
                const int y = sceneRect.getY() + 2;
                g.setColour (juce::Colour (0xFF6EF6FF));
                g.fillRect (x, y + 1, 3, 1);
                g.fillRect (x + 1, y, 1, 3);
            }
        }

        void drawBeatPad (juce::Graphics& g)
        {
            auto bp = beatPadBounds.toFloat();
            bp = juce::Rectangle<float> (
                bp.getCentreX() - bp.getWidth() * 0.5f * beatPadPulse,
                bp.getCentreY() - bp.getHeight() * 0.5f * beatPadPulse,
                bp.getWidth()  * beatPadPulse,
                bp.getHeight() * beatPadPulse);

            // Gold gradient
            juce::ColourGradient grad (juce::Colour (0xFFFFEE58),
                                        bp.getCentreX(), bp.getY(),
                                        juce::Colour (0xFFD4AF37),
                                        bp.getCentreX(), bp.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bp, 4.0f);
            // Dark border
            g.setColour (juce::Colour (0xFF1A0504));
            g.drawRoundedRectangle (bp, 4.0f, 1.5f);
            // "TAP" text
            g.setColour (juce::Colour (0xFF1A0504));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                   10.0f, juce::Font::bold));
            g.drawText ("TAP", bp.toNearestInt(), juce::Justification::centred, false);
            // Corner pads (decoration)
            g.setColour (juce::Colour (0xFF3D0A0A));
            g.fillRect (bp.getX() + 3, bp.getY() + 3, 4.0f, 4.0f);
            g.fillRect (bp.getRight() - 7, bp.getY() + 3, 4.0f, 4.0f);
            g.fillRect (bp.getX() + 3, bp.getBottom() - 7, 4.0f, 4.0f);
            g.fillRect (bp.getRight() - 7, bp.getBottom() - 7, 4.0f, 4.0f);
        }

        void drawShop (juce::Graphics& g)
        {
            // Shop divider line
            g.setColour (juce::Colour (0xFFFFC107).withAlpha (0.3f));
            g.drawHorizontalLine (shopRect.getY() - 1,
                                  (float) shopRect.getX() + 4,
                                  (float) shopRect.getRight() - 4);

            for (int i = 0; i < NUM_UPGRADES; ++i)
            {
                const auto& r = shopRects[(size_t) i];
                const bool unlocked   = state.isUpgradeUnlocked (i);
                const double cost     = state.getNextCost (i);
                const bool affordable = unlocked && state.money >= cost;

                juce::Colour bg;
                if      (affordable) bg = juce::Colour (0xFF5C1010);
                else if (unlocked)   bg = juce::Colour (0xFF2E0A07);
                else                 bg = juce::Colour (0xFF0E0714);

                g.setColour (bg);
                g.fillRoundedRectangle (r.toFloat(), 3.0f);
                g.setColour (affordable ? juce::Colour (0xFFFFC107)
                                        : juce::Colour (0xFF663333));
                g.drawRoundedRectangle (r.toFloat(), 3.0f, 1.0f);

                // Short name (top)
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                       9.0f, juce::Font::bold));
                g.setColour (unlocked ? juce::Colour (0xFFFFEE58) : juce::Colour (0xFF882020));
                g.drawFittedText (getUpgrades()[(size_t) i].shortName,
                                  r.withHeight (12).withY (r.getY() + 2),
                                  juce::Justification::centred, 1);

                if (unlocked)
                {
                    // Count
                    g.setColour (juce::Colour (0xFFFFF8DC));
                    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                           8.0f, juce::Font::plain));
                    g.drawFittedText ("x" + juce::String (state.counts[(size_t) i]),
                                      r.withHeight (10).withY (r.getY() + 16),
                                      juce::Justification::centred, 1);

                    // Cost
                    g.setColour (affordable ? juce::Colour (0xFFFFEE58) : juce::Colour (0xFF886622));
                    g.drawFittedText ("$" + formatMoney (cost),
                                      r.withHeight (10).withY (r.getY() + 28),
                                      juce::Justification::centred, 1);
                }
                else
                {
                    // "@ $X" lock hint
                    g.setColour (juce::Colour (0xFF663333));
                    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                           7.0f, juce::Font::plain));
                    g.drawFittedText ("UNLOCK",
                                      r.withHeight (10).withY (r.getY() + 16),
                                      juce::Justification::centred, 1);
                    g.drawFittedText ("$" + formatMoney (getUpgrades()[(size_t) i].unlockThreshold),
                                      r.withHeight (10).withY (r.getY() + 26),
                                      juce::Justification::centred, 1);
                }
            }
        }

        void drawFloatingTexts (juce::Graphics& g)
        {
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                   9.0f, juce::Font::bold));
            for (const auto& f : floaters)
            {
                g.setColour (f.colour.withAlpha (std::max (0.0f, f.life)));
                juce::AffineTransform t = juce::AffineTransform::scale (f.scale)
                                            .translated (f.x, f.y);
                juce::Graphics::ScopedSaveState ss (g);
                g.addTransform (t);
                g.drawText (f.text,
                            juce::Rectangle<int> (-40, 0, 80, 12),
                            juce::Justification::centred);
            }
        }

        void drawEventBanner (juce::Graphics& g)
        {
            const int64_t nowMs = juce::Time::currentTimeMillis();
            if (state.eventEndMs > nowMs)
            {
                // Event active: show multiplier strip across top of scene
                juce::Rectangle<int> banner = sceneRect.withHeight (12);
                g.setColour (juce::Colour (0xFF1A0504).withAlpha (0.8f));
                g.fillRoundedRectangle (banner.toFloat(), 2.0f);
                g.setColour (juce::Colour (0xFFFF5252));
                g.drawRoundedRectangle (banner.toFloat(), 2.0f, 1.0f);
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                       8.0f, juce::Font::bold));
                g.setColour (juce::Colour (0xFFFFEE58));
                g.drawFittedText ("x" + juce::String (state.eventMultiplier, 1) + " ACTIVE",
                                  banner, juce::Justification::centred, 1);
            }

            // Offline earnings banner (first 3 sec after open)
            if (offlineBannerTimer > 0 && offlineBannerAmount > 0.0)
            {
                const float alpha = std::min (1.0f, offlineBannerTimer / 30.0f);
                juce::Rectangle<int> banner = sceneRect.withHeight (16);
                g.setColour (juce::Colour (0xFF1A0504).withAlpha (0.9f * alpha));
                g.fillRoundedRectangle (banner.toFloat(), 2.0f);
                g.setColour (juce::Colour (0xFFFFEE58).withAlpha (alpha));
                g.drawRoundedRectangle (banner.toFloat(), 2.0f, 1.0f);
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                       8.0f, juce::Font::bold));
                g.drawFittedText ("OFFLINE +$" + formatMoney (offlineBannerAmount),
                                  banner, juce::Justification::centred, 1);
            }
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TycoonGame)
    };

} // namespace th::game
