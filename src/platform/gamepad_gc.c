/*
 * macOS gamepads: Apple's GameController framework (GCController), driven
 * from C through the Objective-C runtime like the rest of the Cocoa backend.
 *
 * Supports every controller macOS recognises: Xbox, PlayStation
 * (DualShock 4 / DualSense), Switch Pro, MFi. Controllers are read as
 * snapshots each frame via the "extended gamepad" profile; newer buttons
 * (Menu, Options, Home, stick clicks) are used only when the OS has them.
 * The controller's player LED is set to match its Bronto slot.
 *
 * Rumble is not implemented on macOS (it needs Core Haptics): the rumble
 * call reports false.
 *
 * Included by platform_macos.c after its objc_msgSend helpers.
 * Link: -framework GameController.
 */

static struct {
    id slots[BR_MAX_GAMEPADS];          /* retained GCController per slot */
    br_gamepad_raw raw[BR_MAX_GAMEPADS];
    br_gamepad_tracker tracker;
} GC;

static float msg_float(id o, const char *s) { return ((float (*)(id, SEL))objc_msgSend)(o, SEL_(s)); }
static bool responds(id o, const char *s) {
    return o && ((BOOL (*)(id, SEL, SEL))objc_msgSend)(o, SEL_("respondsToSelector:"), SEL_(s));
}

static bool gc_pressed(id gamepad, const char *button) {
    id b;
    if (!responds(gamepad, button)) return false;    /* not on this OS version */
    b = msg_id(gamepad, button);
    return b && msg_bool(b, "isPressed");
}

static void gc_read(id controller, br_gamepad_raw *r) {
    id gp = msg_id(controller, "extendedGamepad");
    static const struct { const char *sel; int button; } buttons[] = {
        { "buttonA", BR_PAD_A }, { "buttonB", BR_PAD_B }, { "buttonX", BR_PAD_X }, { "buttonY", BR_PAD_Y },
        { "leftShoulder", BR_PAD_LEFT_BUMPER }, { "rightShoulder", BR_PAD_RIGHT_BUMPER },
        { "buttonOptions", BR_PAD_BACK }, { "buttonMenu", BR_PAD_START }, { "buttonHome", BR_PAD_GUIDE },
        { "leftThumbstickButton", BR_PAD_LEFT_STICK }, { "rightThumbstickButton", BR_PAD_RIGHT_STICK }
    };
    id dpad, ls, rs;
    size_t i;
    r->buttons = 0;
    if (!gp) return;
    for (i = 0; i < BR_ARRAY_COUNT(buttons); ++i)
        if (gc_pressed(gp, buttons[i].sel)) r->buttons |= 1u << buttons[i].button;
    dpad = msg_id(gp, "dpad");
    if (dpad) {
        if (gc_pressed(dpad, "up")) r->buttons |= 1u << BR_PAD_DPAD_UP;
        if (gc_pressed(dpad, "down")) r->buttons |= 1u << BR_PAD_DPAD_DOWN;
        if (gc_pressed(dpad, "left")) r->buttons |= 1u << BR_PAD_DPAD_LEFT;
        if (gc_pressed(dpad, "right")) r->buttons |= 1u << BR_PAD_DPAD_RIGHT;
    }
    ls = msg_id(gp, "leftThumbstick");
    rs = msg_id(gp, "rightThumbstick");
    if (ls) {
        r->axes[BR_PAD_AXIS_LEFT_X] = msg_float(msg_id(ls, "xAxis"), "value");
        r->axes[BR_PAD_AXIS_LEFT_Y] = -msg_float(msg_id(ls, "yAxis"), "value");   /* GC is y-up */
    }
    if (rs) {
        r->axes[BR_PAD_AXIS_RIGHT_X] = msg_float(msg_id(rs, "xAxis"), "value");
        r->axes[BR_PAD_AXIS_RIGHT_Y] = -msg_float(msg_id(rs, "yAxis"), "value");
    }
    r->axes[BR_PAD_AXIS_LEFT_TRIGGER] = msg_float(msg_id(gp, "leftTrigger"), "value");
    r->axes[BR_PAD_AXIS_RIGHT_TRIGGER] = msg_float(msg_id(gp, "rightTrigger"), "value");
}

static void gc_poll(br_events *events) {
    id list;
    unsigned long n, k;
    bool seen[BR_MAX_GAMEPADS] = { false, false, false, false };
    int s;
    if (!objc_getClass("GCController")) return;
    list = msg_id(cls("GCController"), "controllers");
    n = list ? msg_ulong(list, "count") : 0;
    for (k = 0; k < n; ++k) {
        id c = ((id (*)(id, SEL, unsigned long))objc_msgSend)(list, SEL_("objectAtIndex:"), k);
        int slot = -1;
        if (!c || !msg_id(c, "extendedGamepad")) continue;   /* e.g. Siri Remote */
        for (s = 0; s < BR_MAX_GAMEPADS; ++s) if (GC.slots[s] == c) slot = s;
        if (slot < 0) {
            for (s = 0; s < BR_MAX_GAMEPADS && slot < 0; ++s) if (!GC.slots[s]) slot = s;
            if (slot < 0) continue;                           /* more than 4 pads */
            GC.slots[slot] = msg_id(c, "retain");
            memset(&GC.raw[slot], 0, sizeof(GC.raw[slot]));
            GC.raw[slot].connected = true;
            {
                id vendor = msg_id(c, "vendorName");
                const char *name = vendor ? msg_cstr(vendor, "UTF8String") : NULL;
                snprintf(GC.raw[slot].name, sizeof(GC.raw[slot].name), "%s", name ? name : "Game Controller");
            }
            msg_v_long(c, "setPlayerIndex:", (long)slot);     /* light the matching LED */
            BR_INFO("gamepad: '%s' connected", GC.raw[slot].name);
        }
        seen[slot] = true;
        gc_read(c, &GC.raw[slot]);
    }
    for (s = 0; s < BR_MAX_GAMEPADS; ++s) {
        if (GC.slots[s] && !seen[s]) {
            BR_INFO("gamepad: '%s' disconnected", GC.raw[s].name);
            msg_v(GC.slots[s], "release");
            GC.slots[s] = (id)0;
            memset(&GC.raw[s], 0, sizeof(GC.raw[s]));
        }
        br_gamepads_sync(&GC.tracker, s, &GC.raw[s], events);
    }
}

static void gc_shutdown(void) {
    int s;
    for (s = 0; s < BR_MAX_GAMEPADS; ++s)
        if (GC.slots[s]) msg_v(GC.slots[s], "release");
    memset(&GC, 0, sizeof(GC));
}
