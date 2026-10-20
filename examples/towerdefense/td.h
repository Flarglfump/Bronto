/*
 * POP DEFENSE - the game rules (public API).
 *
 * The rules are plain, deterministic C with no engine calls: no window,
 * no audio, no clock and no randomness. The presentation layer (view_*.c,
 * sfx.c, main.c) reads this state to draw it, calls the commands below
 * when the player clicks, and reacts to the events the rules emit.
 *
 * One file per part of the game:
 *
 *   td_data.c        what things ARE: balloon, tower, upgrade and round tables
 *   td_map.c         WHERE things are: the path and where towers may be built
 *   td_balloon.c     balloons: spawning, moving along the path, popping, leaking
 *   td_tower.c       towers: targeting and attacking
 *   td_projectile.c  darts, tacks and bombs in flight
 *   td_round.c       sending each round's balloons
 *   td_game.c        the game itself: state, the per-step update, player commands
 *   td_autopilot.c   a scripted player (attract mode, headless demo, balance tests)
 *
 * One call to td_game_step() advances the game by TD_STEP seconds:
 *
 *   rounds -> balloons move -> towers attack -> projectiles fly -> clean up -> round over?
 */
#ifndef TD_H
#define TD_H

#include "bronto/types.h"
#include "bronto/bmath.h"

#define TD_STEP (1.0f / 60.0f)          /* seconds per td_game_step */

#define TD_MAX_BALLOONS    2048
#define TD_MAX_TOWERS      64
#define TD_MAX_PROJECTILES 768
#define TD_MAX_EVENTS      1024          /* per step */
#define TD_MAX_HITS        32            /* balloons a projectile remembers hitting */

#define TD_START_CASH   650
#define TD_START_LIVES  150
#define TD_SELL_PERCENT 70

/* ========================================================================
 * Things (the tables live in td_data.c)
 * ======================================================================== */

/* How something pops balloons; some balloons are immune to some types. */
typedef enum td_damage {
    TD_DMG_SHARP = 0,   /* darts, tacks         - lead is immune           */
    TD_DMG_EXPLOSIVE,   /* bombs                - black and zebra immune   */
    TD_DMG_COLD,        /* ice                  - white, zebra, blimp      */
    TD_DMG_NORMAL,      /* fire, heavy bullets  - pops everything          */
    TD_DMG_COUNT
} td_damage;
#define TD_IMMUNE(d) (1u << (d))

typedef enum td_balloon_type {
    TD_RED = 0, TD_BLUE, TD_GREEN, TD_YELLOW, TD_PINK,
    TD_BLACK, TD_WHITE, TD_LEAD, TD_ZEBRA, TD_RAINBOW, TD_CERAMIC, TD_BLIMP,
    TD_BALLOON_TYPE_COUNT
} td_balloon_type;

typedef struct td_balloon_def {
    const char *name;
    float speed;                    /* path pixels per second */
    float radius;                   /* collision radius */
    int hp;                         /* hits this layer takes before popping */
    unsigned immune;                /* TD_IMMUNE(...) bits */
    int child_count;                /* balloons released when it pops */
    td_balloon_type children[4];
} td_balloon_def;

typedef enum td_tower_type { TD_DART = 0, TD_TACK, TD_BOMB, TD_ICE, TD_SNIPER, TD_TOWER_TYPE_COUNT } td_tower_type;

typedef enum td_attack {
    TD_ATK_AIMED = 0,   /* throws projectiles at a target          (dart, bomb) */
    TD_ATK_RADIAL,      /* sprays projectiles in every direction   (tack)       */
    TD_ATK_PULSE,       /* hits everything in range at once        (ice, fire)  */
    TD_ATK_HITSCAN      /* instant hit anywhere on the map         (sniper)     */
} td_attack;

typedef enum td_targeting { TD_TARGET_FIRST = 0, TD_TARGET_LAST, TD_TARGET_STRONG, TD_TARGET_CLOSE, TD_TARGETING_COUNT } td_targeting;

typedef enum td_proj_kind { TD_PROJ_DART = 0, TD_PROJ_TACK, TD_PROJ_SPIKEBALL, TD_PROJ_BOMB, TD_PROJ_BOMBLET } td_proj_kind;

/* Everything about how a tower attacks. A tower's stats are its base stats
 * with each bought upgrade applied in turn (see td_tower_def.paths). */
typedef struct td_stats {
    td_attack attack;
    td_proj_kind proj;
    td_damage damage_type;
    float range;           /* pixels (hitscan: ignored, whole map)            */
    float interval;        /* seconds between attacks                         */
    int damage;            /* layers popped per hit                           */
    int pierce;            /* balloons one projectile / pulse can hit         */
    int count;             /* projectiles per attack                          */
    float speed;           /* projectile pixels per second                    */
    float proj_radius;
    float splash;          /* explosion radius, 0 = no explosion              */
    int splash_pierce;     /* balloons one explosion can hit                  */
    int cluster;           /* bomblets released by each explosion             */
    float stun;            /* explosions stop balloons for this long          */
    float freeze;          /* pulse: freezes balloons for this long           */
    float slow;            /* frozen balloons then move at slow_factor ...    */
    float slow_factor;     /* ... for `slow` seconds                          */
    bool aura;             /* pulse tower also slows everything in range      */
    bool camo;             /* can target camo balloons                        */
    int income;            /* cash paid at the end of every round             */
} td_stats;

typedef struct td_upgrade_def {
    const char *name;               /* <= 17 characters (fits the side panel) */
    const char *desc;
    int cost;
    void (*apply)(td_stats *s);
} td_upgrade_def;

#define TD_PATHS 2
#define TD_TIERS 3
typedef struct td_tower_def {
    const char *name;
    const char *desc;
    int cost;
    float radius;                   /* footprint */
    td_targeting targeting;         /* default priority */
    td_stats base;
    td_upgrade_def paths[TD_PATHS][TD_TIERS];
} td_tower_def;

/* One group of identical balloons inside a round. */
typedef struct td_group {
    int round;                      /* 1-based round this group belongs to */
    td_balloon_type type;
    int count;
    float spacing;                  /* seconds between balloons */
    float delay;                    /* seconds after the round starts */
    bool camo;
} td_group;

const td_balloon_def *td_balloon_info(td_balloon_type t);
const td_tower_def *td_tower_info(td_tower_type t);
int td_balloon_type_rbe(td_balloon_type t);   /* "red balloon equivalent": pops to clear it */
int td_round_count(void);
int td_round_groups(int round, const td_group **groups);   /* returns the group count */
const char *td_targeting_name(td_targeting t);

/* ========================================================================
 * The map (td_map.c)
 * ======================================================================== */
#define TD_MAP_W 480
#define TD_MAP_H 360
#define TD_PATH_MAX_POINTS 512
#define TD_MAX_OBSTACLES 32

typedef enum td_obstacle_kind { TD_OBSTACLE_TREE = 0, TD_OBSTACLE_POND, TD_OBSTACLE_ROCK } td_obstacle_kind;
typedef struct td_obstacle { td_obstacle_kind kind; br_vec2 pos; float radius; } td_obstacle;

typedef struct td_map {
    const char *name;
    br_vec2 points[TD_PATH_MAX_POINTS];   /* the path as a dense polyline */
    float along[TD_PATH_MAX_POINTS];      /* path distance at each point */
    int point_count;
    float length;                         /* total path length */
    float path_radius;                    /* half the path width */
    td_obstacle obstacles[TD_MAX_OBSTACLES];
    int obstacle_count;
} td_map;

void td_map_build(td_map *m);                                /* the meadow map */
br_vec2 td_map_point(const td_map *m, float dist);           /* position at a path distance */
br_vec2 td_map_direction(const td_map *m, float dist);       /* unit travel direction there */
float td_map_distance_to_path(const td_map *m, br_vec2 p);
bool td_map_buildable(const td_map *m, br_vec2 p, float radius);

/* ========================================================================
 * Game state
 * ======================================================================== */
typedef struct td_balloon {
    uint32_t id;
    td_balloon_type type;
    bool camo;
    bool alive;
    int hp;                     /* hits left on this layer */
    float dist;                 /* how far along the path */
    br_vec2 pos;                /* td_map_point(dist), refreshed every step */
    float frozen, stunned;      /* seconds left not moving */
    float slowed, slow_factor;  /* seconds left moving slower, and how much slower */
    float frost_guard;          /* can't be frozen again until this runs out */
    uint32_t shield;            /* projectile that popped its parent can't hit it */
} td_balloon;

typedef struct td_tower {
    uint32_t id;
    td_tower_type type;
    br_vec2 pos;
    int tier[TD_PATHS];         /* upgrades bought on each path (0..3) */
    td_targeting targeting;
    td_stats stats;             /* base + upgrades, see td_tower_refresh_stats */
    float cooldown;             /* seconds until it may attack again */
    br_vec2 aim;                /* unit vector the turret faces */
    int pops;
    int spent;                  /* price + upgrades, for the sell value */
} td_tower;

typedef struct td_projectile {
    uint32_t id;
    uint32_t tower;             /* tower id, credited with the pops */
    td_proj_kind kind;
    bool alive;
    br_vec2 pos, vel;
    float radius, life;
    int damage, pierce;
    td_damage damage_type;
    float splash, stun;
    int splash_pierce, cluster;
    bool explode_on_expire;
    uint32_t hits[TD_MAX_HITS];
    int hit_count;
} td_projectile;

/* What happened during a step - for sound, effects and statistics. */
typedef enum td_event_type {
    TD_EV_POP = 0,          /* kind = balloon type popped                  */
    TD_EV_HIT,              /* a hit that didn't pop (ceramic, blimp)      */
    TD_EV_BLOCKED,          /* an immune balloon shrugged a hit off        */
    TD_EV_LEAK,             /* amount = lives lost                         */
    TD_EV_ATTACK,           /* kind = tower type, pos = tower, to = aim    */
    TD_EV_EXPLOSION,        /* radius                                      */
    TD_EV_PULSE,            /* kind = tower type (ice / tack ring), radius */
    TD_EV_SNIPE,            /* pos = tower, to = target                    */
    TD_EV_ROUND_START,      /* amount = round                              */
    TD_EV_ROUND_END,        /* amount = cash bonus                         */
    TD_EV_TOWER_PLACED,     /* kind = tower type                           */
    TD_EV_TOWER_UPGRADED,   /* kind = tower type, amount = path            */
    TD_EV_TOWER_SOLD,       /* amount = refund                             */
    TD_EV_DEFEAT,
    TD_EV_VICTORY,
    TD_EVENT_TYPE_COUNT
} td_event_type;

typedef struct td_event {
    td_event_type type;
    int kind;
    int amount;
    br_vec2 pos, to;
    float radius;
} td_event;

typedef enum td_phase {
    TD_PHASE_BUILD = 0,     /* between rounds: waiting for the player */
    TD_PHASE_ROUND,         /* balloons on the way                    */
    TD_PHASE_DEFEAT,
    TD_PHASE_VICTORY
} td_phase;

typedef struct td_game {
    td_map map;
    td_phase phase;
    int cash, lives;
    int round;                  /* current (or last finished) round, 0 before the first */
    float round_time;           /* seconds since the round started */
    int spawned[16];            /* per group of the current round */

    td_balloon balloons[TD_MAX_BALLOONS];
    int balloon_count;
    td_tower towers[TD_MAX_TOWERS];
    int tower_count;
    td_projectile projectiles[TD_MAX_PROJECTILES];
    int projectile_count;

    td_event events[TD_MAX_EVENTS];     /* collected until td_game_clear_events */
    int event_count;
    uint32_t events_dropped;

    uint32_t next_id;
    uint64_t steps;
    int pops, leaked;           /* totals for the end screen */
} td_game;

/* ========================================================================
 * Playing (td_game.c)
 * ======================================================================== */
typedef enum td_result {
    TD_OK = 0,
    TD_ERR_CASH,            /* can't afford it                   */
    TD_ERR_BLOCKED,         /* path, obstacle, tower or off-map  */
    TD_ERR_FULL,            /* too many towers                   */
    TD_ERR_MAXED,           /* path fully upgraded               */
    TD_ERR_PATH_LOCKED,     /* the other path already went to 3  */
    TD_ERR_NO_TOWER,
    TD_ERR_PHASE            /* not now (round running / game over) */
} td_result;

void td_game_init(td_game *g);
void td_game_step(td_game *g);
/* Events pile up in g->events (from steps and commands alike) until the
 * presentation has handled them and clears them, once per frame. */
void td_game_clear_events(td_game *g);

td_result td_start_round(td_game *g);
td_result td_can_place(const td_game *g, td_tower_type type, br_vec2 pos);
td_result td_place_tower(td_game *g, td_tower_type type, br_vec2 pos);   /* new tower is last */
td_result td_can_upgrade(const td_game *g, int tower, int path);
td_result td_upgrade_tower(td_game *g, int tower, int path);
int  td_sell_tower(td_game *g, int tower);                /* refund, 0 if no tower */
void td_cycle_targeting(td_game *g, int tower, int dir); /* dir +1 / -1 */

int  td_tower_at(const td_game *g, br_vec2 p);            /* index, -1 if none */
int  td_sell_value(const td_tower *t);
int  td_balloon_rbe(const td_balloon *b);                 /* what a leak costs */
bool td_game_over(const td_game *g);
const char *td_result_text(td_result r);

/* ========================================================================
 * Autopilot (td_autopilot.c)
 * ======================================================================== */
typedef struct td_autopilot { int next; } td_autopilot;
void td_autopilot_init(td_autopilot *a);
void td_autopilot_play(td_autopilot *a, td_game *g);      /* call before each step */

#endif /* TD_H */
