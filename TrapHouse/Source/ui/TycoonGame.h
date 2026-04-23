#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include "LookAndFeel1017.h"

// =============================================================================
// HUSTLE MTP — embedded pixel-art action game in 3PACK CLIP.
//
// Not a tycoon. Not an idle clicker. An ACTIVE hustle: your character walks
// the streets of Montpellier, opps spawn and push toward your base, you
// tap them to shoot. Defeated opps drop cash coins and occasionally a
// weapon. Walk over coins to collect. Walk back to BASE to bank your stash
// (dies = you lose unbanked cash). Weapons upgrade your firepower.
//
// PERMANENCE
//   Class name `TycoonGame` is preserved for processor/editor API compat
//   (the plugin reads "count_3" for the ICE character unlock, "prestige"
//   for plugin flourishes). v5.3 maps those onto new concepts:
//     count_3   -> "reached COMEDIE zone at least once" (1 = yes)
//     prestige  -> "completed the game (bosses killed + banked 1M)"
//   Other legacy keys (money, totalEarned, signedArtistMask, etc.) are
//   kept / ignored cleanly so old save states don't blow up.
//
// WORLD
//   - 800 px wide (camera scrolls inside the 470 px viewport)
//   - 3 zones: ECUSSON (easy), ARCEAUX (medium), COMEDIE (hard)
//   - Each zone has its own palette + building silhouettes + opp spawns
//   - Day/night cycle shifts tint (retained from v2)
//
// LOOP
//   1. Click ground -> player walks there
//   2. Click opp    -> player auto-shoots (if in range / cooldown ready)
//   3. Opp dead     -> spawns cash coins + maybe a weapon pickup
//   4. Walk over pickup -> auto-collect
//   5. Walk back to BASE (at x=30) -> stash gets banked
//   6. Die          -> lose 50% of unbanked stash, respawn at base
//
// WEAPONS
//   FISTS -> GLOCK -> MAC10 -> AK47 -> DRACO
//   Picked up as loot. Upgrades auto-equip if better than current weapon.
//
// CPU
//   30 Hz tick, ≤20 opps and ≤50 coins on screen at any time, no allocation
//   in the per-frame path. Render uses only fillRect/fillEllipse/drawText.
//
// API (unchanged, plugin-facing)
//   TycoonGame();
//   void paint(Graphics&); void resized(); void mouseDown/Drag/Up(...)
//   void setAudioActivity(float);
//   ValueTree getSaveState() const;
//   void loadSaveState(const ValueTree&);
//   std::function<void(const MouseEvent&)> onDragHandleDown/Drag/Up;
// =============================================================================
namespace th::game
{
    //==========================================================================
    // Weapons
    //==========================================================================
    struct WeaponDef
    {
        const char* name;
        const char* shortName;
        int   damage;
        float rangePx;
        int   cooldownMs;
        juce::Colour tracerColour;
    };

    static constexpr int NUM_WEAPONS = 5;

    static const std::array<WeaponDef, NUM_WEAPONS>& getWeapons()
    {
        static const std::array<WeaponDef, NUM_WEAPONS> W = {{
            { "FISTS",     "FIST",  1,  16.0f, 450, juce::Colour (0xFFFFEE58) },
            { "GLOCK 17",  "GLOCK", 2,  95.0f, 300, juce::Colour (0xFFC6FF00) },
            { "MAC-10",    "MAC",   2, 110.0f, 160, juce::Colour (0xFFFFEB3B) },
            { "AK-47",     "AK",    4, 150.0f, 230, juce::Colour (0xFFFF5252) },
            { "DRACO",     "DRACO", 8, 140.0f, 380, juce::Colour (0xFFE040FB) },
        }};
        return W;
    }

    //==========================================================================
    // Zones — horizontal strips of the world
    //==========================================================================
    struct ZoneDef
    {
        const char* name;
        float startX, endX;
        int   unlockBankedCash;
        juce::Colour skyTop, skyBot, ground;
    };

    static constexpr int NUM_ZONES = 3;

    static const std::array<ZoneDef, NUM_ZONES>& getZones()
    {
        static const std::array<ZoneDef, NUM_ZONES> Z = {{
            { "ECUSSON",    0.0f,    340.0f,       0,
              juce::Colour (0xFF2E1F4C), juce::Colour (0xFF0A1000), juce::Colour (0xFF2A2A2A) },
            { "ARCEAUX",    340.0f,  600.0f,     500,
              juce::Colour (0xFF3B1020), juce::Colour (0xFF0A0A18), juce::Colour (0xFF333330) },
            { "COMEDIE",    600.0f,  900.0f,    5000,
              juce::Colour (0xFF1A237E), juce::Colour (0xFF05070F), juce::Colour (0xFF383038) },
        }};
        return Z;
    }
    static constexpr float WORLD_WIDTH = 900.0f;

    //==========================================================================
    // Actors
    //==========================================================================
    struct Player
    {
        float x         { 60.0f };    // world space
        float y         { 145.0f };   // world space (ground line varies slightly)
        float targetX   { 60.0f };
        int   hp        { 100 };
        int   maxHp     { 100 };
        int   weaponIdx { 0 };
        float shootCd   { 0.0f };     // seconds until next shot
        int   dir       { 0 };        // 0=facing right, 1=left
        float walkPhase { 0.0f };     // 0..1 leg animation
        float hurtFlash { 0.0f };     // 0..1 red flash on hit
    };

    struct Opp
    {
        float x           { 0.0f };
        float y           { 145.0f };
        float vx          { 0.0f };
        int   hp          { 1 };
        int   type        { 0 };      // 0=rookie, 1=vet, 2=boss
        float shootCd     { 0.0f };
        int   dropCash    { 10 };
        int   dropWeapon  { -1 };     // -1 if no weapon drop, otherwise idx
    };

    struct Bullet
    {
        float x   { 0.0f };
        float y   { 0.0f };
        float vx  { 0.0f };
        float vy  { 0.0f };
        int   dmg { 1 };
        bool  fromPlayer { true };
        float life { 2.0f };
        juce::Colour colour { juce::Colours::yellow };
    };

    enum class LootKind { Cash, Weapon, Health };
    struct Loot
    {
        float x         { 0.0f };
        float y         { 0.0f };
        float bounce    { 0.0f };     // vertical bobbing phase
        float life      { 10.0f };
        LootKind kind   { LootKind::Cash };
        int   value     { 0 };
        int   weaponIdx { 0 };
    };

    struct Cloud
    {
        float x { 0.0f };
        float y { 0.0f };
        float w { 0.0f };
        float speed { 0.0f };
    };

    struct FloatingText
    {
        float x, y, vy;
        float life;
        juce::String text;
        juce::Colour colour;
    };

    //==========================================================================
    // Save state (round-trips through plugin serialization)
    //==========================================================================
    struct SaveState
    {
        double money       { 0.0 };   // cash banked at base (safe)
        double totalEarned { 0.0 };   // lifetime earnings
        double stash       { 0.0 };   // unbanked cash carried by player
        int    bestWeapon  { 0 };     // highest weapon index ever owned
        int    deepestZone { 0 };     // furthest zone unlocked
        int    kills       { 0 };
        int    deaths      { 0 };
        int    prestige    { 0 };     // complete once → flourish in plugin UI
        int64_t lastSavedMs { 0 };

        juce::ValueTree toValueTree() const
        {
            juce::ValueTree vt ("TycoonState");
            vt.setProperty ("money",         money,         nullptr);
            vt.setProperty ("totalEarned",   totalEarned,   nullptr);
            vt.setProperty ("stash",         stash,         nullptr);
            vt.setProperty ("bestWeapon",    bestWeapon,    nullptr);
            vt.setProperty ("deepestZone",   deepestZone,   nullptr);
            vt.setProperty ("kills",         kills,         nullptr);
            vt.setProperty ("deaths",        deaths,        nullptr);
            vt.setProperty ("prestige",      prestige,      nullptr);
            vt.setProperty ("lastSavedMs",   (juce::int64) lastSavedMs, nullptr);
            // v5.2 compat: `count_3` = "reached COMEDIE zone" (1 if yes).
            // Processor reads this to unlock the ICE character.
            vt.setProperty ("count_3", (deepestZone >= 2 ? 1 : 0), nullptr);
            return vt;
        }

        void fromValueTree (const juce::ValueTree& vt)
        {
            if (! vt.isValid()) return;
            money       = (double) vt.getProperty ("money",       0.0);
            totalEarned = (double) vt.getProperty ("totalEarned", 0.0);
            stash       = (double) vt.getProperty ("stash",       0.0);
            bestWeapon  = (int)    vt.getProperty ("bestWeapon",  0);
            deepestZone = (int)    vt.getProperty ("deepestZone", 0);
            kills       = (int)    vt.getProperty ("kills",       0);
            deaths      = (int)    vt.getProperty ("deaths",      0);
            prestige    = (int)    vt.getProperty ("prestige",    0);
            lastSavedMs = (int64_t) (juce::int64) vt.getProperty ("lastSavedMs", 0);
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
            lastTickMs   = juce::Time::currentTimeMillis();
            cycleStartMs = lastTickMs;

            // Seed ambient clouds
            for (int i = 0; i < 4; ++i)
            {
                Cloud c;
                c.x = (float) rng.nextInt ((int) WORLD_WIDTH);
                c.y = 8.0f + (float) rng.nextInt (22);
                c.w = 16.0f + rng.nextFloat() * 22.0f;
                c.speed = 0.2f + rng.nextFloat() * 0.3f;
                clouds.push_back (c);
            }
            startTimerHz (30);
        }

        ~TycoonGame() override { stopTimer(); }

        //----- JUCE component -----
        void paint (juce::Graphics& g) override
        {
            drawBackground (g);
            drawWorldActors (g);
            drawHUD (g);
            drawFloaters (g);
            drawOfflineBanner (g);
        }

        void resized() override { layoutBounds(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto p = e.getPosition();

            // Drag handle first — top-right 14×14
            if (getDragHandleRect().contains (p))
            {
                draggingHandle = true;
                if (onDragHandleDown) onDragHandleDown (e);
                return;
            }

            // Game over / respawn prompt: tap anywhere to respawn
            if (player.hp <= 0)
            {
                respawnPlayer();
                return;
            }

            // Click falls inside the gameplay area (below top HUD)
            if (! gameplayRect.contains (p)) return;

            // Convert screen coords -> world coords (camera-adjusted)
            const float wx = (float) p.x + cameraX;

            // Did we click on an opp?
            for (auto& o : opps)
                if (std::abs (o.x - wx) < 9.0f && std::abs (o.y - (float) p.y) < 14.0f)
                {
                    // Close enough — player auto-shoots at it
                    shootAt (o.x, o.y);
                    return;
                }

            // Otherwise: click-to-move. Target = click world X.
            player.targetX = juce::jlimit (20.0f, WORLD_WIDTH - 20.0f, wx);
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
            // Restore player weapon to best ever owned
            player.weaponIdx = save.bestWeapon;
            // Offline progress: give a small base income so returning feels
            // rewarding, without making idle the main loop. Cap 10 min.
            if (save.lastSavedMs > 0)
            {
                const int64_t nowMs = juce::Time::currentTimeMillis();
                const double elapsedS = juce::jlimit (
                    0.0, 600.0, (double) (nowMs - save.lastSavedMs) / 1000.0);
                const double passive = save.deepestZone * 0.5; // $/sec while away
                const double earned  = passive * elapsedS;
                if (earned >= 1.0)
                {
                    save.money       += earned;
                    save.totalEarned += earned;
                    offlineBannerTimer  = 120;
                    offlineBannerAmount = earned;
                }
                save.lastSavedMs = nowMs;
            }
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

        Player             player;
        std::vector<Opp>   opps;
        std::vector<Bullet> bullets;
        std::vector<Loot>   loot;
        std::vector<Cloud>  clouds;
        std::vector<FloatingText> floaters;

        float audioActivity { 0.0f };
        float cameraX       { 0.0f };
        int64_t lastTickMs  { 0 };
        int64_t cycleStartMs { 0 };
        float framesSinceSpawn { 0.0f };
        float screenShake { 0.0f };
        bool  draggingHandle { false };

        int    offlineBannerTimer  { 0 };
        double offlineBannerAmount { 0.0 };

        juce::Random rng;

        // Rects
        juce::Rectangle<int> hudRect;
        juce::Rectangle<int> gameplayRect;

        //======================================================================
        // Constants
        //======================================================================
        static constexpr float GROUND_Y   = 155.0f;   // world y of the street line
        static constexpr float PLAYER_SPD = 1.4f;     // px / frame at 30 Hz
        static constexpr float BULLET_SPD = 6.0f;
        static constexpr int   MAX_OPPS   = 16;
        static constexpr int   MAX_LOOT   = 48;

        //======================================================================
        // Layout
        //======================================================================
        void layoutBounds()
        {
            const auto b = getLocalBounds();
            hudRect      = b.withHeight (18);
            gameplayRect = b.withTrimmedTop (18);
        }

        //======================================================================
        // Game actions
        //======================================================================
        void shootAt (float worldX, float worldY)
        {
            const auto& w = getWeapons()[(size_t) player.weaponIdx];
            if (player.shootCd > 0.0f) return;

            // Turn to face the target
            player.dir = (worldX < player.x) ? 1 : 0;

            // Fire a bullet toward the target
            Bullet b;
            b.x = player.x + (player.dir == 0 ? 6.0f : -6.0f);
            b.y = player.y - 6.0f;
            const float dx = worldX - b.x;
            const float dy = worldY - b.y;
            const float len = std::sqrt (dx * dx + dy * dy);
            const float inv = len > 0.1f ? 1.0f / len : 1.0f;
            b.vx = dx * inv * BULLET_SPD;
            b.vy = dy * inv * BULLET_SPD;
            b.dmg = w.damage;
            b.fromPlayer = true;
            b.life = juce::jmin (2.0f, w.rangePx / (BULLET_SPD * 30.0f));
            b.colour = w.tracerColour;
            bullets.push_back (b);

            player.shootCd = (float) w.cooldownMs / 1000.0f;
        }

        void killOpp (size_t i)
        {
            auto& o = opps[i];
            save.kills++;

            // Drop cash (1-3 coins per opp depending on type)
            const int nCoins = 1 + o.type + rng.nextInt (2);
            for (int k = 0; k < nCoins && (int) loot.size() < MAX_LOOT; ++k)
            {
                Loot c;
                c.x = o.x + (rng.nextFloat() - 0.5f) * 14.0f;
                c.y = o.y - 4.0f;
                c.kind = LootKind::Cash;
                c.value = o.dropCash + rng.nextInt (o.dropCash + 1);
                c.life = 12.0f;
                c.bounce = rng.nextFloat() * juce::MathConstants<float>::twoPi;
                loot.push_back (c);
            }

            // Weapon drop
            if (o.dropWeapon >= 0 && o.dropWeapon < NUM_WEAPONS
                && (int) loot.size() < MAX_LOOT)
            {
                Loot w;
                w.x = o.x;
                w.y = o.y - 6.0f;
                w.kind = LootKind::Weapon;
                w.weaponIdx = o.dropWeapon;
                w.life = 20.0f;
                loot.push_back (w);
            }

            // Health pack (low chance)
            if (rng.nextFloat() < 0.06f && (int) loot.size() < MAX_LOOT)
            {
                Loot h;
                h.x = o.x;
                h.y = o.y - 6.0f;
                h.kind = LootKind::Health;
                h.value = 25;
                h.life = 15.0f;
                loot.push_back (h);
            }

            // +$ floater in HUD-space — we spawn screen-space via camera conversion
            spawnFloater (juce::String ("+$") + juce::String (o.dropCash),
                          o.x - cameraX, o.y, juce::Colour (0xFFFFEE58));

            opps.erase (opps.begin() + (std::ptrdiff_t) i);
        }

        void damagePlayer (int dmg)
        {
            player.hp -= dmg;
            player.hurtFlash = 1.0f;
            screenShake = 6.0f;
            if (player.hp <= 0)
            {
                // Die: lose half stash
                const double lost = save.stash * 0.5;
                save.stash -= lost;
                save.deaths++;
                spawnFloater ("YOU DIED",
                              (float) getWidth() * 0.5f, 40.0f,
                              juce::Colour (0xFFFF5252));
                if (lost > 0.0)
                    spawnFloater (juce::String ("-$") + juce::String (lost, 0),
                                  (float) getWidth() * 0.5f, 56.0f,
                                  juce::Colour (0xFFFF5252));
            }
        }

        void respawnPlayer()
        {
            player.hp = player.maxHp;
            player.x  = 40.0f;
            player.targetX = player.x;
            player.hurtFlash = 0.0f;
            opps.clear();
            bullets.clear();
        }

        void collectLoot (size_t i)
        {
            auto& l = loot[i];
            switch (l.kind)
            {
                case LootKind::Cash:
                {
                    save.stash        += l.value;
                    save.totalEarned  += l.value;
                    spawnFloater (juce::String ("+$") + juce::String (l.value),
                                  l.x - cameraX, l.y - 6.0f,
                                  juce::Colour (0xFFFFEE58));
                    break;
                }
                case LootKind::Weapon:
                {
                    if (l.weaponIdx > player.weaponIdx)
                    {
                        player.weaponIdx = l.weaponIdx;
                        if (l.weaponIdx > save.bestWeapon)
                            save.bestWeapon = l.weaponIdx;
                        spawnFloater (juce::String ("+ ") + getWeapons()[(size_t) l.weaponIdx].name,
                                      l.x - cameraX, l.y - 6.0f,
                                      juce::Colour (0xFFC6FF00));
                    }
                    break;
                }
                case LootKind::Health:
                {
                    player.hp = juce::jmin (player.maxHp, player.hp + l.value);
                    spawnFloater (juce::String ("+") + juce::String (l.value) + " HP",
                                  l.x - cameraX, l.y - 6.0f,
                                  juce::Colour (0xFFFF4081));
                    break;
                }
            }
            loot.erase (loot.begin() + (std::ptrdiff_t) i);
        }

        // Deposit stash when player is at the base building (x < 50)
        void tryBankAtBase()
        {
            if (player.x < 50.0f && save.stash >= 1.0)
            {
                save.money += save.stash;
                spawnFloater (juce::String ("BANKED $") + juce::String (save.stash, 0),
                              60.0f, 70.0f,
                              juce::Colour (0xFFC6FF00));
                save.stash = 0.0;
                // Check if prestige triggered (banked >= 1M + killed at least 1 boss)
                if (save.money >= 1000000.0 && save.kills >= 50 && save.prestige < 1)
                    save.prestige = 1;
            }
        }

        void spawnFloater (const juce::String& text, float screenX, float screenY,
                           juce::Colour col)
        {
            floaters.push_back ({ screenX, screenY, -1.2f, 1.0f, text, col });
            if (floaters.size() > 40)
                floaters.erase (floaters.begin());
        }

        void spawnOpp()
        {
            if ((int) opps.size() >= MAX_OPPS) return;
            Opp o;
            const float fromLeft = rng.nextBool() ? 0.0f : WORLD_WIDTH;
            o.x = fromLeft == 0.0f ? -8.0f : WORLD_WIDTH + 8.0f;
            o.y = GROUND_Y - 10.0f;
            o.vx = fromLeft == 0.0f ? 0.6f : -0.6f;

            // Type & progression scaling
            const int zoneIdx = getCurrentZoneIdx();
            const float r = rng.nextFloat();
            if (r < 0.05f + zoneIdx * 0.03f)
            {
                o.type = 2;                  // boss
                o.hp = 10 + zoneIdx * 6;
                o.dropCash = 150 + zoneIdx * 200;
                o.dropWeapon = juce::jmin (NUM_WEAPONS - 1, 1 + zoneIdx + rng.nextInt (2));
                o.vx *= 0.65f;
            }
            else if (r < 0.30f + zoneIdx * 0.06f)
            {
                o.type = 1;                  // vet
                o.hp = 3 + zoneIdx;
                o.dropCash = 35 + zoneIdx * 40;
                o.dropWeapon = (rng.nextFloat() < 0.15f + zoneIdx * 0.05f)
                                ? juce::jmin (NUM_WEAPONS - 1, zoneIdx + rng.nextInt (2))
                                : -1;
            }
            else
            {
                o.type = 0;                  // rookie
                o.hp = 1;
                o.dropCash = 10 + zoneIdx * 15;
                o.dropWeapon = (rng.nextFloat() < 0.04f) ? (rng.nextInt (2) + 1) : -1;
            }
            opps.push_back (o);
        }

        int getCurrentZoneIdx() const noexcept
        {
            for (int i = NUM_ZONES - 1; i >= 0; --i)
                if (player.x >= getZones()[(size_t) i].startX) return i;
            return 0;
        }

        //======================================================================
        // Tick
        //======================================================================
        void timerCallback() override
        {
            const int64_t nowMs = juce::Time::currentTimeMillis();
            float dtS = (float) (nowMs - lastTickMs) / 1000.0f;
            if (dtS > 0.1f) dtS = 0.1f;
            lastTickMs = nowMs;

            // Player move toward targetX
            const float diff = player.targetX - player.x;
            if (std::abs (diff) > 1.0f)
            {
                const float step = juce::jlimit (-PLAYER_SPD, PLAYER_SPD, diff);
                player.x += step;
                player.dir = (step < 0.0f) ? 1 : 0;
                player.walkPhase += 0.25f;
                if (player.walkPhase > juce::MathConstants<float>::twoPi)
                    player.walkPhase -= juce::MathConstants<float>::twoPi;
            }

            // Zone unlock tracking
            {
                const int z = getCurrentZoneIdx();
                if (z > save.deepestZone
                    && save.money >= getZones()[(size_t) z].unlockBankedCash)
                {
                    save.deepestZone = z;
                    spawnFloater (juce::String ("UNLOCKED ") + getZones()[(size_t) z].name,
                                  (float) getWidth() * 0.5f, 40.0f,
                                  juce::Colour (0xFFC6FF00));
                }
                else if (z > save.deepestZone + 0)
                {
                    // Too hostile — kick player back
                    const float lockX = getZones()[(size_t) (save.deepestZone + 1)].startX - 8.0f;
                    player.x = juce::jmin (player.x, lockX);
                    player.targetX = player.x;
                    spawnFloater (juce::String ("NEED $")
                                  + juce::String (getZones()[(size_t) (save.deepestZone + 1)].unlockBankedCash),
                                  (float) getWidth() * 0.5f, 40.0f,
                                  juce::Colour (0xFFFF5252));
                }
            }

            // Opp spawn pacing — faster as progression goes up
            const float spawnIntervalS = juce::jmax (1.0f,
                5.0f - (float) save.deepestZone * 0.8f - juce::jmin (4.0f, (float) save.kills * 0.01f));
            framesSinceSpawn += dtS;
            if (framesSinceSpawn >= spawnIntervalS && player.hp > 0)
            {
                spawnOpp();
                framesSinceSpawn = 0.0f;
            }

            // Advance opps
            for (auto& o : opps)
            {
                o.x += o.vx;
                // Simple contact damage if opp reaches player
                if (std::abs (o.x - player.x) < 10.0f && std::abs (o.y - player.y) < 16.0f)
                {
                    o.vx = 0.0f; // stop
                    o.shootCd -= dtS;
                    if (o.shootCd <= 0.0f)
                    {
                        damagePlayer (o.type == 2 ? 15 : (o.type == 1 ? 8 : 3));
                        o.shootCd = 0.8f;
                    }
                }
            }
            // Purge off-screen (past far edge) opps quietly
            opps.erase (std::remove_if (opps.begin(), opps.end(),
                [] (const Opp& o) { return o.x < -20.0f || o.x > WORLD_WIDTH + 20.0f; }),
                opps.end());

            // Advance bullets
            for (auto& b : bullets)
            {
                b.x += b.vx;
                b.y += b.vy;
                b.life -= dtS;
            }
            // Bullet → opp hit test
            for (auto it = bullets.begin(); it != bullets.end();)
            {
                bool hit = false;
                if (it->fromPlayer)
                {
                    for (size_t j = 0; j < opps.size(); ++j)
                    {
                        auto& o = opps[j];
                        if (std::abs (it->x - o.x) < 6.0f && std::abs (it->y - o.y) < 10.0f)
                        {
                            o.hp -= it->dmg;
                            if (o.hp <= 0) { killOpp (j); break; }
                            hit = true;
                            break;
                        }
                    }
                }
                if (hit || it->life <= 0.0f)
                    it = bullets.erase (it);
                else
                    ++it;
            }
            // Limit bullets
            if (bullets.size() > 32)
                bullets.erase (bullets.begin(), bullets.begin()
                               + (std::ptrdiff_t) (bullets.size() - 32));

            // Advance loot (bobbing + pickup)
            for (size_t i = 0; i < loot.size(); )
            {
                auto& l = loot[i];
                l.bounce += dtS * 6.0f;
                l.life   -= dtS;
                // Auto-collect if player overlaps
                if (std::abs (l.x - player.x) < 10.0f && std::abs (l.y - player.y) < 14.0f)
                {
                    collectLoot (i);
                    continue;
                }
                if (l.life <= 0.0f)
                    loot.erase (loot.begin() + (std::ptrdiff_t) i);
                else
                    ++i;
            }

            // Auto-bank when at base
            tryBankAtBase();

            // Shoot cooldown + hurt fade
            player.shootCd   = juce::jmax (0.0f, player.shootCd - dtS);
            player.hurtFlash = juce::jmax (0.0f, player.hurtFlash - dtS * 2.0f);
            screenShake      = juce::jmax (0.0f, screenShake - dtS * 24.0f);

            // Camera follows player (viewport half = getWidth/2)
            const float targetCam = player.x - (float) getWidth() * 0.5f;
            cameraX += (targetCam - cameraX) * 0.15f;
            cameraX = juce::jlimit (0.0f,
                                     WORLD_WIDTH - (float) getWidth(),
                                     cameraX);

            // Clouds drift
            for (auto& c : clouds)
            {
                c.x -= c.speed;
                if (c.x + c.w < 0.0f) c.x = WORLD_WIDTH;
            }

            // Floaters
            for (auto& f : floaters)
            {
                f.y  += f.vy;
                f.life -= dtS / 1.4f;
            }
            floaters.erase (std::remove_if (floaters.begin(), floaters.end(),
                [] (const FloatingText& f) { return f.life <= 0.0f; }), floaters.end());

            if (offlineBannerTimer > 0) --offlineBannerTimer;

            save.lastSavedMs = nowMs;
            repaint();
        }

        //======================================================================
        // Helpers
        //======================================================================
        static juce::String formatMoney (double a)
        {
            if (a < 1000.0)       return juce::String ((int) a);
            if (a < 1000000.0)    return juce::String (a / 1000.0, 1) + "K";
            if (a < 1000000000.0) return juce::String (a / 1000000.0, 2) + "M";
            return juce::String (a / 1000000000.0, 2) + "B";
        }

        float getDayNightPhase() const noexcept
        {
            const double periodMs = 4.0 * 60.0 * 1000.0;
            const double t = (double) ((juce::Time::currentTimeMillis() - cycleStartMs)
                                        % (int64_t) periodMs);
            return (float) (t / periodMs);
        }

        // Returns screen X for a given world X, applying camera.
        float w2sX (float worldX) const noexcept { return worldX - cameraX; }

        //======================================================================
        // Rendering
        //======================================================================
        void drawBackground (juce::Graphics& g)
        {
            const float phase = getDayNightPhase();
            const bool isNight = (phase > 0.55f && phase < 0.95f);

            // Interpolate zone palette as the camera crosses boundaries.
            const float midX = cameraX + (float) getWidth() * 0.5f;
            const auto& zs = getZones();
            int zi = 0;
            for (int i = NUM_ZONES - 1; i >= 0; --i)
                if (midX >= zs[(size_t) i].startX) { zi = i; break; }
            const auto& z = zs[(size_t) zi];

            // Sky gradient with night shift
            auto skyT = z.skyTop;
            auto skyB = z.skyBot;
            if (isNight)
            {
                skyT = skyT.darker (0.5f);
                skyB = skyB.darker (0.5f);
            }
            juce::ColourGradient sky (skyT, 0.0f, 0.0f,
                                       skyB, 0.0f, (float) getHeight(), false);
            g.setGradientFill (sky);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

            // Stars at night
            if (isNight)
            {
                juce::Random starRng (1234);
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                for (int i = 0; i < 40; ++i)
                {
                    const int sx = starRng.nextInt (getWidth());
                    const int sy = starRng.nextInt (getHeight() / 3);
                    const float tw = 0.5f + 0.5f * std::sin (phase * 40.0f + (float) i);
                    g.setColour (juce::Colours::white.withAlpha (0.3f + 0.4f * tw));
                    g.fillRect ((float) sx, (float) sy, 1.0f, 1.0f);
                }
            }

            // Clouds (parallax)
            for (const auto& c : clouds)
            {
                const float sx = c.x - cameraX * 0.4f;
                if (sx + c.w < 0 || sx > (float) getWidth()) continue;
                g.setColour (juce::Colours::white.withAlpha (isNight ? 0.10f : 0.22f));
                g.fillRoundedRectangle (sx, c.y, c.w, 6.0f, 3.0f);
                g.fillRoundedRectangle (sx + 4.0f, c.y - 2.5f, c.w * 0.7f, 6.0f, 3.0f);
            }

            // Distant buildings — simple silhouettes (parallax × 0.55)
            drawBackgroundBuildings (g, isNight);

            // Road / ground
            const float groundScreenY = GROUND_Y;
            g.setColour (z.ground);
            g.fillRect (0.0f, groundScreenY, (float) getWidth(),
                        (float) getHeight() - groundScreenY);

            // Yellow road dashes
            g.setColour (juce::Colour (0xFFFFEB3B).withAlpha (0.6f));
            for (int i = 0; i < 30; ++i)
            {
                const float wx = (float) i * 32.0f;
                const float sx = wx - cameraX;
                if (sx < -8 || sx > (float) getWidth()) continue;
                g.fillRect (sx, groundScreenY + 10.0f, 14.0f, 2.0f);
            }

            // Gold border pulsing with audio
            const float pulse = 0.35f + audioActivity * 0.5f;
            g.setColour (juce::Colour (0xFFC6FF00).withAlpha (pulse));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.2f);
        }

        void drawBackgroundBuildings (juce::Graphics& g, bool isNight)
        {
            // Deterministic silhouettes positioned every 60 world-px
            for (int i = 0; i < (int) (WORLD_WIDTH / 60.0f) + 1; ++i)
            {
                const float wx  = (float) i * 60.0f;
                const float sx  = (wx - cameraX * 0.55f);
                if (sx < -40.0f || sx > (float) getWidth() + 40.0f) continue;

                const int h = 40 + (i * 37) % 46;
                const int w = 35 + (i * 19) % 18;
                const float by = GROUND_Y - (float) h;

                const juce::Colour silCol = isNight ? juce::Colour (0xFF0A0F18)
                                                     : juce::Colour (0xFF1A2032);
                g.setColour (silCol);
                g.fillRect (sx, by, (float) w, (float) h);

                // Windows (4 cols × N rows) — only lit at night for flavor
                const int rows = h / 10;
                for (int r = 0; r < rows; ++r)
                    for (int c = 0; c < 4; ++c)
                    {
                        const bool lit = isNight && ((i + r + c) * 7 % 5 != 0);
                        g.setColour (lit ? juce::Colour (0xFFFFEB3B).withAlpha (0.8f)
                                          : silCol.brighter (0.1f));
                        g.fillRect (sx + 3.0f + (float) c * ((w - 6.0f) / 4.0f),
                                    by + 4.0f + (float) r * 10.0f,
                                    3.0f, 3.0f);
                    }
            }
        }

        //----------------------------------------------------------------------
        void drawWorldActors (juce::Graphics& g)
        {
            // BASE HOUSE (left edge) — "Trap house" where you bank cash
            {
                const float bx = 20.0f - cameraX;
                if (bx > -40.0f && bx < (float) getWidth())
                {
                    const float by = GROUND_Y - 26.0f;
                    g.setColour (juce::Colour (0xFF3A2A1E));
                    g.fillRect (bx, by + 6.0f, 30.0f, 20.0f);
                    juce::Path roof;
                    roof.addTriangle (bx, by + 6.0f, bx + 30.0f, by + 6.0f,
                                      bx + 15.0f, by - 2.0f);
                    g.setColour (juce::Colour (0xFF1A0F0A));
                    g.fillPath (roof);
                    // Window lit
                    g.setColour (juce::Colour (0xFFFFEB3B));
                    g.fillRect (bx + 12.0f, by + 10.0f, 6.0f, 5.0f);
                    // "BANK" label
                    g.setColour (juce::Colour (0xFFC6FF00));
                    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
                    g.drawFittedText ("BANK",
                                      juce::Rectangle<int> ((int) bx, (int) (by - 10.0f), 30, 7),
                                      juce::Justification::centred, 1);
                }
            }

            // Zone transition markers
            for (size_t i = 1; i < (size_t) NUM_ZONES; ++i)
            {
                const auto& z = getZones()[i];
                const float sx = z.startX - cameraX;
                if (sx < -10.0f || sx > (float) getWidth() + 10.0f) continue;
                g.setColour (juce::Colour (0xFFCDDC39).withAlpha (0.25f));
                g.fillRect (sx - 1.0f, GROUND_Y - 60.0f, 2.0f, 60.0f);
                g.setColour (juce::Colour (0xFFCDDC39));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
                g.drawFittedText (z.name,
                                  juce::Rectangle<int> ((int) (sx - 30.0f),
                                                         (int) (GROUND_Y - 72.0f), 60, 8),
                                  juce::Justification::centred, 1);
            }

            // Loot first so they sit under actors
            for (const auto& l : loot)
            {
                const float sx = l.x - cameraX;
                if (sx < -10 || sx > (float) getWidth() + 10) continue;
                const float by = l.y + std::sin (l.bounce) * 1.5f;
                const float a  = juce::jmin (1.0f, l.life / 3.0f);
                switch (l.kind)
                {
                    case LootKind::Cash:
                        g.setColour (juce::Colour (0xFFFFEB3B).withAlpha (a));
                        g.fillEllipse (sx - 3.0f, by - 3.0f, 6.0f, 6.0f);
                        g.setColour (juce::Colour (0xFFFFF176).withAlpha (a));
                        g.fillEllipse (sx - 1.5f, by - 2.0f, 2.0f, 2.0f);
                        break;
                    case LootKind::Weapon:
                        g.setColour (juce::Colour (0xFFC6FF00).withAlpha (a));
                        g.fillRect (sx - 4.0f, by - 1.0f, 8.0f, 2.0f);
                        g.fillRect (sx - 2.0f, by - 3.0f, 3.0f, 2.0f);
                        // Glow
                        g.setColour (juce::Colour (0xFFC6FF00).withAlpha (a * 0.25f));
                        g.fillEllipse (sx - 8.0f, by - 6.0f, 16.0f, 12.0f);
                        break;
                    case LootKind::Health:
                        g.setColour (juce::Colour (0xFFFF4081).withAlpha (a));
                        g.fillRect (sx - 3.0f, by - 1.0f, 6.0f, 2.0f);
                        g.fillRect (sx - 1.0f, by - 3.0f, 2.0f, 6.0f);
                        break;
                }
            }

            // Opps
            for (const auto& o : opps)
            {
                const float sx = o.x - cameraX;
                if (sx < -20 || sx > (float) getWidth() + 20) continue;
                drawOpp (g, sx, o.y, o.type);
            }

            // Bullets
            for (const auto& b : bullets)
            {
                const float sx = b.x - cameraX;
                if (sx < -4 || sx > (float) getWidth() + 4) continue;
                g.setColour (b.colour);
                g.fillRect (sx - 1.5f, b.y - 1.0f, 3.0f, 2.0f);
                // Tracer glow
                g.setColour (b.colour.withAlpha (0.35f));
                g.fillRect (sx - 3.5f, b.y - 0.5f, 7.0f, 1.0f);
            }

            // Player
            {
                const float sx = player.x - cameraX;
                drawPlayer (g, sx, player.y);

                // Dim screen red on hurt
                if (player.hurtFlash > 0.01f)
                {
                    g.setColour (juce::Colour (0xFFFF1744).withAlpha (player.hurtFlash * 0.18f));
                    g.fillRect (gameplayRect);
                }
            }

            // Game over overlay
            if (player.hp <= 0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.fillRect (gameplayRect);
                g.setColour (juce::Colour (0xFFFF5252));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::bold));
                g.drawFittedText ("YOU'RE DONE",
                                  gameplayRect.withHeight (20).translated (0, 40),
                                  juce::Justification::centred, 1);
                g.setColour (juce::Colour (0xFFFFEE58));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain));
                g.drawFittedText ("tap to respawn",
                                  gameplayRect.withHeight (14).translated (0, 66),
                                  juce::Justification::centred, 1);
            }
        }

        void drawPlayer (juce::Graphics& g, float sx, float y)
        {
            // 8x12 pixel sprite (roughly). Legs animate via walkPhase.
            const int leg = (int) (std::sin (player.walkPhase) * 1.0f);
            const juce::Colour skin  (0xFFEAC6A9);
            const juce::Colour hood  (0xFF1B5E20);
            const juce::Colour pants (0xFF212121);
            const juce::Colour chain (0xFFFFD700);

            // Body
            g.setColour (hood);
            g.fillRect (sx - 4.0f, y - 12.0f, 8.0f, 8.0f);
            // Head
            g.setColour (skin);
            g.fillRect (sx - 3.0f, y - 18.0f, 6.0f, 6.0f);
            // Hood top
            g.setColour (hood);
            g.fillRect (sx - 4.0f, y - 20.0f, 8.0f, 3.0f);
            // Eyes (direction aware)
            g.setColour (juce::Colour (0xFF0A1000));
            if (player.dir == 0)
                g.fillRect (sx + 0.0f, y - 15.0f, 1.0f, 1.0f);
            else
                g.fillRect (sx - 1.0f, y - 15.0f, 1.0f, 1.0f);
            // Chain
            g.setColour (chain);
            g.fillRect (sx - 2.0f, y - 6.0f, 4.0f, 1.0f);
            // Legs
            g.setColour (pants);
            g.fillRect (sx - 3.0f, y - 4.0f, 2.0f, 4.0f + (float) leg);
            g.fillRect (sx + 1.0f, y - 4.0f, 2.0f, 4.0f - (float) leg);

            // Weapon sprite in hand (if armed)
            if (player.weaponIdx > 0)
            {
                const int dir = (player.dir == 0) ? 1 : -1;
                const float wx = sx + (float) dir * 4.0f;
                const float wy = y - 10.0f;
                const auto wcol = getWeapons()[(size_t) player.weaponIdx].tracerColour.darker (0.2f);
                g.setColour (wcol);
                g.fillRect (wx - (dir > 0 ? 0.0f : 5.0f), wy, 5.0f, 2.0f);
                g.setColour (juce::Colour (0xFF212121));
                g.fillRect (wx + (dir > 0 ? 1.0f : -2.0f), wy + 2.0f, 1.0f, 2.0f);
            }
        }

        void drawOpp (juce::Graphics& g, float sx, float y, int type)
        {
            // Rookie = red, Vet = darker red with bandana, Boss = gold crown
            const juce::Colour skin  (0xFFD7CCC8);
            const juce::Colour shirt = type == 2 ? juce::Colour (0xFF6A1B9A)
                                      : type == 1 ? juce::Colour (0xFFB71C1C)
                                                   : juce::Colour (0xFFE53935);
            const juce::Colour pants (0xFF424242);

            // Body
            g.setColour (shirt);
            g.fillRect (sx - 4.0f, y - 12.0f, 8.0f, 8.0f);
            // Head
            g.setColour (skin);
            g.fillRect (sx - 3.0f, y - 18.0f, 6.0f, 6.0f);
            // Hair / cap
            g.setColour (juce::Colour (0xFF0A1000));
            g.fillRect (sx - 4.0f, y - 19.0f, 8.0f, 2.0f);
            if (type == 1)
            {
                // Bandana
                g.setColour (juce::Colour (0xFFB71C1C));
                g.fillRect (sx - 4.0f, y - 17.0f, 8.0f, 1.0f);
            }
            if (type == 2)
            {
                // Gold crown
                g.setColour (juce::Colour (0xFFFFD700));
                g.fillRect (sx - 4.0f, y - 22.0f, 8.0f, 2.0f);
                g.fillRect (sx - 3.0f, y - 24.0f, 1.0f, 2.0f);
                g.fillRect (sx + 2.0f, y - 24.0f, 1.0f, 2.0f);
                g.fillRect (sx - 0.5f, y - 24.0f, 1.0f, 2.0f);
            }
            // Eyes red
            g.setColour (juce::Colour (0xFFFF5252));
            g.fillRect (sx - 1.5f, y - 15.0f, 1.0f, 1.0f);
            g.fillRect (sx + 0.5f, y - 15.0f, 1.0f, 1.0f);
            // Legs
            g.setColour (pants);
            g.fillRect (sx - 3.0f, y - 4.0f, 2.0f, 4.0f);
            g.fillRect (sx + 1.0f, y - 4.0f, 2.0f, 4.0f);

            // Health bar above (only if > rookie)
            if (type > 0)
            {
                const int maxHp = (type == 2) ? 10 : 3;
                // (we don't know current HP here cheaply; a stylised bar)
                g.setColour (juce::Colour (0xFF0A1000).withAlpha (0.7f));
                g.fillRect (sx - 4.0f, y - 26.0f, 8.0f, 2.0f);
                g.setColour (juce::Colour (0xFFFF5252));
                g.fillRect (sx - 4.0f, y - 26.0f, (float) maxHp * 0.7f, 2.0f);
                juce::ignoreUnused (maxHp);
            }
        }

        //----------------------------------------------------------------------
        void drawHUD (juce::Graphics& g)
        {
            // Dim HUD backdrop
            g.setColour (juce::Colour (0xFF0A1000).withAlpha (0.78f));
            g.fillRect (hudRect);

            auto r = hudRect.reduced (4, 2);

            // Stash (unbanked)
            g.setColour (juce::Colour (0xFFFFEE58));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
            g.drawText (juce::String ("STASH $") + formatMoney (save.stash),
                        r.withWidth (90), juce::Justification::centredLeft);
            // Banked
            g.setColour (juce::Colour (0xFFC6FF00));
            g.drawText (juce::String ("BANK $") + formatMoney (save.money),
                        r.withX (r.getX() + 92).withWidth (90), juce::Justification::centredLeft);

            // HP bar
            const auto hpR = juce::Rectangle<int> (r.getX() + 186, r.getY() + 3, 54, 8);
            g.setColour (juce::Colour (0xFF0A1000));
            g.fillRoundedRectangle (hpR.toFloat(), 2.0f);
            const float hpF = juce::jlimit (0.0f, 1.0f, (float) player.hp / (float) player.maxHp);
            g.setColour (juce::Colour (0xFFFF5252).interpolatedWith (juce::Colour (0xFFC6FF00), hpF));
            g.fillRoundedRectangle (hpR.toFloat().withWidth ((float) hpR.getWidth() * hpF), 2.0f);
            g.setColour (juce::Colour (0xFFF1F8E9).withAlpha (0.6f));
            g.drawRoundedRectangle (hpR.toFloat(), 2.0f, 1.0f);

            // Weapon badge
            const auto& w = getWeapons()[(size_t) player.weaponIdx];
            const auto wR = juce::Rectangle<int> (r.getX() + 246, r.getY() + 2, 80, 12);
            g.setColour (w.tracerColour.withAlpha (0.35f));
            g.fillRoundedRectangle (wR.toFloat(), 2.0f);
            g.setColour (juce::Colour (0xFFF1F8E9));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
            g.drawFittedText (w.name, wR, juce::Justification::centred, 1);

            // Zone name (right side)
            const int zi = getCurrentZoneIdx();
            g.setColour (juce::Colour (0xFFCDDC39));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
            g.drawText (getZones()[(size_t) zi].name,
                        r.withX (r.getRight() - 72).withWidth (56),
                        juce::Justification::centredRight);

            // Drag handle
            drawDragHandle (g, getDragHandleRect());
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

        void drawOfflineBanner (juce::Graphics& g)
        {
            if (offlineBannerTimer <= 0 || offlineBannerAmount <= 0.0) return;
            const float alpha = juce::jmin (1.0f, (float) offlineBannerTimer / 30.0f);
            const auto b = juce::Rectangle<int> (
                getWidth() / 2 - 70, 24, 140, 14);
            g.setColour (juce::Colour (0xFF0A1000).withAlpha (0.9f * alpha));
            g.fillRoundedRectangle (b.toFloat(), 3.0f);
            g.setColour (juce::Colour (0xFFFFEB3B).withAlpha (alpha));
            g.drawRoundedRectangle (b.toFloat(), 3.0f, 1.0f);
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
            g.drawFittedText ("WELCOME BACK +$" + formatMoney (offlineBannerAmount),
                              b, juce::Justification::centred, 1);
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TycoonGame)
    };
} // namespace th::game
