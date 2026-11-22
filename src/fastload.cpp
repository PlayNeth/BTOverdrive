#include "common.h"
#include "fastload.h"
#include "game.h"
#include "framerate.h"
#include "sound.h"

// ---------------------------------------------------------------------------
// Race loading (fastLoad). Half a race load - 3 s on every track - was step 17
// of the race loader (GSWorldInitState_c, 0x47AA50) waiting for the drivers'
// and gunners' models, queued for every car in step 16. Each is a load that
// moves one state per game step (0x63B7C0: dependencies, stream its archive,
// wait, build, ...), its archives one state per frame (0x5B6F90), and they go
// one at a time (a lock, 0x938124: they build in shared globals): ~12 steps a
// model, 16 models, ~185 steps at 60 a second. Nothing's slow - it's paced.
// While step 17 waits (its check, 0x47B7E3), the same updates are run again
// and again - the archives' (0x5B6F90) and the two load managers' (0x63C9F0) -
// for up to 12 ms an update, so the loading screen still draws at 60 fps; the
// game's own lock and one-start-a-step still hold. Also the archive I/O
// thread polls every 1 ms instead of 10 (InstallSoundFx).
bool __attribute__((thiscall)) HkJobsDone(BYTE* jobs) {
    typedef bool (__attribute__((thiscall)) *Done_t)(BYTE*);
    typedef void (__attribute__((thiscall)) *Tick_t)(void*);
    Done_t done = reinterpret_cast<Done_t>(hot::fnJobsDone);
    static double perMs = 0;
    if (!perMs) { LARGE_INTEGER q; QueryPerformanceFrequency(&q); perMs = q.QuadPart / 1000.0; }
    LONGLONG t0 = Qpc();
    for (;;) {
        if (done(jobs)) return true;                            // (as often as it's asked: each callback once, by its flag)
        reinterpret_cast<void (__cdecl *)()>(hot::fnArcService)();
        for (int k = 0; k < 2; k++) {
            void* mgr = *reinterpret_cast<void**>(jobs + k * 0xC);
            if (mgr) reinterpret_cast<Tick_t>(hot::fnLoadMgrTick)(mgr);
        }
        if ((Qpc() - t0) / perMs > 12.0) break;
        Sleep(0);                                               // (the I/O thread's turn)
    }
    return done(jobs);
}
