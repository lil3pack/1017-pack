#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <array>
#include <algorithm>
#include <functional>
#include <cmath>
#include "LookAndFeel1017.h"

// =============================================================================
// 1017 EMPIRE MOGUL — v6 MASTERCLASS TYCOON
//
// A deep, replayable, run-based top-down tycoon in the spirit of Clash of
// Clans × Dead Cells × Game Dev Tycoon. Embedded inside the 3PACK CLIP
// plugin, in a 470×200 area.
//
// CORE LOOP (one RUN)
//   1. Main menu: press "GO TRAPPIN'" to start
//   2. STARTER PICK — choose one of 3 randomly-rolled starter loadouts
//      (HUSTLER / PRODUCER / MOGUL, each changes the playstyle)
//   3. PLAYING — on a 10×5 grid, place buildings, earn cash, level up,
//      survive opp raids, react to random choice events
//   4. Run ends when empire HP = 0 (destroyed) OR you click RETIRE
//      (bank your haul as PRESTIGE POINTS — meta currency)
//
// META PROGRESSION (persists across runs)
//   - UNLOCKED BUILDINGS (bitmask): complete achievements to unlock more
//     buildings in future runs
//   - PLUGIN SKINS (bitmask): earn by hitting prestige thresholds; they
//     appear in the plugin UI (future hook via `skinMask` field)
//   - LEGEND POINTS: perm +income% stacked
//   - deepestZone: compat mirror for processor's ICE-character gate
//
// BUILDINGS (13 + Empty)
//   Each has: base cost, passive income, HP, category.
//   - TRAP HOUSE  (street income, fragile, high %)
//   - STUDIO      (beats/sec, solid passive)
//   - BOOTH       (+20% to neighbouring studios)
//   - LABEL HQ    (streaming royalties, scales per-level)
//   - MERCH SHOP  (physical sales, burst income)
//   - RADIO       (multiplies ALL income nearby)
//   - VENUE       (event income, benefits from near-Studio synergy)
//   - BARBER      (+hype, which boosts run-wide income)
//   - MANSION     (prestige gate, perm boost)
//   - ARMORY      (reduce raid damage)
//   - TURRET      (auto-kill opp waves, destroys them before they land)
//   - WALL        (absorbs hits, cheap HP)
//   - CRYPTO FARM (high passive income but drains power = extra upkeep)
//
// OPP RAIDS (procedural danger)
//   Every 60–90 seconds a raid is announced with a "WARNING !" banner,
//   target direction, and power. 5 seconds later the opps walk onto your
//   map from the announced edge, attacking the nearest income building
//   on their path. Turrets/Walls/Armory buildings reduce their progress.
//   If your total empire HP drops to 0, run ends.
//
// REPLAYABILITY — every run is different:
//   - Starter: 3 random options from a pool of 6
//   - Run modifiers (seeded): "RAIN SEASON" (-20% income +cheap build),
//     "TAX AUDIT" (-10% banked), "VIRAL MOMENT" (+50% income first 60s),
//     "HEAT" (double raid frequency), etc.
//   - Building "featured" bonuses per run (one building type gets +50%
//     income at random; encourages trying different builds)
//   - Event shuffles (random subset of 12 events)
//
// PERMANENT API (editor/processor-facing, UNCHANGED)
//   class TycoonGame : Component
//     void paint/resized/mouseDown/mouseDrag/mouseUp
//     void setAudioActivity(float)
//     ValueTree getSaveState() const
//     void loadSaveState(const ValueTree&)
//     std::function<void(MouseEvent)> onDragHandleDown/Drag/Up
//
// Save keys preserved for processor compat:
//     count_3  = 1 when LABEL HQ was ever built (gates ICE character)
//     prestige = prestige count (gates plugin flourish)
// =============================================================================
namespace th::game
{
    //==========================================================================
    // Constants
    //==========================================================================
    constexpr int   GRID_W = 10;
    constexpr int   GRID_H = 5;
    constexpr float TILE_W = 42.0f;
    constexpr float TILE_H = 26.0f;
    constexpr int   HUD_TOP_H    = 20;
    constexpr int   HUD_BOTTOM_H = 20;

    //==========================================================================
    // Building types
    //==========================================================================
    enum class BType : int
    {
        Empty = 0,
        TrapHouse,
        Studio,
        Booth,
        Label,
        Merch,
        Radio,
        Venue,
        Barber,
        Mansion,
        Armory,
        Turret,
        Wall,
        Crypto,
        NUM_TYPES
    };

    struct BuildingDef
    {
        const char*  name;
        const char*  shortName;
        int          baseCost;
        int          baseIncome;     // per sec at lv 1
        int          hp;
        int          category;       // 0=income, 1=defense, 2=boost
        int          unlockThreshold; // totalEarned required to unlock building
        juce::Colour colour;
    };

    inline const std::array<BuildingDef, (size_t) BType::NUM_TYPES>& getBuildings()
    {
        static const std::array<BuildingDef, (size_t) BType::NUM_TYPES> B = {{
            /* Empty      */ { "Empty",      "---",    0,     0,   0, 0,        0, juce::Colour (0xFF1A2500) },
            /* TrapHouse  */ { "TRAP HOUSE", "TRAP",   50,    2,  15, 0,        0, juce::Colour (0xFF8D6E63) },
            /* Studio     */ { "STUDIO",     "STUD",   250,   8,  25, 0,       50, juce::Colour (0xFF5E35B1) },
            /* Booth      */ { "BOOTH",      "BTH",    600,   2,  20, 2,      300, juce::Colour (0xFFEF6C00) },
            /* Label      */ { "LABEL HQ",   "LBL",    2000,  40, 80, 0,     1500, juce::Colour (0xFF1E88E5) },
            /* Merch      */ { "MERCH",      "MRC",    900,   20, 30, 0,     1000, juce::Colour (0xFFD81B60) },
            /* Radio      */ { "RADIO",      "RAD",    1500,  8,  45, 2,     2500, juce::Colour (0xFF43A047) },
            /* Venue      */ { "VENUE",      "VEN",    4000,  85, 60, 0,     8000, juce::Colour (0xFFC2185B) },
            /* Barber     */ { "BARBER",     "BRB",    500,   0,  15, 2,      250, juce::Colour (0xFFFB8C00) },
            /* Mansion    */ { "MANSION",    "MAN",    25000, 350,200, 0,   50000, juce::Colour (0xFFFFD700) },
            /* Armory     */ { "ARMORY",     "ARM",    1200,  0,  70, 1,     1200, juce::Colour (0xFF455A64) },
            /* Turret     */ { "TURRET",     "TUR",    700,   0,  40, 1,      600, juce::Colour (0xFF37474F) },
            /* Wall       */ { "WALL",       "WAL",    150,   0,  60, 1,      100, juce::Colour (0xFF616161) },
            /* Crypto     */ { "CRYPTO FARM","CRY",    6000,  180,25, 0,    15000, juce::Colour (0xFF00ACC1) },
        }};
        return B;
    }

    //==========================================================================
    // Starter loadouts (Dead-Cells-style: pick 1 of 3 at the start of a run)
    //==========================================================================
    struct StarterDef
    {
        const char* name;
        const char* desc;
        int         startMoney;
        float       incomeMul;
        BType       gift;         // free building placed at run start
        int         giftLevel;    // its starting level
    };

    inline const std::array<StarterDef, 6>& getStarters()
    {
        static const std::array<StarterDef, 6> S = {{
            { "HUSTLER",   "2x TRAP HOUSE, +40% street",  100,  1.00f, BType::TrapHouse, 2 },
            { "PRODUCER",  "Free STUDIO lv3, +beats",     250,  1.10f, BType::Studio,    3 },
            { "MOGUL",     "$5K start, unlock LABEL",    5000,  1.00f, BType::Empty,     0 },
            { "VIRAL",     "+100% for first 60s",          500,  2.00f, BType::Empty,     0 },
            { "DEFENDER",  "Free WALLx4, TURRETx1",        300,  0.90f, BType::Turret,    2 },
            { "WHALE",     "$2K, +LEGEND, -raids",        2000,  0.85f, BType::Mansion,   1 },
        }};
        return S;
    }

    //==========================================================================
    // Random choice events
    //==========================================================================
    struct EventDef
    {
        const char* title;
        const char* choiceA;
        const char* choiceB;
        int         cashDeltaA;      // amount of money change on A
        int         cashDeltaB;
        float       incomeMulDeltaA; // permanent adjustment to run income mul
        float       incomeMulDeltaB;
    };

    inline const std::array<EventDef, 10>& getEvents()
    {
        static const std::array<EventDef, 10> E = {{
            { "Major label offer",      "Sign",          "Stay indie",   5000, 0,   -0.10f, 0.00f },
            { "Opp beefing on IG",      "Respond",       "Ignore",      -500,  0,    0.10f, 0.00f },
            { "Tour invite",            "Book",          "Pass",         0,    200,  0.20f, 0.00f },
            { "Crypto tip",             "Invest 1K",     "Pass",        -1000,0,    0.00f, 0.00f },
            { "Manager wants cut",      "Give 10%",      "Fire",         -500, 0,    0.05f, -0.05f },
            { "Viral TikTok",           "Ride the wave", "Cash out",     0,    2000, 0.30f, 0.00f },
            { "Ghost writer offer",     "Pay $2K",       "Decline",     -2000, 0,    0.15f, 0.00f },
            { "Radio payola",           "Pay $1K",       "Decline",     -1000, 0,    0.10f, 0.00f },
            { "Studio fire",            "Rebuild",       "Claim insur.", -1500, 3000, 0.00f, -0.05f },
            { "Award nomination",       "Campaign",      "Organic",     -500,  0,    0.12f, 0.05f },
        }};
        return E;
    }

    //==========================================================================
    // Per-run modifiers
    //==========================================================================
    struct RunMod
    {
        const char* name;
        const char* desc;
        float       incomeMul;
        float       buildCostMul;
        float       raidFreqMul;
    };

    inline const std::array<RunMod, 6>& getRunMods()
    {
        static const std::array<RunMod, 6> M = {{
            { "FAIR WEATHER",  "nothing to worry",         1.00f, 1.00f, 1.00f },
            { "RAIN SEASON",   "-20% income, cheap build", 0.80f, 0.75f, 1.00f },
            { "HEAT WAVE",     "+20% income, x2 raids",    1.20f, 1.00f, 2.00f },
            { "TAX AUDIT",     "-10% income always",       0.90f, 1.00f, 1.00f },
            { "VIRAL ERA",     "+50% income, x1.5 cost",   1.50f, 1.50f, 1.00f },
            { "CRUNCH TIME",   "x2 income, x3 raids",      2.00f, 1.10f, 3.00f },
        }};
        return M;
    }

    //==========================================================================
    // Runtime data
    //==========================================================================
    struct Tile
    {
        BType type  { BType::Empty };
        int   level { 0 };
        int   hp    { 0 };        // current; 0 means destroyed
        bool  hazard { false };    // procedural hazard tile (cannot build)
    };

    struct Opp
    {
        float x, y;            // tile-grid coords (float for interpolation)
        float vx, vy;
        int   hp, maxHp;
        int   dmg;
        int   type;            // 0=rookie, 1=vet, 2=boss
        int   targetCol, targetRow;  // next tile to attack
        float atkCooldown;
    };

    struct RaidWarning
    {
        int edge;         // 0=top, 1=right, 2=bottom, 3=left
        int power;
        float timeToSpawn; // seconds remaining
        int waveCount;
    };

    struct FloatingText
    {
        float x, y, vy;
        float life;
        juce::String text;
        juce::Colour colour;
    };

    //==========================================================================
    // Phases
    //==========================================================================
    enum class Phase { Menu, StarterPick, Playing, Event, GameOver };

    //==========================================================================
    // Persistent save state (survives plugin close + DAW session save)
    //==========================================================================
    struct SaveState
    {
        // META (across runs)
        double  bankedAllTime { 0.0 };    // lifetime banked
        int     runsCompleted { 0 };
        int     legendPoints  { 0 };
        int     prestige      { 0 };      // gates plugin flourish
        int     unlockMask    { 0 };      // bit per BType unlocked for future runs
        int     skinMask      { 0 };      // bit per plugin skin unlocked
        int     deepestZone   { 0 };      // compat mirror (1 = ever owned LABEL HQ)
        int64_t lastSavedMs   { 0 };

        // CURRENT RUN (phase + in-progress state)
        Phase   phase         { Phase::Menu };
        double  money         { 0.0 };
        double  totalEarnedRun{ 0.0 };
        int     starterIdx    { -1 };
        int     runModIdx     {  0 };
        int     featuredBType {  1 };     // random building type getting +50%
        std::array<std::array<Tile, GRID_H>, GRID_W> grid {};
        int     runTick       { 0 };
        int     empireHp      { 100 };
        int     empireMaxHp   { 100 };
        float   hype          { 50.0f };
        // Starter rolls (3 random indices)
        std::array<int, 3> offeredStarters { { 0, 1, 2 } };

        juce::ValueTree toValueTree() const
        {
            juce::ValueTree vt ("TycoonState");
            vt.setProperty ("bankedAllTime", bankedAllTime, nullptr);
            vt.setProperty ("runsCompleted", runsCompleted, nullptr);
            vt.setProperty ("legendPoints",  legendPoints,  nullptr);
            vt.setProperty ("prestige",      prestige,      nullptr);
            vt.setProperty ("unlockMask",    unlockMask,    nullptr);
            vt.setProperty ("skinMask",      skinMask,      nullptr);
            vt.setProperty ("deepestZone",   deepestZone,   nullptr);
            vt.setProperty ("lastSavedMs",   (juce::int64) lastSavedMs, nullptr);
            // Plugin-facing compat (ICE gate checks count_3 > 0)
            vt.setProperty ("count_3", (deepestZone >= 1 ? 1 : 0), nullptr);

            vt.setProperty ("phase",         (int) phase,   nullptr);
            vt.setProperty ("money",         money,         nullptr);
            vt.setProperty ("totalEarnedRun",totalEarnedRun,nullptr);
            vt.setProperty ("starterIdx",    starterIdx,    nullptr);
            vt.setProperty ("runModIdx",     runModIdx,     nullptr);
            vt.setProperty ("featuredBType", featuredBType, nullptr);
            vt.setProperty ("runTick",       runTick,       nullptr);
            vt.setProperty ("empireHp",      empireHp,      nullptr);
            vt.setProperty ("empireMaxHp",   empireMaxHp,   nullptr);
            vt.setProperty ("hype",          hype,          nullptr);

            // Grid serialised as a flat string "type:level:hp:hazard;type:..."
            juce::String g;
            for (int c = 0; c < GRID_W; ++c)
                for (int r = 0; r < GRID_H; ++r)
                {
                    const auto& t = grid[(size_t) c][(size_t) r];
                    g += juce::String ((int) t.type) + ":"
                       + juce::String (t.level) + ":"
                       + juce::String (t.hp) + ":"
                       + (t.hazard ? "1" : "0") + ";";
                }
            vt.setProperty ("grid", g, nullptr);
            return vt;
        }

        void fromValueTree (const juce::ValueTree& vt)
        {
            if (! vt.isValid()) return;
            bankedAllTime = (double) vt.getProperty ("bankedAllTime", 0.0);
            runsCompleted = (int)    vt.getProperty ("runsCompleted", 0);
            legendPoints  = (int)    vt.getProperty ("legendPoints",  0);
            prestige      = (int)    vt.getProperty ("prestige",      0);
            unlockMask    = (int)    vt.getProperty ("unlockMask",    0);
            skinMask      = (int)    vt.getProperty ("skinMask",      0);
            deepestZone   = (int)    vt.getProperty ("deepestZone",   0);
            lastSavedMs   = (int64_t) (juce::int64) vt.getProperty ("lastSavedMs", 0);

            phase         = (Phase)  (int) vt.getProperty ("phase", (int) Phase::Menu);
            money         = (double) vt.getProperty ("money",         0.0);
            totalEarnedRun= (double) vt.getProperty ("totalEarnedRun",0.0);
            starterIdx    = (int)    vt.getProperty ("starterIdx",   -1);
            runModIdx     = (int)    vt.getProperty ("runModIdx",     0);
            featuredBType = (int)    vt.getProperty ("featuredBType", 1);
            runTick       = (int)    vt.getProperty ("runTick",       0);
            empireHp      = (int)    vt.getProperty ("empireHp",    100);
            empireMaxHp   = (int)    vt.getProperty ("empireMaxHp", 100);
            hype          = (float)  (double) vt.getProperty ("hype", 50.0);

            const juce::String gstr = vt.getProperty ("grid", juce::String()).toString();
            if (gstr.isNotEmpty())
            {
                int idx = 0;
                juce::StringArray parts;
                parts.addTokens (gstr, ";", {});
                for (int c = 0; c < GRID_W; ++c)
                    for (int r = 0; r < GRID_H; ++r)
                    {
                        if (idx >= parts.size()) break;
                        juce::StringArray f;
                        f.addTokens (parts[idx++], ":", {});
                        if (f.size() < 4) continue;
                        auto& t = grid[(size_t) c][(size_t) r];
                        t.type   = (BType) f[0].getIntValue();
                        t.level  = f[1].getIntValue();
                        t.hp     = f[2].getIntValue();
                        t.hazard = (f[3].getIntValue() != 0);
                    }
            }
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

            // Default-unlocked buildings: TrapHouse, Studio, Wall, Barber
            save.unlockMask |= (1 << (int) BType::TrapHouse);
            save.unlockMask |= (1 << (int) BType::Studio);
            save.unlockMask |= (1 << (int) BType::Wall);
            save.unlockMask |= (1 << (int) BType::Barber);

            startTimerHz (30);
        }
        ~TycoonGame() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            drawBackdrop (g);

            switch (save.phase)
            {
                case Phase::Menu:        drawMenu (g);        break;
                case Phase::StarterPick: drawStarterPick (g); break;
                case Phase::Playing:     drawPlaying (g);     break;
                case Phase::Event:       drawPlaying (g); drawEventOverlay (g); break;
                case Phase::GameOver:    drawPlaying (g); drawGameOver (g);     break;
            }

            drawFloaters (g);
            drawDragHandle (g, getDragHandleRect());
        }

        void resized() override { layoutBounds(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto p = e.getPosition();

            if (getDragHandleRect().contains (p))
            {
                draggingHandle = true;
                if (onDragHandleDown) onDragHandleDown (e);
                return;
            }

            switch (save.phase)
            {
                case Phase::Menu:        clickMenu (p);        break;
                case Phase::StarterPick: clickStarterPick (p); break;
                case Phase::Playing:     clickPlaying (p);     break;
                case Phase::Event:       clickEvent (p);       break;
                case Phase::GameOver:    clickGameOver (p);    break;
            }
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

        //----- audio reactive -----
        void setAudioActivity (float rms01) noexcept
        {
            audioActivity = 0.92f * audioActivity + 0.08f * juce::jlimit (0.0f, 1.0f, rms01);
        }

        //----- save / load -----
        juce::ValueTree getSaveState() const
        {
            save.lastSavedMs = juce::Time::currentTimeMillis();
            return save.toValueTree();
        }

        void loadSaveState (const juce::ValueTree& vt)
        {
            save.fromValueTree (vt);
            // On reload while in a run, recompute derived state
            recomputeEmpireHp();
        }

        //----- drag handle -----
        juce::Rectangle<int> getDragHandleRect() const noexcept
        {
            return { getWidth() - 16, 2, 14, 14 };
        }
        std::function<void (const juce::MouseEvent&)> onDragHandleDown;
        std::function<void (const juce::MouseEvent&)> onDragHandleDrag;
        std::function<void (const juce::MouseEvent&)> onDragHandleUp;

    private:
        //======================================================================
        // State
        //======================================================================
        mutable SaveState save;

        std::vector<Opp>           opps;
        std::vector<RaidWarning>   warnings;
        std::vector<FloatingText>  floaters;
        juce::Random               rng;
        bool                       draggingHandle { false };

        int64_t lastTickMs { 0 };
        float   audioActivity { 0.0f };

        // Active event in Event phase
        int activeEventIdx { -1 };

        // Build menu (when a tile is clicked in Playing phase)
        bool  buildMenuOpen { false };
        int   buildMenuCol  { 0 };
        int   buildMenuRow  { 0 };
        int   buildMenuScroll { 0 };  // for paging through build options

        // Timing
        float secondsToNextRaid  { 45.0f };
        float secondsToNextEvent { 60.0f };

        // Layout rects
        juce::Rectangle<int> gridRect;
        juce::Rectangle<int> hudTopRect;
        juce::Rectangle<int> hudBotRect;

        //======================================================================
        // Layout
        //======================================================================
        void layoutBounds()
        {
            const auto b = getLocalBounds();
            hudTopRect = b.withHeight (HUD_TOP_H);
            hudBotRect = b.withY (b.getBottom() - HUD_BOTTOM_H).withHeight (HUD_BOTTOM_H);
            gridRect   = b.withTrimmedTop (HUD_TOP_H).withTrimmedBottom (HUD_BOTTOM_H);
        }

        juce::Point<float> tileToScreen (int c, int r) const noexcept
        {
            // Centre the grid in gridRect
            const float gx = (float) gridRect.getCentreX() - (GRID_W * TILE_W) * 0.5f;
            const float gy = (float) gridRect.getCentreY() - (GRID_H * TILE_H) * 0.5f;
            return { gx + (float) c * TILE_W, gy + (float) r * TILE_H };
        }

        bool screenToTile (juce::Point<int> p, int& cOut, int& rOut) const noexcept
        {
            const float gx = (float) gridRect.getCentreX() - (GRID_W * TILE_W) * 0.5f;
            const float gy = (float) gridRect.getCentreY() - (GRID_H * TILE_H) * 0.5f;
            const float fx = ((float) p.x - gx) / TILE_W;
            const float fy = ((float) p.y - gy) / TILE_H;
            if (fx < 0 || fy < 0 || fx >= GRID_W || fy >= GRID_H) return false;
            cOut = (int) fx;
            rOut = (int) fy;
            return true;
        }

        //======================================================================
        // Run lifecycle
        //======================================================================
        void startNewRun()
        {
            // Roll 3 random starters
            std::array<int, 6> pool { { 0, 1, 2, 3, 4, 5 } };
            for (int i = 5; i > 0; --i)
                std::swap (pool[(size_t) i], pool[(size_t) rng.nextInt (i + 1)]);
            for (int i = 0; i < 3; ++i) save.offeredStarters[(size_t) i] = pool[(size_t) i];

            save.runModIdx     = rng.nextInt (6);
            save.featuredBType = 1 + rng.nextInt ((int) BType::NUM_TYPES - 1);

            // Reset run-scoped state
            for (auto& col : save.grid)
                for (auto& t : col)
                {
                    t = Tile{};
                    // ~8% chance of a procedural hazard tile (cannot build there)
                    if (rng.nextFloat() < 0.08f) t.hazard = true;
                }
            save.money = 0.0;
            save.totalEarnedRun = 0.0;
            save.runTick = 0;
            save.empireHp = 0;
            save.empireMaxHp = 0;
            save.hype = 50.0f;
            opps.clear();
            warnings.clear();
            secondsToNextRaid  = 50.0f;
            secondsToNextEvent = 70.0f;
            save.phase = Phase::StarterPick;
        }

        void applyStarter (int idx)
        {
            if (idx < 0 || idx >= 6) return;
            const auto& s = getStarters()[(size_t) idx];
            save.starterIdx = idx;
            save.money += s.startMoney;
            // Gift building — place in first available slot
            if (s.gift != BType::Empty)
            {
                for (int c = 0; c < GRID_W && s.giftLevel > 0; ++c)
                    for (int r = 0; r < GRID_H; ++r)
                    {
                        auto& t = save.grid[(size_t) c][(size_t) r];
                        if (t.type == BType::Empty && ! t.hazard)
                        {
                            t.type  = s.gift;
                            t.level = s.giftLevel;
                            t.hp    = getBuildings()[(size_t) s.gift].hp * s.giftLevel;
                            save.empireHp    += t.hp;
                            save.empireMaxHp += t.hp;
                            goto done;
                        }
                    }
                done: ;
            }
            // DEFENDER: give 4 walls in a row and a turret
            if (idx == 4) // Defender
            {
                int placed = 0;
                for (int c = 0; c < GRID_W && placed < 4; ++c)
                {
                    auto& t = save.grid[(size_t) c][0];
                    if (t.type == BType::Empty && ! t.hazard)
                    {
                        t.type = BType::Wall;
                        t.level = 1;
                        t.hp = getBuildings()[(size_t) BType::Wall].hp;
                        save.empireHp    += t.hp;
                        save.empireMaxHp += t.hp;
                        ++placed;
                    }
                }
            }
            // MOGUL: unlock LABEL HQ permanently
            if (idx == 2)
                save.unlockMask |= (1 << (int) BType::Label);

            save.phase = Phase::Playing;
            recomputeEmpireHp();
        }

        void endRun (bool destroyed)
        {
            save.runsCompleted++;
            const int earned = (int) (save.totalEarnedRun / 1000.0);
            save.legendPoints += earned;                         // 1 legend / 1K banked this run
            save.bankedAllTime += save.totalEarnedRun;
            if (! destroyed)
                save.prestige++;
            // Unlock progression: each run unlocks a new building (up to all)
            for (int i = 1; i < (int) BType::NUM_TYPES; ++i)
                if ((save.unlockMask & (1 << i)) == 0
                    && save.bankedAllTime >= getBuildings()[(size_t) i].unlockThreshold)
                {
                    save.unlockMask |= (1 << i);
                    spawnFloater (juce::String ("UNLOCKED ")
                                  + getBuildings()[(size_t) i].name,
                                  (float) getWidth() * 0.5f, 60.0f,
                                  juce::Colour (0xFFC6FF00));
                }
            save.phase = Phase::GameOver;
        }

        //======================================================================
        // Economy
        //======================================================================
        float currentIncomeMul() const
        {
            const auto& mod = getRunMods()[(size_t) save.runModIdx];
            const float hypeMul = 1.0f + (save.hype / 100.0f) * 0.5f;   // 1.0..1.5
            const float legMul  = 1.0f + save.legendPoints * 0.02f;     // +2% per point
            float mul = mod.incomeMul * hypeMul * legMul;
            // Viral starter: first 60s x2
            if (save.starterIdx == 3 && save.runTick < 30 * 60)
                mul *= 2.0f;
            return mul;
        }

        int getBuildingCost (BType type) const
        {
            const int base = getBuildings()[(size_t) type].baseCost;
            return (int) std::round ((float) base
                                     * getRunMods()[(size_t) save.runModIdx].buildCostMul);
        }

        void tickIncome (float dt)
        {
            double earnedThisTick = 0.0;
            const auto& B = getBuildings();
            for (int c = 0; c < GRID_W; ++c)
                for (int r = 0; r < GRID_H; ++r)
                {
                    const auto& t = save.grid[(size_t) c][(size_t) r];
                    if (t.type == BType::Empty || t.hp <= 0) continue;
                    if (B[(size_t) t.type].category != 0) continue;
                    float inc = (float) (B[(size_t) t.type].baseIncome * t.level);
                    // Featured bonus
                    if ((int) t.type == save.featuredBType) inc *= 1.5f;
                    // Booth boost to neighbouring Studio
                    if (t.type == BType::Studio)
                        if (hasNeighbour (c, r, BType::Booth))
                            inc *= 1.20f;
                    // Radio boost all income nearby
                    if (hasNeighbour (c, r, BType::Radio))
                        inc *= 1.25f;
                    earnedThisTick += inc * dt * currentIncomeMul();
                }
            save.money         += earnedThisTick;
            save.totalEarnedRun += earnedThisTick;
            save.bankedAllTime += earnedThisTick;
            if (earnedThisTick > 0.0)
            {
                // Mark deepestZone compat for processor ICE gate
                // (set when the player has ever owned a Label HQ)
                for (int c = 0; c < GRID_W && save.deepestZone < 1; ++c)
                    for (int r = 0; r < GRID_H && save.deepestZone < 1; ++r)
                        if (save.grid[(size_t) c][(size_t) r].type == BType::Label)
                            save.deepestZone = 1;
            }
        }

        bool hasNeighbour (int c, int r, BType t) const
        {
            const std::array<std::pair<int,int>, 4> dirs = {{
                {-1, 0}, {1, 0}, {0, -1}, {0, 1}
            }};
            for (auto d : dirs)
            {
                const int nc = c + d.first;
                const int nr = r + d.second;
                if (nc < 0 || nr < 0 || nc >= GRID_W || nr >= GRID_H) continue;
                if (save.grid[(size_t) nc][(size_t) nr].type == t
                    && save.grid[(size_t) nc][(size_t) nr].hp > 0) return true;
            }
            return false;
        }

        void recomputeEmpireHp()
        {
            int total = 0, totalMax = 0;
            const auto& B = getBuildings();
            for (int c = 0; c < GRID_W; ++c)
                for (int r = 0; r < GRID_H; ++r)
                {
                    const auto& t = save.grid[(size_t) c][(size_t) r];
                    if (t.type == BType::Empty) continue;
                    total += t.hp;
                    totalMax += B[(size_t) t.type].hp * juce::jmax (1, t.level);
                }
            save.empireHp = total;
            save.empireMaxHp = juce::jmax (1, totalMax);
        }

        //======================================================================
        // Build / upgrade / demolish
        //======================================================================
        void tryBuild (int c, int r, BType type)
        {
            if (type == BType::Empty) return;
            auto& t = save.grid[(size_t) c][(size_t) r];
            if (t.type != BType::Empty || t.hazard) return;
            if ((save.unlockMask & (1 << (int) type)) == 0) return;
            const int cost = getBuildingCost (type);
            if ((int) save.money < cost) return;
            save.money -= cost;
            t.type  = type;
            t.level = 1;
            t.hp    = getBuildings()[(size_t) type].hp;
            recomputeEmpireHp();
            spawnFloater (juce::String ("+") + getBuildings()[(size_t) type].shortName,
                          tileToScreen (c, r).x + TILE_W * 0.5f,
                          tileToScreen (c, r).y,
                          juce::Colour (0xFFC6FF00));
            buildMenuOpen = false;
        }

        void tryUpgrade (int c, int r)
        {
            auto& t = save.grid[(size_t) c][(size_t) r];
            if (t.type == BType::Empty) return;
            const int costToUp = getBuildingCost (t.type) * (t.level + 1);
            if ((int) save.money < costToUp) return;
            save.money -= costToUp;
            t.level++;
            const int addHp = getBuildings()[(size_t) t.type].hp;
            t.hp += addHp;
            recomputeEmpireHp();
            spawnFloater (juce::String ("LV ") + juce::String (t.level),
                          tileToScreen (c, r).x + TILE_W * 0.5f,
                          tileToScreen (c, r).y,
                          juce::Colour (0xFFFFEB3B));
            buildMenuOpen = false;
        }

        void demolish (int c, int r)
        {
            auto& t = save.grid[(size_t) c][(size_t) r];
            if (t.type == BType::Empty) return;
            const int refund = getBuildingCost (t.type) / 2;
            save.money += refund;
            t = Tile{};
            recomputeEmpireHp();
            buildMenuOpen = false;
        }

        //======================================================================
        // Raids / opps
        //======================================================================
        void scheduleRaid()
        {
            RaidWarning w;
            w.edge         = rng.nextInt (4);
            w.timeToSpawn  = 5.0f;
            w.waveCount    = 3 + save.runTick / (30 * 30); // grows with time
            w.power        = w.waveCount * 20;
            warnings.push_back (w);
            spawnFloater ("RAID INCOMING",
                          (float) getWidth() * 0.5f, 44.0f,
                          juce::Colour (0xFFFF5252));
        }

        void spawnRaidOpps (const RaidWarning& w)
        {
            for (int i = 0; i < w.waveCount; ++i)
            {
                Opp o;
                switch (w.edge)
                {
                    case 0: o.x = (float) rng.nextInt (GRID_W); o.y = -0.5f;        break; // top
                    case 1: o.x = GRID_W - 0.5f; o.y = (float) rng.nextInt (GRID_H);break; // right
                    case 2: o.x = (float) rng.nextInt (GRID_W); o.y = GRID_H - 0.5f;break; // bottom
                    default:o.x = -0.5f;          o.y = (float) rng.nextInt (GRID_H);break; // left
                }
                const float r = rng.nextFloat();
                if (r < 0.07f) { o.type = 2; o.hp = 20 + save.runTick / (30 * 60); o.dmg = 12; }
                else if (r < 0.30f) { o.type = 1; o.hp = 6;  o.dmg = 5; }
                else                { o.type = 0; o.hp = 2;  o.dmg = 2; }
                o.maxHp = o.hp;
                o.vx = 0.0f; o.vy = 0.0f;
                retargetOpp (o);
                o.atkCooldown = 0.0f;
                opps.push_back (o);
            }
        }

        void retargetOpp (Opp& o)
        {
            // Pick the nearest income building (category 0) with HP > 0; if none, any
            int bestC = -1, bestR = -1;
            float bestDist = 1e30f;
            const auto& B = getBuildings();
            auto considerTile = [&] (int c, int r, bool incomeOnly)
            {
                const auto& t = save.grid[(size_t) c][(size_t) r];
                if (t.type == BType::Empty || t.hp <= 0) return;
                if (incomeOnly && B[(size_t) t.type].category != 0) return;
                const float dx = (float) c - o.x;
                const float dy = (float) r - o.y;
                const float d  = dx * dx + dy * dy;
                if (d < bestDist) { bestDist = d; bestC = c; bestR = r; }
            };
            for (int c = 0; c < GRID_W; ++c)
                for (int r = 0; r < GRID_H; ++r) considerTile (c, r, true);
            if (bestC < 0)
            {
                for (int c = 0; c < GRID_W; ++c)
                    for (int r = 0; r < GRID_H; ++r) considerTile (c, r, false);
            }
            o.targetCol = (bestC < 0) ? GRID_W / 2 : bestC;
            o.targetRow = (bestR < 0) ? GRID_H / 2 : bestR;
        }

        void advanceOpps (float dt)
        {
            for (auto& o : opps)
            {
                const float dx = (float) o.targetCol - o.x;
                const float dy = (float) o.targetRow - o.y;
                const float d  = std::sqrt (dx * dx + dy * dy);
                const float speed = (o.type == 2) ? 0.35f : (o.type == 1 ? 0.55f : 0.75f);
                if (d > 0.05f)
                {
                    o.x += dx / d * speed * dt;
                    o.y += dy / d * speed * dt;
                }
                else
                {
                    // Attack the tile
                    auto& t = save.grid[(size_t) o.targetCol][(size_t) o.targetRow];
                    o.atkCooldown -= dt;
                    if (o.atkCooldown <= 0.0f && t.hp > 0)
                    {
                        t.hp -= o.dmg;
                        if (t.hp < 0) t.hp = 0;
                        o.atkCooldown = 0.8f;
                        spawnFloater (juce::String ("-") + juce::String (o.dmg),
                                      tileToScreen (o.targetCol, o.targetRow).x + TILE_W * 0.5f,
                                      tileToScreen (o.targetCol, o.targetRow).y,
                                      juce::Colour (0xFFFF5252));
                        if (t.hp == 0)
                        {
                            spawnFloater (juce::String ("DESTROYED"),
                                          tileToScreen (o.targetCol, o.targetRow).x + TILE_W * 0.5f,
                                          tileToScreen (o.targetCol, o.targetRow).y,
                                          juce::Colour (0xFFFF1744));
                            t = Tile{};
                            retargetOpp (o);
                        }
                    }
                }
            }
            // Turret auto-fire: each turret kills nearest opp in range per 1s cooldown.
            // We simulate with a "cooldown" per turret by reusing the tile's level as
            // a free counter; cheaper than tracking extra state.
            static float turretCd = 0.0f;
            turretCd -= dt;
            if (turretCd <= 0.0f)
            {
                turretCd = 1.0f;
                for (int c = 0; c < GRID_W; ++c)
                    for (int r = 0; r < GRID_H; ++r)
                    {
                        const auto& t = save.grid[(size_t) c][(size_t) r];
                        if (t.type != BType::Turret || t.hp <= 0) continue;
                        // Shoot nearest opp within 2.5 tiles
                        int bestIdx = -1;
                        float bestD = 6.25f;
                        for (size_t i = 0; i < opps.size(); ++i)
                        {
                            const float dx = opps[i].x - (float) c;
                            const float dy = opps[i].y - (float) r;
                            const float d  = dx * dx + dy * dy;
                            if (d < bestD) { bestD = d; bestIdx = (int) i; }
                        }
                        if (bestIdx >= 0)
                        {
                            opps[(size_t) bestIdx].hp -= 4 * t.level;
                            spawnFloater (juce::String ("-") + juce::String (4 * t.level),
                                          tileToScreen (c, r).x + TILE_W * 0.5f,
                                          tileToScreen (c, r).y + TILE_H * 0.2f,
                                          juce::Colour (0xFFC6FF00));
                        }
                    }
            }
            // Cull dead opps and drop cash
            opps.erase (std::remove_if (opps.begin(), opps.end(),
                [&] (const Opp& o) {
                    if (o.hp <= 0)
                    {
                        const int drop = 30 + o.type * 80;
                        save.money += drop;
                        save.totalEarnedRun += drop;
                        return true;
                    }
                    return false;
                }), opps.end());

            recomputeEmpireHp();
            if (save.empireHp <= 0 && save.phase == Phase::Playing && save.empireMaxHp > 0)
                endRun (true);
        }

        //======================================================================
        // Events
        //======================================================================
        void triggerEvent()
        {
            activeEventIdx = rng.nextInt ((int) getEvents().size());
            save.phase = Phase::Event;
        }

        void applyEventChoice (bool choseA)
        {
            if (activeEventIdx < 0) return;
            const auto& e = getEvents()[(size_t) activeEventIdx];
            const int money = choseA ? e.cashDeltaA : e.cashDeltaB;
            save.money += (double) money;
            save.totalEarnedRun += juce::jmax (0, money);
            if (money != 0)
                spawnFloater ((money > 0 ? juce::String ("+$") : juce::String ("-$"))
                              + juce::String (std::abs (money)),
                              (float) getWidth() * 0.5f, 44.0f,
                              money > 0 ? juce::Colour (0xFFC6FF00) : juce::Colour (0xFFFF5252));
            // (incomeMulDelta would need a run-scoped modifier; for v6 we fold
            //  it into hype for visible effect.)
            save.hype = juce::jlimit (0.0f, 100.0f,
                save.hype + (choseA ? e.incomeMulDeltaA : e.incomeMulDeltaB) * 100.0f);
            activeEventIdx = -1;
            save.phase = Phase::Playing;
        }

        //======================================================================
        // Floaters
        //======================================================================
        void spawnFloater (const juce::String& text, float x, float y, juce::Colour c)
        {
            floaters.push_back ({ x, y, -1.2f, 1.0f, text, c });
            if (floaters.size() > 50) floaters.erase (floaters.begin());
        }

        //======================================================================
        // Timer
        //======================================================================
        void timerCallback() override
        {
            const int64_t nowMs = juce::Time::currentTimeMillis();
            float dt = (float) (nowMs - lastTickMs) / 1000.0f;
            if (dt > 0.2f) dt = 0.2f;
            lastTickMs = nowMs;

            if (save.phase == Phase::Playing)
            {
                save.runTick++;
                tickIncome (dt);

                // Hype decay slowly
                save.hype = juce::jmax (0.0f, save.hype - dt * 1.0f);

                // Raid timer
                secondsToNextRaid -= dt * getRunMods()[(size_t) save.runModIdx].raidFreqMul;
                if (secondsToNextRaid <= 0.0f)
                {
                    scheduleRaid();
                    secondsToNextRaid = 40.0f + (float) rng.nextInt (30);
                }

                // Event timer
                secondsToNextEvent -= dt;
                if (secondsToNextEvent <= 0.0f)
                {
                    triggerEvent();
                    secondsToNextEvent = 60.0f + (float) rng.nextInt (45);
                }

                // Advance warnings → spawn opps
                for (auto it = warnings.begin(); it != warnings.end();)
                {
                    it->timeToSpawn -= dt;
                    if (it->timeToSpawn <= 0.0f)
                    {
                        spawnRaidOpps (*it);
                        it = warnings.erase (it);
                    }
                    else ++it;
                }

                // Advance opps
                advanceOpps (dt);
            }

            // Floaters
            for (auto& f : floaters)
            {
                f.y    += f.vy;
                f.life -= dt / 1.5f;
            }
            floaters.erase (std::remove_if (floaters.begin(), floaters.end(),
                [] (const FloatingText& f) { return f.life <= 0.0f; }), floaters.end());

            save.lastSavedMs = nowMs;
            repaint();
        }

        //======================================================================
        // Click handlers
        //======================================================================
        void clickMenu (juce::Point<int> p)
        {
            const auto btn = getGoTrappinBtnRect();
            if (btn.contains (p))
                startNewRun();
        }

        void clickStarterPick (juce::Point<int> p)
        {
            // Three starter cards side by side
            const int cardW = (getWidth() - 40) / 3;
            const int cardY = 40;
            const int cardH = getHeight() - 80;
            for (int i = 0; i < 3; ++i)
            {
                const juce::Rectangle<int> r (10 + i * (cardW + 5), cardY, cardW, cardH);
                if (r.contains (p))
                {
                    applyStarter (save.offeredStarters[(size_t) i]);
                    return;
                }
            }
        }

        void clickPlaying (juce::Point<int> p)
        {
            // Retire button (bottom-right)
            if (getRetireBtnRect().contains (p))
            {
                endRun (false);
                return;
            }

            if (buildMenuOpen)
            {
                handleBuildMenuClick (p);
                return;
            }

            int c, r;
            if (screenToTile (p, c, r))
            {
                auto& t = save.grid[(size_t) c][(size_t) r];
                if (t.hazard) return;
                buildMenuOpen = true;
                buildMenuCol = c;
                buildMenuRow = r;
                buildMenuScroll = 0;
            }
        }

        void handleBuildMenuClick (juce::Point<int> p)
        {
            const auto& t = save.grid[(size_t) buildMenuCol][(size_t) buildMenuRow];
            // Menu rect: occupies bottom half of gridRect
            const auto menuR = getBuildMenuRect();
            if (! menuR.contains (p))
            {
                buildMenuOpen = false;
                return;
            }
            if (t.type == BType::Empty)
            {
                // Build options — 4 per row
                int idx = 0;
                const int perRow = 4;
                const int btnW = (menuR.getWidth() - 10) / perRow;
                const int btnH = (menuR.getHeight() - 16) / 3;
                for (int i = 1; i < (int) BType::NUM_TYPES; ++i)
                {
                    if ((save.unlockMask & (1 << i)) == 0) continue;
                    const int col = idx % perRow;
                    const int row = idx / perRow;
                    idx++;
                    const juce::Rectangle<int> r (
                        menuR.getX() + 4 + col * btnW,
                        menuR.getY() + 14 + row * btnH,
                        btnW - 4, btnH - 2);
                    if (r.contains (p))
                    {
                        tryBuild (buildMenuCol, buildMenuRow, (BType) i);
                        return;
                    }
                }
            }
            else
            {
                // Upgrade / Demolish
                const juce::Rectangle<int> upR (
                    menuR.getX() + 8, menuR.getY() + 20,
                    menuR.getWidth() / 2 - 12, menuR.getHeight() - 28);
                const juce::Rectangle<int> dmR (
                    menuR.getX() + menuR.getWidth() / 2 + 4, menuR.getY() + 20,
                    menuR.getWidth() / 2 - 12, menuR.getHeight() - 28);
                if (upR.contains (p)) tryUpgrade (buildMenuCol, buildMenuRow);
                else if (dmR.contains (p)) demolish (buildMenuCol, buildMenuRow);
            }
        }

        void clickEvent (juce::Point<int> p)
        {
            const auto btnA = getEventBtnRect (true);
            const auto btnB = getEventBtnRect (false);
            if (btnA.contains (p)) applyEventChoice (true);
            else if (btnB.contains (p)) applyEventChoice (false);
        }

        void clickGameOver (juce::Point<int> p)
        {
            // Tap anywhere to return to menu
            juce::ignoreUnused (p);
            save.phase = Phase::Menu;
        }

        //======================================================================
        // Helper rects for menu buttons
        //======================================================================
        juce::Rectangle<int> getGoTrappinBtnRect() const noexcept
        {
            const int w = 180, h = 34;
            return { getWidth() / 2 - w / 2, getHeight() / 2 - 10, w, h };
        }
        juce::Rectangle<int> getRetireBtnRect() const noexcept
        {
            return { getWidth() - 66, getHeight() - HUD_BOTTOM_H + 3, 60, 14 };
        }
        juce::Rectangle<int> getBuildMenuRect() const noexcept
        {
            // Positioned over the bottom half of the grid area
            return { gridRect.getX() + 4, gridRect.getCentreY(),
                     gridRect.getWidth() - 8, gridRect.getHeight() / 2 };
        }
        juce::Rectangle<int> getEventBtnRect (bool a) const noexcept
        {
            const int w = 100, h = 20;
            const int x = getWidth() / 2 + (a ? -w - 4 : 4);
            return { x, getHeight() / 2 + 16, w, h };
        }

        //======================================================================
        // Rendering — shared backdrop
        //======================================================================
        static juce::String formatMoney (double a)
        {
            if (a < 1000.0)       return juce::String ((int) a);
            if (a < 1000000.0)    return juce::String (a / 1000.0, 1) + "K";
            if (a < 1000000000.0) return juce::String (a / 1000000.0, 2) + "M";
            return juce::String (a / 1000000000.0, 2) + "B";
        }

        void drawBackdrop (juce::Graphics& g)
        {
            juce::ColourGradient bg (
                juce::Colour (0xFF0A1000), 0.0f, 0.0f,
                juce::Colour (0xFF1A2500), 0.0f, (float) getHeight(), false);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

            const float pulse = 0.35f + audioActivity * 0.5f;
            g.setColour (juce::Colour (0xFFC6FF00).withAlpha (pulse));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.2f);
        }

        //======================================================================
        // Rendering — Phase::Menu
        //======================================================================
        void drawMenu (juce::Graphics& g)
        {
            // Title
            g.setColour (juce::Colour (0xFFFFEB3B));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
            g.drawFittedText ("1017 EMPIRE",
                              getLocalBounds().withY (20).withHeight (20),
                              juce::Justification::centred, 1);

            g.setColour (juce::Colour (0xFFCDDC39));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain));
            g.drawFittedText ("build your empire  .  survive the opps",
                              getLocalBounds().withY (42).withHeight (12),
                              juce::Justification::centred, 1);

            // GO TRAPPIN' button
            const auto btn = getGoTrappinBtnRect();
            g.setColour (juce::Colour (0xFFC6FF00));
            g.fillRoundedRectangle (btn.toFloat(), 6.0f);
            g.setColour (juce::Colour (0xFF0A1000));
            g.drawRoundedRectangle (btn.toFloat(), 6.0f, 2.0f);
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::bold));
            g.drawFittedText ("GO TRAPPIN'", btn, juce::Justification::centred, 1);

            // Stats
            g.setColour (juce::Colour (0xFFF1F8E9));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
            const auto statsR = getLocalBounds().withY (btn.getBottom() + 12).withHeight (32);
            g.drawFittedText (juce::String ("RUNS ") + juce::String (save.runsCompleted)
                              + "  .  BANKED $" + formatMoney (save.bankedAllTime)
                              + "  .  LEGEND " + juce::String (save.legendPoints)
                              + "  .  PRESTIGE " + juce::String (save.prestige),
                              statsR, juce::Justification::centred, 2);

            // Unlocked building count
            int unlocked = 0;
            for (int i = 1; i < (int) BType::NUM_TYPES; ++i)
                if (save.unlockMask & (1 << i)) ++unlocked;
            g.setColour (juce::Colour (0xFFFFEB3B).withAlpha (0.8f));
            g.drawFittedText (juce::String (unlocked) + " / "
                              + juce::String ((int) BType::NUM_TYPES - 1)
                              + " BUILDINGS UNLOCKED",
                              getLocalBounds().withY (getHeight() - 14).withHeight (12),
                              juce::Justification::centred, 1);
        }

        //======================================================================
        // Rendering — Phase::StarterPick
        //======================================================================
        void drawStarterPick (juce::Graphics& g)
        {
            // Title
            g.setColour (juce::Colour (0xFFFFEB3B));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));
            g.drawFittedText ("CHOOSE YOUR HUSTLE",
                              getLocalBounds().withY (6).withHeight (14),
                              juce::Justification::centred, 1);

            // Run modifier banner
            const auto& m = getRunMods()[(size_t) save.runModIdx];
            g.setColour (juce::Colour (0xFFFF5252));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
            g.drawFittedText (juce::String (m.name) + "  (" + m.desc + ")",
                              getLocalBounds().withY (22).withHeight (12),
                              juce::Justification::centred, 1);

            // 3 starter cards
            const int cardW = (getWidth() - 40) / 3;
            const int cardY = 40;
            const int cardH = getHeight() - 80;
            for (int i = 0; i < 3; ++i)
            {
                const juce::Rectangle<int> r (10 + i * (cardW + 5), cardY, cardW, cardH);
                const auto& s = getStarters()[(size_t) save.offeredStarters[(size_t) i]];

                g.setColour (juce::Colour (0xFF05080A));
                g.fillRoundedRectangle (r.toFloat(), 4.0f);
                g.setColour (juce::Colour (0xFFCDDC39).withAlpha (0.6f));
                g.drawRoundedRectangle (r.toFloat(), 4.0f, 1.5f);

                g.setColour (juce::Colour (0xFFFFEB3B));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));
                g.drawFittedText (s.name,
                                  r.withHeight (18).translated (0, 6),
                                  juce::Justification::centred, 1);

                g.setColour (juce::Colour (0xFFF1F8E9));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
                g.drawFittedText (s.desc,
                                  r.reduced (6, 24),
                                  juce::Justification::centred, 3);

                g.setColour (juce::Colour (0xFFC6FF00));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
                g.drawFittedText (juce::String ("$") + juce::String (s.startMoney) + " start",
                                  r.withY (r.getBottom() - 16).withHeight (14),
                                  juce::Justification::centred, 1);
            }

            g.setColour (juce::Colour (0xFFCDDC39).withAlpha (0.6f));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
            g.drawFittedText ("tap a card to commit — starters shuffle every run",
                              getLocalBounds().withY (getHeight() - 14).withHeight (12),
                              juce::Justification::centred, 1);
        }

        //======================================================================
        // Rendering — Phase::Playing
        //======================================================================
        void drawPlaying (juce::Graphics& g)
        {
            drawGrid (g);
            drawOpps (g);
            drawWarnings (g);
            drawHUDTop (g);
            drawHUDBottom (g);
            if (buildMenuOpen) drawBuildMenu (g);
        }

        void drawGrid (juce::Graphics& g)
        {
            // Ground tint per tile (subtle variance)
            for (int c = 0; c < GRID_W; ++c)
                for (int r = 0; r < GRID_H; ++r)
                {
                    const auto p = tileToScreen (c, r);
                    const juce::Rectangle<float> tr (p.x, p.y, TILE_W - 1.0f, TILE_H - 1.0f);
                    const auto& t = save.grid[(size_t) c][(size_t) r];

                    if (t.hazard)
                    {
                        g.setColour (juce::Colour (0xFF1A0A0A));
                        g.fillRect (tr);
                        // X pattern
                        g.setColour (juce::Colour (0xFFB71C1C).withAlpha (0.4f));
                        g.drawLine (tr.getX() + 2, tr.getY() + 2,
                                     tr.getRight() - 2, tr.getBottom() - 2, 1.0f);
                        g.drawLine (tr.getRight() - 2, tr.getY() + 2,
                                     tr.getX() + 2, tr.getBottom() - 2, 1.0f);
                        continue;
                    }

                    // Base tile (slightly alternating for chessboard feel)
                    g.setColour (((c + r) % 2 == 0) ? juce::Colour (0xFF14201F)
                                                     : juce::Colour (0xFF101A19));
                    g.fillRect (tr);

                    if (t.type != BType::Empty) drawTile (g, c, r, t);
                }

            // Grid lines
            g.setColour (juce::Colour (0xFFCDDC39).withAlpha (0.12f));
            for (int c = 0; c <= GRID_W; ++c)
            {
                const float x = tileToScreen (c, 0).x;
                g.fillRect (x, (float) gridRect.getCentreY() - (GRID_H * TILE_H) * 0.5f,
                            1.0f, (float) GRID_H * TILE_H);
            }
            for (int r = 0; r <= GRID_H; ++r)
            {
                const float y = tileToScreen (0, r).y;
                g.fillRect ((float) gridRect.getCentreX() - (GRID_W * TILE_W) * 0.5f,
                            y, (float) GRID_W * TILE_W, 1.0f);
            }
        }

        void drawTile (juce::Graphics& g, int c, int r, const Tile& t)
        {
            const auto p  = tileToScreen (c, r);
            const auto& B = getBuildings()[(size_t) t.type];
            const juce::Rectangle<float> body (p.x + 2, p.y + 2, TILE_W - 4, TILE_H - 4);

            // Damage tint
            const float hpF = (B.hp > 0) ? (float) t.hp / (float) (B.hp * juce::jmax (1, t.level)) : 1.0f;
            const auto colour = B.colour.withBrightness (0.5f + 0.4f * hpF);
            g.setColour (colour);
            g.fillRoundedRectangle (body, 3.0f);

            // Inner highlight
            g.setColour (B.colour.brighter (0.3f).withAlpha (0.4f));
            g.fillRoundedRectangle (body.withHeight (body.getHeight() * 0.3f), 3.0f);

            // Outline
            g.setColour (juce::Colour (0xFF0A1000));
            g.drawRoundedRectangle (body, 3.0f, 1.0f);

            // Featured bonus glow
            if ((int) t.type == save.featuredBType)
            {
                g.setColour (juce::Colour (0xFFFFEB3B).withAlpha (0.7f));
                g.drawRoundedRectangle (body.expanded (1.0f), 3.0f, 1.2f);
            }

            // Short name text
            g.setColour (juce::Colour (0xFFF1F8E9));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
            g.drawFittedText (B.shortName,
                              body.toNearestInt().withY ((int) body.getCentreY() - 4).withHeight (8),
                              juce::Justification::centred, 1);

            // Level pips
            for (int i = 0; i < juce::jmin (t.level, 5); ++i)
            {
                g.setColour (juce::Colour (0xFFFFEB3B));
                g.fillRect (body.getX() + 2.0f + (float) i * 3.0f,
                            body.getY() + 2.0f, 2.0f, 2.0f);
            }

            // HP bar at top edge
            const float bx = body.getX();
            const float by = body.getY() - 2.0f;
            const float bw = body.getWidth();
            g.setColour (juce::Colour (0xFF0A1000));
            g.fillRect (bx, by, bw, 1.5f);
            g.setColour (juce::Colour (0xFFFF5252).interpolatedWith (juce::Colour (0xFFC6FF00), hpF));
            g.fillRect (bx, by, bw * hpF, 1.5f);
        }

        void drawOpps (juce::Graphics& g)
        {
            for (const auto& o : opps)
            {
                const auto p = tileToScreen (0, 0);
                const float sx = p.x + o.x * TILE_W + TILE_W * 0.5f;
                const float sy = p.y + o.y * TILE_H + TILE_H * 0.5f;
                const juce::Colour col = o.type == 2 ? juce::Colour (0xFF6A1B9A)
                                         : o.type == 1 ? juce::Colour (0xFFB71C1C)
                                                        : juce::Colour (0xFFE53935);
                g.setColour (col);
                g.fillEllipse (sx - 5.0f, sy - 5.0f, 10.0f, 10.0f);
                g.setColour (juce::Colour (0xFF0A1000));
                g.drawEllipse (sx - 5.0f, sy - 5.0f, 10.0f, 10.0f, 1.0f);
                // Red eyes
                g.setColour (juce::Colour (0xFFFF5252));
                g.fillRect (sx - 2.0f, sy - 1.0f, 1.0f, 1.0f);
                g.fillRect (sx + 1.0f, sy - 1.0f, 1.0f, 1.0f);
                // Crown for boss
                if (o.type == 2)
                {
                    g.setColour (juce::Colour (0xFFFFD700));
                    g.fillRect (sx - 4.0f, sy - 8.0f, 8.0f, 2.0f);
                }
                // HP bar
                const float hpF = juce::jlimit (0.0f, 1.0f, (float) o.hp / (float) o.maxHp);
                g.setColour (juce::Colour (0xFF0A1000));
                g.fillRect (sx - 5.0f, sy - 9.0f, 10.0f, 1.5f);
                g.setColour (juce::Colour (0xFFFF5252));
                g.fillRect (sx - 5.0f, sy - 9.0f, 10.0f * hpF, 1.5f);
            }
        }

        void drawWarnings (juce::Graphics& g)
        {
            for (const auto& w : warnings)
            {
                const float blink = std::sin ((float) save.runTick * 0.5f);
                if (blink < 0.0f) continue;
                juce::String edgeName;
                juce::Rectangle<int> edgeR;
                switch (w.edge)
                {
                    case 0: edgeName = "TOP";    edgeR = juce::Rectangle<int> (gridRect.getX(),    gridRect.getY(),    gridRect.getWidth(), 4); break;
                    case 1: edgeName = "RIGHT";  edgeR = juce::Rectangle<int> (gridRect.getRight() - 4, gridRect.getY(), 4, gridRect.getHeight()); break;
                    case 2: edgeName = "BOTTOM"; edgeR = juce::Rectangle<int> (gridRect.getX(),    gridRect.getBottom() - 4, gridRect.getWidth(), 4); break;
                    default:edgeName = "LEFT";   edgeR = juce::Rectangle<int> (gridRect.getX(),    gridRect.getY(),    4, gridRect.getHeight()); break;
                }
                g.setColour (juce::Colour (0xFFFF5252));
                g.fillRect (edgeR);
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
                g.drawFittedText (juce::String ("RAID ") + edgeName + " "
                                  + juce::String ((int) w.timeToSpawn) + "s",
                                  hudTopRect, juce::Justification::centred, 1);
            }
        }

        void drawHUDTop (juce::Graphics& g)
        {
            g.setColour (juce::Colour (0xFF0A1000).withAlpha (0.85f));
            g.fillRect (hudTopRect);

            auto r = hudTopRect.reduced (4, 2);
            g.setColour (juce::Colour (0xFFFFEE58));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold));
            g.drawText (juce::String ("$") + formatMoney (save.money),
                        r.withWidth (90), juce::Justification::centredLeft);

            g.setColour (juce::Colour (0xFFC6FF00));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
            g.drawText (juce::String ("RUN ")
                        + juce::String (save.runTick / 30)
                        + "s  .  DAY "
                        + juce::String (1 + save.runTick / (30 * 60)),
                        r.withX (r.getX() + 90).withWidth (110),
                        juce::Justification::centredLeft);

            // Hype bar
            const auto hR = juce::Rectangle<int> (r.getRight() - 120, r.getY() + 3, 110, 10);
            g.setColour (juce::Colour (0xFF1A2500));
            g.fillRoundedRectangle (hR.toFloat(), 2.0f);
            const float hf = save.hype / 100.0f;
            juce::ColourGradient hypeGrad (
                juce::Colour (0xFF9CCC65), (float) hR.getX(), 0.0f,
                juce::Colour (0xFFFF5252), (float) hR.getRight(), 0.0f, false);
            hypeGrad.addColour (0.5, juce::Colour (0xFFFFEB3B));
            g.setGradientFill (hypeGrad);
            g.fillRoundedRectangle (hR.toFloat().withWidth ((float) hR.getWidth() * hf), 2.0f);
            g.setColour (juce::Colour (0xFFF1F8E9).withAlpha (0.8f));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
            g.drawText ("HYPE", hR.translated (-30, 0).withWidth (26),
                        juce::Justification::centredRight);
        }

        void drawHUDBottom (juce::Graphics& g)
        {
            g.setColour (juce::Colour (0xFF0A1000).withAlpha (0.85f));
            g.fillRect (hudBotRect);

            // Empire HP bar
            const auto bar = juce::Rectangle<int> (6, hudBotRect.getY() + 4,
                                                    getWidth() - 80, 12);
            g.setColour (juce::Colour (0xFF1A2500));
            g.fillRoundedRectangle (bar.toFloat(), 2.0f);
            const float hpF = save.empireMaxHp > 0
                            ? (float) save.empireHp / (float) save.empireMaxHp : 0.0f;
            g.setColour (juce::Colour (0xFFFF5252).interpolatedWith (juce::Colour (0xFFC6FF00), hpF));
            g.fillRoundedRectangle (bar.toFloat().withWidth ((float) bar.getWidth() * hpF), 2.0f);
            g.setColour (juce::Colour (0xFFF1F8E9));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
            g.drawFittedText (juce::String ("EMPIRE  ")
                              + juce::String (save.empireHp) + " / "
                              + juce::String (save.empireMaxHp),
                              bar, juce::Justification::centred, 1);

            // Retire button
            const auto rb = getRetireBtnRect();
            g.setColour (juce::Colour (0xFFB71C1C).withAlpha (0.85f));
            g.fillRoundedRectangle (rb.toFloat(), 2.0f);
            g.setColour (juce::Colour (0xFFFFEB3B));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
            g.drawFittedText ("RETIRE", rb, juce::Justification::centred, 1);
        }

        void drawBuildMenu (juce::Graphics& g)
        {
            const auto m = getBuildMenuRect();
            g.setColour (juce::Colour (0xFF05080A).withAlpha (0.95f));
            g.fillRoundedRectangle (m.toFloat(), 4.0f);
            g.setColour (juce::Colour (0xFFC6FF00));
            g.drawRoundedRectangle (m.toFloat(), 4.0f, 1.5f);

            const auto& t = save.grid[(size_t) buildMenuCol][(size_t) buildMenuRow];
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));

            if (t.type == BType::Empty)
            {
                g.setColour (juce::Colour (0xFFFFEB3B));
                g.drawFittedText ("BUILD", m.withHeight (12).translated (0, 2),
                                  juce::Justification::centred, 1);
                // 4 cols × up to 3 rows
                const int perRow = 4;
                const int btnW = (m.getWidth() - 10) / perRow;
                const int btnH = (m.getHeight() - 16) / 3;
                int idx = 0;
                for (int i = 1; i < (int) BType::NUM_TYPES; ++i)
                {
                    if ((save.unlockMask & (1 << i)) == 0) continue;
                    const int col = idx % perRow;
                    const int row = idx / perRow;
                    idx++;
                    const juce::Rectangle<int> br (
                        m.getX() + 4 + col * btnW,
                        m.getY() + 14 + row * btnH,
                        btnW - 4, btnH - 2);
                    const auto& B = getBuildings()[(size_t) i];
                    const int cost = getBuildingCost ((BType) i);
                    const bool canAfford = (int) save.money >= cost;
                    g.setColour (canAfford ? B.colour.withAlpha (0.6f)
                                            : juce::Colour (0xFF1A2500));
                    g.fillRoundedRectangle (br.toFloat(), 2.0f);
                    g.setColour (canAfford ? juce::Colour (0xFFFFEB3B)
                                            : juce::Colour (0xFF555555));
                    g.drawRoundedRectangle (br.toFloat(), 2.0f, 1.0f);
                    g.setColour (juce::Colour (0xFFF1F8E9));
                    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
                    g.drawFittedText (B.shortName,
                                      br.withHeight (10).translated (0, 2),
                                      juce::Justification::centred, 1);
                    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 6.0f, juce::Font::plain));
                    g.setColour (canAfford ? juce::Colour (0xFFC6FF00) : juce::Colour (0xFFFF5252));
                    g.drawFittedText (juce::String ("$") + juce::String (cost),
                                      br.withHeight (8).translated (0, (float) br.getHeight() - 10),
                                      juce::Justification::centred, 1);
                }
            }
            else
            {
                const auto& B = getBuildings()[(size_t) t.type];
                g.setColour (juce::Colour (0xFFFFEB3B));
                g.drawFittedText (juce::String (B.name) + " LV" + juce::String (t.level),
                                  m.withHeight (12).translated (0, 2),
                                  juce::Justification::centred, 1);
                const juce::Rectangle<int> upR (
                    m.getX() + 8, m.getY() + 20,
                    m.getWidth() / 2 - 12, m.getHeight() - 28);
                const juce::Rectangle<int> dmR (
                    m.getX() + m.getWidth() / 2 + 4, m.getY() + 20,
                    m.getWidth() / 2 - 12, m.getHeight() - 28);
                const int upCost = getBuildingCost (t.type) * (t.level + 1);
                const int refund = getBuildingCost (t.type) / 2;
                g.setColour (juce::Colour (0xFFC6FF00).withAlpha (0.65f));
                g.fillRoundedRectangle (upR.toFloat(), 3.0f);
                g.setColour (juce::Colour (0xFF0A1000));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
                g.drawFittedText (juce::String ("UPGRADE $") + juce::String (upCost),
                                  upR, juce::Justification::centred, 1);
                g.setColour (juce::Colour (0xFFB71C1C).withAlpha (0.65f));
                g.fillRoundedRectangle (dmR.toFloat(), 3.0f);
                g.setColour (juce::Colour (0xFFFFEB3B));
                g.drawFittedText (juce::String ("DEMOLISH +$") + juce::String (refund),
                                  dmR, juce::Justification::centred, 1);
            }
        }

        //======================================================================
        // Rendering — Phase::Event overlay
        //======================================================================
        void drawEventOverlay (juce::Graphics& g)
        {
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRect (getLocalBounds());

            const auto card = getLocalBounds().withSizeKeepingCentre (300, 120);
            g.setColour (juce::Colour (0xFF0A1000));
            g.fillRoundedRectangle (card.toFloat(), 6.0f);
            g.setColour (juce::Colour (0xFFFFEB3B));
            g.drawRoundedRectangle (card.toFloat(), 6.0f, 1.5f);

            if (activeEventIdx >= 0)
            {
                const auto& e = getEvents()[(size_t) activeEventIdx];
                g.setColour (juce::Colour (0xFFFFEB3B));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold));
                g.drawFittedText (e.title,
                                  card.withHeight (20).translated (0, 6),
                                  juce::Justification::centred, 1);

                g.setColour (juce::Colour (0xFFC6FF00));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
                g.drawFittedText (juce::String (e.choiceA)
                                  + "  [" + (e.cashDeltaA >= 0 ? juce::String ("+") : juce::String ("-"))
                                  + "$" + juce::String (std::abs (e.cashDeltaA)) + "]",
                                  getEventBtnRect (true), juce::Justification::centred, 1);
                g.drawFittedText (juce::String (e.choiceB)
                                  + "  [" + (e.cashDeltaB >= 0 ? juce::String ("+") : juce::String ("-"))
                                  + "$" + juce::String (std::abs (e.cashDeltaB)) + "]",
                                  getEventBtnRect (false), juce::Justification::centred, 1);

                // Button backgrounds
                const auto btnA = getEventBtnRect (true);
                const auto btnB = getEventBtnRect (false);
                g.setColour (juce::Colour (0xFFC6FF00).withAlpha (0.2f));
                g.drawRoundedRectangle (btnA.toFloat(), 2.0f, 1.0f);
                g.drawRoundedRectangle (btnB.toFloat(), 2.0f, 1.0f);
            }
        }

        //======================================================================
        // Rendering — Phase::GameOver
        //======================================================================
        void drawGameOver (juce::Graphics& g)
        {
            g.setColour (juce::Colours::black.withAlpha (0.78f));
            g.fillRect (getLocalBounds());

            g.setColour (juce::Colour (0xFFFF5252));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::bold));
            g.drawFittedText (save.empireHp <= 0 ? "EMPIRE DESTROYED" : "RETIRED",
                              getLocalBounds().withHeight (30).translated (0, 40),
                              juce::Justification::centred, 1);

            g.setColour (juce::Colour (0xFFFFEB3B));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
            g.drawFittedText (juce::String ("EARNED $") + formatMoney (save.totalEarnedRun)
                              + "   +LEGEND " + juce::String ((int) (save.totalEarnedRun / 1000.0)),
                              getLocalBounds().withHeight (20).translated (0, 80),
                              juce::Justification::centred, 1);

            g.setColour (juce::Colour (0xFFC6FF00));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
            g.drawFittedText ("tap to return to menu",
                              getLocalBounds().withHeight (12).translated (0, 120),
                              juce::Justification::centred, 1);
        }

        //======================================================================
        // Rendering — shared
        //======================================================================
        void drawFloaters (juce::Graphics& g)
        {
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
            for (const auto& f : floaters)
            {
                g.setColour (f.colour.withAlpha (juce::jlimit (0.0f, 1.0f, f.life)));
                g.drawText (f.text,
                            juce::Rectangle<int> ((int) f.x - 40, (int) f.y, 80, 12),
                            juce::Justification::centred, false);
            }
        }

        void drawDragHandle (juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setColour (juce::Colour (0xFF0A1000).withAlpha (0.6f));
            g.fillRoundedRectangle (r.toFloat(), 2.0f);
            g.setColour (juce::Colour (0xFFCDDC39).withAlpha (0.85f));
            for (int col = 0; col < 2; ++col)
                for (int row = 0; row < 3; ++row)
                {
                    const float dx = r.getX() + 3.0f + (float) col * 5.0f;
                    const float dy = r.getY() + 3.0f + (float) row * 3.0f;
                    g.fillRect (dx, dy, 2.0f, 2.0f);
                }
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TycoonGame)
    };
} // namespace th::game
