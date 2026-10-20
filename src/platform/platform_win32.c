/*
 * Windows backend: Win32 window + GDI presentation.
 *
 * - Per-monitor DPI aware (v2 where available); sizes requested in points
 *   are converted with the window's DPI, and moving between monitors with
 *   different scaling resizes the window as Windows suggests.
 * - Presentation: StretchDIBits from a BGRA staging copy of the framebuffer
 *   into the engine's viewport, black PatBlt bars around it.
 * - Fullscreen is borderless-windowed covering the current monitor (instant
 *   Alt+Tab, no display mode change).
 * - Audio: waveOut (audio_waveout.c). Gamepads: XInput (gamepad_xinput.c).
 * - Only system DLLs are used: user32, gdi32 (linked), winmm (loaded at run
 *   time for 1 ms timer resolution and audio), ole32 and shell32 (loaded
 *   for the folder dialog and opening files).
 *
 * Included by platform_native.c.
 */
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0601            /* Windows 7: IFileOpenDialog */
#endif
#include <windows.h>
#include <shobjidl.h>                  /* IFileOpenDialog (declarations only) */
#include <string.h>

#include "bronto/platform.h"
#include "bronto/log.h"
#include "bronto/memory.h"

#if defined(_MSC_VER)
#  pragma comment(lib, "user32.lib")
#  pragma comment(lib, "gdi32.lib")
#endif

#ifndef WM_DPICHANGED
#  define WM_DPICHANGED 0x02E0
#endif
#ifndef WM_MOUSEHWHEEL
#  define WM_MOUSEHWHEEL 0x020E
#endif

#define W32_PENDING_MAX 1024

typedef BOOL (WINAPI *SetProcessDpiAwarenessContext_fn)(HANDLE);
typedef BOOL (WINAPI *SetProcessDPIAware_fn)(void);
typedef UINT (WINAPI *GetDpiForWindow_fn)(HWND);
typedef BOOL (WINAPI *AdjustWindowRectExForDpi_fn)(LPRECT, DWORD, BOOL, DWORD, UINT);
typedef UINT (WINAPI *timeBeginPeriod_fn)(UINT);

static struct {
    HINSTANCE inst;
    HWND hwnd;
    HICON icon_big, icon_small;     /* set by br_window_set_icon */
    HDC hdc;
    br_platform_config cfg;
    int width, height;          /* client area, pixels */
    UINT dpi;
    bool fullscreen, minimized, maximized, cursor_hidden, mouse_tracked;
    WINDOWPLACEMENT saved_placement;
    LONG saved_style;
    br_event pending[W32_PENDING_MAX];
    int pending_count;
    uint32_t *staging;
    size_t staging_size;
    const br_image *last_fb;    /* for repainting during modal resize */
    LARGE_INTEGER freq;
    WCHAR high_surrogate;
    float last_mx, last_my;
    int buttons_down;
    GetDpiForWindow_fn GetDpiForWindow;
    AdjustWindowRectExForDpi_fn AdjustWindowRectExForDpi;
    HMODULE winmm;
} W;

/* GetProcAddress returns FARPROC; copy through memcpy so strict compilers
 * do not warn about casting between incompatible function types. */
typedef void (*w32_any_fn)(void);
static w32_any_fn w32_proc(HMODULE m, const char *name) {
    FARPROC p = m ? GetProcAddress(m, name) : NULL;
    w32_any_fn f;
    memcpy(&f, &p, sizeof(f));
    return f;
}
#define W32_PROC(T, m, name) ((T)(void (*)(void))w32_proc((m), (name)))

#include "audio_waveout.c"
#include "gamepad_xinput.c"

static void w32_push(const br_event *e) {
    if (W.pending_count < W32_PENDING_MAX) W.pending[W.pending_count++] = *e;
}
static void w32_push_simple(uint32_t type) { br_event e = br_event_make(type); w32_push(&e); }

static float w32_scale(void) { return W.dpi ? (float)W.dpi / 96.0f : 1.0f; }

static DWORD w32_style(void) {
    DWORD s = WS_OVERLAPPEDWINDOW;
    if (!W.cfg.resizable) s &= ~(DWORD)(WS_THICKFRAME | WS_MAXIMIZEBOX);
    return s;
}

/* Outer window size for a client size in pixels. */
static void w32_outer_size(int cw, int ch, int *ow, int *oh) {
    RECT r;
    r.left = 0; r.top = 0; r.right = cw; r.bottom = ch;
    if (W.AdjustWindowRectExForDpi) W.AdjustWindowRectExForDpi(&r, w32_style(), FALSE, 0, W.dpi);
    else AdjustWindowRectEx(&r, w32_style(), FALSE, 0);
    *ow = r.right - r.left;
    *oh = r.bottom - r.top;
}

/* ---- keyboard ---------------------------------------------------------------- */
static br_key w32_translate_key(WPARAM vk, LPARAM lp) {
    bool extended = (lp & (1 << 24)) != 0;
    if (vk >= 'A' && vk <= 'Z') return (br_key)(BR_KEY_A + (int)(vk - 'A'));
    if (vk >= '0' && vk <= '9') return (br_key)(BR_KEY_0 + (int)(vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12) return (br_key)(BR_KEY_F1 + (int)(vk - VK_F1));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return (br_key)(BR_KEY_KP_0 + (int)(vk - VK_NUMPAD0));
    switch (vk) {
    case VK_SPACE: return BR_KEY_SPACE;
    case VK_OEM_7: return BR_KEY_APOSTROPHE;
    case VK_OEM_COMMA: return BR_KEY_COMMA;
    case VK_OEM_MINUS: return BR_KEY_MINUS;
    case VK_OEM_PERIOD: return BR_KEY_PERIOD;
    case VK_OEM_2: return BR_KEY_SLASH;
    case VK_OEM_1: return BR_KEY_SEMICOLON;
    case VK_OEM_PLUS: return BR_KEY_EQUAL;
    case VK_OEM_4: return BR_KEY_LEFT_BRACKET;
    case VK_OEM_5: return BR_KEY_BACKSLASH;
    case VK_OEM_6: return BR_KEY_RIGHT_BRACKET;
    case VK_OEM_3: return BR_KEY_GRAVE;
    case VK_ESCAPE: return BR_KEY_ESCAPE;
    case VK_RETURN: return extended ? BR_KEY_KP_ENTER : BR_KEY_ENTER;
    case VK_TAB: return BR_KEY_TAB;
    case VK_BACK: return BR_KEY_BACKSPACE;
    case VK_INSERT: return BR_KEY_INSERT;
    case VK_DELETE: return BR_KEY_DELETE;
    case VK_RIGHT: return BR_KEY_RIGHT;
    case VK_LEFT: return BR_KEY_LEFT;
    case VK_DOWN: return BR_KEY_DOWN;
    case VK_UP: return BR_KEY_UP;
    case VK_PRIOR: return BR_KEY_PAGE_UP;
    case VK_NEXT: return BR_KEY_PAGE_DOWN;
    case VK_HOME: return BR_KEY_HOME;
    case VK_END: return BR_KEY_END;
    case VK_CAPITAL: return BR_KEY_CAPS_LOCK;
    case VK_SCROLL: return BR_KEY_SCROLL_LOCK;
    case VK_NUMLOCK: return BR_KEY_NUM_LOCK;
    case VK_SNAPSHOT: return BR_KEY_PRINT_SCREEN;
    case VK_PAUSE: return BR_KEY_PAUSE;
    case VK_DECIMAL: return BR_KEY_KP_DECIMAL;
    case VK_DIVIDE: return BR_KEY_KP_DIVIDE;
    case VK_MULTIPLY: return BR_KEY_KP_MULTIPLY;
    case VK_SUBTRACT: return BR_KEY_KP_SUBTRACT;
    case VK_ADD: return BR_KEY_KP_ADD;
    case VK_SHIFT: {
        UINT sc = (UINT)((lp >> 16) & 0xFF);
        return MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT ? BR_KEY_RIGHT_SHIFT : BR_KEY_LEFT_SHIFT;
    }
    case VK_CONTROL: return extended ? BR_KEY_RIGHT_CONTROL : BR_KEY_LEFT_CONTROL;
    case VK_MENU: return extended ? BR_KEY_RIGHT_ALT : BR_KEY_LEFT_ALT;
    case VK_LWIN: return BR_KEY_LEFT_SUPER;
    case VK_RWIN: return BR_KEY_RIGHT_SUPER;
    case VK_APPS: return BR_KEY_MENU;
    default: return BR_KEY_UNKNOWN;
    }
}

static uint32_t w32_mods(void) {
    uint32_t m = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= BR_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= BR_MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000) m |= BR_MOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) m |= BR_MOD_SUPER;
    if (GetKeyState(VK_CAPITAL) & 1) m |= BR_MOD_CAPS_LOCK;
    return m;
}

/* ---- presentation ----------------------------------------------------------- */
static void w32_blit(const br_image *fb, br_recti vp) {
    BITMAPINFO bmi;
    size_t n = (size_t)fb->width * (size_t)fb->height, i;
    if (!W.hdc || W.width <= 0 || W.height <= 0) return;
    if (n > W.staging_size) {
        uint32_t *s = (uint32_t *)br_realloc(W.staging, n * sizeof(uint32_t));
        if (!s) return;
        W.staging = s;
        W.staging_size = n;
    }
    for (i = 0; i < n; ++i) {   /* RGBA bytes -> 0x00RRGGBB (BGRA in memory) */
        br_color c = fb->pixels[i];
        W.staging[i] = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
    }
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = fb->width;
    bmi.bmiHeader.biHeight = -fb->height;   /* negative = top-down rows */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(W.hdc, vp.x, vp.y, vp.w, vp.h, 0, 0, fb->width, fb->height, W.staging, &bmi,
                  DIB_RGB_COLORS, SRCCOPY);
    if (vp.y > 0) PatBlt(W.hdc, 0, 0, W.width, vp.y, BLACKNESS);
    if (vp.y + vp.h < W.height) PatBlt(W.hdc, 0, vp.y + vp.h, W.width, W.height - (vp.y + vp.h), BLACKNESS);
    if (vp.x > 0) PatBlt(W.hdc, 0, vp.y, vp.x, vp.h, BLACKNESS);
    if (vp.x + vp.w < W.width) PatBlt(W.hdc, vp.x + vp.w, vp.y, W.width - (vp.x + vp.w), vp.h, BLACKNESS);
}

static void w32_present(const br_image *fb, br_recti vp) {
    W.last_fb = fb;
    w32_blit(fb, vp);
}

/* ---- window procedure ----------------------------------------------------- */
static void w32_mouse_button(br_mouse_button b, bool down, LPARAM lp) {
    br_event e = br_event_mouse_button(down ? BR_EVENT_MOUSE_BUTTON_DOWN : BR_EVENT_MOUSE_BUTTON_UP, b,
                                       (float)(short)LOWORD(lp), (float)(short)HIWORD(lp));
    /* Capture so drags that leave the window still report the release. */
    if (down) { if (W.buttons_down++ == 0) SetCapture(W.hwnd); }
    else if (W.buttons_down > 0 && --W.buttons_down == 0) ReleaseCapture();
    w32_push(&e);
}

static LRESULT CALLBACK w32_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
        w32_push_simple(BR_EVENT_WINDOW_CLOSE_REQUESTED);
        return 0;
    case WM_SIZE: {
        int w = (int)LOWORD(lp), h = (int)HIWORD(lp);
        if (wp == SIZE_MINIMIZED) {
            if (!W.minimized) { W.minimized = true; w32_push_simple(BR_EVENT_WINDOW_MINIMIZED); }
            return 0;
        }
        if (W.minimized) { W.minimized = false; w32_push_simple(BR_EVENT_WINDOW_RESTORED); }
        if (wp == SIZE_MAXIMIZED && !W.maximized) { W.maximized = true; w32_push_simple(BR_EVENT_WINDOW_MAXIMIZED); }
        else if (wp == SIZE_RESTORED && W.maximized) { W.maximized = false; w32_push_simple(BR_EVENT_WINDOW_RESTORED); }
        if (w != W.width || h != W.height) {
            br_event e = br_event_make(BR_EVENT_WINDOW_RESIZED);
            W.width = w;
            W.height = h;
            e.u.resize.width = w;
            e.u.resize.height = h;
            w32_push(&e);
        }
        return 0;
    }
    case WM_MOVE: {
        br_event e = br_event_make(BR_EVENT_WINDOW_MOVED);
        e.u.window.x = (short)LOWORD(lp);
        e.u.window.y = (short)HIWORD(lp);
        w32_push(&e);
        return 0;
    }
    case WM_SETFOCUS:  w32_push_simple(BR_EVENT_WINDOW_FOCUS_GAINED); return 0;
    case WM_KILLFOCUS: w32_push_simple(BR_EVENT_WINDOW_FOCUS_LOST); return 0;
    case WM_DPICHANGED: {
        const RECT *r = (const RECT *)lp;
        br_event e = br_event_make(BR_EVENT_WINDOW_SCALE_CHANGED);
        W.dpi = HIWORD(wp);
        e.u.window.scale = w32_scale();
        w32_push(&e);
        if (!W.fullscreen)
            SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        if (W.cfg.min_width > 0 && W.cfg.min_height > 0 && !W.fullscreen && W.hwnd) {
            int ow, oh;
            w32_outer_size((int)((float)W.cfg.min_width * w32_scale() + 0.5f),
                           (int)((float)W.cfg.min_height * w32_scale() + 0.5f), &ow, &oh);
            mm->ptMinTrackSize.x = ow;
            mm->ptMinTrackSize.y = oh;
        }
        return 0;
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
    case WM_KEYUP:   case WM_SYSKEYUP: {
        bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        br_key key = w32_translate_key(wp, lp);
        br_event e = br_event_key(down ? BR_EVENT_KEY_DOWN : BR_EVENT_KEY_UP, key, w32_mods(),
                                  down && (lp & (1 << 30)) != 0);
        e.u.key.scancode = (int)((lp >> 16) & 0x1FF);
        if (!down && key == BR_KEY_PRINT_SCREEN) {   /* only key-up is reported */
            br_event d = e;
            d.type = BR_EVENT_KEY_DOWN;
            w32_push(&d);
        }
        w32_push(&e);
        if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) break;  /* keep Alt+F4 working */
        return 0;
    }
    case WM_CHAR: {
        uint32_t cp = (uint32_t)wp;
        if (cp >= 0xD800 && cp <= 0xDBFF) { W.high_surrogate = (WCHAR)cp; return 0; }
        if (cp >= 0xDC00 && cp <= 0xDFFF) {
            if (!W.high_surrogate) return 0;
            cp = 0x10000u + (((uint32_t)W.high_surrogate - 0xD800u) << 10) + (cp - 0xDC00u);
            W.high_surrogate = 0;
        }
        if (cp >= 32 && cp != 127) {
            br_event e = br_event_make(BR_EVENT_TEXT_INPUT);
            e.u.text.codepoint = cp;
            w32_push(&e);
        }
        return 0;
    }
    case WM_SYSCHAR:
        return 0;   /* no beep for Alt+key without a menu */
    case WM_MOUSEMOVE: {
        float x = (float)(short)LOWORD(lp), y = (float)(short)HIWORD(lp);
        br_event e = br_event_mouse_move(x, y, x - W.last_mx, y - W.last_my);
        if (!W.mouse_tracked) {
            TRACKMOUSEEVENT t;
            memset(&t, 0, sizeof(t));
            t.cbSize = sizeof(t);
            t.dwFlags = TME_LEAVE;
            t.hwndTrack = hwnd;
            TrackMouseEvent(&t);
            W.mouse_tracked = true;
            w32_push_simple(BR_EVENT_MOUSE_ENTER);
        }
        W.last_mx = x;
        W.last_my = y;
        w32_push(&e);
        return 0;
    }
    case WM_MOUSELEAVE:
        W.mouse_tracked = false;
        w32_push_simple(BR_EVENT_MOUSE_LEAVE);
        return 0;
    case WM_LBUTTONDOWN: w32_mouse_button(BR_MOUSE_LEFT, true, lp); return 0;
    case WM_LBUTTONUP:   w32_mouse_button(BR_MOUSE_LEFT, false, lp); return 0;
    case WM_RBUTTONDOWN: w32_mouse_button(BR_MOUSE_RIGHT, true, lp); return 0;
    case WM_RBUTTONUP:   w32_mouse_button(BR_MOUSE_RIGHT, false, lp); return 0;
    case WM_MBUTTONDOWN: w32_mouse_button(BR_MOUSE_MIDDLE, true, lp); return 0;
    case WM_MBUTTONUP:   w32_mouse_button(BR_MOUSE_MIDDLE, false, lp); return 0;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        w32_mouse_button(HIWORD(wp) == XBUTTON1 ? BR_MOUSE_X1 : BR_MOUSE_X2, msg == WM_XBUTTONDOWN, lp);
        return TRUE;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        br_event e = br_event_make(BR_EVENT_MOUSE_WHEEL);
        float d = (float)(short)HIWORD(wp) / (float)WHEEL_DELTA;
        if (msg == WM_MOUSEWHEEL) e.u.wheel.dy = d; else e.u.wheel.dx = d;
        w32_push(&e);
        return 0;
    }
    case WM_SETCURSOR:
        if (W.cursor_hidden && LOWORD(lp) == HTCLIENT) { SetCursor(NULL); return TRUE; }
        break;
    case WM_SYSCOMMAND: {
        WPARAM cmd = wp & 0xFFF0;
        if (cmd == SC_KEYMENU) return 0;   /* F10 / Alt would freeze the game in menu mode */
        if ((cmd == SC_SCREENSAVE || cmd == SC_MONITORPOWER) && W.fullscreen) return 0;
        break;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        /* While the user drags the border, Windows runs a modal loop and the
         * game is not ticking: keep showing the last frame, fitted. */
        if (W.last_fb && W.last_fb->pixels) {
            br_viewport vp = br_viewport_compute(W.last_fb->width, W.last_fb->height, W.width, W.height, BR_SCALE_FIT);
            w32_blit(W.last_fb, vp.rect);
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- window control ---------------------------------------------------------- */
static void w32_set_title(const char *title) {
    WCHAR buf[512];
    if (!MultiByteToWideChar(CP_UTF8, 0, title, -1, buf, (int)BR_ARRAY_COUNT(buf))) buf[0] = 0;
    SetWindowTextW(W.hwnd, buf);
}

static void w32_set_window_size(int wpt, int hpt) {
    int ow, oh;
    if (W.fullscreen) return;
    if (wpt < W.cfg.min_width) wpt = W.cfg.min_width;
    if (hpt < W.cfg.min_height) hpt = W.cfg.min_height;
    w32_outer_size((int)((float)wpt * w32_scale() + 0.5f), (int)((float)hpt * w32_scale() + 0.5f), &ow, &oh);
    SetWindowPos(W.hwnd, NULL, 0, 0, ow, oh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void w32_set_fullscreen(bool fs) {
    br_event e;
    if (fs == W.fullscreen) return;
    if (fs) {
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        W.saved_placement.length = sizeof(W.saved_placement);
        GetWindowPlacement(W.hwnd, &W.saved_placement);
        W.saved_style = GetWindowLongW(W.hwnd, GWL_STYLE);
        GetMonitorInfoW(MonitorFromWindow(W.hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        W.fullscreen = true;
        SetWindowLongW(W.hwnd, GWL_STYLE,
                       (LONG)(((DWORD)W.saved_style & ~(DWORD)WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE));
        SetWindowPos(W.hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        W.fullscreen = false;
        SetWindowLongW(W.hwnd, GWL_STYLE, W.saved_style);
        SetWindowPlacement(W.hwnd, &W.saved_placement);
        SetWindowPos(W.hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    e = br_event_make(BR_EVENT_WINDOW_FULLSCREEN);
    e.u.window.fullscreen = fs;
    w32_push(&e);
}

static void w32_set_cursor_visible(bool visible) {
    POINT p;
    RECT r;
    W.cursor_hidden = !visible;
    /* Apply immediately if the pointer is over the client area. */
    if (GetCursorPos(&p) && ScreenToClient(W.hwnd, &p) && GetClientRect(W.hwnd, &r) &&
        p.x >= r.left && p.y >= r.top && p.x < r.right && p.y < r.bottom)
        SetCursor(visible ? LoadCursor(NULL, IDC_ARROW) : NULL);
}

/* ---- lifecycle ------------------------------------------------------------------ */
static bool w32_init(const br_platform_config *cfg) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    SetProcessDpiAwarenessContext_fn set_ctx;
    WNDCLASSEXW wc;
    RECT work, cr;
    int cw, ch, ow, oh, x, y;

    memset(&W, 0, sizeof(W));
    W.cfg = *cfg;
    W.inst = GetModuleHandleW(NULL);
    QueryPerformanceFrequency(&W.freq);

    /* DPI awareness: per-monitor v2 (Windows 10 1703+), else system-aware. */
    set_ctx = W32_PROC(SetProcessDpiAwarenessContext_fn, user32, "SetProcessDpiAwarenessContext");
    if (!set_ctx || !set_ctx((HANDLE)(intptr_t)-4)) {
        SetProcessDPIAware_fn aware = W32_PROC(SetProcessDPIAware_fn, user32, "SetProcessDPIAware");
        if (aware) aware();
    }
    W.GetDpiForWindow = W32_PROC(GetDpiForWindow_fn, user32, "GetDpiForWindow");
    W.AdjustWindowRectExForDpi = W32_PROC(AdjustWindowRectExForDpi_fn, user32, "AdjustWindowRectExForDpi");

    /* 1 ms sleep granularity for the frame limiter. */
    W.winmm = LoadLibraryW(L"winmm.dll");
    {
        timeBeginPeriod_fn tbp = W32_PROC(timeBeginPeriod_fn, W.winmm, "timeBeginPeriod");
        if (tbp) tbp(1);
    }

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = w32_wndproc;
    wc.hInstance = W.inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = L"BrontoWindow";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        BR_ERROR("Win32: RegisterClassEx failed (%lu)", (unsigned long)GetLastError());
        return false;
    }

    W.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Bronto", w32_style(), CW_USEDEFAULT, CW_USEDEFAULT,
                             640, 480, NULL, NULL, W.inst, NULL);
    if (!W.hwnd) {
        BR_ERROR("Win32: CreateWindowEx failed (%lu)", (unsigned long)GetLastError());
        return false;
    }
    W.dpi = W.GetDpiForWindow ? W.GetDpiForWindow(W.hwnd) : 96;
    if (!W.dpi) W.dpi = 96;
    W.hdc = GetDC(W.hwnd);
    SetStretchBltMode(W.hdc, COLORONCOLOR);
    w32_set_title(cfg->title ? cfg->title : "Bronto");

    /* Size the client area (points -> pixels) and centre on the work area. */
    cw = (int)((float)(cfg->width > 0 ? cfg->width : 960) * w32_scale() + 0.5f);
    ch = (int)((float)(cfg->height > 0 ? cfg->height : 540) * w32_scale() + 0.5f);
    w32_outer_size(cw, ch, &ow, &oh);
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        work.left = work.top = 0;
        work.right = ow;
        work.bottom = oh;
    }
    x = work.left + ((work.right - work.left) - ow) / 2;
    y = work.top + ((work.bottom - work.top) - oh) / 2;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
    SetWindowPos(W.hwnd, NULL, x, y, ow, oh, SWP_NOZORDER | SWP_NOACTIVATE);
    GetClientRect(W.hwnd, &cr);
    W.width = cr.right - cr.left;
    W.height = cr.bottom - cr.top;
    W.pending_count = 0;   /* drop the setup-time WM_SIZE/WM_MOVE noise */
    if (!cfg->hidden) {
        ShowWindow(W.hwnd, SW_SHOW);
        SetForegroundWindow(W.hwnd);
        UpdateWindow(W.hwnd);
    }
    if (cfg->fullscreen) w32_set_fullscreen(true);
    if (cfg->audio_sample_rate > 0) wo_open(cfg->audio_sample_rate, cfg->audio_latency_ms);
    xi_init();
    BR_INFO("Win32: %dx%d client area at %u DPI", W.width, W.height, (unsigned)W.dpi);
    return true;
}

/* A 32-bit icon (straight alpha) of the given size, made from `icon`. */
static HICON w32_make_icon(const br_image *icon, int size) {
    BITMAPV5HEADER bi;
    br_image small;
    void *bits = NULL;
    HDC dc;
    HBITMAP color, mask;
    ICONINFO ii;
    HICON result;
    int p;
    if (!br_image_downscale(icon, &small, size, size)) return NULL;
    memset(&bi, 0, sizeof(bi));
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;                       /* top-down */
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    dc = GetDC(NULL);
    color = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!color || !bits) { br_image_destroy(&small); return NULL; }
    for (p = 0; p < size * size; ++p) {
        br_color c = small.pixels[p];
        ((uint32_t *)bits)[p] = ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
    }
    br_image_destroy(&small);
    mask = CreateBitmap(size, size, 1, 1, NULL);
    memset(&ii, 0, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    result = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return result;
}

static void w32_set_icon(const br_image *icon) {
    HICON big = NULL, small = NULL;
    if (!W.hwnd) return;
    if (icon) {
        big = w32_make_icon(icon, GetSystemMetrics(SM_CXICON));
        small = w32_make_icon(icon, GetSystemMetrics(SM_CXSMICON));
    }
    SendMessageW(W.hwnd, WM_SETICON, ICON_BIG, (LPARAM)(big ? big : LoadIcon(NULL, IDI_APPLICATION)));
    SendMessageW(W.hwnd, WM_SETICON, ICON_SMALL, (LPARAM)(small ? small : LoadIcon(NULL, IDI_APPLICATION)));
    if (W.icon_big) DestroyIcon(W.icon_big);
    if (W.icon_small) DestroyIcon(W.icon_small);
    W.icon_big = big;
    W.icon_small = small;
}

static void w32_shutdown(void) {
    wo_close();
    xi_shutdown();
    if (!W.hwnd) return;
    if (W.winmm) {
        timeBeginPeriod_fn tep = W32_PROC(timeBeginPeriod_fn, W.winmm, "timeEndPeriod");
        if (tep) tep(1);
        FreeLibrary(W.winmm);
    }
    ReleaseDC(W.hwnd, W.hdc);
    DestroyWindow(W.hwnd);
    if (W.icon_big) DestroyIcon(W.icon_big);
    if (W.icon_small) DestroyIcon(W.icon_small);
    br_free(W.staging);
    memset(&W, 0, sizeof(W));
}

static void w32_poll(br_events *events) {
    MSG msg;
    int i;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { w32_push_simple(BR_EVENT_QUIT); continue; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    for (i = 0; i < W.pending_count; ++i) br_events_post(events, &W.pending[i]);
    W.pending_count = 0;
    xi_poll(events);
}

static double w32_time(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)W.freq.QuadPart;
}

static void w32_sleep(double s) {
    double target = w32_time() + s;
    /* Coarse sleep, then a short spin for accuracy. */
    if (s > 0.002) Sleep((DWORD)((s - 0.0015) * 1000.0));
    while (w32_time() < target) Sleep(0);
}

static uint32_t w32_audio_wanted(void) { return wo_wanted(); }
static void w32_audio_submit(const int16_t *d, uint32_t n) { wo_submit(d, n); }
static void w32_get_window_size(int *w, int *h) { *w = W.width; *h = W.height; }
static float w32_get_window_scale(void) { return w32_scale(); }

static bool w32_set_gamepad_rumble(int pad, float lo, float hi) { return xi_rumble(pad, lo, hi); }

/* ---- desktop: folder dialog and opening files ------------------------------
 * The Vista+ IFileOpenDialog in folder mode, called through its vtable
 * from C. ole32 and shell32 are loaded at run time, and the two GUIDs are
 * spelled out here, so nothing extra is linked. */
typedef HRESULT (WINAPI *CoInitializeEx_fn)(LPVOID, DWORD);
typedef void    (WINAPI *CoUninitialize_fn)(void);
typedef HRESULT (WINAPI *CoCreateInstance_fn)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *);
typedef void    (WINAPI *CoTaskMemFree_fn)(LPVOID);
typedef HINSTANCE (WINAPI *ShellExecuteW_fn)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT);

static const GUID k_clsid_file_open_dialog = { 0xDC1C5A9C, 0xE88A, 0x4DDE, { 0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7 } };
static const GUID k_iid_file_open_dialog   = { 0xD57C7288, 0xD4AD, 0x4768, { 0xBE, 0x02, 0x9D, 0x96, 0x95, 0x32, 0xD9, 0x60 } };

static int w32_utf8_to_wide(const char *s, wchar_t *out, int count) { return MultiByteToWideChar(CP_UTF8, 0, s, -1, out, count); }

static int w32_pick_folder(const char *title, const char *start, char *out, size_t size) {
    HMODULE ole = LoadLibraryA("ole32.dll");
    CoInitializeEx_fn co_init = ole ? W32_PROC(CoInitializeEx_fn, ole, "CoInitializeEx") : NULL;
    CoUninitialize_fn co_uninit = ole ? W32_PROC(CoUninitialize_fn, ole, "CoUninitialize") : NULL;
    CoCreateInstance_fn co_create = ole ? W32_PROC(CoCreateInstance_fn, ole, "CoCreateInstance") : NULL;
    CoTaskMemFree_fn co_free = ole ? W32_PROC(CoTaskMemFree_fn, ole, "CoTaskMemFree") : NULL;
    IFileOpenDialog *dlg = NULL;
    IShellItem *item = NULL;
    PWSTR wpath = NULL;
    HRESULT init_hr;
    int result = -1;
    BR_UNUSED(start);                    /* the dialog remembers the last folder by itself */
    if (!co_init || !co_uninit || !co_create || !co_free) { if (ole) FreeLibrary(ole); return -1; }
    init_hr = co_init(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(co_create(&k_clsid_file_open_dialog, NULL, CLSCTX_INPROC_SERVER, &k_iid_file_open_dialog, (void **)&dlg)) && dlg) {
        FILEOPENDIALOGOPTIONS opts = 0;
        wchar_t wtitle[256];
        dlg->lpVtbl->GetOptions(dlg, &opts);
        dlg->lpVtbl->SetOptions(dlg, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (w32_utf8_to_wide(title, wtitle, 256) > 0) dlg->lpVtbl->SetTitle(dlg, wtitle);
        result = 0;
        if (SUCCEEDED(dlg->lpVtbl->Show(dlg, W.hwnd)) && SUCCEEDED(dlg->lpVtbl->GetResult(dlg, &item)) && item) {
            if (SUCCEEDED(item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, &wpath)) && wpath) {
                if (WideCharToMultiByte(CP_UTF8, 0, wpath, -1, out, (int)size, NULL, NULL) > 0) {
                    char *p;
                    for (p = out; *p; ++p) if (*p == '\\') *p = '/';
                    result = 1;
                }
                co_free(wpath);
            }
            item->lpVtbl->Release(item);
        }
        dlg->lpVtbl->Release(dlg);
    }
    if (SUCCEEDED(init_hr)) co_uninit();
    FreeLibrary(ole);
    return result;
}

static bool w32_open_path(const char *path) {
    HMODULE shell = LoadLibraryA("shell32.dll");
    ShellExecuteW_fn exec = shell ? W32_PROC(ShellExecuteW_fn, shell, "ShellExecuteW") : NULL;
    wchar_t wpath[1024];
    bool ok = false;
    if (exec && w32_utf8_to_wide(path, wpath, 1024) > 0)
        ok = (INT_PTR)exec(W.hwnd, L"open", wpath, NULL, NULL, SW_SHOWNORMAL) > 32;
    if (shell) FreeLibrary(shell);
    return ok;
}

static const br_platform k_win32 = {
    "win32", w32_init, w32_shutdown, w32_poll, w32_present, w32_time, w32_sleep,
    w32_audio_wanted, w32_audio_submit,
    w32_get_window_size, w32_get_window_scale, w32_set_title, w32_set_window_size,
    w32_set_fullscreen, w32_set_cursor_visible, w32_set_icon, w32_pick_folder, w32_open_path,
    w32_set_gamepad_rumble,
    NULL
};

const br_platform *br_platform_native(void) { return &k_win32; }
