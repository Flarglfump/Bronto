/*
 * POP DEFENSE - rounds: sending each group of balloons on schedule.
 *
 * A round is a list of groups (td_data.c). Balloon k of a group enters
 * the path `delay + k * spacing` seconds after the round starts.
 */
#include "td_internal.h"
#include <string.h>

#define MAX_GROUPS ((int)BR_ARRAY_COUNT(((td_game *)0)->spawned))

void td_round_begin(td_game *g) {
    g->round += 1;
    g->round_time = 0;
    memset(g->spawned, 0, sizeof(g->spawned));
}

void td_round_spawn(td_game *g, float dt) {
    const td_group *groups;
    int n = td_round_groups(g->round, &groups), i;
    g->round_time += dt;
    for (i = 0; i < n && i < MAX_GROUPS; ++i) {
        const td_group *gr = &groups[i];
        while (g->spawned[i] < gr->count && g->round_time >= gr->delay + gr->spacing * (float)g->spawned[i]) {
            td_balloon_spawn(g, gr->type, gr->camo, 0.0f);
            g->spawned[i] += 1;
        }
    }
}

bool td_round_all_sent(const td_game *g) {
    const td_group *groups;
    int n = td_round_groups(g->round, &groups), i;
    for (i = 0; i < n && i < MAX_GROUPS; ++i)
        if (g->spawned[i] < groups[i].count) return false;
    return true;
}
