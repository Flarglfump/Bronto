/*
 * Linux evdev gamepad mapping test (developer tool; no hardware needed).
 *
 * Builds the evdev backend into this program and drives it with the same
 * struct input_event records the kernel produces - through a real pipe, so
 * the non-blocking read loop is exercised too - for three device styles:
 * an Xbox pad (xpad), a PlayStation pad (spec layout) and a generic HID
 * pad with a hat d-pad and digital triggers.
 *
 *   make evdev-test
 */
#define _POSIX_C_SOURCE 200809L
#include "bronto/bronto.h"
#include "../../src/platform/gamepad_evdev.c"

#include <stdlib.h>

static int g_checks, g_fail;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, #c); } } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-3f)

static int g_pipe[2];

static void absinfo(evdev_pad *p, unsigned code, int min, int max) {
    p->abs[code].present = true;
    p->abs[code].min = min;
    p->abs[code].max = max;
}

/* Install a fake device in slot 0 whose "fd" is the read end of a pipe. */
static evdev_pad *fake_device(const char *name, bool sony) {
    evdev_pad *p = &EV.pads[0];
    memset(p, 0, sizeof(*p));
    p->fd = g_pipe[0];
    p->ff_id = -1;
    p->raw.connected = true;
    strcpy(p->raw.name, name);
    strcpy(p->path, "/dev/input/fake");
    p->spec_face_layout = sony;
    return p;
}

static void send_ev(unsigned short type, unsigned short code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(g_pipe[1], &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) abort();
}

static void pump(br_input *in, br_events *events) {
    br_event e;
    br_input_begin_frame(in);
    evdev_poll(events);
    while (br_events_poll(events, &e)) br_input_handle_event(in, &e);
    br_input_end_events(in);
}

int main(void) {
    br_input in;
    br_events events;
    evdev_pad *p;
    int i;
    if (pipe(g_pipe) != 0) return 2;
    fcntl(g_pipe[0], F_SETFL, O_NONBLOCK);
    br_events_init(&events, 64);
    br_input_init(&in);
    memset(&EV, 0, sizeof(EV));
    for (i = 0; i < BR_MAX_GAMEPADS; ++i) EV.pads[i].fd = -1;
    EV.initialised = true;
    EV.next_scan = 1e300;            /* never touch the real /dev/input */
    (void)evdev_init;                /* the real entry point scans hardware */

    /* ---- Xbox controller (xpad driver) ---- */
    p = fake_device("Microsoft X-Box 360 pad", false);
    absinfo(p, ABS_X, -32768, 32767); absinfo(p, ABS_Y, -32768, 32767);
    absinfo(p, ABS_RX, -32768, 32767); absinfo(p, ABS_RY, -32768, 32767);
    absinfo(p, ABS_Z, 0, 255); absinfo(p, ABS_RZ, 0, 255);
    absinfo(p, ABS_HAT0X, -1, 1); absinfo(p, ABS_HAT0Y, -1, 1);
    evdev_layout_axes(p);
    CHECK(p->analog_triggers);
    pump(&in, &events);
    CHECK(br_pad_connected(&in, 0) && strcmp(br_pad_name(&in, 0), "Microsoft X-Box 360 pad") == 0);

    send_ev(EV_KEY, BTN_SOUTH, 1);
    send_ev(EV_KEY, BTN_NORTH, 1);           /* xpad: BTN_X == BTN_NORTH -> X */
    send_ev(EV_KEY, BTN_START, 1);
    send_ev(EV_ABS, ABS_X, 32767);
    send_ev(EV_ABS, ABS_Y, -32768);          /* stick pushed up */
    send_ev(EV_ABS, ABS_RZ, 255);
    send_ev(EV_ABS, ABS_HAT0Y, -1);
    send_ev(EV_SYN, SYN_REPORT, 0);
    pump(&in, &events);
    CHECK(br_pad_pressed(&in, 0, BR_PAD_A) && br_pad_down(&in, 0, BR_PAD_X) && br_pad_down(&in, 0, BR_PAD_START));
    CHECK(!br_pad_down(&in, 0, BR_PAD_Y));
    CHECK(NEAR(in.pads[0].axes[BR_PAD_AXIS_LEFT_X], 1.0f));
    CHECK(NEAR(in.pads[0].axes[BR_PAD_AXIS_LEFT_Y], -1.0f));   /* up = negative */
    CHECK(NEAR(in.pads[0].axes[BR_PAD_AXIS_RIGHT_TRIGGER], 1.0f));
    CHECK(br_pad_down(&in, 0, BR_PAD_DPAD_UP) && !br_pad_down(&in, 0, BR_PAD_DPAD_DOWN));

    send_ev(EV_KEY, BTN_SOUTH, 0);
    send_ev(EV_ABS, ABS_X, 0);               /* centre of -32768..32767 -> exactly 0 */
    send_ev(EV_ABS, ABS_HAT0Y, 1);
    pump(&in, &events);
    CHECK(!br_pad_down(&in, 0, BR_PAD_A));
    CHECK(in.pads[0].axes[BR_PAD_AXIS_LEFT_X] == 0.0f);
    CHECK(!br_pad_down(&in, 0, BR_PAD_DPAD_UP) && br_pad_down(&in, 0, BR_PAD_DPAD_DOWN));

    /* nothing queued: the non-blocking read must not stall or change state */
    pump(&in, &events);
    CHECK(br_pad_down(&in, 0, BR_PAD_X));

    /* unplug: slot closes, everything is released */
    evdev_close_pad(p);
    pump(&in, &events);
    CHECK(!br_pad_connected(&in, 0) && !br_pad_down(&in, 0, BR_PAD_X) && !br_pad_down(&in, 0, BR_PAD_START));

    /* ---- PlayStation controller: kernel spec face layout ---- */
    if (pipe(g_pipe) != 0) return 2;
    fcntl(g_pipe[0], F_SETFL, O_NONBLOCK);
    p = fake_device("Sony Interactive Entertainment Wireless Controller", true);
    absinfo(p, ABS_X, 0, 255); absinfo(p, ABS_Y, 0, 255);
    absinfo(p, ABS_RX, 0, 255); absinfo(p, ABS_RY, 0, 255);
    absinfo(p, ABS_Z, 0, 255); absinfo(p, ABS_RZ, 0, 255);
    evdev_layout_axes(p);
    send_ev(EV_KEY, BTN_WEST, 1);            /* square (left)  -> X */
    send_ev(EV_KEY, BTN_NORTH, 1);           /* triangle (top) -> Y */
    send_ev(EV_ABS, ABS_RX, 255);
    send_ev(EV_ABS, ABS_Z, 128);
    pump(&in, &events);
    CHECK(br_pad_down(&in, 0, BR_PAD_X) && br_pad_down(&in, 0, BR_PAD_Y));
    CHECK(NEAR(in.pads[0].axes[BR_PAD_AXIS_RIGHT_X], 1.0f));
    CHECK(in.pads[0].axes[BR_PAD_AXIS_LEFT_TRIGGER] > 0.49f && in.pads[0].axes[BR_PAD_AXIS_LEFT_TRIGGER] < 0.51f);
    evdev_close_pad(p);
    pump(&in, &events);

    /* ---- generic HID pad: Z/RZ right stick, digital triggers ---- */
    if (pipe(g_pipe) != 0) return 2;
    fcntl(g_pipe[0], F_SETFL, O_NONBLOCK);
    p = fake_device("Generic USB Gamepad", false);
    absinfo(p, ABS_X, 0, 255); absinfo(p, ABS_Y, 0, 255);
    absinfo(p, ABS_Z, 0, 255); absinfo(p, ABS_RZ, 0, 255);
    evdev_layout_axes(p);
    CHECK(!p->analog_triggers);
    send_ev(EV_ABS, ABS_Z, 0);               /* right stick fully left */
    send_ev(EV_ABS, ABS_RZ, 255);            /* ... and down */
    send_ev(EV_KEY, BTN_TR2, 1);             /* digital right trigger */
    pump(&in, &events);
    CHECK(br_pad_connected(&in, 0));
    CHECK(NEAR(in.pads[0].axes[BR_PAD_AXIS_RIGHT_X], -1.0f) && NEAR(in.pads[0].axes[BR_PAD_AXIS_RIGHT_Y], 1.0f));
    CHECK(NEAR(in.pads[0].axes[BR_PAD_AXIS_RIGHT_TRIGGER], 1.0f));
    send_ev(EV_KEY, BTN_TR2, 0);
    pump(&in, &events);
    CHECK(in.pads[0].axes[BR_PAD_AXIS_RIGHT_TRIGGER] == 0.0f);
    CHECK(!evdev_rumble(0, 1, 1));           /* no FF on this device */
    evdev_close_pad(p);
    close(g_pipe[1]);

    /* ---- Steam Input: the Deck's own controls and Steam's virtual pad of them: only the virtual one counts ---- */
    {
        int deck_pipe[2], virt_pipe[2];
        evdev_pad *deck = &EV.pads[0], *virt = &EV.pads[1];
        if (pipe(deck_pipe) != 0 || pipe(virt_pipe) != 0) return 2;
        fcntl(deck_pipe[0], F_SETFL, O_NONBLOCK);
        fcntl(virt_pipe[0], F_SETFL, O_NONBLOCK);
        g_pipe[0] = deck_pipe[0]; g_pipe[1] = deck_pipe[1];
        deck = fake_device("Steam Deck", false);
        deck->vendor = EVDEV_VALVE; deck->product = 0x1205;
        absinfo(deck, ABS_X, -32768, 32767); absinfo(deck, ABS_Y, -32768, 32767);
        evdev_layout_axes(deck);
        EV.pads[1] = *deck;                                  /* (the same, then made the virtual pad) */
        virt->fd = virt_pipe[0];
        virt->product = EVDEV_STEAM_VIRTUAL;
        strcpy(virt->raw.name, "Steam Virtual Gamepad");
        send_ev(EV_KEY, BTN_START, 1);                       /* to the Deck's own device */
        g_pipe[1] = virt_pipe[1];
        send_ev(EV_KEY, BTN_START, 1);                       /* and to the virtual one */
        pump(&in, &events);
        CHECK(!br_pad_connected(&in, 0) && !br_pad_down(&in, 0, BR_PAD_START));
        CHECK(br_pad_connected(&in, 1) && br_pad_pressed(&in, 1, BR_PAD_START));
        evdev_close_pad(virt);                               /* Steam gone: the Deck's own controls count again */
        pump(&in, &events);
        CHECK(br_pad_connected(&in, 0) && br_pad_down(&in, 0, BR_PAD_START) && !br_pad_connected(&in, 1));
        evdev_close_pad(deck);
        close(deck_pipe[1]);
        close(virt_pipe[1]);
        pump(&in, &events);
    }

    /* the real scan must cope with whatever this machine has (or nothing) */
    EV.next_scan = 0;
    pump(&in, &events);
    evdev_shutdown();
    br_events_destroy(&events);
    printf("evdev mapping: %d checks, %d failed -> %s\n", g_checks, g_fail, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
