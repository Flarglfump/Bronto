/*
 * POP DEFENSE - what things ARE.
 *
 * Every number that tunes the game lives in this file: balloon layers,
 * towers, their upgrades and the 30 rounds. Change a number here and the
 * rest of the game follows.
 */
#include "td.h"

/* ========================================================================
 * Balloons
 *
 * A balloon is a stack of layers: popping one releases its children. Speed
 * is relative to a red balloon (50 path pixels per second).
 * ======================================================================== */
#define U 50.0f
#define NO_KIDS 0, { TD_RED, TD_RED, TD_RED, TD_RED }
#define KIDS1(a) 1, { a, a, a, a }
#define KIDS2(a, b) 2, { a, b, a, b }
#define KIDS4(a) 4, { a, a, a, a }

static const td_balloon_def k_balloons[TD_BALLOON_TYPE_COUNT] = {
    /*   name       speed     radius hp  immune to                                         children */
    { "Red",     1.0f * U,  7.0f,  1, 0,                                                  NO_KIDS },
    { "Blue",    1.4f * U,  7.5f,  1, 0,                                                  KIDS1(TD_RED) },
    { "Green",   1.8f * U,  8.0f,  1, 0,                                                  KIDS1(TD_BLUE) },
    { "Yellow",  2.8f * U,  8.5f,  1, 0,                                                  KIDS1(TD_GREEN) },
    { "Pink",    3.0f * U,  8.5f,  1, 0,                                                  KIDS1(TD_YELLOW) },
    { "Black",   1.8f * U,  5.5f,  1, TD_IMMUNE(TD_DMG_EXPLOSIVE),                        KIDS2(TD_PINK, TD_PINK) },
    { "White",   2.0f * U,  5.5f,  1, TD_IMMUNE(TD_DMG_COLD),                             KIDS2(TD_PINK, TD_PINK) },
    { "Lead",    1.0f * U,  8.5f,  1, TD_IMMUNE(TD_DMG_SHARP),                            KIDS2(TD_BLACK, TD_BLACK) },
    { "Zebra",   1.8f * U,  8.0f,  1, TD_IMMUNE(TD_DMG_EXPLOSIVE) | TD_IMMUNE(TD_DMG_COLD), KIDS2(TD_BLACK, TD_WHITE) },
    { "Rainbow", 2.2f * U,  9.0f,  1, 0,                                                  KIDS2(TD_ZEBRA, TD_ZEBRA) },
    { "Ceramic", 2.5f * U,  9.5f, 10, 0,                                                  KIDS2(TD_RAINBOW, TD_RAINBOW) },
    { "Blimp",   1.0f * U, 20.0f, 200, TD_IMMUNE(TD_DMG_COLD),                            KIDS4(TD_CERAMIC) },
};

/* ========================================================================
 * Towers and upgrades
 *
 * Each upgrade is a small function that changes a tower's stats. A tower's
 * stats are its base stats with every bought upgrade applied in order, so
 * upgrades on the two paths combine naturally.
 * ======================================================================== */
static int max_i(int a, int b) { return a > b ? a : b; }

/* --- Dart Tower: cheap, single target, upgrades into pierce or volume --- */
static void up_sharp_darts(td_stats *s)  { s->pierce += 1; }
static void up_razor_darts(td_stats *s)  { s->pierce += 3; }
static void up_spike_ball(td_stats *s)   { s->proj = TD_PROJ_SPIKEBALL; s->pierce = max_i(s->pierce, 18); s->damage += 1;
                                           s->proj_radius = 7; s->speed = 330; s->range += 6; }
static void up_quick_shots(td_stats *s)  { s->interval *= 0.85f; }
static void up_eagle_eye(td_stats *s)    { s->range += 20; s->camo = true; }
static void up_triple_shot(td_stats *s)  { s->count = 3; }

/* --- Tack Tower: short range, sprays in all directions --- */
static void up_faster_tacks(td_stats *s) { s->interval *= 0.75f; }
static void up_even_faster(td_stats *s)  { s->interval *= 0.70f; }
static void up_blade_storm(td_stats *s)  { s->count = max_i(s->count, 16); s->pierce += 2; s->interval *= 0.85f; }
static void up_long_tacks(td_stats *s)   { s->range += 12; s->pierce += 1; }
static void up_more_tacks(td_stats *s)   { s->count = max_i(s->count, 12); }
static void up_ring_of_fire(td_stats *s) { s->attack = TD_ATK_PULSE; s->damage_type = TD_DMG_NORMAL; s->pierce = 80;
                                           s->range += 8; s->interval *= 0.6f; }

/* --- Bomb Tower: explosions pop lead but not black or zebra --- */
static void up_bigger_bombs(td_stats *s) { s->splash += 8; s->splash_pierce += 10; }
static void up_heavy_bombs(td_stats *s)  { s->damage += 1; }
static void up_concussion(td_stats *s)   { s->stun = 0.7f; s->damage += 1; s->splash_pierce += 10; }
static void up_fast_reload(td_stats *s)  { s->interval *= 0.75f; }
static void up_missiles(td_stats *s)     { s->range += 20; s->speed *= 1.6f; s->interval *= 0.85f; }
static void up_cluster(td_stats *s)      { s->cluster = 8; }

/* --- Ice Tower: freezes everything nearby; can't touch white or zebra --- */
static void up_permafrost(td_stats *s)   { s->slow = 2.5f; s->slow_factor = 0.6f; }
static void up_deep_freeze(td_stats *s)  { s->freeze = 2.2f; }
static void up_ice_shards(td_stats *s)   { s->damage = 1; s->pierce += 20; }
static void up_wide_freeze(td_stats *s)  { s->range += 14; }
static void up_quick_freeze(td_stats *s) { s->interval *= 0.7f; }
static void up_arctic_wind(td_stats *s)  { s->aura = true; s->range += 10; }

/* --- Sniper Tower: hits anything, anywhere; slow but strong --- */
static void up_armor_pierce(td_stats *s) { s->damage = 4; s->damage_type = TD_DMG_NORMAL; }
static void up_heavy_rounds(td_stats *s) { s->damage = 7; }
static void up_deadeye(td_stats *s)      { s->damage = 18; }
static void up_faster_fire(td_stats *s)  { s->interval *= 0.7f; }
static void up_semi_auto(td_stats *s)    { s->interval *= 0.33f; }
static void up_bounty_drop(td_stats *s)  { s->income += 200; }

static const td_tower_def k_towers[TD_TOWER_TYPE_COUNT] = {
    { "Dart Tower", "Throws darts at the\nfirst balloon in range.", 200, 9.0f, TD_TARGET_FIRST,
      /* attack       proj            damage type      range interval dmg pierce cnt speed  prad splash sp cl stun frz slow sf aura camo inc */
      { TD_ATK_AIMED, TD_PROJ_DART, TD_DMG_SHARP,      88, 0.80f, 1,  2,    1,  480, 3.0f, 0,  0, 0, 0, 0, 0, 1, false, false, 0 },
      { { { "Sharp Darts",  "Darts pop 1 more",          140, up_sharp_darts },
          { "Razor Darts",  "Darts pop 3 more",          170, up_razor_darts },
          { "Spike Ball",   "Huge spiked balls",         550, up_spike_ball } },
        { { "Quick Shots",  "Throws 15% faster",         100, up_quick_shots },
          { "Eagle Eye",    "More range, sees camo",     120, up_eagle_eye },
          { "Triple Shot",  "Three darts at once",       400, up_triple_shot } } } },

    { "Tack Tower", "Sprays 8 tacks in all\ndirections. Short range.", 260, 9.0f, TD_TARGET_FIRST,
      { TD_ATK_RADIAL, TD_PROJ_TACK, TD_DMG_SHARP,     52, 1.10f, 1,  1,    8,  300, 2.5f, 0,  0, 0, 0, 0, 0, 1, false, false, 0 },
      { { { "Faster Tacks", "Shoots 25% faster",         150, up_faster_tacks },
          { "Even Faster",  "Shoots 30% faster again",   300, up_even_faster },
          { "Blade Storm",  "16 sharper blades",         600, up_blade_storm } },
        { { "Long Tacks",   "More range and pierce",     100, up_long_tacks },
          { "More Tacks",   "Shoots 12 tacks",           200, up_more_tacks },
          { "Ring of Fire", "Burns everything in range", 1000, up_ring_of_fire } } } },

    { "Bomb Tower", "Explodes groups. Pops\nlead, not black/zebra.", 450, 10.0f, TD_TARGET_FIRST,
      { TD_ATK_AIMED, TD_PROJ_BOMB, TD_DMG_EXPLOSIVE,  90, 1.50f, 1,  1,    1,  300, 4.0f, 22, 14, 0, 0, 0, 0, 1, false, false, 0 },
      { { { "Bigger Bombs", "Larger blast radius",       350, up_bigger_bombs },
          { "Heavy Bombs",  "Blasts pop 2 layers",       650, up_heavy_bombs },
          { "Concussion",   "Blasts stun balloons",      900, up_concussion } },
        { { "Fast Reload",  "Fires 25% faster",          250, up_fast_reload },
          { "Missiles",     "Faster, longer range",      400, up_missiles },
          { "Cluster Bombs","Blasts scatter bomblets",   800, up_cluster } } } },

    { "Ice Tower", "Freezes nearby balloons.\nNot white/zebra/blimp.", 400, 9.0f, TD_TARGET_FIRST,
      { TD_ATK_PULSE, TD_PROJ_DART, TD_DMG_COLD,       48, 2.20f, 0, 40,    0,    0, 0.0f, 0,  0, 0, 0, 1.4f, 0, 1, false, false, 0 },
      { { { "Permafrost",   "Thawed balloons stay slow", 100, up_permafrost },
          { "Deep Freeze",  "Freezes for longer",        220, up_deep_freeze },
          { "Ice Shards",   "Freezing pops a layer",     650, up_ice_shards } },
        { { "Wide Freeze",  "Larger freeze radius",      150, up_wide_freeze },
          { "Quick Freeze", "Freezes 30% more often",    250, up_quick_freeze },
          { "Arctic Wind",  "Slows all balloons near",   900, up_arctic_wind } } } },

    { "Sniper Tower", "Hits anywhere on the map.\nSees camo. Not lead.", 350, 9.0f, TD_TARGET_FIRST,
      { TD_ATK_HITSCAN, TD_PROJ_DART, TD_DMG_SHARP,  9999, 2.00f, 2,  1,    1,    0, 0.0f, 0,  0, 0, 0, 0, 0, 1, false, true, 0 },
      { { { "Armor Piercing","4 damage, pops lead",      350, up_armor_pierce },
          { "Heavy Rounds", "7 damage per shot",         900, up_heavy_rounds },
          { "Deadeye",      "18 damage per shot",       2000, up_deadeye } },
        { { "Faster Firing","Fires 30% faster",          400, up_faster_fire },
          { "Semi-Auto",    "Fires 3x faster",          1400, up_semi_auto },
          { "Bounty Drop",  "+$200 every round",        1800, up_bounty_drop } } } },
};

/* ========================================================================
 * Rounds: { round, balloon, count, seconds apart, start delay, camo }
 * ======================================================================== */
static const td_group k_groups[] = {
    {  1, TD_RED,     20, 0.90f,  0, false },
    {  2, TD_RED,     32, 0.60f,  0, false },
    {  3, TD_RED,     20, 0.50f,  0, false }, {  3, TD_BLUE,     6, 0.90f,  6, false },
    {  4, TD_RED,     25, 0.45f,  0, false }, {  4, TD_BLUE,    18, 0.60f,  3, false },
    {  5, TD_BLUE,    16, 0.55f,  0, false }, {  5, TD_RED,     24, 0.30f,  5, false },
    {  6, TD_GREEN,   12, 0.80f,  0, false }, {  6, TD_BLUE,    16, 0.50f,  3, false },
    {  7, TD_BLUE,    22, 0.40f,  0, false }, {  7, TD_GREEN,   14, 0.60f,  4, false },
    {  8, TD_GREEN,   12, 0.50f,  0, false }, {  8, TD_BLUE,    24, 0.30f,  2, false }, { 8, TD_RED, 30, 0.2f, 8, false },
    {  9, TD_GREEN,   30, 0.40f,  0, false },
    { 10, TD_BLUE,   100, 0.15f,  0, false },
    { 11, TD_YELLOW,  10, 0.70f,  0, false }, { 11, TD_GREEN,   16, 0.50f,  2, false },
    { 12, TD_YELLOW,  15, 0.60f,  0, false }, { 12, TD_BLUE,    30, 0.25f,  2, false },
    { 13, TD_GREEN,   50, 0.25f,  0, false }, { 13, TD_YELLOW,  12, 0.50f,  5, false },
    { 14, TD_PINK,    16, 0.60f,  0, false }, { 14, TD_YELLOW,  10, 0.40f,  4, false },
    { 15, TD_BLACK,    8, 0.90f,  0, false }, { 15, TD_YELLOW,  14, 0.50f,  3, false },
    { 16, TD_WHITE,   10, 0.80f,  0, false }, { 16, TD_PINK,    16, 0.55f,  2, false },
    { 17, TD_RED,     16, 0.50f,  0, true  }, { 17, TD_YELLOW,  12, 0.50f,  3, false },
    { 18, TD_LEAD,     6, 1.20f,  0, false }, { 18, TD_GREEN,   24, 0.35f,  1, false },
    { 19, TD_BLACK,   12, 0.60f,  0, false }, { 19, TD_WHITE,   12, 0.60f,  4, false },
    { 20, TD_ZEBRA,    5, 1.20f,  0, false }, { 20, TD_PINK,    20, 0.35f,  2, false },
    { 21, TD_YELLOW,  24, 0.35f,  0, false }, { 21, TD_PINK,     8, 0.60f,  4, true  },
    { 22, TD_ZEBRA,    9, 0.80f,  0, false }, { 22, TD_LEAD,     8, 1.00f,  3, false },
    { 23, TD_RAINBOW,  4, 1.40f,  0, false }, { 23, TD_PINK,    24, 0.35f,  2, false },
    { 24, TD_CERAMIC,  2, 2.50f,  0, false }, { 24, TD_ZEBRA,   14, 0.60f,  2, false },
    { 25, TD_RAINBOW,  6, 1.00f,  0, false }, { 25, TD_ZEBRA,    8, 0.80f,  3, true  },
    { 26, TD_CERAMIC,  4, 1.50f,  0, false }, { 26, TD_BLACK,    8, 0.60f,  2, false }, { 26, TD_WHITE,  8, 0.6f, 4, false },
    { 27, TD_RAINBOW, 12, 0.60f,  0, false }, { 27, TD_LEAD,    10, 0.60f,  3, false },
    { 28, TD_CERAMIC,  8, 1.00f,  0, false }, { 28, TD_LEAD,    10, 0.50f,  2, false },
    { 29, TD_CERAMIC, 10, 0.90f,  0, false }, { 29, TD_RAINBOW,  8, 0.80f,  4, true  },
    { 30, TD_BLIMP,    1, 1.00f,  0, false }, { 30, TD_CERAMIC,  6, 1.00f,  8, false },
};
#define ROUND_COUNT 30

/* ======================================================================== */

const td_balloon_def *td_balloon_info(td_balloon_type t) { return &k_balloons[t]; }
const td_tower_def *td_tower_info(td_tower_type t) { return &k_towers[t]; }

int td_balloon_type_rbe(td_balloon_type t) {
    const td_balloon_def *d = &k_balloons[t];
    int i, rbe = d->hp;
    for (i = 0; i < d->child_count; ++i) rbe += td_balloon_type_rbe(d->children[i]);
    return rbe;
}

int td_round_count(void) { return ROUND_COUNT; }

int td_round_groups(int round, const td_group **groups) {
    int i, first = -1, n = 0;
    for (i = 0; i < (int)BR_ARRAY_COUNT(k_groups); ++i) {
        if (k_groups[i].round != round) continue;
        if (first < 0) first = i;
        ++n;
    }
    *groups = first >= 0 ? &k_groups[first] : NULL;
    return n;
}

const char *td_targeting_name(td_targeting t) {
    static const char *names[TD_TARGETING_COUNT] = { "First", "Last", "Strong", "Close" };
    return names[t];
}
