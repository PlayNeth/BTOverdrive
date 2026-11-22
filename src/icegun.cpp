#include "common.h"
#include "icegun.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "framerate.h"
#include "pacing.h"
#include "hazards.h"
#include "items.h"

// The ice gun (iceFix). A car's freeze (car+0x1360: +0 the car, +4 who froze it,
// +8 the ice built up - each plain ice gun pellet 1, melting 0.34 a second -
// +0x10 its timer, +0x1C frozen) starts (0x478E40) when the ice reaches 10, or
// at once for an upgraded ice gun's shot (0x4A33B0): 3.5 s, the ice block on
// the car, and the controller's no-steering flag (car+0x810 +0x364, which
// zeroes the steering at 0x66C266). It ends (0x478C60) when the time's up or
// the driver breaks out (wiggling the stick left-right, 0x478D20), taking the
// flag off. Each shot of the upgrade started it again on top (a new block, the
// time from the start); the flag is shared with the tesla ray's; and an AI car
// drives on frozen. Here: no freezing a car that's frozen, or broke out less
// than a second ago; a no-steering flag still up a quarter second after the ice
// went (and not since put up by anything else) is taken down; and a frozen AI
// car's turning (its spin about its own up) is held at nothing each step.
// ---------------------------------------------------------------------------

static IceSeen g_ice[16];
static LONG g_iceLogged = 0;
IceSeen* IceFind(BYTE* car, bool add) {
    DWORD now = GetTickCount();
    IceSeen* slot = &g_ice[0];
    for (IceSeen& e : g_ice) { if (e.car == car) return &e; if (now - e.seenMs > now - slot->seenMs) slot = &e; }
    if (!add) return nullptr;
    *slot = IceSeen{};
    slot->car = car; slot->seenMs = now;
    return slot;
}
static void IceLog(BYTE* car, const char* fmt, ...) {
    if (InterlockedIncrement(&g_iceLogged) > 60) return;
    char buf[160];
    va_list ap; va_start(ap, fmt); _vsnprintf(buf, sizeof(buf) - 1, fmt, ap); va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    DWORD vt = *reinterpret_cast<DWORD*>(car);
    Logf("ice: car %p%s %s\n", car, vt == 0x8A0384 ? " (you)" : vt == 0x8A0144 ? " (AI)" : "", buf);
}
static void __attribute__((thiscall)) HkIceStart(BYTE* frz) {   // replaces the two calls of 0x478E40 (ecx = car+0x1360)
    typedef void (__attribute__((thiscall)) *Fn)(BYTE*);
    BYTE* car = *reinterpret_cast<BYTE**>(frz);
    if (car && g_cfg.iceFix) {
        IceSeen* e = IceFind(car, false);
        DWORD now = GetTickCount();
        if (frz[0x1C]) { IceLog(car, "hit again while frozen (%lu ms in): not frozen again on top", e ? now - e->frozeMs : 0ul); return; }
        if (e && e->endMs && now - e->endMs < 1000) {
            *reinterpret_cast<float*>(frz + 8) = 0.0f;           // (the plain gun's ice built up: gone with it)
            IceLog(car, "hit %lu ms after breaking out: not frozen again yet", now - e->endMs);
            return;
        }
    }
    reinterpret_cast<Fn>(0x478E40)(frz);
}
void IceTrack(BYTE* car) {                               // from RegCar, once per car per step
    DWORD vt = *reinterpret_cast<DWORD*>(car);
    if (vt != 0x8A0384 && vt != 0x8A0144 && vt != 0x8A3FA4) return;   // race cars only
    BYTE* ctl = *reinterpret_cast<BYTE**>(car + 0x810);
    if (!ctl) return;
    IceSeen* e = IceFind(car, true);
    DWORD now = GetTickCount();
    e->seenMs = now;
    bool frozen = car[0x1360 + 0x1C] != 0;
    bool lock = ctl[0x364] != 0;
    bool ours = e->lockUntil && (LONG)(now - e->lockUntil) < 0;  // (a mega shield's lock still on)
    if (frozen && !e->frozen) {
        e->frozeMs = now;
        IceLog(car, "frozen by car %p", *reinterpret_cast<void**>(car + 0x1364));
    } else if (!frozen && e->frozen) {
        e->endMs = now;
        e->leftover = lock && !ours;                            // (the flag outlasting the ice)
        IceLog(car, "thawed after %lu ms%s", now - e->frozeMs, lock ? " - its steering still locked" : "");
    }
    if (lock && !e->lock) e->lockMs = now;
    if (!lock || ours) e->leftover = false;
    else if (!frozen && e->lockMs > e->endMs) e->leftover = false;   // (put up again since: the tesla's)
    if (g_cfg.iceFix && e->leftover && now - e->endMs > 250) {
        ctl[0x364] = 0; lock = false; e->leftover = false;
        IceLog(car, "steering still locked %lu ms after the ice went: unlocked", now - e->endMs);
    }
    // a mega shield's lock: the flag held up, then taken down (unless the ice has it)
    if (ours) { ctl[0x364] = 1; lock = true; e->ourLock = true; }
    else if (e->lockUntil) {
        if (e->ourLock && !frozen) { ctl[0x364] = 0; lock = false; }
        e->lockUntil = 0; e->ourLock = false;
    }
    e->frozen = frozen; e->lock = lock;
    // a locked AI car: no turning (the controller's flag only stops a driver's stick)
    if (((frozen && g_cfg.iceFix) || ours) && vt == 0x8A0144) {   // (GSAIRaceCar_c)
        const float* m = reinterpret_cast<const float*>(car + 0x10);
        const float* up = m + 4;
        const float* w = reinterpret_cast<const float*>(car + 0x26C);
        float sp = Dot3(w, up);
        if (sp > 0.02f || sp < -0.02f) {
            float nw[3] = { w[0] - up[0] * sp, w[1] - up[1] * sp, w[2] - up[2] * sp };
            reinterpret_cast<SetVec_t>(hot::fnSetAngVel)(car, nw);
        }
    }
    ItemFxStep(car, e, now);
}
void InstallIceFix() {
    if (!g_cfg.iceFix) { Logf("ice gun: as the game (iceFix=0)\n"); return; }
    if (!InstallCarSync() || !PatchCall(hot::callIceHit, reinterpret_cast<void*>(&HkIceStart), 5) ||
        !PatchCall(hot::callIceFull, reinterpret_cast<void*>(&HkIceStart), 5)) { Logf("ice gun: patch FAILED\n"); return; }
    g_carFxOn = true;
    Logf("ice gun: no refreezing a frozen car, a second free after breaking out, frozen AI can't steer\n");
}
