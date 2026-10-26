/*
 * simc - sim.py's Basil, in C, for speed: the same steps in the same order with the same
 * doubles, so it lands on exactly the same pixel (testsim.py checks both against the game).
 * The bot loads it with ctypes (simfast.py); it builds with any C compiler:
 *
 *   cc -O2 -shared -fPIC -o simc.so simc.c -lm
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RUN 1.5
#define GRAVITY 0.25
#define JUMP 5.4
#define HIGH_JUMP 7.0
#define CUT 2.0
#define MAX_FALL 5.0
#define DT (1.0 / 60.0)

enum { STAND, CROUCH, BALL };
static const double SIZE_W[3] = { 12, 12, 12 }, SIZE_H[3] = { 26, 18, 12 };

/* keys, as bits */
enum { K_LEFT = 1, K_RIGHT = 2, K_UP = 4, K_DOWN = 8, K_JUMP = 16, K_AIM = 32, K_BALL = 64, K_FIRE = 128, K_ROCKET = 256 };
#define EDGES (K_UP | K_DOWN | K_JUMP | K_BALL | K_FIRE | K_ROCKET)

typedef struct grid {
    int cols, rows;
    double x0, y0, ts, W, H;
    unsigned char *solid;     /* cols * rows */
    char *tile;               /* the tile characters */
    unsigned char *liquid;    /* front map: liquid here */
    int fcols, frows;
    double fx0, fy0;
    double *plat;             /* frozen enemies: x, y pairs */
    int nplat;
    int has_dough, has_whisk, has_leaven;
} grid;

typedef struct basil {
    double x, y, vx, vy, sx, sy;
    double hurt_t, safe_t, lava_t, shot_cd;
    int facing, grounded, ball, crouch, spin, lifted, in_liquid;
    int hurt_count;
    int left_room;            /* 0, or 'W' 'E' 'N' 'S' */
} basil;

static double rnd(double v) { return floor(v + 0.5); }
static int sgn(double v) { return (v > 0) - (v < 0); }

static int pose_of(const basil *b) { return b->ball ? BALL : b->crouch ? CROUCH : STAND; }

static char cell(const grid *g, int c, int r) {
    if (r >= 0 && r < g->rows && c >= 0 && c < g->cols) return g->tile[r * g->cols + c];
    return ' ';
}

static char tile_at(const grid *g, double x, double y) {
    int c = (int)floor((x - g->x0) / g->ts), r = (int)floor((y - g->y0) / g->ts);
    return cell(g, c, r);
}

static int front_liquid(const grid *g, double x, double y) {
    int c, r;
    if (!g->liquid) return 0;
    c = (int)floor((x - g->fx0) / g->ts);
    r = (int)floor((y - g->fy0) / g->ts);
    if (r >= 0 && r < g->frows && c >= 0 && c < g->fcols) return g->liquid[r * g->fcols + c];
    return 0;
}

static int box_solid(const grid *g, double cx, double cy, double w, double h) {
    double x0 = cx - w / 2 - g->x0, x1 = cx + w / 2 - g->x0, y0 = cy - h / 2 - g->y0, y1 = cy + h / 2 - g->y0;
    int c0 = (int)floor(x0 / g->ts), c1 = (int)ceil(x1 / g->ts) - 1, r0 = (int)floor(y0 / g->ts), r1 = (int)ceil(y1 / g->ts) - 1;
    int r, c;
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 > g->cols - 1) c1 = g->cols - 1;
    if (r1 > g->rows - 1) r1 = g->rows - 1;
    for (r = r0; r <= r1; ++r)
        for (c = c0; c <= c1; ++c)
            if (g->solid[r * g->cols + c]) return 1;
    return 0;
}

int sim_blocked(const grid *g, double x, double y, int pose) {
    double w = SIZE_W[pose], h = SIZE_H[pose], cx = x, cy = y + 16 - h / 2;
    int i;
    if (box_solid(g, cx, cy, w, h)) return 1;
    for (i = 0; i < g->nplat; ++i) {
        double px = g->plat[2 * i], py = g->plat[2 * i + 1];
        if (fabs(cx - px) < w / 2 + 8 && fabs(cy - py) < h / 2 + 8) return 1;
    }
    return 0;
}

static void middle(const basil *b, double *mx, double *my) {
    double h = SIZE_H[pose_of(b)];
    *mx = b->x;
    *my = b->y + 16 - h / 2;
}

static int move_x(basil *b, double dx, const grid *g) {
    double steps;
    int s, i, n, pose = pose_of(b);
    b->sx += dx;
    steps = rnd(b->sx);
    b->sx -= steps;
    s = sgn(steps);
    n = (int)fabs(steps);
    for (i = 0; i < n; ++i) {
        if (sim_blocked(g, b->x + s, b->y, pose)) { b->sx = 0; return 0; }
        b->x += s;
    }
    return 1;
}

static int move_y(basil *b, double dy, const grid *g) {
    double steps;
    int s, i, n, pose = pose_of(b);
    b->sy += dy;
    steps = rnd(b->sy);
    b->sy -= steps;
    s = sgn(steps);
    n = (int)fabs(steps);
    for (i = 0; i < n; ++i) {
        if (sim_blocked(g, b->x, b->y + s, pose)) { b->sy = 0; return 0; }
        b->y += s;
    }
    return 1;
}

static void jump(basil *b, int d, const grid *g) {
    double v = g->has_leaven ? HIGH_JUMP : JUMP;
    if (b->in_liquid) v = v * 0.75;
    b->vy = -v;
    b->grounded = 0;
    b->spin = d != 0 && g->has_whisk;
}

static void hazards(basil *b, const grid *g) {
    double mx, my, sx = SIZE_W[pose_of(b)] / 2, sy = SIZE_H[pose_of(b)] / 2;
    double px[4], py[4];
    int lava = 0, spikes = 0, i;
    middle(b, &mx, &my);
    px[0] = mx - sx + 2; py[0] = my + sy - 2;
    px[1] = mx + sx - 2; py[1] = my + sy - 2;
    px[2] = mx; py[2] = my;
    for (i = 0; i < 3; ++i) if (tile_at(g, px[i], py[i]) == '~') lava = 1;
    if (lava) {
        b->lava_t -= DT;
        if (b->lava_t <= 0) { b->lava_t = 0.4; b->hurt_count++; }
        if (b->vy > 1) b->vy = 1;
    }
    px[2] = mx - sx + 2; py[2] = my - sy + 1;
    px[3] = mx + sx - 2; py[3] = my - sy + 1;
    for (i = 0; i < 4; ++i) { char t = tile_at(g, px[i], py[i]); if (t == '^' || t == 'v') spikes = 1; }
    if (spikes) {
        b->hurt_count++;
        if (b->safe_t <= 0) {
            int away = b->facing ? sgn((double)b->facing) : 0;
            b->safe_t = 1.1;
            b->hurt_t = 0.25;
            if (away == 0) away = -b->facing;
            b->vx = away * 1.6;
            b->vy = b->ball ? -2 : -2.5;
            b->spin = 0;
        }
    }
}

/* One step of basil.bs's update() (in play). held: keys down; pressed: keys that went down. */
void sim_step(basil *b, int held, int pressed, const grid *g) {
    double mx, my, speed;
    int control, left, right, d = 0, aiming, sideways = 0, was;
    if (b->safe_t > 0) b->safe_t -= DT;
    if (b->hurt_t > 0) b->hurt_t -= DT;
    if (b->shot_cd > 0) b->shot_cd -= 1;
    middle(b, &mx, &my);
    b->in_liquid = front_liquid(g, mx, my);
    control = b->hurt_t <= 0;
    left = (held & K_LEFT) != 0;
    right = (held & K_RIGHT) != 0;
    if (control && left && !right) d = -1;
    if (control && right && !left) d = 1;
    aiming = control && (held & K_AIM) && !b->ball;
    if (left && !right) sideways = -1;
    if (right && !left) sideways = 1;
    if (sideways != 0 && control) b->facing = sideways;

    if (control && !b->ball && !aiming) {
        int roll = (pressed & K_BALL) && b->grounded;
        if (b->crouch && (pressed & K_DOWN)) roll = 1;
        if (roll && g->has_dough) {
            b->ball = 1;
            b->crouch = 0;
            b->spin = 0;
        } else if (!b->crouch && b->grounded && (pressed & K_DOWN)) {
            b->crouch = 1;
        } else if (b->crouch && (sideways != 0 || (pressed & K_UP) || (pressed & K_JUMP))) {
            if (!sim_blocked(g, b->x, b->y, STAND)) b->crouch = 0;
        }
    } else if (control && b->ball && ((pressed & K_UP) || (pressed & K_JUMP) || (pressed & K_BALL))) {
        if (!sim_blocked(g, b->x, b->y, STAND)) b->ball = 0;
        else if (!sim_blocked(g, b->x, b->y, CROUCH)) { b->ball = 0; b->crouch = 1; }
    }
    if (b->crouch && !b->grounded && !sim_blocked(g, b->x, b->y, STAND)) b->crouch = 0;
    if (aiming || b->crouch) d = 0;

    speed = b->in_liquid ? RUN * 0.6 : RUN;
    if (control) b->vx = d * speed;
    else if (b->grounded) b->vx = b->vx * 0.8;
    if (!move_x(b, b->vx, g)) b->vx = 0;

    was = b->grounded;
    b->grounded = b->vy >= 0 && sim_blocked(g, b->x, b->y + 1, pose_of(b));
    if (b->grounded) {
        if (!was) b->spin = 0;
        b->vy = 0;
        if (control && !b->ball && !b->crouch && (pressed & K_JUMP)) jump(b, d, g);
    } else {
        double gr = GRAVITY, top = MAX_FALL;
        if (b->in_liquid) { gr = GRAVITY * 0.5; top = MAX_FALL * 0.5; }
        b->vy = b->vy + gr < top ? b->vy + gr : top;
        if (control && !b->ball && b->spin && g->has_whisk && (pressed & K_JUMP) && b->vy > -2) jump(b, d, g);
        if (b->vy >= 0) b->lifted = 0;
        if (b->vy < -CUT && !(held & K_JUMP) && b->hurt_t <= 0 && !b->lifted) b->vy = -CUT;
    }
    if (!move_y(b, b->vy, g)) b->vy = b->vy < 0 ? 0.5 : 0;

    if (control && (pressed & (K_FIRE | K_ROCKET)) && b->shot_cd <= 0) b->shot_cd = 9;
    hazards(b, g);
    middle(b, &mx, &my);
    if (mx < 0) b->left_room = 'W';
    else if (mx > g->W) b->left_room = 'E';
    else if (my < 0) b->left_room = 'N';
    else if (my > g->H) b->left_room = 'S';
}

/* A frame: `steps` fixed steps; keys that went down reach the first only. */
void sim_frame(basil *b, int held, int pressed, int steps, const grid *g) {
    int i;
    for (i = 0; i < steps; ++i) sim_step(b, held, i == 0 ? pressed : 0, g);
}

/* Moving hazards (enemies, spit): x, y, vx, vy, hx, hy each. */
int touches_any(const basil *b, const double *hz, int nhz, int k, double margin) {
    double mx, my, w = SIZE_W[pose_of(b)], h = SIZE_H[pose_of(b)];
    int i;
    middle(b, &mx, &my);
    for (i = 0; i < nhz; ++i) {
        const double *z = hz + 6 * i;
        double x = z[0] + z[2] * k, y = z[1] + z[3] * k;
        if (fabs(x - mx) < z[4] + w / 2 + margin && fabs(y - my) < z[5] + h / 2 + margin) return 1;
    }
    return 0;
}

/* Plays a move (keys[f] for f < nkeys, then nothing) from b, in grid g. kind: 0 jump, 1 walk,
 * 2 toggle. popcorn: a bounce move (fire on frame 0 drops popcorn; it pops 49 steps on).
 * Returns how it ended: 1 land, 2 exit, 3 hurt, 0 neither; *frames_out the frames it took;
 * b is left at the end. states (if not NULL, room for max_frames) gets each frame's state. */
int sim_play(const grid *g, basil *b, const int *keys, int nkeys, int length, int kind, int popcorn, int held0,
             const int *schedule, int nsched, int max_frames, const double *hz, int nhz, int allow_hurt,
             int *frames_out, basil *states) {
    int prev = held0, airborne = !b->grounded, pending = 0, f;
    int pop_n = 0;                              /* steps since dropped (0: not yet, -1: popped) */
    double pop_x = 0, pop_y = 0;
    for (f = 0; f < max_frames; ++f) {
        int held = (f < nkeys && f < 120) ? keys[f] : 0;
        int pressed = held & ~prev & EDGES;
        int steps = schedule && f < nsched ? schedule[f] : 1;
        int hurt_before = b->hurt_count, i;
        pressed |= pending;
        pending = steps == 0 ? pressed : 0;
        if (popcorn) {
            for (i = 0; i < steps; ++i) {
                sim_step(b, held, i == 0 ? pressed : 0, g);
                if (pop_n == 0 && i == 0 && (pressed & K_FIRE) && b->ball) {
                    pop_n = 1; pop_x = b->x; pop_y = b->y + 10;
                } else if (pop_n > 0) {
                    pop_n++;
                    if (pop_n == 49) {
                        pop_n = -1;
                        if (b->ball && fabs(b->x - pop_x) < 14 && b->y + 10 - pop_y > -20 && b->y + 10 - pop_y < 4) {
                            b->vy = -4.6; b->grounded = 0; b->lifted = 1;
                        }
                    }
                }
            }
        } else {
            sim_frame(b, held, pressed, steps, g);
        }
        if (states) states[f] = *b;
        prev = held;
        *frames_out = f + 1;
        if (b->hurt_count > hurt_before && !allow_hurt) return 3;
        if (nhz && touches_any(b, hz, nhz, f + 1, 2)) return 3;
        if (b->left_room) return 2;
        if (!b->grounded) airborne = 1;
        else if (popcorn && pop_n >= 0) continue;
        else if (airborne) return 1;
        else if (popcorn) return 0;
        else if (kind == 2 && f >= 1) return 1;
        else if (f + 1 >= length && f >= 1 && !held) return 1;
    }
    *frames_out = max_frames;
    return 0;
}
