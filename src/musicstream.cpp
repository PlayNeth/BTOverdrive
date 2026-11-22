#include "common.h"
#include "musicstream.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "framerate.h"

// ---------------------------------------------------------------------------
// Music streaming. A track is decoded (Ogg Vorbis) by its own thread (0x53B740)
// into a looping DirectSound buffer of 3 segments of 0x72D8 bytes - half a
// second at 44.1 kHz stereo - refilling a segment when the buffer's position
// notification says it has just played: each must be ready within 1/3 s. But
// its file reads go through the game's one archive I/O thread (PCArcFile::Read
// 0x58E290 queues the request and waits for it; 0x58E3C0 serves one request per
// 10 ms poll, after whatever reads are ahead of it). Behind another read the
// refill misses its turn and the buffer replays its last half second, over and
// over until the read comes through: the "scratched disc". (In the vanilla game
// too.) With musicStream=1:
//  - segments of 0x10000 bytes (the decode buffers are that size already): 1.1 s
//    of buffer, 3/4 s to refill each in
//  - the stream's read callback reads an archive file itself, from its own
//    handle on the archive (where the I/O thread would: start + position), not
//    through the queue; other files go to the game's callback
//  - the refill thread runs at above-normal priority.
// Reads that still take over 100 ms are logged.
typedef size_t (__cdecl *OggRead_t)(void*, size_t, size_t, void*);
static HANDLE g_musArc = INVALID_HANDLE_VALUE;
static volatile LONG g_musArcTried = 0;
static double MsSince(LONGLONG t0) {
    static LONGLONG f = 0;
    if (!f) { LARGE_INTEGER q; QueryPerformanceFrequency(&q); f = q.QuadPart; }
    return (Qpc() - t0) * 1000.0 / (double)f;
}
static void MusicOpenArc() {                                    // our own read handle on the game's archive (once)
    if (InterlockedExchange(&g_musArcTried, 1)) return;
    HANDLE game = *reinterpret_cast<HANDLE*>(hot::arcHandle);
    typedef HANDLE (WINAPI *ReOpen_t)(HANDLE, DWORD, DWORD, DWORD);
    ReOpen_t reopen = reinterpret_cast<ReOpen_t>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "ReOpenFile")));
    if (game && game != INVALID_HANDLE_VALUE && reopen)
        g_musArc = reopen(game, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0);
    Logf(g_musArc != INVALID_HANDLE_VALUE ? "music: reading the archive directly\n"
                                          : "music: can't open the archive for reading (%lu): the game's reads kept\n", GetLastError());
}
static LONG g_musSlowLogged = 0;
static size_t __cdecl MusicRead(void* ptr, size_t size, size_t n, void* src) {
    LONGLONG t0 = Qpc();
    size_t got = 0;
    BYTE* f = src ? *reinterpret_cast<BYTE**>(static_cast<BYTE*>(src) + 0x14) : nullptr;
    bool arc = f && *reinterpret_cast<DWORD*>(f) == hot::vtPCArcFile;
    if (arc && !g_musArcTried) MusicOpenArc();
    if (arc && g_musArc != INVALID_HANDLE_VALUE && size) {
        LONG* pos = reinterpret_cast<LONG*>(f + 0x2C);
        LONG start = *reinterpret_cast<LONG*>(f + 0x30), len = *reinterpret_cast<LONG*>(f + 0x34);
        LONG want = (LONG)(size * n);
        if (want > len - *pos) want = len - *pos;
        DWORD rd = 0;
        if (want > 0) {
            OVERLAPPED ov = {}; ov.Offset = (DWORD)(start + *pos);   // (a positioned read: the handle's own pointer isn't used)
            if (!ReadFile(g_musArc, ptr, (DWORD)want, &rd, &ov)) rd = 0;
            *pos += (LONG)rd;
        }
        got = rd / size;
    } else {
        got = reinterpret_cast<OggRead_t>(hot::fnMusReadCb)(ptr, size, n, src);
    }
    double ms = MsSince(t0);
    if (ms > 100.0 && InterlockedIncrement(&g_musSlowLogged) <= 20) Logf("music: a stream read took %.0f ms\n", ms);
    return got;
}
static HANDLE WINAPI MusicCreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T stack, LPTHREAD_START_ROUTINE fn, LPVOID arg,
                                       DWORD flags, LPDWORD id) {
    // (a new track's refill thread: arg is its stream. Its 3 "segment played"
    // events (+0x16C, auto-reset) can still be set from the last track - one would
    // have a segment refilled before it played, skipping ~1/6 s at the start. They
    // exist exactly while its DS buffer (+0x138) does: made and closed with it,
    // 0x53B150 / 0x53AE00; the constructor clears +0x138, not the events)
    if (arg && *reinterpret_cast<void**>(static_cast<BYTE*>(arg) + 0x138))
        for (int i = 0; i < 3; i++) {
            HANDLE e = reinterpret_cast<HANDLE*>(static_cast<BYTE*>(arg) + 0x16C)[i];
            if (e) ResetEvent(e);
        }
    HANDLE h = CreateThread(sa, stack, fn, arg, flags, id);
    if (h) SetThreadPriority(h, THREAD_PRIORITY_ABOVE_NORMAL);
    return h;
}
// ov_read's errors: the decode loop (0x53AF00) retries on OV_HOLE (-3) and
// OV_EBADLINK (-137), but takes any other negative result as a byte count - a
// copy of minus some bytes. Those are made 0 (the end of the file: the track
// loops or ends as it would).
// An error that doesn't go away (a failed read, a seek that left the file
// unready) would make that a loop that never moves on - 0, seek to the start, 0
// again - on an above-normal thread: from the second in a row, silence is
// returned instead (the length asked for, zeroed), with a short sleep, so the
// stream keeps moving and can be stopped.
static long __cdecl HkOvRead(void* vf, char* buf, int len, int big, int word, int sgn, int* bs) {
    static int errorsInRow = 0;                                 // (the music thread only)
    long r = reinterpret_cast<long (__cdecl *)(void*, char*, int, int, int, int, int*)>(hot::fnOvRead)(vf, buf, len, big, word, sgn, bs);
    if (r < 0 && r != -3 && r != -137) {
        static LONG n = 0;
        if (InterlockedIncrement(&n) <= 5) Logf("music: decode error %ld\n", r);
        if (++errorsInRow < 2 || len <= 0) return 0;            // (once: the track's end - it loops or stops)
        memset(buf, 0, (size_t)len);
        Sleep(10);
        return len;
    }
    if (r > 0) errorsInRow = 0;
    return r;
}
// Looping (musicLoop). At a song's end the stream's decoder (0x53AF00) seeks
// back to its loop point only if the song was started to loop (+0x28, from the
// player's +0x307B8 - set by the track select, a race, the attract mode); the
// main menus' songs are started without it, so they played once and the
// stream stopped for good. The "not looping: stop" branch (0x53B041) is
// removed: a song played normally (mode 0) always starts over.
void InstallMusicLoop() {
    if (!g_cfg.musicLoop) return;
    static const BYTE nop6[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    Logf(PatchBytes(hot::musEndNoLoop, nop6, 6) ? "music: every song starts over at its end\n" : "music: loop patch FAILED\n");
}
void InstallMusicStream() {
    if (!g_cfg.musicStream) return;
    // (the archive's own handle is opened later than this: ours is made on first use)
    static const DWORD chunk = 0x10000;
    bool ok = PatchBytes(hot::musChunkMov + 6, &chunk, 4);
    DWORD cb = reinterpret_cast<DWORD>(&MusicRead);
    ok = ok && PatchBytes(hot::musReadCbMov + 6, &cb, 4);
    ok = ok && PatchCall(hot::callMusThread, reinterpret_cast<void*>(&MusicCreateThread), 6);
    ok = ok && PatchCall(hot::callOvRead, reinterpret_cast<void*>(&HkOvRead), 5);
    Logf(ok ? "music: stream buffer 1.1 s (game: 0.5), reads of its own, above-normal priority\n" : "music: stream patch FAILED\n");
}

// ---------------------------------------------------------------------------
// Race music at GO. The race's track is opened while the race loads
// (GSWorldInitState_c, 0x47C5CF) and started by the intro (GSBaseIntroState_c,
// 0x4796B0, every mode's intro): its countdown step calls Music play (0x538C20)
// every frame (0x479780), so it plays under the 3-2-1. That call is dropped and
// the track is started at GO instead, where the intro clears its "countdown on"
// byte (0x4797BF: mov byte ptr [0x959F80],0 - done here too). Leaving the pause
// menu plays the music if it isn't paused (0x47E6D2, 0x47EEF0): during the
// countdown it's only opened, so that would start it early - held until GO.
// A restart (GSWorldResetState_c) re-opens the track and runs the intro again.
static void __cdecl MusicIntroFrame() {}                       // (the countdown's per-frame play: nothing)
static void __cdecl MusicAtGo() {
    *reinterpret_cast<volatile BYTE*>(hot::introActive) = 0;   // (what the patched instruction did)
    reinterpret_cast<VoidFn_t>(hot::fnMusicPlay)();
}
static void __cdecl MusicPauseResume() {
    if (*reinterpret_cast<volatile BYTE*>(hot::introActive)) return;   // (still counting down: GO starts it)
    reinterpret_cast<VoidFn_t>(hot::fnMusicPlay)();
}
void InstallMusicAtGo() {
    if (!g_cfg.musicAtGo) return;
    bool ok = PatchCall(hot::callIntroPlay, reinterpret_cast<void*>(&MusicIntroFrame), 5) &&
              PatchCall(hot::movIntroGo, reinterpret_cast<void*>(&MusicAtGo), 7) &&
              PatchCall(hot::callPauseResume1, reinterpret_cast<void*>(&MusicPauseResume), 5) &&
              PatchCall(hot::callPauseResume2, reinterpret_cast<void*>(&MusicPauseResume), 5);
    Logf(ok ? "race music: starts at GO\n" : "race music: patch FAILED\n");
}
