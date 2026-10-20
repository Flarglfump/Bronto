/*
 * POP DEFENSE - how the rule modules talk to each other.
 *
 * Only td_*.c files include this. The presentation layer uses td.h.
 */
#ifndef TD_INTERNAL_H
#define TD_INTERNAL_H

#include "td.h"

/* td_game.c ------------------------------------------------------------- */
td_event *td_emit(td_game *g, td_event_type type, br_vec2 pos);   /* never NULL */
uint32_t td_new_id(td_game *g);

/* td_balloon.c ---------------------------------------------------------- */
#define TD_BLOCKED (-1)
td_balloon *td_balloon_spawn(td_game *g, td_balloon_type type, bool camo, float dist);
void td_balloons_move(td_game *g, float dt);
/* Hit balloon `index` for `damage` layers. Returns the number of pops, or
 * TD_BLOCKED if the balloon is immune. Popped balloons release their
 * children, and damage left over carries through to them. */
int  td_balloon_damage(td_game *g, int index, int damage, td_damage type, uint32_t projectile, uint32_t tower);
void td_balloon_freeze(td_balloon *b, const td_stats *s);
float td_balloon_speed(const td_balloon *b);              /* current, after freeze/slow */
bool td_balloon_hurt_by(const td_balloon *b, td_damage type);
void td_balloons_remove_dead(td_game *g);

/* td_tower.c ------------------------------------------------------------ */
void td_tower_refresh_stats(td_tower *t);
void td_towers_update(td_game *g, float dt);
void td_tower_credit(td_game *g, uint32_t tower, int pops);

/* td_projectile.c ------------------------------------------------------- */
td_projectile *td_projectile_spawn(td_game *g, const td_tower *t, br_vec2 pos, br_vec2 dir);
void td_projectiles_update(td_game *g, float dt);
void td_projectiles_remove_dead(td_game *g);

/* td_round.c ------------------------------------------------------------ */
void td_round_begin(td_game *g);
void td_round_spawn(td_game *g, float dt);
bool td_round_all_sent(const td_game *g);

#endif /* TD_INTERNAL_H */
