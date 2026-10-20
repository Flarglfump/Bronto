/*
 * Linux gamepads: evdev (/dev/input/event*), the kernel's native input API.
 * Uses only libc and the kernel UAPI headers that ship with every Linux C
 * toolchain (<linux/input.h>).
 *
 * - Hot-plug: /dev/input is rescanned every 2 s; unplugged pads report
 *   ENODEV and are released.
 * - Mapping: kernel gamepad codes -> Bronto's Xbox-style layout. D-pads
 *   reported as hats become buttons; axes are normalised with the
 *   device's own min/max; digital-only triggers report 0 or 1.
 * - Face buttons: the xpad (Xbox) driver reports X/Y as BTN_NORTH/BTN_WEST
 *   while Sony pads follow the kernel spec (BTN_WEST = left, BTN_NORTH =
 *   top); both are mapped by physical position.
 * - Rumble through force feedback (FF_RUMBLE) when the device supports it.
 *
 * Access: most distributions grant the logged-in user access to game
 * controllers (systemd "uaccess"); otherwise add the user to "input".
 *
 * Included by the Linux window backend (and by tests/platform/evdev_test.c).
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define EVDEV_ABS_TRACKED 0x20            /* ABS codes 0x00..0x1f cover sticks, triggers, hats */
#define EVDEV_RESCAN_SECONDS 2.0
#define EVDEV_BITS(n) (((n) + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long)))
#define EVDEV_TEST_BIT(arr, bit) (((arr)[(bit) / (8 * sizeof(unsigned long))] >> ((bit) % (8 * sizeof(unsigned long)))) & 1ul)

typedef struct evdev_pad {
    int fd;
    char path[300];
    br_gamepad_raw raw;
    struct { int min, max; bool present; } abs[EVDEV_ABS_TRACKED];
    int axis_of_abs[EVDEV_ABS_TRACKED];  /* ABS code -> br_gamepad_axis or -1 */
    bool spec_face_layout;               /* Sony: BTN_WEST is the left face button */
    bool analog_triggers;
    bool has_rumble;
    int ff_id;
} evdev_pad;

static struct {
    evdev_pad pads[BR_MAX_GAMEPADS];
    br_gamepad_tracker tracker;
    double next_scan;
    bool initialised;
} EV;

static double evdev_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ---- mapping (pure: unit-tested by feeding events) ------------------------ */

static int evdev_button(const evdev_pad *p, unsigned code) {
    switch (code) {
    case BTN_SOUTH: return BR_PAD_A;
    case BTN_EAST:  return BR_PAD_B;
    case BTN_NORTH: return p->spec_face_layout ? BR_PAD_Y : BR_PAD_X;
    case BTN_WEST:  return p->spec_face_layout ? BR_PAD_X : BR_PAD_Y;
    case BTN_TL:    return BR_PAD_LEFT_BUMPER;
    case BTN_TR:    return BR_PAD_RIGHT_BUMPER;
    case BTN_SELECT: return BR_PAD_BACK;
    case BTN_START: return BR_PAD_START;
    case BTN_MODE:  return BR_PAD_GUIDE;
    case BTN_THUMBL: return BR_PAD_LEFT_STICK;
    case BTN_THUMBR: return BR_PAD_RIGHT_STICK;
    case BTN_DPAD_UP: return BR_PAD_DPAD_UP;
    case BTN_DPAD_DOWN: return BR_PAD_DPAD_DOWN;
    case BTN_DPAD_LEFT: return BR_PAD_DPAD_LEFT;
    case BTN_DPAD_RIGHT: return BR_PAD_DPAD_RIGHT;
    default: return -1;
    }
}

static void evdev_set_button(evdev_pad *p, int b, bool down) {
    if (down) p->raw.buttons |= 1u << b;
    else p->raw.buttons &= ~(1u << b);
}

/* Decide which ABS code feeds which axis, from the codes the device has. */
static void evdev_layout_axes(evdev_pad *p) {
    int i;
    bool has_rxy = p->abs[ABS_RX].present && p->abs[ABS_RY].present;
    for (i = 0; i < EVDEV_ABS_TRACKED; ++i) p->axis_of_abs[i] = -1;
    p->axis_of_abs[ABS_X] = BR_PAD_AXIS_LEFT_X;
    p->axis_of_abs[ABS_Y] = BR_PAD_AXIS_LEFT_Y;
    if (has_rxy) {           /* Xbox / PlayStation: RX/RY stick, Z/RZ triggers */
        p->axis_of_abs[ABS_RX] = BR_PAD_AXIS_RIGHT_X;
        p->axis_of_abs[ABS_RY] = BR_PAD_AXIS_RIGHT_Y;
        p->axis_of_abs[ABS_Z] = BR_PAD_AXIS_LEFT_TRIGGER;
        p->axis_of_abs[ABS_RZ] = BR_PAD_AXIS_RIGHT_TRIGGER;
    } else {                 /* generic HID: Z/RZ is the right stick */
        p->axis_of_abs[ABS_Z] = BR_PAD_AXIS_RIGHT_X;
        p->axis_of_abs[ABS_RZ] = BR_PAD_AXIS_RIGHT_Y;
    }
    if (p->abs[ABS_BRAKE].present) p->axis_of_abs[ABS_BRAKE] = BR_PAD_AXIS_LEFT_TRIGGER;
    if (p->abs[ABS_GAS].present) p->axis_of_abs[ABS_GAS] = BR_PAD_AXIS_RIGHT_TRIGGER;
    p->analog_triggers = false;
    for (i = 0; i < EVDEV_ABS_TRACKED; ++i)
        if (p->abs[i].present && (p->axis_of_abs[i] == BR_PAD_AXIS_LEFT_TRIGGER || p->axis_of_abs[i] == BR_PAD_AXIS_RIGHT_TRIGGER))
            p->analog_triggers = true;
}

static void evdev_apply_abs(evdev_pad *p, unsigned code, int value) {
    int axis;
    float range, v;
    if (code >= EVDEV_ABS_TRACKED) return;
    if (code == ABS_HAT0X) {
        evdev_set_button(p, BR_PAD_DPAD_LEFT, value < 0);
        evdev_set_button(p, BR_PAD_DPAD_RIGHT, value > 0);
        return;
    }
    if (code == ABS_HAT0Y) {
        evdev_set_button(p, BR_PAD_DPAD_UP, value < 0);
        evdev_set_button(p, BR_PAD_DPAD_DOWN, value > 0);
        return;
    }
    axis = p->axis_of_abs[code];
    if (axis < 0 || !p->abs[code].present) return;
    range = (float)(p->abs[code].max - p->abs[code].min);
    if (range <= 0) return;
    v = (float)(value - p->abs[code].min) / range;              /* 0..1 */
    if (axis == BR_PAD_AXIS_LEFT_TRIGGER || axis == BR_PAD_AXIS_RIGHT_TRIGGER) {
        p->raw.axes[axis] = br_clampf(v, 0, 1);
    } else {
        v = v * 2.0f - 1.0f;
        if (v > -1.0f / 128 && v < 1.0f / 128) v = 0;            /* exact centre for odd ranges */
        p->raw.axes[axis] = br_clampf(v, -1, 1);
    }
}

/* Apply one kernel event to the pad's snapshot. */
static void evdev_apply(evdev_pad *p, const struct input_event *ev) {
    if (ev->type == EV_KEY) {
        int b = evdev_button(p, ev->code);
        if (b >= 0) evdev_set_button(p, b, ev->value != 0);
        else if (!p->analog_triggers && (ev->code == BTN_TL2 || ev->code == BTN_TR2))
            p->raw.axes[ev->code == BTN_TL2 ? BR_PAD_AXIS_LEFT_TRIGGER : BR_PAD_AXIS_RIGHT_TRIGGER] = ev->value ? 1.0f : 0.0f;
    } else if (ev->type == EV_ABS) {
        evdev_apply_abs(p, ev->code, ev->value);
    }
}

/* ---- devices ------------------------------------------------------------------ */

static void evdev_close_pad(evdev_pad *p) {
    if (p->fd >= 0) close(p->fd);
    memset(p, 0, sizeof(*p));
    p->fd = -1;
}

/* Re-read the full current state (initial open, or after SYN_DROPPED). */
static void evdev_resync(evdev_pad *p) {
    unsigned long keys[EVDEV_BITS(KEY_CNT)];
    unsigned code;
    memset(keys, 0, sizeof(keys));
    p->raw.buttons = 0;
    if (ioctl(p->fd, EVIOCGKEY(sizeof(keys)), keys) >= 0) {
        for (code = BTN_MISC; code < KEY_CNT; ++code) {
            struct input_event ev;
            if (!EVDEV_TEST_BIT(keys, code)) continue;
            memset(&ev, 0, sizeof(ev));
            ev.type = EV_KEY;
            ev.code = (unsigned short)code;
            ev.value = 1;
            evdev_apply(p, &ev);
        }
    }
    for (code = 0; code < EVDEV_ABS_TRACKED; ++code) {
        struct input_absinfo ai;
        if (!p->abs[code].present || ioctl(p->fd, EVIOCGABS(code), &ai) < 0) continue;
        evdev_apply_abs(p, code, ai.value);
    }
}

static bool evdev_try_open(evdev_pad *p, const char *path) {
    unsigned long evbits[EVDEV_BITS(EV_CNT)], keybits[EVDEV_BITS(KEY_CNT)], absbits[EVDEV_BITS(ABS_CNT)];
    unsigned long ffbits[EVDEV_BITS(FF_CNT)];
    struct input_id id;
    unsigned code;
    int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);   /* no rumble then */
    if (fd < 0) return false;
    memset(evbits, 0, sizeof(evbits));
    memset(keybits, 0, sizeof(keybits));
    memset(absbits, 0, sizeof(absbits));
    memset(ffbits, 0, sizeof(ffbits));
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0 ||
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0 ||
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) < 0 ||
        !EVDEV_TEST_BIT(evbits, EV_KEY) || !EVDEV_TEST_BIT(evbits, EV_ABS) ||
        !EVDEV_TEST_BIT(keybits, BTN_GAMEPAD) || !EVDEV_TEST_BIT(absbits, ABS_X)) {
        close(fd);   /* not a gamepad (keyboard, mouse, touchpad, joystick...) */
        return false;
    }
    memset(p, 0, sizeof(*p));
    p->fd = fd;
    p->ff_id = -1;
    snprintf(p->path, sizeof(p->path), "%s", path);
    p->raw.connected = true;
    if (ioctl(fd, EVIOCGNAME(sizeof(p->raw.name) - 1), p->raw.name) < 0) strcpy(p->raw.name, "Gamepad");
    if (ioctl(fd, EVIOCGID, &id) >= 0) p->spec_face_layout = id.vendor == 0x054c;   /* Sony */
    for (code = 0; code < EVDEV_ABS_TRACKED; ++code) {
        struct input_absinfo ai;
        if (!EVDEV_TEST_BIT(absbits, code) || ioctl(fd, EVIOCGABS(code), &ai) < 0) continue;
        p->abs[code].present = true;
        p->abs[code].min = ai.minimum;
        p->abs[code].max = ai.maximum;
    }
    evdev_layout_axes(p);
    if (EVDEV_TEST_BIT(evbits, EV_FF) && ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ffbits)), ffbits) >= 0)
        p->has_rumble = EVDEV_TEST_BIT(ffbits, FF_RUMBLE) != 0;
    evdev_resync(p);
    BR_INFO("gamepad: '%s' on %s%s", p->raw.name, path, p->has_rumble ? " (rumble)" : "");
    return true;
}

static bool evdev_path_open(const char *path) {
    int i;
    for (i = 0; i < BR_MAX_GAMEPADS; ++i)
        if (EV.pads[i].fd >= 0 && strcmp(EV.pads[i].path, path) == 0) return true;
    return false;
}

static void evdev_scan(void) {
    DIR *dir = opendir("/dev/input");
    struct dirent *ent;
    if (!dir) return;
    while ((ent = readdir(dir)) != NULL) {
        char path[300];
        int slot;
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        if (evdev_path_open(path)) continue;
        for (slot = 0; slot < BR_MAX_GAMEPADS && EV.pads[slot].fd >= 0; ++slot) {}
        if (slot == BR_MAX_GAMEPADS) break;   /* all slots taken */
        evdev_try_open(&EV.pads[slot], path);
    }
    closedir(dir);
}

static void evdev_init(void) {
    int i;
    memset(&EV, 0, sizeof(EV));
    for (i = 0; i < BR_MAX_GAMEPADS; ++i) EV.pads[i].fd = -1;
    EV.initialised = true;
    evdev_scan();
    EV.next_scan = evdev_now() + EVDEV_RESCAN_SECONDS;
}

static void evdev_shutdown(void) {
    int i;
    if (!EV.initialised) return;
    for (i = 0; i < BR_MAX_GAMEPADS; ++i) evdev_close_pad(&EV.pads[i]);
    EV.initialised = false;
}

static void evdev_poll(br_events *events) {
    int i;
    if (!EV.initialised) return;
    if (evdev_now() >= EV.next_scan) {
        evdev_scan();
        EV.next_scan = evdev_now() + EVDEV_RESCAN_SECONDS;
    }
    for (i = 0; i < BR_MAX_GAMEPADS; ++i) {
        evdev_pad *p = &EV.pads[i];
        if (p->fd >= 0) {
            struct input_event buf[64];
            for (;;) {
                ssize_t n = read(p->fd, buf, sizeof(buf));
                size_t k;
                if (n < 0) {
                    if (errno != EAGAIN && errno != EINTR) {   /* ENODEV: unplugged */
                        BR_INFO("gamepad: '%s' disconnected", p->raw.name);
                        evdev_close_pad(p);
                    }
                    break;
                }
                for (k = 0; k < (size_t)n / sizeof(buf[0]); ++k) {
                    if (buf[k].type == EV_SYN && buf[k].code == SYN_DROPPED) evdev_resync(p);
                    else evdev_apply(p, &buf[k]);
                }
                if ((size_t)n < sizeof(buf)) break;
            }
        }
        br_gamepads_sync(&EV.tracker, i, &p->raw, events);   /* fd < 0 => raw is zeroed */
    }
}

static bool evdev_rumble(int pad, float lo, float hi) {
    evdev_pad *p;
    struct ff_effect fx;
    struct input_event play;
    if (pad < 0 || pad >= BR_MAX_GAMEPADS) return false;
    p = &EV.pads[pad];
    if (p->fd < 0 || !p->has_rumble) return false;
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    if (lo <= 0 && hi <= 0) {
        if (p->ff_id >= 0) {
            play.code = (unsigned short)p->ff_id;
            play.value = 0;
            if (write(p->fd, &play, sizeof(play)) < 0) return false;
        }
        return true;
    }
    memset(&fx, 0, sizeof(fx));
    fx.type = FF_RUMBLE;
    fx.id = (short)p->ff_id;                     /* -1 creates, else updates */
    fx.u.rumble.strong_magnitude = (unsigned short)(lo * 65535.0f);
    fx.u.rumble.weak_magnitude = (unsigned short)(hi * 65535.0f);
    fx.replay.length = 0xFFFF;                   /* the engine stops it */
    if (ioctl(p->fd, EVIOCSFF, &fx) < 0) return false;
    p->ff_id = fx.id;
    play.code = (unsigned short)fx.id;
    play.value = 1;
    return write(p->fd, &play, sizeof(play)) == (ssize_t)sizeof(play);
}
