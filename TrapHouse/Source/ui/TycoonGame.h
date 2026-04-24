#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <array>
#include <algorithm>
#include <functional>
#include <cmath>
#include "LookAndFeel1017.h"

// =============================================================================
// STACK THE PLAQUES — pixel-art arcade stacker, embedded in 3PACK CLIP.
//
// Core loop (2-5 min / run):
//   - A vinyl record swings left/right at the top
//   - TAP anywhere to drop it straight down
//   - If it lands FULLY on the tower top: perfect — combo goes up, disc size
//     preserved, era may advance
//   - If misaligned: the overhang is sliced off and falls away with physics,
//     the next disc is as wide as the overlap
//   - If the disc would miss the tower (no overlap): GAME OVER, tap to restart
//   - If the slice leaves the disc too thin (<10 px at current scale): GAME OVER
//
// The game stands alone — you don't need the clipper's knob on. When audio
// IS flowing, the topmost disc pulses with the output RMS (pure visual candy)
// and the bg cycles through hot→cool tints. No audio = still fully playable.
//
// Resize:
//   - Editor now draws a corner drag handle on the tycoon bounds; dragging it
//     grows/shrinks the game viewport (min 280×160, max 940×480).
//
// Pixel-art dense style:
//   - Vinyl records: concentric ring grooves + coloured centre label
//   - Tower scrolls down when it grows past the viewport
//   - Every 5 discs the era advances (new palette, faster swing, new label art)
// =============================================================================
namespace th::game
{
    //==========================================================================
    // Era palettes — cycle every 5 discs, cosmetic + slight difficulty ramp
    //==========================================================================
    struct Era
    {
        const char* name;
        juce::Colour bgTop, bgBot;
        juce::Colour labelFill, labelText;
        juce::Colour vinyl;        // outer ring colour
        float       swingMul;       // multiplier on the base swing speed
    };

    inline const std::array<Era, 6>& getEras()
    {
        static const std::array<Era, 6> E = {{
            { "TRAP HOUSE",  juce::Colour (0xFF0A1000), juce::Colour (0xFF1A2500),
                             juce::Colour (0xFFFFEB3B), juce::Colour (0xFF0A1000),
                             juce::Colour (0xFF0F0F0F), 1.00f },
            { "STUDIO",      juce::Colour (0xFF2E1F4C), juce::Colour (0xFF0E0720),
                             juce::Colour (0xFFC6FF00), juce::Colour (0xFF1A0F2E),
                             juce::Colour (0xFF1A1A1A), 1.15f },
            { "LABEL HQ",    juce::Colour (0xFF1A237E), juce::Colour (0xFF0A1028),
                             juce::Colour (0xFFFFD700), juce::Colour (0xFF0D47A1),
                             juce::Colour (0xFF101018), 1.30f },
            { "WORLD TOUR",  juce::Colour (0xFF3B1020), juce::Colour (0xFF150A15),
                             juce::Colour (0xFFFF5252), juce::Colour (0xFFFFEB3B),
                             juce::Colour (0xFF1A0F10), 1.50f },
            { "PLATINUM",    juce::Colour (0xFF37474F), juce::Colour (0xFF102028),
                             juce::Colour (0xFFE0E0E0), juce::Colour (0xFF263238),
                             juce::Colour (0xFFECEFF1), 1.70f },
            { "DIAMOND",     juce::Colour (0xFF006064), juce::Colour (0xFF002F33),
                             juce::Colour (0xFFFFFFFF), juce::Colour (0xFF00ACC1),
                             juce::Colour (0xFF80DEEA), 2.00f },
        }};
        return E;
    }

    //==========================================================================
    // Disc + falling piece data
    //==========================================================================
    struct Disc
    {
        float cx    { 0.0f };     // centre X (screen px)
        float cy    { 0.0f };     // centre Y (screen px)
        float halfW { 40.0f };    // half-width (effectively the radius)
        int   era   { 0 };
        float placedFlash { 0.0f }; // 0..1 gold flash on land
    };

    struct FallingPiece
    {
        float x, y;
        float vx, vy;
        float w, h;
        float rot   { 0.0f };
        float rotV  { 0.0f };
        float life  { 1.5f };
        juce::Colour col;
        int   era;
    };

    struct Spark
    {
        float x, y;
        float vx, vy;
        float life { 1.0f };
        juce::Colour col;
    };

    struct FloatingText
    {
        float x, y, vy;
        float life;
        juce::String text;
        juce::Colour colour;
        float scale { 1.0f };
    };

    //==========================================================================
    // Persistent save — minimal. High-score and total played across runs.
    //==========================================================================
    struct SaveState
    {
        int     highScore  { 0 };
        int     bestTower  { 0 };   // tallest tower ever
        int     runsPlayed { 0 };
        int     totalPerfects { 0 };
        int     prestige   { 0 };   // bumped once per "DIAMOND" era reached
        int     deepestZone{ 0 };   // compat mirror for plugin ICE gate
        int64_t lastSavedMs{ 0 };

        juce::ValueTree toValueTree() const
        {
            juce::ValueTree vt ("TycoonState");
            vt.setProperty ("highScore",     highScore,     nullptr);
            vt.setProperty ("bestTower",     bestTower,     nullptr);
            vt.setProperty ("runsPlayed",    runsPlayed,    nullptr);
            vt.setProperty ("totalPerfects", totalPerfects, nullptr);
            vt.setProperty ("prestige",      prestige,      nullptr);
            vt.setProperty ("deepestZone",   deepestZone,   nullptr);
            vt.setProperty ("lastSavedMs",   (juce::int64) lastSavedMs, nullptr);
            // compat: plugin reads count_3 > 0 to unlock ICE — map to prestige
            vt.setProperty ("count_3",       prestige > 0 ? 1 : 0, nullptr);
            return vt;
        }

        void fromValueTree (const juce::ValueTree& vt)
        {
            if (! vt.isValid()) return;
            highScore     = (int) vt.getProperty ("highScore", 0);
            bestTower     = (int) vt.getProperty ("bestTower", 0);
            runsPlayed    = (int) vt.getProperty ("runsPlayed", 0);
            totalPerfects = (int) vt.getProperty ("totalPerfects", 0);
            prestige      = (int) vt.getProperty ("prestige", 0);
            deepestZone   = (int) vt.getProperty ("deepestZone", 0);
            lastSavedMs   = (int64_t) (juce::int64) vt.getProperty ("lastSavedMs", 0);
        }
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
            startTimerHz (60);
            resetRun();
        }

        ~TycoonGame() override { stopTimer(); }

        //----- JUCE component -----
        void paint (juce::Graphics& g) override
        {
            drawBackground (g);
            drawTowerAndFloor (g);
            drawFallingPieces (g);
            drawActiveDisc (g);
            drawSparks (g);
            drawFloaters (g);
            drawHUD (g);
            if (gameOver)   drawGameOver (g);
            if (showIntro)  drawIntro (g);
            drawDragHandle (g, getDragHandleRect());
        }

        void resized() override
        {
            // Keep the top disc y relative to height
            layout();
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto p = e.getPosition();
            if (getDragHandleRect().contains (p))
            {
                draggingHandle = true;
                if (onDragHandleDown) onDragHandleDown (e);
                return;
            }
            // Tap anywhere in the play area:
            if (showIntro) { showIntro = false; return; }
            if (gameOver)
            {
                if (gameOverDelay > 0.0f) return;
                resetRun();
                return;
            }
            dropActiveDisc();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (draggingHandle && onDragHandleDrag) onDragHandleDrag (e);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (draggingHandle)
            {
                if (onDragHandleUp) onDragHandleUp (e);
                draggingHandle = false;
            }
        }

        //----- audio (visual-only coupling) -----
        void setAudioActivity (float rms01) noexcept
        {
            audioRMS = 0.80f * audioRMS + 0.20f * juce::jlimit (0.0f, 1.0f, rms01);
        }

        //----- save / load -----
        juce::ValueTree getSaveState() const
        {
            save.lastSavedMs = juce::Time::currentTimeMillis();
            return save.toValueTree();
        }

        void loadSaveState (const juce::ValueTree& vt) { save.fromValueTree (vt); }

        //----- drag handle (editor uses these to resize+move the tycoon) -----
        juce::Rectangle<int> getDragHandleRect() const noexcept
        {
            return { getWidth() - 16, getHeight() - 16, 14, 14 };
        }
        std::function<void (const juce::MouseEvent&)> onDragHandleDown;
        std::function<void (const juce::MouseEvent&)> onDragHandleDrag;
        std::function<void (const juce::MouseEvent&)> onDragHandleUp;

    private:
        //======================================================================
        // State
        //======================================================================
        mutable SaveState save;

        std::vector<Disc>         tower;          // placed discs, bottom-first
        std::vector<FallingPiece> fallingPieces;  // sliced overhangs animating off-screen
        std::vector<Spark>        sparks;
        std::vector<FloatingText> floaters;

        Disc  activeDisc;                         // the disc currently swinging
        bool  hasActive    { false };
        bool  gameOver     { false };
        bool  showIntro    { true };
        bool  draggingHandle { false };

        int   score        { 0 };
        int   combo        { 0 };
        int   perfectStreak{ 0 };
        int   towerCount   { 0 };                 // discs stacked this run
        int   currentEra   { 0 };

        // Swing
        float swingPhase   { 0.0f };
        float swingAmp     { 40.0f };
        float swingSpeed   { 2.4f };              // rad/sec base (multiplied by era)

        // Camera — tower grows tall; shift upwards
        float cameraY      { 0.0f };
        int64_t lastTickMs { 0 };

        // Audio
        float audioRMS { 0.0f };

        // Screen shake on perfect
        float shakeAmp { 0.0f };

        // Game over reveal timer (delay before tap-to-restart)
        float gameOverDelay { 0.0f };

        //======================================================================
        // Layout constants
        //======================================================================
        static constexpr float FLOOR_RATIO   = 0.82f;   // floor y as % of height
        static constexpr float DISC_THICK    = 10.0f;   // single-disc thickness (visual)
        static constexpr float DISC_SPACING  = 12.0f;   // vertical spacing between disc tops
        static constexpr float MIN_DISC_HALF = 6.0f;    // below this -> game over
        static constexpr float BASE_DISC_HALF = 60.0f;  // starting disc half-width (px at 470 wide)

        //======================================================================
        // Run lifecycle
        //======================================================================
        void resetRun()
        {
            if (tower.size() > 1)                           // finalize previous run
            {
                if (score > save.highScore)  save.highScore = score;
                if ((int) tower.size() > save.bestTower)
                    save.bestTower = (int) tower.size();
                save.runsPlayed++;
                if (currentEra >= 5) save.prestige = juce::jmax (save.prestige, 1);
            }
            tower.clear();
            fallingPieces.clear();
            sparks.clear();
            floaters.clear();
            score = 0;
            combo = 0;
            perfectStreak = 0;
            towerCount = 0;
            currentEra = 0;
            cameraY = 0.0f;
            swingPhase = 0.0f;
            shakeAmp = 0.0f;
            gameOver = false;
            gameOverDelay = 0.0f;
            // Starting disc at the floor
            Disc base;
            base.cx = (float) getWidth() * 0.5f;
            base.cy = floorY() - DISC_THICK * 0.5f;
            base.halfW = BASE_DISC_HALF;
            base.era = 0;
            tower.push_back (base);
            spawnActiveDisc();
        }

        void spawnActiveDisc()
        {
            activeDisc.cx     = (float) getWidth() * 0.5f;
            activeDisc.cy     = discYForIndex ((int) tower.size());
            activeDisc.halfW  = tower.back().halfW;   // inherit previous width
            activeDisc.era    = currentEra;
            activeDisc.placedFlash = 0.0f;
            hasActive = true;
            swingPhase = 0.0f;
        }

        void dropActiveDisc()
        {
            if (! hasActive) return;
            hasActive = false;

            const auto& top = tower.back();
            const float leftTop   = top.cx      - top.halfW;
            const float rightTop  = top.cx      + top.halfW;
            const float leftNew   = activeDisc.cx - activeDisc.halfW;
            const float rightNew  = activeDisc.cx + activeDisc.halfW;

            const float overlapL = juce::jmax (leftTop,   leftNew);
            const float overlapR = juce::jmin (rightTop,  rightNew);
            const float overlap  = overlapR - overlapL;

            if (overlap <= 2.0f)
            {
                // Missed completely — the falling disc is a single big falling piece
                FallingPiece f;
                f.x = activeDisc.cx; f.y = activeDisc.cy;
                f.w = activeDisc.halfW * 2.0f; f.h = DISC_THICK;
                f.vx = (activeDisc.cx < top.cx) ? -2.0f : 2.0f;
                f.vy = 0.0f;
                f.rotV = (f.vx < 0 ? -0.12f : 0.12f);
                f.col = currentEraDef().vinyl;
                f.era = activeDisc.era;
                fallingPieces.push_back (f);
                endRun();
                return;
            }

            // Slice: new disc occupies [overlapL..overlapR]. Anything of the
            // incoming disc outside becomes a falling piece.
            const float newHalf = (overlapR - overlapL) * 0.5f;
            const float newCx   = (overlapR + overlapL) * 0.5f;

            if (newHalf < MIN_DISC_HALF)
            {
                FallingPiece f;
                f.x = activeDisc.cx; f.y = activeDisc.cy;
                f.w = activeDisc.halfW * 2.0f; f.h = DISC_THICK;
                f.vx = 0.0f; f.vy = 0.0f;
                f.col = currentEraDef().vinyl;
                f.era = activeDisc.era;
                fallingPieces.push_back (f);
                endRun();
                return;
            }

            // Left overhang
            if (leftNew < overlapL - 0.5f)
            {
                FallingPiece f;
                const float w = overlapL - leftNew;
                f.x = leftNew + w * 0.5f;
                f.y = activeDisc.cy;
                f.w = w; f.h = DISC_THICK;
                f.vx = -1.8f; f.vy = 0.0f;
                f.rotV = -0.08f;
                f.col = currentEraDef().vinyl;
                f.era = activeDisc.era;
                fallingPieces.push_back (f);
            }
            // Right overhang
            if (rightNew > overlapR + 0.5f)
            {
                FallingPiece f;
                const float w = rightNew - overlapR;
                f.x = overlapR + w * 0.5f;
                f.y = activeDisc.cy;
                f.w = w; f.h = DISC_THICK;
                f.vx = 1.8f; f.vy = 0.0f;
                f.rotV = 0.08f;
                f.col = currentEraDef().vinyl;
                f.era = activeDisc.era;
                fallingPieces.push_back (f);
            }

            // Place new disc
            Disc placed;
            placed.cx    = newCx;
            placed.cy    = activeDisc.cy;
            placed.halfW = newHalf;
            placed.era   = activeDisc.era;
            placed.placedFlash = 1.0f;
            tower.push_back (placed);
            towerCount++;

            // Score + combo
            const bool perfect = std::abs (activeDisc.cx - top.cx) < 1.5f
                                  && std::abs (activeDisc.halfW - top.halfW) < 1.5f;
            if (perfect)
            {
                combo = juce::jmin (combo + 1, 99);
                perfectStreak++;
                save.totalPerfects++;
                // Grow the disc slightly back when perfect — rewarding flow
                placed.halfW = juce::jmin (BASE_DISC_HALF, placed.halfW + 2.0f);
                tower.back() = placed;
                shakeAmp = juce::jmax (shakeAmp, 6.0f);
                const int add = 50 + combo * 10;
                score += add;
                spawnSparks (placed.cx, placed.cy, 14, currentEraDef().labelFill);
                spawnFloater ("PERFECT  x" + juce::String (combo + 1),
                              placed.cx, placed.cy - 20.0f,
                              juce::Colour (0xFFFFEB3B), 1.3f);
            }
            else
            {
                combo = 0;
                perfectStreak = 0;
                const int add = 10;
                score += add;
                shakeAmp = juce::jmax (shakeAmp, 2.5f);
                spawnSparks (placed.cx, placed.cy, 6, currentEraDef().vinyl);
                spawnFloater ("+$" + juce::String (add),
                              placed.cx, placed.cy - 16.0f,
                              juce::Colour (0xFFFFEE58), 1.0f);
            }

            // Era advancement every 5 discs
            if (towerCount > 0 && (towerCount % 5) == 0 && currentEra < (int) getEras().size() - 1)
            {
                currentEra++;
                spawnFloater (juce::String ("ERA: ") + getEras()[(size_t) currentEra].name,
                              (float) getWidth() * 0.5f,
                              (float) getHeight() * 0.35f,
                              juce::Colour (0xFFFFEB3B), 1.6f);
                shakeAmp = juce::jmax (shakeAmp, 10.0f);
            }

            spawnActiveDisc();
        }

        void endRun()
        {
            gameOver = true;
            gameOverDelay = 0.6f; // short delay before tap-to-restart
            if (score > save.highScore)                         save.highScore = score;
            if ((int) tower.size() > save.bestTower)             save.bestTower = (int) tower.size();
            save.runsPlayed++;
            if (currentEra >= 5) save.prestige = juce::jmax (save.prestige, 1);
        }

        //======================================================================
        // Layout helpers
        //======================================================================
        void layout() {}

        float floorY() const noexcept
        {
            return (float) getHeight() * FLOOR_RATIO;
        }

        // Y of a disc at a given tower index (from the bottom up).
        float discYForIndex (int idx) const noexcept
        {
            return floorY() - (float) idx * DISC_SPACING - DISC_THICK * 0.5f;
        }

        const Era& currentEraDef() const noexcept
        {
            const int i = juce::jlimit (0, (int) getEras().size() - 1, currentEra);
            return getEras()[(size_t) i];
        }

        //======================================================================
        // Tick
        //======================================================================
        void timerCallback() override
        {
            const int64_t nowMs = juce::Time::currentTimeMillis();
            float dt = (float) (nowMs - lastTickMs) / 1000.0f;
            if (dt > 0.1f) dt = 0.1f;
            lastTickMs = nowMs;

            // Swing the active disc
            if (hasActive)
            {
                const float speed = swingSpeed * currentEraDef().swingMul;
                swingPhase += dt * speed;
                const float amp = juce::jmin (
                    (float) getWidth() * 0.42f,
                    (float) getWidth() * 0.30f + (float) towerCount * 2.0f);
                activeDisc.cx = (float) getWidth() * 0.5f
                              + std::sin (swingPhase) * amp;
                activeDisc.cy = discYForIndex ((int) tower.size());
            }

            // Camera tracks tower top
            const float targetY = juce::jmax (0.0f,
                                              discYForIndex ((int) tower.size()) - (float) getHeight() * 0.35f);
            cameraY += (targetY - cameraY) * juce::jmin (1.0f, dt * 6.0f);

            // Falling pieces physics
            for (auto& f : fallingPieces)
            {
                f.vy += 480.0f * dt;
                f.x  += f.vx  * 60.0f * dt;
                f.y  += f.vy  * dt;
                f.rot += f.rotV;
                f.life -= dt * 0.5f;
            }
            fallingPieces.erase (std::remove_if (fallingPieces.begin(), fallingPieces.end(),
                [this] (const FallingPiece& f) {
                    return f.y > (float) getHeight() + 60.0f || f.life <= 0.0f;
                }), fallingPieces.end());

            // Sparks
            for (auto& s : sparks)
            {
                s.vy += 260.0f * dt;
                s.x  += s.vx * dt;
                s.y  += s.vy * dt;
                s.life -= dt * 1.4f;
            }
            sparks.erase (std::remove_if (sparks.begin(), sparks.end(),
                [] (const Spark& s) { return s.life <= 0.0f; }), sparks.end());

            // Floaters
            for (auto& ft : floaters)
            {
                ft.y    += ft.vy * dt * 60.0f;
                ft.life -= dt / 1.3f;
            }
            floaters.erase (std::remove_if (floaters.begin(), floaters.end(),
                [] (const FloatingText& ft) { return ft.life <= 0.0f; }), floaters.end());

            // Decay placed-flash on every tower disc
            for (auto& d : tower)
                d.placedFlash = juce::jmax (0.0f, d.placedFlash - dt * 2.0f);

            // Screen shake decay
            shakeAmp = juce::jmax (0.0f, shakeAmp - dt * 28.0f);

            // Game over delay tick
            if (gameOver && gameOverDelay > 0.0f)
                gameOverDelay = juce::jmax (0.0f, gameOverDelay - dt);

            save.lastSavedMs = nowMs;
            repaint();
        }

        //======================================================================
        // Helpers
        //======================================================================
        void spawnSparks (float x, float y, int count, juce::Colour col)
        {
            juce::Random& r = rng;
            for (int i = 0; i < count; ++i)
            {
                Spark s;
                s.x = x + (r.nextFloat() - 0.5f) * 10.0f;
                s.y = y + (r.nextFloat() - 0.5f) * 4.0f;
                const float a = r.nextFloat() * juce::MathConstants<float>::twoPi;
                const float sp = 60.0f + r.nextFloat() * 120.0f;
                s.vx = std::cos (a) * sp;
                s.vy = std::sin (a) * sp - 80.0f;
                s.life = 1.0f;
                s.col = col;
                sparks.push_back (s);
            }
        }

        void spawnFloater (const juce::String& text, float x, float y,
                           juce::Colour col, float scale)
        {
            floaters.push_back ({ x, y, -1.0f, 1.0f, text, col, scale });
            if (floaters.size() > 40) floaters.erase (floaters.begin());
        }

        juce::Random rng;

        //======================================================================
        // Rendering
        //======================================================================
        void drawBackground (juce::Graphics& g)
        {
            const auto& e = currentEraDef();
            // Gradient sky tinted by era, pulses subtly with audio
            const float aud = audioRMS;
            juce::Colour top = e.bgTop.brighter (aud * 0.15f);
            juce::Colour bot = e.bgBot.brighter (aud * 0.08f);
            juce::ColourGradient bg (top, 0.0f, 0.0f, bot, 0.0f, (float) getHeight(), false);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

            // Pixel dots "stars" — deterministic; density scales with era
            juce::Random starRng (98765 + currentEra * 17);
            const int nStars = 40 + currentEra * 6;
            for (int i = 0; i < nStars; ++i)
            {
                const int sx = starRng.nextInt (getWidth());
                const int sy = starRng.nextInt ((int) (floorY()));
                const float tw = 0.4f + 0.6f * std::sin (
                    (float) juce::Time::currentTimeMillis() / 500.0f + (float) i);
                g.setColour (e.labelFill.withAlpha (0.25f * tw));
                g.fillRect ((float) sx, (float) sy, 1.0f, 1.0f);
            }

            // Gold border pulse w/ audio
            g.setColour (e.labelFill.withAlpha (0.3f + aud * 0.5f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.2f);
        }

        void drawTowerAndFloor (juce::Graphics& g)
        {
            // Screen shake offset
            juce::Random sr (1);
            const float sx = (shakeAmp > 0.01f) ? (sr.nextFloat() - 0.5f) * shakeAmp : 0.0f;
            const float sy = (shakeAmp > 0.01f) ? (sr.nextFloat() - 0.5f) * shakeAmp : 0.0f;

            // Floor strip — dark stage edge
            const float fy = floorY() + cameraY + sy;
            g.setColour (juce::Colour (0xFF05070A));
            g.fillRect (0.0f, fy, (float) getWidth(), (float) getHeight() - fy);
            g.setColour (juce::Colour (0xFFFFEB3B).withAlpha (0.6f));
            g.fillRect (0.0f, fy, (float) getWidth(), 2.0f);

            // Draw each tower disc (bottom up)
            for (size_t i = 0; i < tower.size(); ++i)
                drawDisc (g, tower[i], sx, sy + cameraY, false);
        }

        void drawActiveDisc (juce::Graphics& g)
        {
            if (! hasActive) return;
            // Screen shake doesn't affect the active disc — feels floaty otherwise
            drawDisc (g, activeDisc, 0.0f, cameraY, true);

            // Drop indicator (vertical dashed line showing where it will land)
            const float x = activeDisc.cx;
            const float y0 = activeDisc.cy + DISC_THICK * 0.5f + cameraY;
            const float y1 = (tower.empty() ? floorY() : tower.back().cy - DISC_THICK * 0.5f) + cameraY;
            g.setColour (currentEraDef().labelFill.withAlpha (0.25f));
            for (float yy = y0; yy < y1; yy += 4.0f)
                g.fillRect (x - 0.5f, yy, 1.0f, 2.0f);
        }

        void drawDisc (juce::Graphics& g, const Disc& d, float shakeX, float cameraOffset, bool active)
        {
            const auto& era = getEras()[(size_t) juce::jlimit (0, (int) getEras().size() - 1, d.era)];

            const float cx = d.cx + shakeX;
            const float cy = d.cy + cameraOffset;
            const float hw = d.halfW;
            const float ht = DISC_THICK * 0.5f;

            // Shadow
            if (! active)
            {
                g.setColour (juce::Colour (0xFF000000).withAlpha (0.4f));
                g.fillRoundedRectangle (cx - hw + 1.0f, cy - ht + 2.0f,
                                         hw * 2.0f, ht * 2.0f, 3.0f);
            }

            // Outer vinyl body
            juce::Colour base = era.vinyl;
            if (d.placedFlash > 0.01f)
                base = base.interpolatedWith (era.labelFill, d.placedFlash * 0.8f);
            g.setColour (base);
            g.fillRoundedRectangle (cx - hw, cy - ht, hw * 2.0f, ht * 2.0f, 3.0f);

            // Groove lines (concentric rings on a side-profile)
            g.setColour (base.brighter (0.35f).withAlpha (0.7f));
            for (int i = 1; i < 4; ++i)
            {
                const float rr = hw - (float) i * 6.0f;
                if (rr < 2.0f) break;
                g.fillRect (cx - rr, cy - 0.5f, rr * 2.0f, 1.0f);
            }

            // Centre label
            const float labelHalf = juce::jmin (hw * 0.35f, 12.0f);
            if (labelHalf > 2.0f)
            {
                juce::Colour lc = era.labelFill;
                if (active) lc = lc.brighter (audioRMS * 0.5f);
                g.setColour (lc);
                g.fillRoundedRectangle (cx - labelHalf, cy - ht + 1.0f,
                                         labelHalf * 2.0f, (ht - 1.0f) * 2.0f, 1.5f);
                // Centre spindle
                g.setColour (era.labelText);
                g.fillRect (cx - 0.5f, cy - 0.5f, 1.0f, 1.0f);
            }

            // Top highlight (gives it roundness visually)
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRoundedRectangle (cx - hw + 2.0f, cy - ht, hw * 2.0f - 4.0f, 1.5f, 1.0f);

            // Perfect flash overlay
            if (d.placedFlash > 0.05f)
            {
                g.setColour (era.labelFill.withAlpha (d.placedFlash * 0.5f));
                g.drawRoundedRectangle (cx - hw - 1.0f, cy - ht - 1.0f,
                                        hw * 2.0f + 2.0f, ht * 2.0f + 2.0f, 3.0f, 1.2f);
            }
        }

        void drawFallingPieces (juce::Graphics& g)
        {
            for (const auto& f : fallingPieces)
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.addTransform (juce::AffineTransform::rotation (f.rot, f.x, f.y));
                g.setColour (f.col);
                g.fillRoundedRectangle (f.x - f.w * 0.5f, f.y - f.h * 0.5f,
                                         f.w, f.h, 2.0f);
                g.setColour (f.col.brighter (0.3f).withAlpha (0.6f));
                g.fillRect (f.x - f.w * 0.5f + 1.0f, f.y - f.h * 0.5f, f.w - 2.0f, 1.0f);
            }
        }

        void drawSparks (juce::Graphics& g)
        {
            for (const auto& s : sparks)
            {
                g.setColour (s.col.withAlpha (juce::jlimit (0.0f, 1.0f, s.life)));
                g.fillRect (s.x - 1.0f, s.y - 1.0f, 2.0f, 2.0f);
            }
        }

        void drawFloaters (juce::Graphics& g)
        {
            for (const auto& f : floaters)
            {
                g.setColour (f.colour.withAlpha (juce::jlimit (0.0f, 1.0f, f.life)));
                const float fz = 9.0f * f.scale;
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                        fz, juce::Font::bold));
                g.drawText (f.text,
                            juce::Rectangle<int> ((int) f.x - 60,
                                                   (int) f.y - 6, 120, 14),
                            juce::Justification::centred, false);
            }
        }

        void drawHUD (juce::Graphics& g)
        {
            const auto& e = currentEraDef();
            // Top-left: score
            g.setColour (e.labelFill);
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                    11.0f, juce::Font::bold));
            g.drawText (juce::String ("$") + juce::String (score),
                        juce::Rectangle<int> (6, 2, 120, 16),
                        juce::Justification::centredLeft);

            // Top-centre: era name (small)
            g.setColour (e.labelFill.withAlpha (0.7f));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                    8.0f, juce::Font::bold));
            g.drawFittedText (juce::String ("ERA ") + juce::String (currentEra + 1)
                              + " / " + juce::String ((int) getEras().size())
                              + "  " + e.name,
                              juce::Rectangle<int> (120, 2, getWidth() - 240, 12),
                              juce::Justification::centred, 1);

            // Top-right: combo (big if >0) and high score
            if (combo > 0)
            {
                const float cg = 0.5f + 0.5f * std::sin (
                    (float) juce::Time::currentTimeMillis() / 120.0f);
                g.setColour (juce::Colour (0xFFFFEB3B).interpolatedWith (
                    juce::Colour (0xFFFF5252), cg));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                        14.0f, juce::Font::bold));
                g.drawText (juce::String ("x") + juce::String (combo + 1),
                            juce::Rectangle<int> (getWidth() - 100, 2, 80, 16),
                            juce::Justification::centredRight);
            }
            else
            {
                g.setColour (e.labelFill.withAlpha (0.6f));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                        8.0f, juce::Font::plain));
                g.drawText (juce::String ("BEST $") + juce::String (save.highScore),
                            juce::Rectangle<int> (getWidth() - 140, 4, 120, 12),
                            juce::Justification::centredRight);
            }

            // Bottom HUD strip: tower height count
            g.setColour (e.labelFill.withAlpha (0.5f));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                    8.0f, juce::Font::bold));
            g.drawFittedText (juce::String ("HEIGHT ") + juce::String ((int) tower.size() - 1)
                              + "   BEST " + juce::String (save.bestTower),
                              juce::Rectangle<int> (0, getHeight() - 14, getWidth(), 12),
                              juce::Justification::centred, 1);
        }

        void drawGameOver (juce::Graphics& g)
        {
            g.setColour (juce::Colours::black.withAlpha (0.72f));
            g.fillRect (getLocalBounds());

            g.setColour (juce::Colour (0xFFFF5252));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                    18.0f, juce::Font::bold));
            g.drawFittedText ("WACK.",
                              getLocalBounds().withY (getHeight() / 2 - 40).withHeight (24),
                              juce::Justification::centred, 1);

            g.setColour (juce::Colour (0xFFFFEB3B));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                    12.0f, juce::Font::bold));
            g.drawFittedText (juce::String ("$") + juce::String (score)
                              + "   TOWER " + juce::String ((int) tower.size() - 1),
                              getLocalBounds().withY (getHeight() / 2 - 14).withHeight (14),
                              juce::Justification::centred, 1);

            if (score >= save.highScore && score > 0)
            {
                const float pulse = 0.5f + 0.5f * std::sin (
                    (float) juce::Time::currentTimeMillis() / 180.0f);
                g.setColour (juce::Colour (0xFFC6FF00).withAlpha (0.6f + 0.4f * pulse));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                        10.0f, juce::Font::bold));
                g.drawFittedText ("NEW HIGH SCORE",
                                  getLocalBounds().withY (getHeight() / 2 + 4).withHeight (14),
                                  juce::Justification::centred, 1);
            }

            if (gameOverDelay <= 0.0f)
            {
                g.setColour (juce::Colour (0xFFCDDC39).withAlpha (
                    0.6f + 0.4f * std::sin ((float) juce::Time::currentTimeMillis() / 250.0f)));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                        9.0f, juce::Font::plain));
                g.drawFittedText ("tap to stack again",
                                  getLocalBounds().withY (getHeight() / 2 + 26).withHeight (14),
                                  juce::Justification::centred, 1);
            }
        }

        void drawIntro (juce::Graphics& g)
        {
            g.setColour (juce::Colours::black.withAlpha (0.68f));
            g.fillRect (getLocalBounds());

            // Pulse title
            const float pulse = 0.5f + 0.5f * std::sin (
                (float) juce::Time::currentTimeMillis() / 300.0f);
            for (int halo = 5; halo > 0; --halo)
            {
                g.setColour (juce::Colour (0xFFFFEB3B).withAlpha (0.05f * pulse));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                        20.0f + (float) halo * 0.5f, juce::Font::bold));
                g.drawFittedText ("STACK THE PLAQUES",
                                  getLocalBounds().withY (getHeight() / 2 - 42).withHeight (26),
                                  juce::Justification::centred, 1);
            }
            g.setColour (juce::Colour (0xFFFFEB3B));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                    20.0f, juce::Font::bold));
            g.drawFittedText ("STACK THE PLAQUES",
                              getLocalBounds().withY (getHeight() / 2 - 40).withHeight (24),
                              juce::Justification::centred, 1);

            // Desc
            g.setColour (juce::Colour (0xFFCDDC39));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                    10.0f, juce::Font::bold));
            g.drawFittedText ("tap to drop the vinyl  .  stack to keep it wide  .  slip = it snaps off",
                              getLocalBounds().withY (getHeight() / 2 - 4).withHeight (16),
                              juce::Justification::centred, 1);

            // CTA
            g.setColour (juce::Colour (0xFFC6FF00).withAlpha (
                0.6f + 0.4f * std::sin ((float) juce::Time::currentTimeMillis() / 180.0f)));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                    13.0f, juce::Font::bold));
            g.drawFittedText ("TAP TO START",
                              getLocalBounds().withY (getHeight() / 2 + 24).withHeight (18),
                              juce::Justification::centred, 1);

            // Best line
            if (save.highScore > 0)
            {
                g.setColour (juce::Colour (0xFFFFEB3B).withAlpha (0.5f));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                        8.0f, juce::Font::plain));
                g.drawFittedText (juce::String ("BEST $") + juce::String (save.highScore)
                                  + "   BEST TOWER " + juce::String (save.bestTower),
                                  getLocalBounds().withY (getHeight() - 14).withHeight (12),
                                  juce::Justification::centred, 1);
            }
        }

        void drawDragHandle (juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setColour (juce::Colour (0xFF0A1000).withAlpha (0.7f));
            g.fillRoundedRectangle (r.toFloat(), 2.0f);
            g.setColour (juce::Colour (0xFFCDDC39).withAlpha (0.9f));
            // Resize corner diagonal lines
            for (int i = 0; i < 3; ++i)
            {
                const float off = (float) i * 3.0f + 2.0f;
                g.drawLine ((float) r.getRight() - off,
                            (float) r.getBottom() - 2.0f,
                            (float) r.getRight() - 2.0f,
                            (float) r.getBottom() - off, 1.0f);
            }
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TycoonGame)
    };
} // namespace th::game
