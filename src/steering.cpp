#include "common.h"
#include "steering.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "items.h"

// ---------------------------------------------------------------------------
// Steering
// ---------------------------------------------------------------------------
// Steering (steerRate). A race car's steering (RaceCarSteering, 0x6C76B0) turns
// its front wheels toward where the stick points at +0x74 rad/s at most (more at
// speed), which its setup (0x6C6D30) sets to 0.96 rad/s for every car - over a
// second from straight to the 1.13 rad lock. That's the slow, heavy turn-in; the
// car's mass and inertia barely touch it. The setup is wrapped (its vtable slot)
// and the rate scaled; a setup that leaves the rate as it was (the game's other
// modes) isn't scaled twice.
typedef void (__attribute__((thiscall)) *SteerSetup_t)(BYTE*);
static SteerSetup_t g_steerSetup = nullptr;
struct SteerSeen { BYTE* obj; float set; };
static SteerSeen g_steerSeen[32];
static int g_steerSeenNext = 0;
static LONG g_steerLogged = 0;
static void __attribute__((thiscall)) HkSteerSetup(BYTE* st) {
    g_steerSetup(st);
    float* rate = reinterpret_cast<float*>(st + 0x74);
    SteerSeen* e = nullptr;
    for (SteerSeen& x : g_steerSeen) if (x.obj == st) { e = &x; break; }
    if (e && *rate == e->set) return;                           // (ours already)
    float was = *rate;
    *rate = was * g_cfg.steerRate;
    if (!e) { e = &g_steerSeen[g_steerSeenNext]; g_steerSeenNext = (g_steerSeenNext + 1) & 31; e->obj = st; }
    e->set = *rate;
    if (InterlockedIncrement(&g_steerLogged) <= 8)
        Logf("steering: %p turns its wheels at %.2f rad/s (game: %.2f), lock %.2f rad\n", st, *rate, was,
             *reinterpret_cast<float*>(st + 0x78));
}
void InstallSteering() {
    if (g_cfg.steerRate == 1.0f) { Logf("steering: the game's\n"); return; }
    g_steerSetup = *reinterpret_cast<SteerSetup_t*>(hot::vtSteerSetup);
    if (!PatchPointer(reinterpret_cast<void**>(hot::vtSteerSetup), reinterpret_cast<void*>(&HkSteerSetup))) {
        Logf("steering: patch FAILED\n"); return;
    }
    Logf("steering: wheels turn x%.2f as fast\n", g_cfg.steerRate);
}
