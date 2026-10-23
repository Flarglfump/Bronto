/*
 * The route: the whole game, segment by segment.
 */
#include "bot.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* From the title to a new game in file 1. */
static void start_game(void) {
    bot_wait(30);
    key_tap(BR_KEY_ENTER);              /* PLAY */
    bot_wait(20);
    key_tap(BR_KEY_ENTER);              /* file 1: a new game */
    if (!wait_play(600)) bot_fail("the game didn't start");
    bot_log("in the game: %s", room_name());
}

static void status(const char *what) {
    bot_log("%s: hearts %g/%g, beans %g, bombs %g, weapon %g, pages %g, deaths %d", what, gnum("hp") / 2, gnum("hearts_max"),
            gnum("beans"), gnum("bombs"), gnum("weapon"), gnum("pages"), g_deaths);
}

/* The healing springs: two screens of the land with hearts lying about
 * that come back every time Pip comes in. Tops up there when low. */
void step_out_and_back(void);
static void take_hearts_here(void) {
    br_entity list[32];
    int n = ents_named("Pickup", list, 32), i;
    for (i = 0; i < n; ++i) {
        if (!ent_alive(list[i]) || strcmp(estr(list[i], "kind"), "heart") != 0 || enum_(list[i], "price", 0) > 0) continue;
        if (gnum("hp") >= gnum("hearts_max") * 2) return;
        goto_point(ent_pos(list[i]), 7, 900);
        wait_play(300);
    }
}

static void top_up(float share) {
    static const char *springs[2] = { "ow_12_0", "ow_3_4" };
    char rooms[2][32];
    int cs[2], rs[2], i, pick, visits;
    if (gnum("hp") >= gnum("hearts_max") * 2 * share) return;
    for (i = 0; i < 2; ++i) { snprintf(rooms[i], 32, "%s", springs[i]); cs[i] = 7; rs[i] = 5; }
    pick = world_nearest((const char (*)[32])rooms, cs, rs, 2, false, false);
    if (pick < 0) return;
    bot_log("topping up at the spring in %s (hearts %g/%g)", springs[pick], gnum("hp") / 2, gnum("hearts_max"));
    if (!travel(springs[pick], -1, -1)) return;
    for (visits = 0; visits < 6 && gnum("hp") < gnum("hearts_max") * 2; ++visits) {
        if (foe_count() > 0) fight_room(1200);
        take_hearts_here();
        if (gnum("hp") < gnum("hearts_max") * 2) step_out_and_back();
    }
}

extern const char *g_test_route;

/* --route boss9 and the like: one part, for working on it (with --test). */
static bool test_route(void) {
    if (!g_test_route) return false;
    wait_play(600);
    if (strncmp(g_test_route, "boss", 4) == 0) {
        int n = g_test_route[4] - '0';
        const char *colon = strchr(g_test_route, ':');
        if (colon) {                                     /* (a variation: stand about a while first) */
            int k = atoi(colon + 1);
            key_hold(k % 2 ? BR_KEY_LEFT : BR_KEY_RIGHT, true);
            bot_wait(k);
            keys_release_all();
        }
        bot_log("testing the guardian of cellar %d", n);
        if (fight_boss(n)) bot_log("beaten"); else bot_log("NOT beaten");
        status("after");
        return true;
    }
    if (strncmp(g_test_route, "cellar", 6) == 0) {
        int n = g_test_route[6] - '0';
        if (explore_cellar(n)) bot_log("cellar done"); else bot_log("cellar NOT done");
        status("after");
        return true;
    }
    return false;
}

void bot_route(void) {
    if (test_route()) return;
    br_replay_segment("Title");
    start_game();

    br_replay_segment("Overworld 1");
    if (!get_pickup("cave_chef", "spoon")) bot_fail("no spoon");
    if (!travel("d1_2_4", -1, -1)) bot_fail("couldn't reach the Cold Cellar");

    br_replay_segment("Cellar 1: The Cold Cellar");
    if (!explore_cellar(1)) bot_fail("the Cold Cellar beat the bot");
    status("Cellar 1 done");

    br_replay_segment("Overworld 2");
    if (!get_pickup("ow_6_2", "heart_pie")) bot_fail("no heart pie by the lake");
    if (!get_pickup("cave_spoon2", "copper_spoon")) bot_fail("no copper spoon");
    top_up(0.7f);
    if (!travel("d2_2_4", -1, -1)) bot_fail("couldn't reach the Pickle Pantry");
    status("At the Pickle Pantry");

    br_replay_segment("Cellar 2: The Pickle Pantry");
    if (!explore_cellar(2)) bot_fail("the Pickle Pantry beat the bot");
    status("Cellar 2 done");

    br_replay_segment("Overworld 3");
    top_up(0.7f);
    if (!travel("d3_2_4", -1, -1)) bot_fail("couldn't reach the Spice Rack");
    status("At the Spice Rack");

    br_replay_segment("Cellar 3: The Spice Rack");
    if (!explore_cellar(3)) bot_fail("the Spice Rack beat the bot");
    status("Cellar 3 done");

    br_replay_segment("Overworld 4");
    if (reveal_warp("ow_1_5", "cave_jacket", "chili")) get_pickup("cave_jacket", "jacket");
    if (reveal_warp("ow_0_7", "cave_pie1", "chili")) get_pickup("cave_pie1", "heart_pie");
    if (!get_pickup("cave_shop2", "bombs_item")) bot_fail("couldn't buy bombs");
    status("Bought bombs");
    top_up(0.7f);
    if (!travel("d4_2_4", -1, -1)) bot_fail("couldn't reach the Jam Jar");
    status("At the Jam Jar");

    br_replay_segment("Cellar 4: The Jam Jar");
    if (!explore_cellar(4)) bot_fail("the Jam Jar beat the bot");
    status("Cellar 4 done");

    br_replay_segment("Overworld 5");
    if (reveal_warp("ow_15_3", "cave_coat", "bombs")) get_pickup("cave_coat", "coat");
    get_pickup("ow_15_7", "heart_pie");
    top_up(0.7f);
    if (!travel("d5_2_4", -1, -1)) bot_fail("couldn't reach the Candy Works");
    status("At the Candy Works");

    br_replay_segment("Cellar 5: The Candy Works");
    if (!explore_cellar(5)) bot_fail("the Candy Works beat the bot");
    status("Cellar 5 done");

    br_replay_segment("Overworld 6");
    get_pickup("cave_pie2", "heart_pie");
    if (reveal_warp("ow_8_0", "cave_ladle", "bombs")) get_pickup("cave_ladle", "golden_ladle");
    get_pickup("ow_0_0", "heart_pie");
    top_up(0.7f);
    if (!travel("d6_2_4", -1, -1)) bot_fail("couldn't reach the Fortune Hall");
    status("At the Fortune Hall");

    br_replay_segment("Cellar 6: The Fortune Hall");
    if (!explore_cellar(6)) bot_fail("the Fortune Hall beat the bot");
    status("Cellar 6 done");

    br_replay_segment("Overworld 7");
    if (gnum("beans") >= 80) get_pickup("cave_shop", "bag");
    if (!reveal_warp("ow_11_4", "d7_2_4", "bombs")) bot_fail("couldn't open the Sugar Mill's cliff");
    top_up(0.7f);
    if (!travel("d7_2_4", -1, -1)) bot_fail("couldn't reach the Sugar Mill");
    status("At the Sugar Mill");

    br_replay_segment("Cellar 7: The Sugar Mill");
    if (!explore_cellar(7)) bot_fail("the Sugar Mill beat the bot");
    status("Cellar 7 done");

    br_replay_segment("Overworld 8");
    if (!reveal_warp("ow_9_1", "d8_2_4", "bombs")) bot_fail("couldn't open the Soup Cauldron's cliff");
    top_up(0.7f);
    if (!travel("d8_2_4", -1, -1)) bot_fail("couldn't reach the Soup Cauldron");
    status("At the Soup Cauldron");

    br_replay_segment("Cellar 8: The Soup Cauldron");
    if (!explore_cellar(8)) bot_fail("the Soup Cauldron beat the bot");
    status("Cellar 8 done");

    br_replay_segment("Overworld 9");
    top_up(0.7f);
    if (!travel("d9_2_4", -1, -1)) bot_fail("couldn't reach Baron Gorge's Banquet Hall");
    status("At the Banquet Hall");

    br_replay_segment("Cellar 9: The Banquet Hall");
    if (!explore_cellar(9)) bot_fail("the Banquet Hall beat the bot");
    {
        int n = 0;
        while (strcmp(game_state(), "won") != 0 && n++ < 600) bot_frame();
    }
    status("The Grand Recipe is whole");
    br_replay_segment("The End");
    keys_release_all();
    bot_wait(60 * 12);                                   /* the ending card */
}
