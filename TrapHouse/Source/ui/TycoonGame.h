#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include "LookAndFeel1017.h"

// =============================================================================
// 1017 ATL EMPIRE — pixel-art Atlanta map tycoon, embedded in 3PACK CLIP.
//
// v2 — MAP-BASED REWRITE.
//
// GAMEPLAY
//   The main view is a pixel-art map of Atlanta with 6 buildings you own /
//   unlock / upgrade as you earn money:
//
//     1. TRAP HOUSE      (start)      — your origin. Click pad for instant $.
//     2. HOME STUDIO     ($500)       — records beats. Passive /sec income.
//     3. RADIO STATION   ($5K)        — viral rotation, chance spawn runners.
//     4. LABEL HQ        ($50K)       — "OPEN YOUR LABEL". Artists sign here.
//     5. ATL AIRPORT     ($500K)      — world tours. Massive /sec.
//     6. ATL MANSION     ($5M)        — prestige gate. Retire to reset.
//
//   Each building has an upgrade level (click building → +1 level, cost
//   scales). Levels change the sprite and boost income. Some actions spawn
//   NPC runners (delivery boys, fans) who walk across the map — click them
//   fast for a cash bonus before they leave the screen.
//
//   HYPE METER (top bar)
//     0–100, decays 2/sec. Any interaction (tap, upgrade, click NPC) adds
//     hype. At >50% income x1.5, >90% x2.5. Forces active play.
//
//   MISSIONS
//     Every 60–120 s a mission pops up (bottom-left card): "Drop a single in
//     90 s and earn $10K". Accept/Decline. Success → reward + hype. Fail →
//     small money loss.
//
//   SIGN ARTISTS (after LABEL HQ unlocks)
//     A named rapper NPC sprite appears walking toward the label building.
//     Tap them before they leave to "sign" them. Signed artists give
//     permanent income bonuses and count toward achievements.
//
//   DAY/NIGHT CYCLE
//     4-minute real-time cycle — background gradient shifts dawn → dusk →
//     night → back. Street lamps light up at night. Cars leave headlight
//     trails.
//
//   PRESTIGE (LEGEND tab)
//     When totalEarned ≥ $10M, the MANSION unlocks "RETIRE TO ATL MANSION".
//     Reset money + upgrades, gain +1 LEGEND POINT (perm +10% income each)
//     and unlock permanent visual flourishes.
//
//   PLUGIN INTEGRATION (unchanged API)
//     - LABEL level ≥1  → unlocks ICE character in processor
//     - prestige ≥1     → unlocks "prestige" plugin flourishes
//     - (new) 3 signed artists → unlocks auto-gain AI refinement
//
//   SAVE STATE
//     juce::ValueTree — round-trips through plugin serialization. Old v1
//     saves are forward-compatible (counts/money survive; new fields
//     default-init cleanly).
// =============================================================================
namespace th::game
{
    //==========================================================================
    // Building catalog (formerly "upgrades")
    //==========================================================================
    struct BuildingDef
    {
        const char* name;
        const char* shortName;
        int         mapX, mapY, w, h;
        double      baseCost;
        double      costMultiplier;
        double      incomePerSec;   // per-level passive income
        double      unlockThreshold;
    };

    static constexpr int NUM_BUILDINGS = 6;

    // v5.2 MONTPELLIER CITY — landmarks replace the generic ATL buildings.
    // Progression slowed vs v2: base costs ×2.5, income curves ×0.7, so
    // the user spends more time on each tier and the late-game Mansion
    // feels earned (≈an hour instead of ≈15 min of idle play).
    // Layout designed for a 470×~130 map area (top HUD takes 30, tab bar 12).
    static const std::array<BuildingDef, NUM_BUILDINGS>& getBuildings()
    {
        static const std::array<BuildingDef, NUM_BUILDINGS> B = {{
            // name              short    x    y    w   h   baseCost   mult    inc       unlock
            { "ECUSSON",         "ECUS",  30,   82, 38, 30,      25.0,  1.10,      0.7,        0.0 },
            { "HOME ARCEAUX",    "ARCX",  90,   76, 40, 36,     250.0,  1.13,      5.6,     1250.0 },
            { "RADIO ANTIGONE",  "ANTI",  150,  70, 42, 42,    2500.0,  1.16,     38.0,    12500.0 },
            { "LABEL COMEDIE",   "COMD",  215,  58, 50, 54,   25000.0,  1.19,    290.0,   125000.0 },
            { "GARE ST-ROCH",    "GARE",  290,  76, 60, 36,  250000.0,  1.22,   2240.0,  1250000.0 },
            { "CHATEAU D'O",     "CHAT",  380,  68, 70, 46, 2500000.0,  1.25,  15400.0, 12500000.0 },
        }};
        return B;
    }

    //==========================================================================
    // Named artists (appear as signing-seekers once LABEL HQ is built)
    //==========================================================================
    struct ArtistDef
    {
        const char* name;
        const char* nickname;
        double      signingFee;
        double      incomeBonus01;   // e.g. 0.05 = +5% passive income
        double      unlockStreams;   // totalEarned needed before they appear
    };

    static constexpr int NUM_ARTISTS = 8;

    // v5.2 MONTPELLIER roster — French names, fees ×2.5 (longer grind).
    static const std::array<ArtistDef, NUM_ARTISTS>& getArtists()
    {
        static const std::array<ArtistDef, NUM_ARTISTS> A = {{
            { "GRAND VELOURS",   "GVL",     12500.0, 0.04,     125000.0 },
            { "LE CHEF",         "CHF",     62500.0, 0.06,     500000.0 },
            { "ROI MTP",         "ROI",    200000.0, 0.08,    1500000.0 },
            { "DOC TRAP",        "DOC",    625000.0, 0.10,    3750000.0 },
            { "34000 BOY",       "340",   2000000.0, 0.12,   10000000.0 },
            { "PAILLADE MAFIA",  "PAI",   6250000.0, 0.15,   25000000.0 },
            { "FANTOME",         "FTM",  20000000.0, 0.18,   75000000.0 },
            { "DIAM DIMITRI",    "DIM",  62500000.0, 0.22,  250000000.0 },
        }};
        return A;
    }

    //==========================================================================
    // Missions — timed objectives that pop up occasionally
    //==========================================================================
    struct MissionDef
    {
        const char* title;
        const char* desc;
        int         durationSec;
        double      moneyTarget;
        double      rewardMul;
        double      rewardFlat;
    };

    static constexpr int NUM_MISSIONS = 5;

    static const std::array<MissionDef, NUM_MISSIONS>& getMissions()
    {
        static const std::array<MissionDef, NUM_MISSIONS> M = {{
            { "HIT 1K", "tap your way to +$1K", 60, 1000.0, 1.0, 500.0 },
            { "HIT 10K", "earn $10K in 90s", 90, 10000.0, 1.5, 5000.0 },
            { "VIRAL DROP", "ride the wave", 45, 5000.0, 2.0, 3000.0 },
            { "TOUR BUS", "fund the tour bus", 120, 50000.0, 1.2, 25000.0 },
            { "LABEL NIGHT", "label anniversary", 60, 25000.0, 1.8, 15000.0 },
        }};
        return M;
    }

    //==========================================================================
    // Achievements — one-shot milestones with perm rewards
    //==========================================================================
    struct AchievementDef
    {
        const char* name;
        const char* desc;
        int         tier; // 0=bronze,1=silver,2=gold,3=diamond
    };

    static constexpr int NUM_ACHIEVEMENTS = 12;

    static const std::array<AchievementDef, NUM_ACHIEVEMENTS>& getAchievements()
    {
        static const std::array<AchievementDef, NUM_ACHIEVEMENTS> A = {{
            { "FIRST DOLLAR",    "earn your first $",              0 },
            { "SIDE HUSTLE",     "earn $1,000 total",              0 },
            { "BANKROLL",        "earn $100,000 total",            1 },
            { "MILLIONAIRE",     "earn $1,000,000 total",          2 },
            { "BILLIONAIRE",     "earn $1,000,000,000 total",      3 },
            { "STUDIO LIFE",     "build the home studio",          0 },
            { "OWN YOUR LABEL",  "unlock Label HQ",                1 },
            { "EMPIRE",          "build all 6 locations",          2 },
            { "FIRST SIGN",      "sign your first artist",         1 },
            { "ROSTER DEEP",     "sign 4 artists",                 2 },
            { "RETIRE RICH",     "prestige once",                  3 },
            { "ATL LEGEND",      "prestige 3 times",               3 },
        }};
        return A;
    }

    //==========================================================================
    // Game state — serialised via juce::ValueTree
    //==========================================================================
    struct GameState
    {
        double  money       { 0.0 };
        double  totalEarned { 0.0 };
        int64_t lastSavedMs { 0 };

        // Legacy 5-upgrade counts still serialised under "count_0..4" for
        // back-compat. v2 maps them 1:1 to the first 5 buildings.
        std::array<int, NUM_BUILDINGS> levels { { 0, 0, 0, 0, 0, 0 } };

        // Artists (bitmask of signed indices)
        uint32_t signedArtistMask { 0 };

        // Achievements (bitmask of unlocked indices)
        uint32_t achievementMask { 0 };

        // Hype (0..100). Decays over time, boosts income.
        double hype { 0.0 };

        // Prestige
        int    prestige      { 0 };
        int    legendPoints  { 0 };

        // Event buff
        double  eventMultiplier { 1.0 };
        int64_t eventEndMs      { 0 };

        // Active mission
        int     activeMission    { -1 }; // -1 = none
        int64_t missionEndMs     { 0 };
        double  missionEarnedAtStart { 0.0 };

        // Active tab (0=MAP, 1=LABEL, 2=LEGEND)
        int activeTab { 0 };

        // Stats
        int totalTaps { 0 };

        //----- derived -----

        double getBuildingCost (int idx) const noexcept
        {
            const auto& b = getBuildings()[(size_t) idx];
            return b.baseCost * std::pow (b.costMultiplier, levels[(size_t) idx]);
        }

        double getArtistIncomeBonus() const noexcept
        {
            double bonus = 0.0;
            for (int i = 0; i < NUM_ARTISTS; ++i)
                if (signedArtistMask & (1u << i))
                    bonus += getArtists()[(size_t) i].incomeBonus01;
            return bonus;
        }

        double getHypeMultiplier() const noexcept
        {
            // 0-50 linear 1.0-1.5, 50-100 linear 1.5-2.5
            if (hype < 50.0)  return 1.0 + (hype / 50.0) * 0.5;
            return 1.5 + ((hype - 50.0) / 50.0) * 1.0;
        }

        double getLegendMultiplier() const noexcept
        {
            return 1.0 + legendPoints * 0.10;
        }

        double getIncomePerSec() const noexcept
        {
            double total = 0.0;
            for (int i = 0; i < NUM_BUILDINGS; ++i)
                total += levels[(size_t) i] * getBuildings()[(size_t) i].incomePerSec;
            return total
                   * (1.0 + getArtistIncomeBonus())
                   * getHypeMultiplier()
                   * getLegendMultiplier()
                   * eventMultiplier;
        }

        double getClickReward() const noexcept
        {
            const double base = 1.0 + levels[0] * 0.15; // MPC/TRAP scales the tap
            return base * getHypeMultiplier() * getLegendMultiplier();
        }

        bool isBuildingUnlocked (int idx) const noexcept
        {
            return totalEarned >= getBuildings()[(size_t) idx].unlockThreshold;
        }

        bool isArtistAvailable (int idx) const noexcept
        {
            if (signedArtistMask & (1u << idx)) return false;         // already signed
            if (levels[3] == 0) return false;                         // needs LABEL HQ
            return totalEarned >= getArtists()[(size_t) idx].unlockStreams;
        }

        int getSignedArtistCount() const noexcept
        {
            int n = 0;
            for (int i = 0; i < NUM_ARTISTS; ++i)
                if (signedArtistMask & (1u << i)) ++n;
            return n;
        }

        int getBuildingCount() const noexcept
        {
            int n = 0;
            for (int i = 0; i < NUM_BUILDINGS; ++i) if (levels[(size_t) i] > 0) ++n;
            return n;
        }

        bool canPrestige() const noexcept { return totalEarned >= 10000000.0; }

        //----- serialize -----
        juce::ValueTree toValueTree() const
        {
            juce::ValueTree vt ("TycoonState");
            vt.setProperty ("money",         money,         nullptr);
            vt.setProperty ("totalEarned",   totalEarned,   nullptr);
            vt.setProperty ("lastSavedMs",   (juce::int64) lastSavedMs, nullptr);
            // legacy count_0..4 map to levels[0..4]; v2 adds count_5
            for (int i = 0; i < NUM_BUILDINGS; ++i)
                vt.setProperty (juce::String ("count_") + juce::String (i),
                                levels[(size_t) i], nullptr);
            vt.setProperty ("signedArtistMask", (int) signedArtistMask, nullptr);
            vt.setProperty ("achievementMask",  (int) achievementMask,  nullptr);
            vt.setProperty ("hype",          hype,          nullptr);
            vt.setProperty ("prestige",      prestige,      nullptr);
            vt.setProperty ("legendPoints",  legendPoints,  nullptr);
            vt.setProperty ("activeTab",     activeTab,     nullptr);
            vt.setProperty ("totalTaps",     totalTaps,     nullptr);
            return vt;
        }

        void fromValueTree (const juce::ValueTree& vt)
        {
            if (! vt.isValid()) return;
            money       = (double) vt.getProperty ("money",       0.0);
            totalEarned = (double) vt.getProperty ("totalEarned", 0.0);
            lastSavedMs = (int64_t) (juce::int64) vt.getProperty ("lastSavedMs", 0);
            for (int i = 0; i < NUM_BUILDINGS; ++i)
                levels[(size_t) i] = (int) vt.getProperty (
                    juce::String ("count_") + juce::String (i), 0);
            signedArtistMask = (uint32_t) (int) vt.getProperty ("signedArtistMask", 0);
            achievementMask  = (uint32_t) (int) vt.getProperty ("achievementMask",  0);
            hype         = (double) vt.getProperty ("hype",         0.0);
            prestige     = (int)    vt.getProperty ("prestige",     0);
            legendPoints = (int)    vt.getProperty ("legendPoints", 0);
            activeTab    = (int)    vt.getProperty ("activeTab",    0);
            totalTaps    = (int)    vt.getProperty ("totalTaps",    0);
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
    // Runner NPC — walks across the map, click for bonus
    //==========================================================================
    struct Runner
    {
        enum class Kind { DeliveryBoy, Fan, Artist, CopCar };
        Kind   kind      { Kind::Fan };
        float  x         { 0.0f };
        float  y         { 0.0f };
        float  vx        { 0.0f };
        float  life      { 1.0f };   // time-left scalar 0..1
        double reward    { 0.0 };    // cash reward on click
        int    artistIdx { 0 };      // valid for Artist kind
    };

    //==========================================================================
    // Car for ambient map traffic
    //==========================================================================
    struct Car
    {
        float x             { 0.0f };
        float y             { 0.0f };
        float vx            { 0.0f };
        juce::Colour colour { juce::Colours::white };
        bool  hasHeadlights { false };
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
            cycleStartMs = lastTickMs;
            startTimerHz (30);

            // Seed a few ambient cars
            for (int i = 0; i < 3; ++i)
                spawnCar();
        }

        ~TycoonGame() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            drawBackground    (g);
            drawTopBar        (g);
            drawTabBar        (g);

            switch (state.activeTab)
            {
                case 0: drawMapTab     (g); break;
                case 1: drawLabelTab   (g); break;
                case 2: drawLegendTab  (g); break;
                default: drawMapTab    (g); break;
            }

            drawMissionCard   (g);
            drawFloatingTexts (g);
            drawOfflineBanner (g);
            drawAchievementToast (g);
        }

        void resized() override { layoutRects(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto p = e.getPosition();

            // v5.2: drag handle — grab the top-right ⋮⋮ to reposition the
            // tycoon window. The editor's callback takes over from here.
            if (getDragHandleRect().contains (p))
            {
                draggingHandle = true;
                if (onDragHandleDown) onDragHandleDown (e);
                return;
            }

            // Tabs (always on top)
            for (int i = 0; i < 3; ++i)
                if (tabRects[(size_t) i].contains (p))
                {
                    state.activeTab = i;
                    return;
                }

            // Mission card accept / decline
            if (missionCardVisible && missionAcceptBtn.contains (p))
            {
                acceptMission();
                return;
            }
            if (missionCardVisible && missionDeclineBtn.contains (p))
            {
                missionCardVisible = false;
                return;
            }

            if (state.activeTab == 0)        handleMapTabClick    (p);
            else if (state.activeTab == 1)   handleLabelTabClick  (p);
            else if (state.activeTab == 2)   handleLegendTabClick (p);
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

        //----- audio coupling -----
        void setAudioActivity (float rms01) noexcept
        {
            audioActivity = 0.92f * audioActivity + 0.08f * juce::jlimit (0.0f, 1.0f, rms01);
        }

        // v5.2: drag handle — editor installs a callback that fires when the
        // user grabs the top-right 14×14 handle so the plugin window can
        // reposition the tycoon. Returns the hit rect so the editor can
        // cross-check (avoids double event processing).
        juce::Rectangle<int> getDragHandleRect() const noexcept
        {
            // Top-right corner of the local bounds, 14×14 with 2px inset.
            return { getWidth() - 16, 2, 14, 14 };
        }
        std::function<void (const juce::MouseEvent&)> onDragHandleDown;
        std::function<void (const juce::MouseEvent&)> onDragHandleDrag;
        std::function<void (const juce::MouseEvent&)> onDragHandleUp;

        //----- save/load -----
        juce::ValueTree getSaveState() const
        {
            state.lastSavedMs = juce::Time::currentTimeMillis();
            return state.toValueTree();
        }

        void loadSaveState (const juce::ValueTree& vt)
        {
            state.fromValueTree (vt);
            // Offline progress (cap 1h)
            if (state.lastSavedMs > 0)
            {
                const int64_t nowMs = juce::Time::currentTimeMillis();
                const double elapsedS = juce::jlimit (
                    0.0, 3600.0, (double) (nowMs - state.lastSavedMs) / 1000.0);
                // Offline income ignores hype (you weren't hyping)
                const double passiveNoHype = [this]
                {
                    double t = 0.0;
                    for (int i = 0; i < NUM_BUILDINGS; ++i)
                        t += state.levels[(size_t) i] * getBuildings()[(size_t) i].incomePerSec;
                    return t * (1.0 + state.getArtistIncomeBonus())
                             * state.getLegendMultiplier();
                }();
                const double earned = passiveNoHype * elapsedS;
                if (earned > 0.0)
                {
                    state.money       += earned;
                    state.totalEarned += earned;
                    offlineBannerTimer  = 120;
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

        float   audioActivity     { 0.0f };
        float   beatPulse         { 1.0f };    // trap house pulse on click
        float   moneyFlash        { 0.0f };
        int     offlineBannerTimer  { 0 };
        double  offlineBannerAmount { 0.0 };        int64_t lastTickMs { 0 };
        int64_t cycleStartMs { 0 };
        int     framesSinceRunner { 0 };
        int     framesSinceMission { 0 };
        bool    draggingHandle { false };

        // Achievement toast (shows when a new one unlocks)
        int         toastTimer { 0 };
        juce::String toastText;

        // Mission card
        bool   missionCardVisible { false };
        int    missionCardIdx { -1 };
        juce::Rectangle<int> missionCardBounds;
        juce::Rectangle<int> missionAcceptBtn;
        juce::Rectangle<int> missionDeclineBtn;

        std::vector<FloatingText> floaters;
        std::vector<Runner>       runners;
        std::vector<Car>          cars;
        juce::Random rng;

        // Rects
        juce::Rectangle<int> topBarRect;
        std::array<juce::Rectangle<int>, 3> tabRects;
        juce::Rectangle<int> tabBarRect;
        juce::Rectangle<int> contentRect;
        juce::Rectangle<int> mapRect;
        std::array<juce::Rectangle<int>, NUM_BUILDINGS> buildingRects;
        // Label tab: clickable artist cards
        std::array<juce::Rectangle<int>, NUM_ARTISTS> artistRects;
        // Legend tab: prestige button + various
        juce::Rectangle<int> prestigeBtn;

        //======================================================================
        // Layout
        //======================================================================
        void layoutRects()
        {
            const auto b = getLocalBounds();
            topBarRect = b.withHeight (18);
            tabBarRect = b.withY (18).withHeight (12);
            contentRect = b.withTrimmedTop (30).withTrimmedBottom (4);

            // Three tabs: MAP | LABEL | LEGEND
            const int tw = tabBarRect.getWidth() / 3;
            for (int i = 0; i < 3; ++i)
                tabRects[(size_t) i] = juce::Rectangle<int> (
                    tabBarRect.getX() + i * tw + 1,
                    tabBarRect.getY() + 1,
                    tw - 2,
                    tabBarRect.getHeight() - 2);

            // Map rect: fills content area
            mapRect = contentRect;

            // Building hit rects relative to the map (map assumes 470-wide
            // canvas; we scale if the component is a different size).
            const float sx = (float) mapRect.getWidth()  / 470.0f;
            const float sy = (float) mapRect.getHeight() / 130.0f;
            for (int i = 0; i < NUM_BUILDINGS; ++i)
            {
                const auto& bd = getBuildings()[(size_t) i];
                buildingRects[(size_t) i] = juce::Rectangle<int> (
                    mapRect.getX() + (int) (bd.mapX * sx),
                    mapRect.getY() + (int) (bd.mapY * sy),
                    (int) (bd.w * sx),
                    (int) (bd.h * sy));
            }

            // Artist card grid 4x2
            const int colW = contentRect.getWidth() / 4;
            const int rowH = contentRect.getHeight() / 2;
            for (int i = 0; i < NUM_ARTISTS; ++i)
            {
                const int col = i % 4;
                const int row = i / 4;
                artistRects[(size_t) i] = juce::Rectangle<int> (
                    contentRect.getX() + col * colW + 2,
                    contentRect.getY() + row * rowH + 2,
                    colW - 4, rowH - 4);
            }

            // Prestige button (legend tab)
            prestigeBtn = juce::Rectangle<int> (
                contentRect.getCentreX() - 60,
                contentRect.getBottom() - 32,
                120, 24);

            // Mission card (bottom-left overlay, 170×48)
            missionCardBounds = juce::Rectangle<int> (
                contentRect.getX() + 4,
                contentRect.getBottom() - 52,
                180, 48);
            const auto btnRow = missionCardBounds.withTop (
                missionCardBounds.getBottom() - 14);
            missionAcceptBtn  = btnRow.withWidth (btnRow.getWidth() / 2).reduced (2);
            missionDeclineBtn = btnRow.withLeft  (btnRow.getCentreX()).reduced (2);
        }

        //======================================================================
        // Click handlers
        //======================================================================
        void handleMapTabClick (juce::Point<int> p)
        {
            // Runners first (they overlap buildings)
            for (size_t i = 0; i < runners.size(); ++i)
            {
                auto& r = runners[i];
                const juce::Rectangle<int> hitBox (
                    (int) r.x - 10, (int) r.y - 10, 20, 20);
                if (hitBox.contains (p))
                {
                    catchRunner ((int) i);
                    return;
                }
            }

            // Building clicks — each click on an unlocked building buys +1 level
            for (int i = 0; i < NUM_BUILDINGS; ++i)
            {
                if (buildingRects[(size_t) i].contains (p))
                {
                    tryUpgradeBuilding (i);
                    return;
                }
            }

            // Empty area of map tap → small hype tap
            bumpHype (2.0);
            state.totalTaps++;
        }

        void handleLabelTabClick (juce::Point<int> p)
        {
            if (state.levels[3] == 0) return; // label not built
            for (int i = 0; i < NUM_ARTISTS; ++i)
                if (artistRects[(size_t) i].contains (p))
                    trySignArtist (i);
        }

        void handleLegendTabClick (juce::Point<int> p)
        {
            if (prestigeBtn.contains (p) && state.canPrestige())
                doPrestige();
        }

        //======================================================================
        // Game actions
        //======================================================================
        void tryUpgradeBuilding (int idx)
        {
            // TRAP HOUSE (idx 0) is always "tap-to-earn" — clicking it gives
            // cash but upgrading costs money like other buildings (if affordable).
            if (idx == 0)
            {
                // Earn on tap
                const double reward = state.getClickReward()
                                    * (1.0 + audioActivity * 0.4);
                state.money       += reward;
                state.totalEarned += reward;
                moneyFlash         = 1.0f;
                beatPulse          = 1.4f;
                state.totalTaps++;
                bumpHype (6.0);
                spawnFloater (juce::String::formatted ("+$%.0f", reward),
                              (float) buildingRects[0].getCentreX(),
                              (float) buildingRects[0].getY(),
                              juce::Colour (0xFFFFEE58), 1.1f);
                checkAchievements();
                return;
            }

            // Other buildings: buy a level if unlocked + affordable
            if (! state.isBuildingUnlocked (idx))
            {
                spawnFloater ("LOCKED",
                              (float) buildingRects[(size_t) idx].getCentreX(),
                              (float) buildingRects[(size_t) idx].getY(),
                              juce::Colour (0xFFFF5252), 0.9f);
                return;
            }
            const double cost = state.getBuildingCost (idx);
            if (state.money < cost)
            {
                spawnFloater ("NEED $" + formatMoney (cost),
                              (float) buildingRects[(size_t) idx].getCentreX(),
                              (float) buildingRects[(size_t) idx].getY(),
                              juce::Colour (0xFFFF5252), 0.9f);
                return;
            }
            state.money -= cost;
            state.levels[(size_t) idx]++;
            bumpHype (15.0);
            spawnFloater (juce::String (getBuildings()[(size_t) idx].shortName) + " LV"
                          + juce::String (state.levels[(size_t) idx]),
                          (float) buildingRects[(size_t) idx].getCentreX(),
                          (float) buildingRects[(size_t) idx].getY(),
                          juce::Colours::white, 1.0f);
            checkAchievements();
        }

        void trySignArtist (int idx)
        {
            if (! state.isArtistAvailable (idx)) return;
            const double fee = getArtists()[(size_t) idx].signingFee;
            if (state.money < fee) return;
            state.money -= fee;
            state.signedArtistMask |= (1u << idx);
            bumpHype (25.0);
            spawnFloater ("SIGNED " + juce::String (getArtists()[(size_t) idx].name),
                          (float) artistRects[(size_t) idx].getCentreX(),
                          (float) artistRects[(size_t) idx].getY(),
                          juce::Colour (0xFFFFEE58), 1.2f);
            checkAchievements();
        }

        void doPrestige()
        {
            state.prestige++;
            state.legendPoints += 1; // simple for v2
            // Reset: keep artists? No, they follow you to the next empire.
            state.money        = 0.0;
            state.totalEarned  = 0.0;
            for (auto& lv : state.levels) lv = 0;
            // hype resets
            state.hype = 0.0;
            bumpHype (50.0);
            spawnFloater ("ATL LEGEND +1",
                          (float) contentRect.getCentreX(),
                          (float) contentRect.getCentreY(),
                          juce::Colour (0xFFFFEE58), 1.5f);
            checkAchievements();
        }

        void catchRunner (int i)
        {
            auto& r = runners[(size_t) i];
            if (r.kind == Runner::Kind::Artist)
            {
                // Artist runner: open their signing card (if label exists)
                if (state.levels[3] > 0 && ! (state.signedArtistMask & (1u << r.artistIdx)))
                {
                    state.activeTab = 1;
                    spawnFloater (juce::String (getArtists()[(size_t) r.artistIdx].name)
                                  + " WANTS TO SIGN",
                                  r.x, r.y, juce::Colour (0xFFFFEE58), 1.2f);
                }
            }
            else if (r.kind == Runner::Kind::CopCar)
            {
                // Cop car: costs money (bribe/lose)
                const double fee = juce::jmin (state.money * 0.05, 5000.0);
                state.money -= fee;
                spawnFloater ("-$" + formatMoney (fee),
                              r.x, r.y, juce::Colour (0xFFFF5252), 1.1f);
                bumpHype (5.0);
            }
            else
            {
                state.money       += r.reward;
                state.totalEarned += r.reward;
                moneyFlash         = 1.0f;
                bumpHype (12.0);
                spawnFloater ("+$" + formatMoney (r.reward),
                              r.x, r.y, juce::Colour (0xFFFFEE58), 1.2f);
            }
            runners.erase (runners.begin() + i);
            checkAchievements();
        }

        void bumpHype (double amount)
        {
            state.hype = juce::jlimit (0.0, 100.0, state.hype + amount);
        }

        void spawnFloater (const juce::String& text, float x, float y,
                           juce::Colour col, float scale = 1.0f)
        {
            floaters.push_back ({ x, y, -1.5f, 1.0f, text, col, scale });
            if (floaters.size() > 60) floaters.erase (floaters.begin());
        }

        //======================================================================
        // Runners / cars spawn
        //======================================================================
        void spawnRunner()
        {
            Runner r;
            r.y = (float) (mapRect.getBottom() - 14 - rng.nextInt (12));
            const bool leftToRight = rng.nextBool();
            r.x = (float) (leftToRight ? mapRect.getX() - 10 : mapRect.getRight() + 10);
            r.vx = leftToRight ? 0.9f : -0.9f;
            r.life = 1.0f;

            // Runner type is weighted by game progress
            const double t = state.totalEarned;
            const float roll = rng.nextFloat();

            if (roll < 0.10f && t > 20000.0)
            {
                r.kind = Runner::Kind::CopCar;
                r.vx  *= 2.2f; // fast
                r.reward = 0.0;
            }
            else if (roll < 0.35f && state.levels[3] > 0)
            {
                r.kind = Runner::Kind::Artist;
                // Pick an unsigned artist who meets the unlock threshold
                std::vector<int> candidates;
                for (int i = 0; i < NUM_ARTISTS; ++i)
                    if (state.isArtistAvailable (i))
                        candidates.push_back (i);
                if (candidates.empty()) return; // skip
                r.artistIdx = candidates[(size_t) rng.nextInt ((int) candidates.size())];
                r.vx *= 0.6f;
            }
            else if (roll < 0.65f)
            {
                r.kind = Runner::Kind::Fan;
                r.reward = juce::jmax (10.0, state.getIncomePerSec() * 4.0);
            }
            else
            {
                r.kind = Runner::Kind::DeliveryBoy;
                r.reward = juce::jmax (25.0, state.getIncomePerSec() * 10.0);
            }
            runners.push_back (r);
        }

        void spawnCar()
        {
            Car c;
            c.y = (float) (mapRect.getBottom() - 8 - rng.nextInt (3));
            const bool ltr = rng.nextBool();
            c.x = (float) (ltr ? mapRect.getX() - 20 : mapRect.getRight() + 20);
            c.vx = (ltr ? 1.0f : -1.0f) * (0.6f + rng.nextFloat() * 0.7f);
            const std::array<juce::Colour, 5> palette = {{
                juce::Colour (0xFFE53935),
                juce::Colour (0xFFFFC107),
                juce::Colour (0xFF424242),
                juce::Colour (0xFFF5F5F5),
                juce::Colour (0xFF1565C0),
            }};
            c.colour = palette[(size_t) rng.nextInt ((int) palette.size())];
            c.hasHeadlights = (getDayNightPhase() > 0.6f);
            cars.push_back (c);
        }

        //======================================================================
        // Missions
        //======================================================================
        void maybePushMission()
        {
            if (missionCardVisible || state.activeMission >= 0) return;
            if (framesSinceMission < 30 * 60) return; // 1 min min
            if (rng.nextFloat() > 0.02f) return;
            framesSinceMission = 0;
            missionCardIdx = rng.nextInt (NUM_MISSIONS);
            missionCardVisible = true;
        }

        void acceptMission()
        {
            if (missionCardIdx < 0) return;
            state.activeMission = missionCardIdx;
            state.missionEndMs = juce::Time::currentTimeMillis()
                                + getMissions()[(size_t) missionCardIdx].durationSec * 1000;
            state.missionEarnedAtStart = state.totalEarned;
            missionCardVisible = false;
        }

        void tickActiveMission()
        {
            if (state.activeMission < 0) return;
            const auto& m = getMissions()[(size_t) state.activeMission];
            const double earnedSinceStart = state.totalEarned - state.missionEarnedAtStart;
            const int64_t nowMs = juce::Time::currentTimeMillis();
            if (earnedSinceStart >= m.moneyTarget)
            {
                // Success
                state.money       += m.rewardFlat;
                state.totalEarned += m.rewardFlat;
                state.eventMultiplier = m.rewardMul;
                state.eventEndMs = nowMs + 20000;
                bumpHype (40.0);
                spawnFloater ("MISSION! +$" + formatMoney (m.rewardFlat),
                              (float) contentRect.getCentreX(),
                              (float) contentRect.getY() + 20.0f,
                              juce::Colour (0xFFC6FF00), 1.4f);
                state.activeMission = -1;
            }
            else if (nowMs > state.missionEndMs)
            {
                // Fail
                const double penalty = juce::jmin (state.money * 0.05, 1000.0);
                state.money -= penalty;
                spawnFloater ("MISSION FAILED -$" + formatMoney (penalty),
                              (float) contentRect.getCentreX(),
                              (float) contentRect.getY() + 20.0f,
                              juce::Colour (0xFFFF5252), 1.1f);
                state.activeMission = -1;
            }
        }

        //======================================================================
        // Achievements
        //======================================================================
        void unlockAchievement (int idx)
        {
            const uint32_t bit = (1u << idx);
            if (state.achievementMask & bit) return;
            state.achievementMask |= bit;
            toastText = juce::String ("* ") + getAchievements()[(size_t) idx].name;
            toastTimer = 120;
            // Small flat bonus per tier
            const int tier = getAchievements()[(size_t) idx].tier;
            const double bonus = 100.0 * std::pow (10.0, tier);
            state.money       += bonus;
            state.totalEarned += bonus;
            bumpHype (20.0);
        }

        void checkAchievements()
        {
            if (state.totalEarned >= 1.0)          unlockAchievement (0);
            if (state.totalEarned >= 1000.0)       unlockAchievement (1);
            if (state.totalEarned >= 100000.0)     unlockAchievement (2);
            if (state.totalEarned >= 1000000.0)    unlockAchievement (3);
            if (state.totalEarned >= 1000000000.0) unlockAchievement (4);
            if (state.levels[1] > 0)               unlockAchievement (5);
            if (state.levels[3] > 0)               unlockAchievement (6);
            if (state.getBuildingCount() >= 6)     unlockAchievement (7);
            const int na = state.getSignedArtistCount();
            if (na >= 1) unlockAchievement (8);
            if (na >= 4) unlockAchievement (9);
            if (state.prestige >= 1) unlockAchievement (10);
            if (state.prestige >= 3) unlockAchievement (11);
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

            // Passive income (capped hype stays live)
            const double earned = state.getIncomePerSec() * dtS;
            if (earned > 0.0)
            {
                state.money       += earned;
                state.totalEarned += earned;
            }

            // Hype decay
            state.hype = juce::jmax (0.0, state.hype - 3.0 * dtS);

            // Event expiration
            if (state.eventEndMs > 0 && nowMs > state.eventEndMs)
            {
                state.eventMultiplier = 1.0;
                state.eventEndMs      = 0;
            }

            // Mission tick
            tickActiveMission();
            framesSinceMission++;
            maybePushMission();

            // Runners: spawn every ~10s on average while on map tab
            framesSinceRunner++;
            if (framesSinceRunner > 30 * 6 && rng.nextFloat() < 0.02f)
            {
                spawnRunner();
                framesSinceRunner = 0;
            }
            // Advance runners
            for (auto& r : runners)
            {
                r.x += r.vx;
                r.life -= (float) dtS * 0.3f;
            }
            runners.erase (std::remove_if (runners.begin(), runners.end(),
                [this] (const Runner& r) {
                    return r.life <= 0.0f
                        || r.x < mapRect.getX() - 20
                        || r.x > mapRect.getRight() + 20;
                }), runners.end());

            // Cars (ambient): keep 3-5 on screen
            for (auto& c : cars) c.x += c.vx;
            cars.erase (std::remove_if (cars.begin(), cars.end(),
                [this] (const Car& c) {
                    return c.x < mapRect.getX() - 30 || c.x > mapRect.getRight() + 30;
                }), cars.end());
            if ((int) cars.size() < 3 && rng.nextFloat() < 0.02f)
                spawnCar();

            // Animation decays
            beatPulse  = 1.0f + (beatPulse - 1.0f) * 0.82f;
            moneyFlash *= 0.90f;
            if (offlineBannerTimer > 0) --offlineBannerTimer;
            if (toastTimer > 0) --toastTimer;

            // Floaters
            for (auto& f : floaters)
            {
                f.y   += f.vy;
                f.life -= 1.0f / 45.0f;
            }
            floaters.erase (std::remove_if (floaters.begin(), floaters.end(),
                [] (const FloatingText& f) { return f.life <= 0.0f; }), floaters.end());

            state.lastSavedMs = nowMs;
            checkAchievements();
            repaint();
        }

        //======================================================================
        // Helpers
        //======================================================================
        static juce::String formatMoney (double amount)
        {
            if (amount < 1000.0)        return juce::String ((int) amount);
            if (amount < 1000000.0)     return juce::String (amount / 1000.0, 1) + "K";
            if (amount < 1000000000.0)  return juce::String (amount / 1000000.0, 2) + "M";
            return juce::String (amount / 1000000000.0, 2) + "B";
        }

        // 0..1 cycle, 4-minute real-time period
        float getDayNightPhase() const noexcept
        {
            const double periodMs = 4.0 * 60.0 * 1000.0;
            const double t = (double) ((juce::Time::currentTimeMillis() - cycleStartMs) % (int64_t) periodMs);
            return (float) (t / periodMs);
        }

        // Returns a background sky colour based on phase (0=dawn .25=day .5=dusk .75=night)
        juce::Colour getSkyColour() const noexcept
        {
            const float p = getDayNightPhase();
            // 4 keyframes: dawn / noon / dusk / night
            const juce::Colour dawn  (0xFF2E1F4C);
            const juce::Colour noon  (0xFF0A1533);
            const juce::Colour dusk  (0xFF3B1020);
            const juce::Colour night (0xFF05070F);
            if (p < 0.25f) return dawn.interpolatedWith  (noon,  p / 0.25f);
            if (p < 0.5f)  return noon.interpolatedWith  (dusk,  (p - 0.25f) / 0.25f);
            if (p < 0.75f) return dusk.interpolatedWith  (night, (p - 0.5f) / 0.25f);
            return night.interpolatedWith (dawn, (p - 0.75f) / 0.25f);
        }

        //======================================================================
        // Rendering — BACKGROUND + HUD
        //======================================================================
        void drawBackground (juce::Graphics& g)
        {
            // Sky gradient
            const auto sky = getSkyColour();
            juce::ColourGradient bg (sky, 0.0f, 0.0f,
                                     sky.darker (0.3f),
                                     (float) getWidth(), (float) getHeight(), false);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

            // Gold border pulsing with audio
            const float pulse = 0.35f + audioActivity * 0.5f;
            g.setColour (juce::Colour (0xFFC6FF00).withAlpha (pulse));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.2f);

            // Night stars
            const float phase = getDayNightPhase();
            if (phase > 0.55f && phase < 0.95f)
            {
                const float a = juce::jmin (1.0f, (phase - 0.55f) / 0.2f)
                              * juce::jmin (1.0f, (0.95f - phase) / 0.2f);
                juce::Random starRng (12345);
                for (int i = 0; i < 30; ++i)
                {
                    const float x = (float) (starRng.nextInt (getWidth()));
                    const float y = (float) (starRng.nextInt (getHeight() / 3));
                    const float twinkle = 0.5f + 0.5f * std::sin (phase * 30.0f + (float) i);
                    g.setColour (juce::Colours::white.withAlpha (a * twinkle * 0.6f));
                    g.fillRect (x, y, 1.0f, 1.0f);
                }
            }
        }

        void drawTopBar (juce::Graphics& g)
        {
            // Money + income + hype bar
            auto r = topBarRect.reduced (3, 2);
            g.setColour (juce::Colour (0xFFFFEE58).interpolatedWith (juce::Colours::white, moneyFlash));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));
            g.drawText ("$" + formatMoney (state.money),
                        r.withWidth (r.getWidth() / 3), juce::Justification::centredLeft);

            g.setColour (juce::Colour (0xFFC6FF00));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain));
            g.drawText ("+$" + formatMoney (state.getIncomePerSec()) + "/s",
                        r.withX (r.getX() + r.getWidth() / 3).withWidth (r.getWidth() / 3),
                        juce::Justification::centred);

            // HYPE BAR (right third)
            const auto hr = r.withX (r.getX() + (r.getWidth() * 2) / 3).withWidth (r.getWidth() / 3);
            // Label
            g.setColour (juce::Colour (0xFFF1F8E9));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
            g.drawText ("HYPE", hr.withWidth (24), juce::Justification::centredLeft);
            // Bar
            const auto barR = hr.withTrimmedLeft (26).toFloat();
            g.setColour (juce::Colour (0xFF1A2500));
            g.fillRoundedRectangle (barR, 2.0f);
            const float hf = (float) (state.hype / 100.0);
            const auto fillR = barR.withWidth (barR.getWidth() * hf);
            juce::ColourGradient hypeGrad (
                juce::Colour (0xFF9CCC65), barR.getX(), 0.0f,
                juce::Colour (0xFFFF5252), barR.getRight(), 0.0f, false);
            hypeGrad.addColour (0.5, juce::Colour (0xFFFFEB3B));
            g.setGradientFill (hypeGrad);
            g.fillRoundedRectangle (fillR, 2.0f);
            g.setColour (juce::Colour (0xFFCDDC39).withAlpha (0.7f));
            g.drawRoundedRectangle (barR, 2.0f, 1.0f);

            // Event mult indicator (tiny)
            if (state.eventMultiplier > 1.0)
            {
                g.setColour (juce::Colour (0xFFFF5252));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
                g.drawText ("x" + juce::String (state.eventMultiplier, 1),
                            hr, juce::Justification::centredRight);
            }

            // v5.2: drag handle — top-right 14×14 square. Drawn as 6 small
            // dots in a 2×3 pattern (looks like a grip/drag handle).
            drawDragHandle (g, getDragHandleRect());
        }

        void drawDragHandle (juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setColour (juce::Colour (0xFF0A1000).withAlpha (0.6f));
            g.fillRoundedRectangle (r.toFloat(), 2.0f);
            g.setColour (juce::Colour (0xFFCDDC39).withAlpha (0.8f));
            // 2 columns × 3 rows of dots
            for (int col = 0; col < 2; ++col)
                for (int row = 0; row < 3; ++row)
                {
                    const float dx = r.getX() + 3.0f + (float) col * 5.0f;
                    const float dy = r.getY() + 3.0f + (float) row * 3.0f;
                    g.fillRect (dx, dy, 2.0f, 2.0f);
                }
        }

        void drawTabBar (juce::Graphics& g)
        {
            const std::array<const char*, 3> names = { "MAP", "LABEL", "LEGEND" };
            for (int i = 0; i < 3; ++i)
            {
                const auto r = tabRects[(size_t) i];
                const bool active = (state.activeTab == i);
                const bool locked = (i == 1 && state.levels[3] == 0);
                g.setColour (active ? juce::Colour (0xFFC6FF00) : juce::Colour (0xFF1A2500));
                g.fillRoundedRectangle (r.toFloat(), 2.0f);
                g.setColour (locked ? juce::Colour (0xFF555555)
                                    : active ? juce::Colour (0xFF0A1000) : juce::Colour (0xFFF1F8E9));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
                g.drawFittedText (locked ? "LABEL?" : names[(size_t) i],
                                  r, juce::Justification::centred, 1);
            }
        }

        //======================================================================
        // Rendering — MAP TAB
        //======================================================================
        void drawMapTab (juce::Graphics& g)
        {
            // Ground strip (road at bottom) — darker than sky
            const float roadY = (float) (mapRect.getBottom() - 12);
            g.setColour (juce::Colour (0xFF1A1A1A));
            g.fillRect ((float) mapRect.getX(), roadY,
                        (float) mapRect.getWidth(), 12.0f);
            // Road dashes
            g.setColour (juce::Colour (0xFFFFEB3B).withAlpha (0.7f));
            for (int x = mapRect.getX() + 4; x < mapRect.getRight(); x += 14)
                g.fillRect ((float) x, roadY + 5.0f, 7.0f, 2.0f);

            // Cars
            drawCars (g);

            // Buildings
            for (int i = 0; i < NUM_BUILDINGS; ++i)
                drawBuilding (g, i);

            // Runners (NPCs)
            drawRunners (g);
        }

        void drawBuilding (juce::Graphics& g, int idx)
        {
            const auto& bd = getBuildings()[(size_t) idx];
            const auto  r  = buildingRects[(size_t) idx];
            const int   lv = state.levels[(size_t) idx];
            const bool  un = state.isBuildingUnlocked (idx);
            const bool  built = (lv > 0);
            const float phase = getDayNightPhase();
            const bool  isNight = (phase > 0.55f && phase < 0.95f);

            if (! un)
            {
                // Lock silhouette
                g.setColour (juce::Colour (0xFF0A1000).withAlpha (0.6f));
                g.fillRoundedRectangle (r.toFloat(), 2.0f);
                g.setColour (juce::Colour (0xFF555555));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
                g.drawFittedText ("$" + formatMoney (bd.unlockThreshold),
                                  r, juce::Justification::centred, 1);
                // padlock in corner
                g.setColour (juce::Colour (0xFFCDDC39).withAlpha (0.6f));
                g.fillRect (r.getX() + 2, r.getY() + 2, 4, 5);
                return;
            }

            // Per-building sprite renderer
            switch (idx)
            {
                case 0: drawTrapHouse  (g, r, lv, isNight); break;
                case 1: drawHomeStudio (g, r, lv, isNight); break;
                case 2: drawRadio      (g, r, lv, isNight); break;
                case 3: drawLabelHQ    (g, r, lv, isNight); break;
                case 4: drawAirport    (g, r, lv, isNight); break;
                case 5: drawMansion    (g, r, lv, isNight); break;
                default: g.setColour (juce::Colour (0xFF663333)); g.fillRect (r); break;
            }

            // Price tag hovering above if affordable
            if (state.money >= state.getBuildingCost (idx))
            {
                const auto priceR = juce::Rectangle<int> (r.getCentreX() - 26, r.getY() - 10, 52, 10);
                g.setColour (juce::Colour (0xFF0A1000).withAlpha (0.75f));
                g.fillRoundedRectangle (priceR.toFloat(), 2.0f);
                g.setColour (juce::Colour (0xFFC6FF00));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
                g.drawFittedText ("$" + formatMoney (state.getBuildingCost (idx)),
                                  priceR, juce::Justification::centred, 1);
            }

            // Level badge
            if (built)
            {
                const auto badge = juce::Rectangle<int> (r.getRight() - 14, r.getY() - 2, 14, 10);
                g.setColour (juce::Colour (0xFFFFEB3B));
                g.fillRoundedRectangle (badge.toFloat(), 2.0f);
                g.setColour (juce::Colour (0xFF0A1000));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
                g.drawFittedText ("L" + juce::String (lv), badge, juce::Justification::centred, 1);
            }

            // Label text
            g.setColour (juce::Colour (0xFFF1F8E9).withAlpha (0.8f));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
            g.drawFittedText (bd.shortName,
                              r.withHeight (8).withY (r.getBottom() + 1),
                              juce::Justification::centred, 1);
        }

        void drawTrapHouse (juce::Graphics& g, juce::Rectangle<int> r, int lv, bool isNight)
        {
            const float pulse = beatPulse;
            auto rp = r.toFloat();
            rp = juce::Rectangle<float> (
                rp.getCentreX() - rp.getWidth() * 0.5f * pulse,
                rp.getCentreY() - rp.getHeight() * 0.5f * pulse,
                rp.getWidth() * pulse, rp.getHeight() * pulse);

            // Wall
            g.setColour (juce::Colour (0xFF3A2A1E));
            g.fillRect (rp.withHeight (rp.getHeight() * 0.75f).withY (rp.getY() + rp.getHeight() * 0.25f));
            // Roof (triangle)
            juce::Path roof;
            roof.addTriangle (rp.getX(), rp.getY() + rp.getHeight() * 0.25f,
                              rp.getRight(), rp.getY() + rp.getHeight() * 0.25f,
                              rp.getCentreX(), rp.getY());
            g.setColour (juce::Colour (0xFF1A0F0A));
            g.fillPath (roof);
            // Window (lit at night)
            const auto winR = juce::Rectangle<float> (rp.getCentreX() - 4, rp.getCentreY() - 2, 8, 6);
            g.setColour (isNight ? juce::Colour (0xFFFFEB3B) : juce::Colour (0xFF444444));
            g.fillRect (winR);
            // Door
            g.setColour (juce::Colour (0xFF0A1000));
            g.fillRect (rp.getX() + 3.0f, rp.getBottom() - 10.0f, 5.0f, 10.0f);
            // "TAP" hint
            g.setColour (juce::Colour (0xFFC6FF00));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
            g.drawFittedText ("TAP", rp.toNearestInt(), juce::Justification::centred, 1);
            // Level badge (upgrades = more antennae / graffiti)
            for (int i = 0; i < juce::jmin (lv, 3); ++i)
            {
                g.setColour (juce::Colour (0xFFFFEB3B));
                g.fillRect (rp.getX() + 5 + i * 3, rp.getY() - 3, 1.0f, 4.0f);
            }
        }

        void drawHomeStudio (juce::Graphics& g, juce::Rectangle<int> r, int lv, bool isNight)
        {
            auto rf = r.toFloat();
            // Flat-roof box
            g.setColour (juce::Colour (0xFF4A3A25));
            g.fillRect (rf.withHeight (rf.getHeight() * 0.8f).withY (rf.getY() + rf.getHeight() * 0.2f));
            g.setColour (juce::Colour (0xFF2E1A0A));
            g.fillRect (rf.withHeight (rf.getHeight() * 0.2f));
            // Windows (multiple, lit at night)
            const int wins = juce::jlimit (2, 6, 2 + lv);
            for (int i = 0; i < wins; ++i)
            {
                const float wx = rf.getX() + 4 + (float) i * 5.0f;
                const float wy = rf.getY() + rf.getHeight() * 0.4f;
                if (wx + 3 > rf.getRight() - 3) break;
                g.setColour (isNight ? juce::Colour (0xFFFFEB3B) : juce::Colour (0xFF555555));
                g.fillRect (wx, wy, 3.0f, 4.0f);
            }
            // Mic sign on top
            g.setColour (juce::Colour (0xFFFFEB3B));
            g.fillEllipse (rf.getCentreX() - 2.0f, rf.getY() - 3.0f, 4.0f, 4.0f);
            g.setColour (juce::Colour (0xFF0A1000));
            g.fillRect (rf.getCentreX() - 1.0f, rf.getY() - 4.0f, 2.0f, 2.0f);
        }

        void drawRadio (juce::Graphics& g, juce::Rectangle<int> r, int lv, bool isNight)
        {
            auto rf = r.toFloat();
            // Building body
            g.setColour (juce::Colour (0xFF37474F));
            g.fillRect (rf.withTrimmedTop (8));
            // Dish on top (rotating illusion via day-phase)
            const float spin = getDayNightPhase() * juce::MathConstants<float>::twoPi * 4.0f;
            const auto dishC = juce::Point<float> (rf.getCentreX(), rf.getY() + 4);
            g.setColour (juce::Colour (0xFF90A4AE));
            g.fillEllipse (dishC.x - 6.0f, dishC.y - 3.0f, 12.0f, 6.0f);
            // Antenna
            g.setColour (juce::Colour (0xFFB0BEC5));
            g.fillRect (dishC.x - 0.5f, rf.getY() - 3, 1.0f, 5.0f);
            // Red blink on tip
            const bool blink = ((juce::Time::currentTimeMillis() / 400) % 2) == 0;
            if (blink)
            {
                g.setColour (juce::Colour (0xFFFF5252));
                g.fillRect (dishC.x - 1, rf.getY() - 4, 2.0f, 2.0f);
            }
            // Broadcast waves (animated)
            g.setColour (juce::Colour (0xFFC6FF00).withAlpha (0.4f));
            for (int i = 1; i <= juce::jmin (lv, 3); ++i)
            {
                const float rad = 4.0f + i * 3.0f + std::sin (spin + (float) i) * 1.0f;
                g.drawEllipse (dishC.x - rad, rf.getY() - rad + 2, rad * 2, rad * 2, 1.0f);
            }
            // Window rows
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                {
                    const float wx = rf.getX() + 4 + (float) j * 7.0f;
                    const float wy = rf.getY() + 12 + (float) i * 6.0f;
                    if (wx + 2 > rf.getRight() - 2) continue;
                    g.setColour (isNight ? juce::Colour (0xFFFFEB3B).withAlpha (0.9f)
                                          : juce::Colour (0xFF546E7A));
                    g.fillRect (wx, wy, 3.0f, 3.0f);
                }
        }

        void drawLabelHQ (juce::Graphics& g, juce::Rectangle<int> r, int lv, bool isNight)
        {
            auto rf = r.toFloat();
            // Tall glass tower (gradient)
            juce::ColourGradient glass (
                juce::Colour (0xFF1A237E), rf.getX(), rf.getY(),
                juce::Colour (0xFF0A1000), rf.getX(), rf.getBottom(), false);
            g.setGradientFill (glass);
            g.fillRect (rf.withTrimmedTop (2));
            // Gold edge
            g.setColour (juce::Colour (0xFFFFEB3B));
            g.drawRect (rf.withTrimmedTop (2), 1.0f);
            // Window grid
            const int cols = 4;
            const int rows = juce::jlimit (4, 8, 4 + lv);
            for (int i = 0; i < rows; ++i)
                for (int j = 0; j < cols; ++j)
                {
                    const float wx = rf.getX() + 3 + (float) j * ((rf.getWidth() - 6) / cols);
                    const float wy = rf.getY() + 4 + (float) i * ((rf.getHeight() - 8) / rows);
                    const bool lit = isNight && ((i + j + (int) (getDayNightPhase() * 4)) % 3 != 0);
                    g.setColour (lit ? juce::Colour (0xFFFFEB3B)
                                      : juce::Colour (0xFF546E7A).withAlpha (0.6f));
                    g.fillRect (wx, wy, 3.0f, 2.0f);
                }
            // "1017" logo on top
            g.setColour (juce::Colour (0xFFFFEB3B));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
            g.drawFittedText ("1017",
                              juce::Rectangle<int> ((int) rf.getX(), (int) rf.getY() - 1, (int) rf.getWidth(), 6),
                              juce::Justification::centred, 1);
        }

        void drawAirport (juce::Graphics& g, juce::Rectangle<int> r, int lv, bool isNight)
        {
            auto rf = r.toFloat();
            // Terminal (flat low building)
            g.setColour (juce::Colour (0xFF90A4AE));
            g.fillRect (rf.withTrimmedTop (rf.getHeight() * 0.4f));
            // Control tower
            const auto tower = juce::Rectangle<float> (rf.getCentreX() - 3, rf.getY(),
                                                        6, rf.getHeight() * 0.55f);
            g.setColour (juce::Colour (0xFFECEFF1));
            g.fillRect (tower);
            g.setColour (juce::Colour (0xFFFFEB3B));
            g.fillRect (tower.withHeight (3));
            // Windows strip
            for (int i = 0; i < 6; ++i)
            {
                const float wx = rf.getX() + 3 + i * 9.0f;
                const float wy = rf.getY() + rf.getHeight() * 0.55f;
                if (wx + 7 > rf.getRight() - 3) continue;
                g.setColour (isNight ? juce::Colour (0xFFFFEB3B) : juce::Colour (0xFF546E7A));
                g.fillRect (wx, wy, 7.0f, 4.0f);
            }
            // Plane flying across (animated by level)
            const float planeT = std::fmod (getDayNightPhase() * 8.0f, 1.0f);
            const float planeX = rf.getX() + rf.getWidth() * planeT;
            const float planeY = rf.getY() - 6 - lv * 0.5f;
            g.setColour (juce::Colour (0xFFECEFF1));
            g.fillRect (planeX, planeY, 6.0f, 1.0f);
            g.fillRect (planeX + 2, planeY - 1, 2.0f, 3.0f);
            if (isNight)
            {
                g.setColour (juce::Colour (0xFFFF5252));
                g.fillRect (planeX, planeY, 1.0f, 1.0f);
            }
        }

        void drawMansion (juce::Graphics& g, juce::Rectangle<int> r, int lv, bool isNight)
        {
            auto rf = r.toFloat();
            // Pillars & wide base
            g.setColour (juce::Colour (0xFFFFF8DC));
            g.fillRect (rf.withY (rf.getY() + rf.getHeight() * 0.3f));
            // Roof triangle
            juce::Path roof;
            roof.addTriangle (rf.getX() - 2, rf.getY() + rf.getHeight() * 0.3f,
                              rf.getRight() + 2, rf.getY() + rf.getHeight() * 0.3f,
                              rf.getCentreX(), rf.getY());
            g.setColour (juce::Colour (0xFFD4AF37));
            g.fillPath (roof);
            // Front columns
            const int cols = 5;
            for (int i = 0; i < cols; ++i)
            {
                const float cx = rf.getX() + 5 + i * ((rf.getWidth() - 10) / (cols - 1));
                g.setColour (juce::Colour (0xFFFFFFFF));
                g.fillRect (cx - 1, rf.getY() + rf.getHeight() * 0.35f, 2.0f, rf.getHeight() * 0.55f);
            }
            // Door
            g.setColour (juce::Colour (0xFF8B0000));
            g.fillRect (rf.getCentreX() - 3, rf.getY() + rf.getHeight() * 0.65f, 6.0f, rf.getHeight() * 0.3f);
            // Gold dome on top
            g.setColour (juce::Colour (0xFFFFD700));
            g.fillEllipse (rf.getCentreX() - 3.0f, rf.getY() - 4.0f, 6.0f, 5.0f);
            // Prestige star count
            for (int i = 0; i < juce::jmin (state.prestige, 5); ++i)
            {
                g.setColour (juce::Colour (0xFFFFEB3B));
                const float sx = rf.getX() + 4 + i * 5.0f;
                g.fillRect (sx, rf.getY() - 8, 2.0f, 2.0f);
            }
            // Windows lit gold at night
            if (isNight)
            {
                for (int i = 0; i < 3; ++i)
                {
                    const float wx = rf.getX() + 10 + i * 15.0f;
                    if (wx + 3 > rf.getRight() - 10) break;
                    g.setColour (juce::Colour (0xFFFFD700));
                    g.fillRect (wx, rf.getY() + rf.getHeight() * 0.5f, 3.0f, 3.0f);
                }
            }
            juce::ignoreUnused (lv);
        }

        //======================================================================
        // Cars + runners
        //======================================================================
        void drawCars (juce::Graphics& g)
        {
            for (const auto& c : cars)
            {
                g.setColour (c.colour);
                g.fillRect (c.x, c.y, 10.0f, 4.0f);
                g.setColour (c.colour.brighter (0.3f));
                g.fillRect (c.x + 1, c.y - 1, 7.0f, 2.0f);
                // Windows
                g.setColour (juce::Colour (0xFF0A1000));
                g.fillRect (c.x + 2, c.y, 2.0f, 1.0f);
                g.fillRect (c.x + 6, c.y, 2.0f, 1.0f);
                // Headlights
                if (c.hasHeadlights)
                {
                    const float hx = (c.vx > 0) ? c.x + 10.0f : c.x - 3.0f;
                    g.setColour (juce::Colour (0xFFFFEB3B).withAlpha (0.7f));
                    g.fillRect (hx, c.y + 1.0f, 3.0f, 1.0f);
                }
            }
        }

        void drawRunners (juce::Graphics& g)
        {
            for (const auto& r : runners)
            {
                const float bob = std::sin ((float) r.x / 3.0f) * 1.0f;
                switch (r.kind)
                {
                    case Runner::Kind::DeliveryBoy:
                        drawPersonSprite (g, r.x, r.y + bob,
                                          juce::Colour (0xFF3949AB),
                                          juce::Colour (0xFFFFEB3B));
                        // Bag
                        g.setColour (juce::Colour (0xFF5D4037));
                        g.fillRect (r.x + 4, r.y + bob + 2, 3.0f, 3.0f);
                        break;
                    case Runner::Kind::Fan:
                        drawPersonSprite (g, r.x, r.y + bob,
                                          juce::Colour (0xFFE91E63),
                                          juce::Colour (0xFFFFF8DC));
                        // Phone raised
                        g.setColour (juce::Colour (0xFF0A1000));
                        g.fillRect (r.x + 6, r.y - 6 + bob, 2.0f, 3.0f);
                        break;
                    case Runner::Kind::Artist:
                        drawPersonSprite (g, r.x, r.y + bob,
                                          juce::Colour (0xFF4A148C),
                                          juce::Colour (0xFFFFD700));
                        // Chain
                        g.setColour (juce::Colour (0xFFFFD700));
                        g.fillRect (r.x + 2, r.y + bob + 2, 5.0f, 1.0f);
                        // Name tag above head
                        g.setColour (juce::Colour (0xFFFFEB3B));
                        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
                        g.drawFittedText (getArtists()[(size_t) r.artistIdx].nickname,
                                          juce::Rectangle<int> ((int) r.x - 10, (int) (r.y + bob) - 14,
                                                                 28, 7),
                                          juce::Justification::centred, 1);
                        break;
                    case Runner::Kind::CopCar:
                    {
                        // A small cop car
                        g.setColour (juce::Colour (0xFF1565C0));
                        g.fillRect (r.x, r.y, 10.0f, 4.0f);
                        g.setColour (juce::Colours::white);
                        g.fillRect (r.x + 2, r.y, 3.0f, 4.0f);
                        // Light bar
                        const bool red = ((juce::Time::currentTimeMillis() / 120) % 2) == 0;
                        g.setColour (red ? juce::Colour (0xFFFF5252) : juce::Colour (0xFF2962FF));
                        g.fillRect (r.x + 3, r.y - 1, 4.0f, 1.0f);
                        break;
                    }
                }
                // Click hint glow
                g.setColour (juce::Colour (0xFFC6FF00).withAlpha (0.25f));
                g.drawEllipse (r.x - 6.0f, r.y + bob - 6.0f, 14.0f, 14.0f, 1.0f);
            }
        }

        static void drawPersonSprite (juce::Graphics& g, float x, float y,
                                      juce::Colour shirt, juce::Colour skin)
        {
            // Tiny 6x10 sprite
            g.setColour (skin);
            g.fillRect (x + 2, y - 6, 3.0f, 3.0f); // head
            g.setColour (shirt);
            g.fillRect (x + 1, y - 3, 5.0f, 5.0f); // body
            g.setColour (juce::Colour (0xFF0A1000));
            g.fillRect (x + 1, y + 2, 2.0f, 3.0f); // left leg
            g.fillRect (x + 4, y + 2, 2.0f, 3.0f); // right leg
        }

        //======================================================================
        // Rendering — LABEL TAB
        //======================================================================
        void drawLabelTab (juce::Graphics& g)
        {
            if (state.levels[3] == 0)
            {
                // Not built yet
                g.setColour (juce::Colour (0xFFF1F8E9).withAlpha (0.7f));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold));
                g.drawFittedText ("BUILD LABEL HQ FIRST ($50K)",
                                  contentRect, juce::Justification::centred, 2);
                return;
            }

            // Grid of 8 artist cards
            for (int i = 0; i < NUM_ARTISTS; ++i)
                drawArtistCard (g, i);
        }

        void drawArtistCard (juce::Graphics& g, int idx)
        {
            const auto  r = artistRects[(size_t) idx];
            const auto& a = getArtists()[(size_t) idx];
            const bool  signed_ = (state.signedArtistMask & (1u << idx)) != 0;
            const bool  unlocked = state.totalEarned >= a.unlockStreams;
            const bool  affordable = unlocked && state.money >= a.signingFee;

            // BG
            juce::Colour bg;
            if      (signed_)   bg = juce::Colour (0xFF1A3300);
            else if (affordable) bg = juce::Colour (0xFF1A2500);
            else if (unlocked)   bg = juce::Colour (0xFF0A1000);
            else                 bg = juce::Colour (0xFF05080A);
            g.setColour (bg);
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
            g.setColour (signed_ ? juce::Colour (0xFFC6FF00)
                                  : affordable ? juce::Colour (0xFFFFEB3B)
                                                : juce::Colour (0xFF555555));
            g.drawRoundedRectangle (r.toFloat(), 3.0f, 1.0f);

            // Name
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
            g.setColour (unlocked ? juce::Colour (0xFFFFEB3B) : juce::Colour (0xFF555555));
            g.drawFittedText (unlocked ? a.name : "???",
                              r.withHeight (10).translated (0, 2),
                              juce::Justification::centred, 1);

            if (signed_)
            {
                g.setColour (juce::Colour (0xFFC6FF00));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
                g.drawFittedText ("SIGNED",
                                  r.withHeight (10).translated (0, 14),
                                  juce::Justification::centred, 1);
                g.setColour (juce::Colour (0xFFCDDC39));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
                g.drawFittedText ("+" + juce::String (a.incomeBonus01 * 100.0, 0) + "% /s",
                                  r.withHeight (10).translated (0, 26),
                                  juce::Justification::centred, 1);
            }
            else if (unlocked)
            {
                g.setColour (juce::Colour (0xFFCDDC39));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
                g.drawFittedText ("FEE $" + formatMoney (a.signingFee),
                                  r.withHeight (10).translated (0, 14),
                                  juce::Justification::centred, 1);
                g.drawFittedText ("+" + juce::String (a.incomeBonus01 * 100.0, 0) + "% /s",
                                  r.withHeight (10).translated (0, 24),
                                  juce::Justification::centred, 1);
                if (affordable)
                {
                    g.setColour (juce::Colour (0xFFC6FF00));
                    g.drawFittedText ("TAP TO SIGN",
                                      r.withHeight (8).translated (0, r.getHeight() - 10),
                                      juce::Justification::centred, 1);
                }
            }
            else
            {
                g.setColour (juce::Colour (0xFF555555));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
                g.drawFittedText ("@ $" + formatMoney (a.unlockStreams),
                                  r.withHeight (10).translated (0, 18),
                                  juce::Justification::centred, 1);
            }
        }

        //======================================================================
        // Rendering — LEGEND TAB
        //======================================================================
        void drawLegendTab (juce::Graphics& g)
        {
            // Stats block
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold));
            g.setColour (juce::Colour (0xFFFFEB3B));
            g.drawFittedText ("LEGEND",
                              contentRect.withHeight (14).translated (0, 2),
                              juce::Justification::centred, 1);

            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
            g.setColour (juce::Colour (0xFFF1F8E9));
            const auto statRect = contentRect.withTrimmedTop (18).withTrimmedBottom (40);
            const int statH = 10;
            int y = statRect.getY();
            auto drawStat = [&] (const juce::String& k, const juce::String& v)
            {
                g.setColour (juce::Colour (0xFFCDDC39));
                g.drawText (k, juce::Rectangle<int> (statRect.getX() + 6, y, 160, statH),
                            juce::Justification::centredLeft);
                g.setColour (juce::Colour (0xFFF1F8E9));
                g.drawText (v, juce::Rectangle<int> (statRect.getX() + 160, y,
                                                      statRect.getWidth() - 160, statH),
                            juce::Justification::centredLeft);
                y += statH;
            };
            drawStat ("Total Earned:",  "$" + formatMoney (state.totalEarned));
            drawStat ("Taps:",          juce::String (state.totalTaps));
            drawStat ("Buildings Owned:", juce::String (state.getBuildingCount()) + " / 6");
            drawStat ("Artists Signed:",  juce::String (state.getSignedArtistCount()) + " / 8");
            drawStat ("Legend Points:",  juce::String (state.legendPoints)
                                          + "  (+" + juce::String ((int) ((state.getLegendMultiplier() - 1.0) * 100.0))
                                          + "% income)");
            drawStat ("Prestige:",  juce::String (state.prestige) + "x");

            // Achievements row
            const auto achR = contentRect.withY (y + 4).withHeight (14);
            g.setColour (juce::Colour (0xFFCDDC39));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
            for (int i = 0; i < NUM_ACHIEVEMENTS; ++i)
            {
                const bool have = (state.achievementMask & (1u << i)) != 0;
                const int bx = achR.getX() + 6 + i * 12;
                if (bx + 10 > achR.getRight()) break;
                g.setColour (have ? juce::Colour (0xFFFFEB3B) : juce::Colour (0xFF1A2500));
                g.fillRect (bx, achR.getY(), 10, 10);
                if (have)
                {
                    g.setColour (juce::Colour (0xFF0A1000));
                    g.drawText ("*", juce::Rectangle<int> (bx, achR.getY(), 10, 10),
                                juce::Justification::centred);
                }
            }

            // Prestige button
            const bool can = state.canPrestige();
            g.setColour (can ? juce::Colour (0xFFC6FF00) : juce::Colour (0xFF2E3D00));
            g.fillRoundedRectangle (prestigeBtn.toFloat(), 4.0f);
            g.setColour (juce::Colour (0xFF0A1000));
            g.drawRoundedRectangle (prestigeBtn.toFloat(), 4.0f, 1.5f);
            g.setColour (juce::Colour (0xFF0A1000));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
            g.drawFittedText (can ? "RETIRE TO MANSION +1" : "RETIRE @ $10M",
                              prestigeBtn, juce::Justification::centred, 1);
        }

        //======================================================================
        // Overlays
        //======================================================================
        void drawMissionCard (juce::Graphics& g)
        {
            const int64_t nowMs = juce::Time::currentTimeMillis();

            // Active mission progress (across top of content)
            if (state.activeMission >= 0)
            {
                const auto& m = getMissions()[(size_t) state.activeMission];
                const double earned = state.totalEarned - state.missionEarnedAtStart;
                const float  f = (float) juce::jlimit (0.0, 1.0, earned / m.moneyTarget);
                const int64_t remMs = juce::jmax ((int64_t) 0, state.missionEndMs - nowMs);
                const auto bar = juce::Rectangle<int> (
                    contentRect.getX() + 4, contentRect.getY() + 4,
                    contentRect.getWidth() - 8, 10);
                g.setColour (juce::Colour (0xFF0A1000).withAlpha (0.85f));
                g.fillRoundedRectangle (bar.toFloat(), 2.0f);
                g.setColour (juce::Colour (0xFFC6FF00));
                g.fillRoundedRectangle (bar.toFloat().withWidth ((float) bar.getWidth() * f), 2.0f);
                g.setColour (juce::Colour (0xFFFFEB3B));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
                g.drawFittedText (juce::String (m.title) + "  "
                                  + formatMoney (earned) + "/" + formatMoney (m.moneyTarget)
                                  + "  " + juce::String ((int) (remMs / 1000)) + "s",
                                  bar, juce::Justification::centred, 1);
            }

            if (! missionCardVisible || missionCardIdx < 0) return;
            const auto& m = getMissions()[(size_t) missionCardIdx];

            g.setColour (juce::Colour (0xFF0A1000).withAlpha (0.92f));
            g.fillRoundedRectangle (missionCardBounds.toFloat(), 4.0f);
            g.setColour (juce::Colour (0xFFFFEB3B));
            g.drawRoundedRectangle (missionCardBounds.toFloat(), 4.0f, 1.5f);

            const auto top = missionCardBounds.withHeight (14).translated (0, 2);
            g.setColour (juce::Colour (0xFFFFEB3B));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
            g.drawFittedText (m.title, top, juce::Justification::centred, 1);

            g.setColour (juce::Colour (0xFFF1F8E9));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
            g.drawFittedText (juce::String ("earn $") + formatMoney (m.moneyTarget) + " in " + juce::String (m.durationSec) + "s",
                              missionCardBounds.withTop (top.getBottom()).withBottom (missionAcceptBtn.getY()),
                              juce::Justification::centred, 2);

            // Accept
            g.setColour (juce::Colour (0xFFC6FF00));
            g.fillRoundedRectangle (missionAcceptBtn.toFloat(), 2.0f);
            g.setColour (juce::Colour (0xFF0A1000));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
            g.drawFittedText ("ACCEPT", missionAcceptBtn, juce::Justification::centred, 1);
            // Decline
            g.setColour (juce::Colour (0xFF555555));
            g.fillRoundedRectangle (missionDeclineBtn.toFloat(), 2.0f);
            g.setColour (juce::Colour (0xFFF1F8E9));
            g.drawFittedText ("NOPE", missionDeclineBtn, juce::Justification::centred, 1);
        }

        void drawFloatingTexts (juce::Graphics& g)
        {
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
            for (const auto& f : floaters)
            {
                g.setColour (f.colour.withAlpha (std::max (0.0f, f.life)));
                juce::AffineTransform t = juce::AffineTransform::scale (f.scale)
                                            .translated (f.x, f.y);
                juce::Graphics::ScopedSaveState ss (g);
                g.addTransform (t);
                g.drawText (f.text,
                            juce::Rectangle<int> (-60, -4, 120, 12),
                            juce::Justification::centred);
            }
        }

        void drawOfflineBanner (juce::Graphics& g)
        {
            if (offlineBannerTimer <= 0 || offlineBannerAmount <= 0.0) return;
            const float alpha = juce::jmin (1.0f, offlineBannerTimer / 30.0f);
            const auto b = juce::Rectangle<int> (
                contentRect.getCentreX() - 70, contentRect.getY() - 2, 140, 14);
            g.setColour (juce::Colour (0xFF0A1000).withAlpha (0.9f * alpha));
            g.fillRoundedRectangle (b.toFloat(), 3.0f);
            g.setColour (juce::Colour (0xFFFFEB3B).withAlpha (alpha));
            g.drawRoundedRectangle (b.toFloat(), 3.0f, 1.0f);
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
            g.drawFittedText ("OFFLINE +$" + formatMoney (offlineBannerAmount),
                              b, juce::Justification::centred, 1);
        }

        void drawAchievementToast (juce::Graphics& g)
        {
            if (toastTimer <= 0 || toastText.isEmpty()) return;
            const float alpha = juce::jmin (1.0f, toastTimer / 30.0f);
            const auto b = juce::Rectangle<int> (
                getWidth() - 170, getHeight() - 22, 166, 18);
            g.setColour (juce::Colour (0xFF0A1000).withAlpha (0.9f * alpha));
            g.fillRoundedRectangle (b.toFloat(), 3.0f);
            g.setColour (juce::Colour (0xFFC6FF00).withAlpha (alpha));
            g.drawRoundedRectangle (b.toFloat(), 3.0f, 1.0f);
            g.setColour (juce::Colour (0xFFFFEB3B).withAlpha (alpha));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
            g.drawFittedText (toastText, b, juce::Justification::centred, 1);
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TycoonGame)
    };
} // namespace th::game
