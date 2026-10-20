/*
 * Windows audio output: waveOut (winmm.dll, loaded at run time).
 *
 * waveOut is available on every Windows version, needs no COM, and goes
 * through the same shared-mode mixer as WASAPI on modern Windows. Audio is
 * pushed from the game thread into a ring of small blocks; the engine tops
 * the queue up to the target latency each frame.
 *
 * Included by platform_win32.c (after <windows.h>).
 */
#include <mmsystem.h>

#define WO_BLOCK_FRAMES 256u          /* ~5.8 ms at 44.1 kHz */
#define WO_BLOCKS 64u                 /* ~370 ms of capacity  */

typedef MMRESULT (WINAPI *waveOutOpen_fn)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
typedef MMRESULT (WINAPI *waveOutHeader_fn)(HWAVEOUT, LPWAVEHDR, UINT);
typedef MMRESULT (WINAPI *waveOutSimple_fn)(HWAVEOUT);

static struct {
    HMODULE lib;
    HWAVEOUT hwo;
    WAVEHDR hdr[WO_BLOCKS];
    int16_t *mem;
    uint32_t cur, cur_fill;          /* block being filled, frames in it */
    uint32_t target;                 /* frames to keep queued */
    bool open;
    waveOutOpen_fn Open;
    waveOutHeader_fn Prepare, Unprepare, Write;
    waveOutSimple_fn Reset, Close;
} WO;

static bool wo_in_queue(uint32_t i) { return (((volatile WAVEHDR *)&WO.hdr[i])->dwFlags & WHDR_INQUEUE) != 0; }

static bool wo_open(int rate, int latency_ms) {
    WAVEFORMATEX fmt;
    uint32_t i;
    MMRESULT mr;
    memset(&WO, 0, sizeof(WO));
    if (rate <= 0) return false;
    WO.lib = LoadLibraryW(L"winmm.dll");
    WO.Open = W32_PROC(waveOutOpen_fn, WO.lib, "waveOutOpen");
    WO.Prepare = W32_PROC(waveOutHeader_fn, WO.lib, "waveOutPrepareHeader");
    WO.Unprepare = W32_PROC(waveOutHeader_fn, WO.lib, "waveOutUnprepareHeader");
    WO.Write = W32_PROC(waveOutHeader_fn, WO.lib, "waveOutWrite");
    WO.Reset = W32_PROC(waveOutSimple_fn, WO.lib, "waveOutReset");
    WO.Close = W32_PROC(waveOutSimple_fn, WO.lib, "waveOutClose");
    if (!WO.Open || !WO.Prepare || !WO.Unprepare || !WO.Write || !WO.Reset || !WO.Close) {
        BR_WARN("audio: winmm.dll unavailable - running silent");
        return false;
    }
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = (DWORD)rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = (DWORD)rate * 4u;
    mr = WO.Open(&WO.hwo, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL);
    if (mr != MMSYSERR_NOERROR) {
        BR_WARN("audio: waveOutOpen failed (%u) - running silent", (unsigned)mr);
        return false;
    }
    WO.mem = (int16_t *)br_calloc((size_t)WO_BLOCKS * WO_BLOCK_FRAMES * 2u, sizeof(int16_t));
    if (!WO.mem) { WO.Close(WO.hwo); return false; }
    for (i = 0; i < WO_BLOCKS; ++i) {
        WO.hdr[i].lpData = (LPSTR)(WO.mem + (size_t)i * WO_BLOCK_FRAMES * 2u);
        WO.hdr[i].dwBufferLength = WO_BLOCK_FRAMES * 4u;
        WO.Prepare(WO.hwo, &WO.hdr[i], sizeof(WAVEHDR));
    }
    WO.target = (uint32_t)((double)rate * (double)(latency_ms > 0 ? latency_ms : 75) / 1000.0);
    WO.target = ((WO.target + WO_BLOCK_FRAMES - 1) / WO_BLOCK_FRAMES) * WO_BLOCK_FRAMES;
    if (WO.target < 2 * WO_BLOCK_FRAMES) WO.target = 2 * WO_BLOCK_FRAMES;
    if (WO.target > (WO_BLOCKS - 2) * WO_BLOCK_FRAMES) WO.target = (WO_BLOCKS - 2) * WO_BLOCK_FRAMES;
    WO.open = true;
    BR_INFO("audio: waveOut, %d Hz, %u ms buffering", rate, (unsigned)(WO.target * 1000u / (uint32_t)rate));
    return true;
}

static void wo_close(void) {
    uint32_t i;
    if (WO.open) {
        WO.Reset(WO.hwo);   /* returns all queued blocks */
        for (i = 0; i < WO_BLOCKS; ++i) WO.Unprepare(WO.hwo, &WO.hdr[i], sizeof(WAVEHDR));
        WO.Close(WO.hwo);
        br_free(WO.mem);
    }
    if (WO.lib) FreeLibrary(WO.lib);
    memset(&WO, 0, sizeof(WO));
}

static uint32_t wo_wanted(void) {
    uint32_t i, queued = WO.cur_fill, free_frames;
    uint32_t busy = 0;
    if (!WO.open) return 0;
    for (i = 0; i < WO_BLOCKS; ++i) if (wo_in_queue(i)) busy++;
    queued += busy * WO_BLOCK_FRAMES;
    free_frames = (WO_BLOCKS - busy) * WO_BLOCK_FRAMES - WO.cur_fill;
    if (queued >= WO.target) return 0;
    return BR_MIN(WO.target - queued, free_frames);
}

static void wo_submit(const int16_t *frames, uint32_t count) {
    if (!WO.open) return;
    while (count > 0) {
        uint32_t n;
        if (wo_in_queue(WO.cur)) return;            /* ring full: drop the rest */
        n = BR_MIN(count, WO_BLOCK_FRAMES - WO.cur_fill);
        memcpy(WO.hdr[WO.cur].lpData + (size_t)WO.cur_fill * 4u, frames, (size_t)n * 4u);
        WO.cur_fill += n;
        frames += (size_t)n * 2u;
        count -= n;
        if (WO.cur_fill == WO_BLOCK_FRAMES) {
            WO.Write(WO.hwo, &WO.hdr[WO.cur], sizeof(WAVEHDR));
            WO.cur = (WO.cur + 1) % WO_BLOCKS;
            WO.cur_fill = 0;
        }
    }
}
