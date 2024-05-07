#include "game.h"

#include <core/logger.h>

b8 game_initialize(struct game * game_inst) {
    B_DEBUG("game_initialize() called!");
    return true;
}
b8 game_update(struct game * game_inst, f32 delta_time) {
    return true;
}
b8 game_render(struct game * game_inst, f32 delta_time) {
    return true;
}
void game_on_resize(struct game * game_inst, u32 width, u32 height) {
    B_DEBUG("game_on_resize() called!");
}