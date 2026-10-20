/*
 * Windows gamepads: XInput (xinput1_4 / 1_3 / 9_1_0, loaded at run time).
 *
 * XInput covers Xbox controllers natively and most others through Steam
 * Input or the vendor's XInput mode. Four slots are polled each frame;
 * empty slots are only probed every 2 s because XInputGetState on an empty
 * slot is slow. The Guide button comes from the undocumented
 * XInputGetStateEx (ordinal 100) when available.
 *
 * Included by platform_win32.c (after <windows.h>).
 */
#include <xinput.h>
#include <stdio.h>

#ifndef XINPUT_GAMEPAD_GUIDE
#  define XINPUT_GAMEPAD_GUIDE 0x0400
#endif
#define XI_PROBE_MS 2000ull

typedef DWORD (WINAPI *XInputGetState_fn)(DWORD, XINPUT_STATE *);
typedef DWORD (WINAPI *XInputSetState_fn)(DWORD, XINPUT_VIBRATION *);

static struct {
    HMODULE lib;
    XInputGetState_fn get;
    XInputSetState_fn set;
    br_gamepad_tracker tracker;
    br_gamepad_raw raw[BR_MAX_GAMEPADS];
    ULONGLONG next_probe[BR_MAX_GAMEPADS];
} XI;

/* XInput button bit -> Bronto button. */
static const struct { WORD bit; int button; } k_xi_buttons[] = {
    { XINPUT_GAMEPAD_A, BR_PAD_A }, { XINPUT_GAMEPAD_B, BR_PAD_B },
    { XINPUT_GAMEPAD_X, BR_PAD_X }, { XINPUT_GAMEPAD_Y, BR_PAD_Y },
    { XINPUT_GAMEPAD_LEFT_SHOULDER, BR_PAD_LEFT_BUMPER }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, BR_PAD_RIGHT_BUMPER },
    { XINPUT_GAMEPAD_BACK, BR_PAD_BACK }, { XINPUT_GAMEPAD_START, BR_PAD_START },
    { XINPUT_GAMEPAD_GUIDE, BR_PAD_GUIDE },
    { XINPUT_GAMEPAD_LEFT_THUMB, BR_PAD_LEFT_STICK }, { XINPUT_GAMEPAD_RIGHT_THUMB, BR_PAD_RIGHT_STICK },
    { XINPUT_GAMEPAD_DPAD_UP, BR_PAD_DPAD_UP }, { XINPUT_GAMEPAD_DPAD_RIGHT, BR_PAD_DPAD_RIGHT },
    { XINPUT_GAMEPAD_DPAD_DOWN, BR_PAD_DPAD_DOWN }, { XINPUT_GAMEPAD_DPAD_LEFT, BR_PAD_DPAD_LEFT }
};

static float xi_stick(SHORT v) { return v < 0 ? (float)v / 32768.0f : (float)v / 32767.0f; }

static void xi_init(void) {
    const wchar_t *dlls[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
    size_t i;
    memset(&XI, 0, sizeof(XI));
    for (i = 0; i < BR_ARRAY_COUNT(dlls) && !XI.lib; ++i) XI.lib = LoadLibraryW(dlls[i]);
    if (!XI.lib) { BR_WARN("gamepad: XInput not available"); return; }
    /* Ordinal 100 = XInputGetStateEx (same signature, adds the Guide bit). */
    XI.get = W32_PROC(XInputGetState_fn, XI.lib, (const char *)(uintptr_t)100);
    if (!XI.get) XI.get = W32_PROC(XInputGetState_fn, XI.lib, "XInputGetState");
    XI.set = W32_PROC(XInputSetState_fn, XI.lib, "XInputSetState");
}

static void xi_shutdown(void) {
    if (XI.lib) FreeLibrary(XI.lib);
    memset(&XI, 0, sizeof(XI));
}

static void xi_poll(br_events *events) {
    ULONGLONG now = GetTickCount64();
    int pad;
    size_t b;
    for (pad = 0; pad < BR_MAX_GAMEPADS; ++pad) {
        br_gamepad_raw *r = &XI.raw[pad];
        XINPUT_STATE st;
        if (XI.get && (r->connected || now >= XI.next_probe[pad])) {
            memset(&st, 0, sizeof(st));
            if (XI.get((DWORD)pad, &st) == ERROR_SUCCESS) {
                const XINPUT_GAMEPAD *g = &st.Gamepad;
                if (!r->connected) {
                    memset(r, 0, sizeof(*r));
                    snprintf(r->name, sizeof(r->name), "XInput Controller %d", pad + 1);
                    BR_INFO("gamepad: %s connected", r->name);
                }
                r->connected = true;
                r->buttons = 0;
                for (b = 0; b < BR_ARRAY_COUNT(k_xi_buttons); ++b)
                    if (g->wButtons & k_xi_buttons[b].bit) r->buttons |= 1u << k_xi_buttons[b].button;
                r->axes[BR_PAD_AXIS_LEFT_X] = xi_stick(g->sThumbLX);
                r->axes[BR_PAD_AXIS_LEFT_Y] = -xi_stick(g->sThumbLY);    /* XInput is y-up */
                r->axes[BR_PAD_AXIS_RIGHT_X] = xi_stick(g->sThumbRX);
                r->axes[BR_PAD_AXIS_RIGHT_Y] = -xi_stick(g->sThumbRY);
                r->axes[BR_PAD_AXIS_LEFT_TRIGGER] = (float)g->bLeftTrigger / 255.0f;
                r->axes[BR_PAD_AXIS_RIGHT_TRIGGER] = (float)g->bRightTrigger / 255.0f;
            } else {
                if (r->connected) BR_INFO("gamepad: %s disconnected", r->name);
                r->connected = false;
                XI.next_probe[pad] = now + XI_PROBE_MS;
            }
        }
        br_gamepads_sync(&XI.tracker, pad, r, events);
    }
}

static bool xi_rumble(int pad, float lo, float hi) {
    XINPUT_VIBRATION v;
    if (pad < 0 || pad >= BR_MAX_GAMEPADS || !XI.set || !XI.raw[pad].connected) return false;
    v.wLeftMotorSpeed = (WORD)(lo * 65535.0f);     /* heavy, low-frequency motor */
    v.wRightMotorSpeed = (WORD)(hi * 65535.0f);    /* light, high-frequency motor */
    return XI.set((DWORD)pad, &v) == ERROR_SUCCESS;
}
