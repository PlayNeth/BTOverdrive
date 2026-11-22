#include "common.h"
#include "ai_route.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "framerate.h"
#include "pacing.h"
#include "hazards.h"
#include "items.h"

// Stuck AI (aiUnstuck). An AI car that leaves its route can lose it for good:
// circling, or backing up and ramming a shortcut's barrier (still unbroken)
// again and again, to the end of the race. Every AI car's (GSAIRaceCar_c)
// position is noted each second, in its own steps (a pause doesn't count):
// one that has driven at least 30 units in the last 6 s but is within 20 of
// where it was then - trying, not getting anywhere; a car standing still
// (finished, waiting) is left alone - and isn't in a countdown, thrown, or
// falling, is put back with the game's put-back (GSResetOnTrackAP_c, as for a
// fall) at the track's nearest reset point (action points of type 0xB4, on the
// road, facing along it) - not one it's just been put at.
struct AiStuck {
    BYTE* car; DWORD seenMs;
    LONG steps, graceStep;
    float at[7][3]; int atN;                    // where it was, a second apart (newest last)
    float path, pathAt[7];                      // distance driven, total and when each was noted
    float last[3];
    bool pending; float to[16];
};
static AiStuck g_aiStuck[16];
static DWORD g_aiStuckLastSeen = 0;
static LONG g_aiStuckLogged = 0;
// The nearest reset point (type 0xB4) ahead of p along fwd - at least 40 units
// on, within 400, facing the same way - as a put-back matrix.
bool ResetAPAhead(const float* p, const float* fwd, float* m) {
    float f[3] = { fwd[0], 0.0f, fwd[2] };
    float fl = sqrtf(Dot3(f, f));
    if (fl < 0.01f) return false;
    f[0] /= fl; f[2] /= fl;
    const DWORD lists[2] = { hot::apUserList, hot::apList };
    const BYTE* best = nullptr; float bd = 1e30f;
    for (DWORD list : lists) {
        BYTE* const* aps = *reinterpret_cast<BYTE* const* const*>(list);
        int na = *reinterpret_cast<const int*>(list + 4);
        for (int i = 0; aps && i < na && i < 4096; i++) {
            const BYTE* ap = aps[i];
            if (!ap || *reinterpret_cast<const short*>(ap + 0x50) != 0xB4) continue;
            const float* q = reinterpret_cast<const float*>(ap + 0x40);
            const float* af = reinterpret_cast<const float*>(ap + 0x30);
            float d[3] = { q[0] - p[0], q[1] - p[1], q[2] - p[2] };
            float along = d[0] * f[0] + d[2] * f[2], dd = Dot3(d, d);
            float al = sqrtf(af[0] * af[0] + af[2] * af[2]);
            if (along < 40.0f || dd > 400.0f * 400.0f || al < 0.01f || (af[0] * f[0] + af[2] * f[2]) / al < 0.3f) continue;
            if (dd < bd) { bd = dd; best = ap; }
        }
    }
    if (!best) return false;
    const float* q = reinterpret_cast<const float*>(best + 0x40);
    const float* af = reinterpret_cast<const float*>(best + 0x30);
    float g[3] = { af[0], 0.0f, af[2] };
    float gl = sqrtf(Dot3(g, g));
    g[0] /= gl; g[2] /= gl;
    memset(m, 0, 64);
    m[0] = g[2]; m[2] = -g[0]; m[5] = 1.0f; m[8] = g[0]; m[10] = g[2];
    m[12] = q[0]; m[13] = q[1] + 0.5f; m[14] = q[2]; m[15] = 1.0f;
    return true;
}
static bool NearestResetAP(const float* p, const float* avoid, float* m) {
    const DWORD lists[2] = { hot::apUserList, hot::apList };
    const BYTE* best = nullptr; float bd = 1e30f;
    for (DWORD list : lists) {
        BYTE* const* aps = *reinterpret_cast<BYTE* const* const*>(list);
        int na = *reinterpret_cast<const int*>(list + 4);
        for (int i = 0; aps && i < na && i < 4096; i++) {
            const BYTE* ap = aps[i];
            if (!ap || *reinterpret_cast<const short*>(ap + 0x50) != 0xB4) continue;
            const float* q = reinterpret_cast<const float*>(ap + 0x40);
            if (avoid) {
                float a[3] = { q[0] - avoid[0], q[1] - avoid[1], q[2] - avoid[2] };
                if (Dot3(a, a) < 5.0f * 5.0f) continue;         // (the one it was just put at: it didn't help)
            }
            float d[3] = { q[0] - p[0], (q[1] - p[1]) * 2.0f, q[2] - p[2] };   // (a level above or below counts double)
            float dd = Dot3(d, d);
            if (dd < bd) { bd = dd; best = ap; }
        }
    }
    if (!best) return false;
    const float* q = reinterpret_cast<const float*>(best + 0x40);
    const float* fw = reinterpret_cast<const float*>(best + 0x30);
    float f[3] = { fw[0], 0.0f, fw[2] };
    float l = sqrtf(Dot3(f, f));
    if (l < 0.01f) return false;
    f[0] /= l; f[2] /= l;
    memset(m, 0, 64);
    m[0] = f[2]; m[2] = -f[0]; m[5] = 1.0f; m[8] = f[0]; m[10] = f[2];   // right, up, forward
    m[12] = q[0]; m[13] = q[1] + 0.5f; m[14] = q[2]; m[15] = 1.0f;
    return true;
}
void AiStuckTrack(BYTE* car) {                           // from RegCar, once per car per step
    if (*reinterpret_cast<DWORD*>(car) != 0x8A0144) return;     // AI race cars only
    DWORD now = GetTickCount();
    if (now - g_aiStuckLastSeen > 3000) for (AiStuck& a : g_aiStuck) a.car = nullptr;   // (a new race)
    g_aiStuckLastSeen = now;
    AiStuck* c = nullptr; AiStuck* slot = nullptr;
    for (AiStuck& a : g_aiStuck) {
        if (a.car == car) { c = &a; break; }
        if (!slot || !a.car || (slot->car && now - a.seenMs > now - slot->seenMs)) slot = &a;
    }
    if (!c) { c = slot; memset(c, 0, sizeof(*c)); c->car = car; c->graceStep = 180; }
    c->seenMs = now;
    LONG step = ++c->steps;
    const float* p = reinterpret_cast<const float*>(car + 0x40);
    if (step > 1) {
        float d[3] = { p[0] - c->last[0], p[1] - c->last[1], p[2] - c->last[2] };
        float dl = sqrtf(Dot3(d, d));
        if (dl < 25.0f) c->path += dl;                          // (a put-back isn't driving)
    }
    memcpy(c->last, p, 12);
    bool busy = *reinterpret_cast<volatile BYTE*>(hot::introActive) != 0;
    for (const HazardKick& k : g_kicks) if (k.car == car && (k.pending || (k.flying && now - k.flying < 4000))) busy = true;
    if (busy) { c->atN = 0; c->graceStep = step + 120; return; }   // (counting down, thrown: start over)
    if (step % 60) return;
    if (c->atN == 7) { memmove(c->at, c->at + 1, sizeof(c->at[0]) * 6); memmove(c->pathAt, c->pathAt + 1, sizeof(float) * 6); c->atN = 6; }
    memcpy(c->at[c->atN], p, 12); c->pathAt[c->atN] = c->path; c->atN++;
    if (c->atN < 7 || c->pending || step < c->graceStep) return;
    float d[3] = { p[0] - c->at[0][0], p[1] - c->at[0][1], p[2] - c->at[0][2] };
    float moved = sqrtf(Dot3(d, d)), driven = c->path - c->pathAt[0];
    if (moved > 20.0f || driven < 30.0f) return;
    static float lastTo[16][3];                                 // (where each slot was last put)
    int idx = (int)(c - g_aiStuck);
    bool hadTo = c->to[15] != 0.0f;
    if (!NearestResetAP(p, hadTo ? lastTo[idx] : nullptr, c->to)) return;
    memcpy(lastTo[idx], c->to + 12, 12);
    c->pending = true;
    c->graceStep = step + 360;                                  // (6 s to get going again)
    c->atN = 0;
    if (InterlockedIncrement(&g_aiStuckLogged) <= 30)
        Logf("ai: car %p stuck (drove %.0f units in 6 s, got %.0f from where it was): back to (%.1f %.1f %.1f)\n",
             car, driven, moved, c->to[12], c->to[13], c->to[14]);
}
void AiStuckResets() {                                   // from the sound update, once per step
    for (AiStuck& a : g_aiStuck) {
        if (!a.car || !a.pending) continue;
        a.pending = false;
        if (*reinterpret_cast<DWORD*>(a.car) != 0x8A0144) { a.car = nullptr; continue; }   // (gone)
        memset(g_resetObj, 0, sizeof(g_resetObj));
        memcpy(g_resetObj + 0x10, a.to, sizeof(a.to));
        *reinterpret_cast<BYTE**>(g_resetObj + 0x60) = a.car;
        reinterpret_cast<void (__attribute__((thiscall)) *)(BYTE*)>(hot::fnCarReset)(g_resetObj);
    }
}
void InstallAiUnstuck() {
    if (!g_cfg.aiUnstuck) { Logf("ai: stuck cars left to themselves (aiUnstuck=0)\n"); return; }
    if (!g_sndStepHooked || !InstallCarSync()) { Logf("ai: unstuck patch FAILED\n"); return; }
    g_aiStuckOn = true;
    Logf("ai: a car stuck for 6 s is put back on the track\n");
}
// AI on loops (aiLoopSpeed). The AI eases off for a big loop's climb and, too
// slow, drops off its wall or ceiling and tries again, and again. An AI car
// (GSAIRaceCar_c) tilted more than 60 degrees from level, with a wheel on the
// road in the last quarter second and moving forward, has its speed brought up
// to aiLoopSpeed along the way it's going (at its physics sync, as the hazard
// throws are). Your car is left alone.
static LONG g_aiLoopLogged = 0;
void AiLoopAssist(BYTE* car) {                           // from RegCar, once per car per step
    if (*reinterpret_cast<DWORD*>(car) != 0x8A0144) return;
    struct Seen { BYTE* car; DWORD groundMs; };
    static Seen seen[16];
    DWORD now = GetTickCount();
    Seen* e = nullptr; Seen* slot = &seen[0];
    for (Seen& x : seen) { if (x.car == car) { e = &x; break; } if (now - x.groundMs > now - slot->groundMs) slot = &x; }
    if (!e) { e = slot; e->car = car; e->groundMs = 0; }
    if (*reinterpret_cast<void**>(car + 0x121C)) e->groundMs = now;
    const float* m = reinterpret_cast<const float*>(car + 0x10);
    if (m[5] > 0.5f || !e->groundMs || now - e->groundMs > 250) return;   // (level enough, or off the road)
    if (*reinterpret_cast<volatile BYTE*>(hot::introActive)) return;
    for (const HazardKick& k : g_kicks) if (k.car == car && k.flying && now - k.flying < 4000) return;   // (thrown)
    const float* v = reinterpret_cast<const float*>(car + 0x260);
    float sp = sqrtf(Dot3(v, v));
    if (sp < 1.0f || sp >= g_cfg.aiLoopSpeed || Dot3(v, m + 8) <= 0.0f) return;   // (still, fast enough, or going backward)
    float k = g_cfg.aiLoopSpeed / sp;
    float nv[3] = { v[0] * k, v[1] * k, v[2] * k };
    reinterpret_cast<SetVec_t>(hot::fnSetLinVel)(car, nv);
    if (InterlockedIncrement(&g_aiLoopLogged) <= 10)
        Logf("ai: car %p on a loop at %.0f units/s (tilted, up %.2f): brought up to %.0f\n", car, sp, m[5], g_cfg.aiLoopSpeed);
}
