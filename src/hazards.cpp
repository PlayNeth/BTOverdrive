#include "common.h"
#include "hazards.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "framerate.h"
#include "pacing.h"
#include "ai_route.h"
#include "items.h"

// ---------------------------------------------------------------------------
// Hazard hits. A track's animated hazards (the vampire's hands on the graveyard
// track...) are trigger events whose model gets collision boxes, one per part,
// that follow the parts every step (set up by 0x4E67F0, moved by 0x4E6630).
// Two game bugs:
//  - The boxes of the slabs and hands reach well beyond what's drawn (invisible
//    walls around the slabs): they're made half the size while the actor is made
//    (0x5F2260). (Not because the game doubles them, as first thought: each box
//    is the size the level file gives it, a CollisionBox's +0x88 being its
//    half-size - the authored boxes are just bigger than the parts. The gate's
//    panels match theirs, so they're left at full size; see "Gate panels".)
//  - The parts that reach furthest (the vampire's long fingers) get no box at all
//    (size 0), and the boxes' speed is next to nothing, so a swipe can't throw a
//    car: the hands only ever blocked or nudged.
// So the mod gives moving hazards their own hit: each step it tracks every box's
// position (its own speed from step to step), and a race car that's within the
// box's reach - hazardReach times the part's size, which takes in the fingers -
// while the part moves faster than hazardMinSpeed is thrown: sideways away from
// the part and up, at most once per swipe, kept pushing for hazardPushTime. The
// throw is applied at the car's physics sync (0x5FB260). A thrown car passes
// through walls for hazardFlyTime, and one that falls off the track is put back.
// ---------------------------------------------------------------------------

HazardKick g_kicks[16];
HazardCar g_hzCars[16];
HazardBox g_hzBoxes[64];
LONG g_hzKicks = 0, g_hzLogged = 0;
void ApplyHazardKick(BYTE* car) {
    DWORD now = GetTickCount();
    for (HazardKick& k : g_kicks) {
        if (!k.pending) continue;
        bool mine = k.car == car;
        if (!mine && now - k.ms < 500) continue;                        // (a car that's gone: dropped)
        if (InterlockedCompareExchange(&k.pending, 0, 1) != 1) continue;
        InterlockedDecrement(&g_kicksPending);
        if (!mine) continue;
        const float* v = reinterpret_cast<const float*>(car + 0x260);
        const float* w = reinterpret_cast<const float*>(car + 0x26C);
        float nv[3] = { v[0] + k.dv[0], v[1] + k.dv[1], v[2] + k.dv[2] };
        float nw[3] = { w[0] + k.w[0], w[1] + k.w[1], w[2] + k.w[2] };
        reinterpret_cast<SetVec_t>(hot::fnSetLinVel)(car, nv);
        reinterpret_cast<SetVec_t>(hot::fnSetAngVel)(car, nw);
        bool airborne = k.watch && k.flying && now - k.flying < 8000;  // hit again in the air: keep where it was
        if (!airborne) {                                                //   first hit (on the track) for the put-back
            memcpy(k.m0, car + 0x10, sizeof(k.m0));
            DWORD vt = *reinterpret_cast<const DWORD*>(car);
            bool race = vt == 0x8A0384 || vt == 0x8A0144 || vt == 0x8A3FA4;
            k.lap0 = race ? *reinterpret_cast<const LONG*>(car + 0xF58) : LONG_MAX;   // (race cars count laps)
        }
        k.flying = now ? now : 1;
        if (g_cfg.hazardSmoke && !k.weapon)                             // a burst of smoke on the swatted car
            reinterpret_cast<void (__attribute__((thiscall)) *)(BYTE*)>(hot::fnCarDustBall)(car);
        k.watch = g_cfg.hazardFallReset > 0.0f;
        k.reset = false;
    }
}
// A car thrown off the track falls forever (nothing below catches it, and the
// game's reset of a stuck car finds no reset point from down there). So a
// thrown car that's still in the air and clearly lost - hazardFallReset below
// where it was thrown and below every road a car has driven this race, or
// still falling fast 5 seconds after the throw - is put back where it was hit,
// with the game's own put-back (GSResetOnTrackAP_c, 0x433F90: position, stops
// the car, camera...). A thrown car that lands (a wheel on the ground, half a
// second after the throw) is no longer watched: it landed lower down the track,
// not off it. That runs with the hazards' step (HkKinStep), outside the car's
// physics sync.
static float g_fallMinY = 0.0f; static bool g_fallMinOk = false;   // the lowest road driven this race (FallTrack)
void HazardFallCheck(BYTE* car) {                        // from RegCar
    DWORD now = GetTickCount();
    for (HazardKick& k : g_kicks) {
        if (k.car != car || !k.flying || k.reset) continue;
        float y = reinterpret_cast<const float*>(car + 0x10)[13];
        float vy = reinterpret_cast<const float*>(car + 0x260)[1];
        // The car's own handling takes the sideways speed out within half a second
        // (12 units/s left of 150, in the air too), so the throw keeps pushing for
        // hazardPushTime: its speed along the throw is kept up to hazardPush.
        if (now - k.flying < k.pushMs) {
            const float* v = reinterpret_cast<const float*>(car + 0x260);
            float along = v[0] * k.dir[0] + v[2] * k.dir[1];
            if (along < k.push) {
                float add = k.push - along;
                float nv[3] = { v[0] + k.dir[0] * add, v[1], v[2] + k.dir[1] * add };
                reinterpret_cast<SetVec_t>(hot::fnSetLinVel)(car, nv);
            }
        }
        if (!k.watch) continue;
        if (now - k.flying > 8000) { k.watch = false; continue; }      // landed on the track
        bool ground = *reinterpret_cast<void**>(car + 0x121C) != nullptr;
        if (ground && now - k.flying > 500) { k.watch = false; continue; }   // landed (lower down the track too)
        float drop = g_cfg.hazardFallReset < 10.0f ? 10.0f : g_cfg.hazardFallReset;   // (never while it's still near the track)
        bool belowRoads = !g_fallMinOk || y < g_fallMinY - 25.0f;     // (no road below it; unknown without fallReset)
        if ((y < k.m0[13] - drop && vy < -10.0f && belowRoads) ||
            (now - k.flying > 5000 && vy < -30.0f && y < k.m0[13] - drop)) {
            k.reset = true;
            Logf("hazards: thrown car %p fell %.0f units (y %.1f -> %.1f, falling %.0f units/s): back on the track\n",
                 car, k.m0[13] - y, k.m0[13], y, -vy);
        }
    }
}
alignas(16) BYTE g_resetObj[0x88];                       // a GSResetOnTrackAP_c's fields for 0x433F90
// Laps. Crossing the finish line forward counts a lap (0x455800, from the line's
// trigger, 0x4AF1C0: car+0xF58 += 1); crossing it backward sets car+0xF9C, and
// the next forward crossing then doesn't count. A put-back takes a car back to
// where it was hit, or to where it was a second before it left the road: if it
// was thrown or jumped over the line before it fell, that lap was counted, and
// the put-back leaves it behind the line again without the game seeing it go
// back over - the next crossing counted a second lap ("a lap ahead"). So a car
// whose lap count went up since its put-back spot is marked as having crossed
// back, as the game would mark it.
static LONG g_lapLogged = 0;
static void LapBackOver(BYTE* car, LONG lapThen) {
    LONG lap = *reinterpret_cast<const LONG*>(car + 0xF58);
    if (lap <= lapThen || car[0xF9C]) return;                   // (no lap since, or already marked)
    car[0xF9C] = 1;
    if (InterlockedIncrement(&g_lapLogged) <= 20)
        Logf("laps: car %p crossed the finish line since its put-back spot (lap %ld, then %ld): "
             "its next crossing doesn't count\n", car, lap, lapThen);
}
void HazardResets() {                                    // from HkKinStep
    for (HazardKick& k : g_kicks) {
        if (!k.reset) continue;
        k.reset = k.watch = false;
        k.flying = 0;                                           // walls are walls again
        k.safe = GetTickCount();
        DWORD vt = *reinterpret_cast<DWORD*>(k.car);
        if (vt != 0x8A0384 && vt != 0x8A0144 && vt != 0x8A3FA4) continue;   // (gone)
        memset(g_resetObj, 0, sizeof(g_resetObj));
        memcpy(g_resetObj + 0x10, k.m0, sizeof(k.m0));         // +0x10: matrix (position at +0x40)
        *reinterpret_cast<BYTE**>(g_resetObj + 0x60) = k.car;  // +0x60: the car
        reinterpret_cast<void (__attribute__((thiscall)) *)(BYTE*)>(hot::fnCarReset)(g_resetObj);
        LapBackOver(k.car, k.lap0);
    }
}
// Falling off (fallReset). Nothing below a track catches a car that leaves it,
// and the game's own reset of a stuck car finds no place from down there: it
// falls forever. So every race car's place is noted, every quarter second, while
// it's on the track - a wheel touching (car+0x121C: the surface under it, set
// each step by 0x456340), upright, not thrown - and a car that's been off the
// ground for 2.5 s, falling fast, 15 units below where it left the ground and 25
// below anywhere a car has driven this race (a big jump down to a lower road
// isn't a fall), or 5 s and still falling fast, or 3 s and 50 below anywhere a
// car has driven, is put back where it was a second
// before it left the ground (the same put-back as a thrown hazard's, from the
// sound update: outside the physics). Timed in the car's own steps (a pause
// doesn't count).
struct FallCar {
    BYTE* car; DWORD seenMs;
    LONG steps, groundStep, graceStep;          // its steps; when last on the ground; no new put-back before
    float groundY;                              // its height then
    struct { float m[16]; LONG step; bool ok; LONG lap; } at[8];   // where it's been, a quarter second apart (and its lap)
    int next;
    bool pending;                               // a put-back is due
    float lastTo[3]; DWORD lastToMs;            // where it was last put back, when
    float to[16];
    LONG toLap;                                 // its lap count at that spot (LONG_MAX: a spot ahead)
};
static FallCar g_fall[16];
static DWORD g_fallLastSeen = 0;
static LONG g_fallLogged = 0;
void FallTrack(BYTE* car) {                              // from RegCar, once per car per step
    DWORD vt = *reinterpret_cast<DWORD*>(car);
    if (vt != 0x8A0384 && vt != 0x8A0144 && vt != 0x8A3FA4) return;   // race cars only
    DWORD now = GetTickCount();
    if (now - g_fallLastSeen > 3000) {                          // (a new race: forget the last)
        for (FallCar& f : g_fall) f.car = nullptr;
        g_fallMinOk = false;
    }
    g_fallLastSeen = now;
    FallCar* c = nullptr; FallCar* slot = nullptr;
    for (FallCar& f : g_fall) {
        if (f.car == car) { c = &f; break; }
        if (!slot || !f.car || (slot->car && now - f.seenMs > now - slot->seenMs)) slot = &f;
    }
    if (!c) {
        c = slot;
        memset(c, 0, sizeof(*c));
        c->car = car;
    }
    c->seenMs = now;
    LONG step = ++c->steps;
    const float* m = reinterpret_cast<const float*>(car + 0x10);    // rows: right, up, front; position at [12]
    float y = m[13], vy = reinterpret_cast<const float*>(car + 0x260)[1];
    bool thrown = false;
    for (const HazardKick& k : g_kicks) if (k.car == car && (k.pending || (k.flying && now - k.flying < 4000))) thrown = true;
    bool ground = *reinterpret_cast<void**>(car + 0x121C) != nullptr;
    if (ground) {
        c->groundStep = step; c->groundY = y;
        if (!g_fallMinOk || y < g_fallMinY) { g_fallMinY = y; g_fallMinOk = true; }
        int last = (c->next + 7) & 7;
        if (m[5] > 0.7f && !thrown && (!c->at[last].ok || step - c->at[last].step >= 15)) {   // (upright; a quarter second on)
            memcpy(c->at[c->next].m, m, sizeof(c->at[0].m));
            c->at[c->next].step = step; c->at[c->next].ok = true;
            c->at[c->next].lap = *reinterpret_cast<const LONG*>(car + 0xF58);
            c->next = (c->next + 1) & 7;
        }
        return;
    }
    if (c->pending || step < c->graceStep || c->groundStep == 0) return;
    bool lost = step - c->groundStep > 150 && vy < -25.0f && y < c->groundY - 15.0f &&
                (!g_fallMinOk || y < g_fallMinY - 25.0f);           // (below every road driven: not a jump down)
    if (step - c->groundStep > 300 && vy < -25.0f && y < c->groundY - 15.0f) lost = true;   // (5 s: nothing caught it)
    bool void_ = g_fallMinOk && y < g_fallMinY - 50.0f && vy < -10.0f && step - c->groundStep > 180;   // (3 s: not a long drop)
    if (!lost && !void_) return;
    if (thrown && !void_) return;                               // (a thrown car: its own put-back first)
    // where it was a second before it left the ground (or the oldest noted)
    int best = -1;
    for (int i = 0; i < 8; i++) {
        if (!c->at[i].ok) continue;
        bool before = c->at[i].step <= c->groundStep - 60;
        if (best < 0) { best = i; continue; }
        bool bBefore = c->at[best].step <= c->groundStep - 60;
        if (before && (!bBefore || c->at[i].step > c->at[best].step)) best = i;
        else if (!before && !bBefore && c->at[i].step < c->at[best].step) best = i;
    }
    if (best < 0) return;                                       // (never on the ground: nothing to go back to)
    memcpy(c->to, c->at[best].m, sizeof(c->to));
    c->toLap = c->at[best].lap;
    // An AI car that falls off at the same place again (put back there within the
    // last 45 s, the spot within 40 units) can't make that stretch - a loop it's too
    // slow for: it goes on to the nearest reset point past it instead.
    bool ahead = false;
    if (vt == 0x8A0144 && c->lastToMs && now - c->lastToMs < 45000) {
        float d[3] = { c->to[12] - c->lastTo[0], c->to[13] - c->lastTo[1], c->to[14] - c->lastTo[2] };
        if (Dot3(d, d) < 40.0f * 40.0f) ahead = ResetAPAhead(c->to + 12, c->to + 8, c->to);
        if (ahead) c->toLap = LONG_MAX;                         // (a spot further on: not behind the line)
    }
    memcpy(c->lastTo, c->to + 12, 12); c->lastToMs = now;
    c->pending = true;
    c->graceStep = step + 120;
    if (InterlockedIncrement(&g_fallLogged) <= 30)
        Logf("fall reset: car %p%s fell off (y %.1f, falling %.0f units/s, %.1f s off the ground): %s (%.1f %.1f %.1f)\n",
             car, vt == 0x8A0384 ? " (you)" : "", y, -vy, (step - c->groundStep) / 60.0f,
             ahead ? "fell there before - on past it to" : "back to", c->to[12], c->to[13], c->to[14]);
}
void FallResets() {                                      // from the sound update, once per step
    for (FallCar& f : g_fall) {
        if (!f.car || !f.pending) continue;
        f.pending = false;
        DWORD vt = *reinterpret_cast<DWORD*>(f.car);
        if (vt != 0x8A0384 && vt != 0x8A0144 && vt != 0x8A3FA4) { f.car = nullptr; continue; }   // (gone)
        for (HazardKick& k : g_kicks) if (k.car == f.car) { k.watch = k.reset = false; k.flying = 0; }   // (not twice)
        memset(g_resetObj, 0, sizeof(g_resetObj));
        memcpy(g_resetObj + 0x10, f.to, sizeof(f.to));
        *reinterpret_cast<BYTE**>(g_resetObj + 0x60) = f.car;
        reinterpret_cast<void (__attribute__((thiscall)) *)(BYTE*)>(hot::fnCarReset)(g_resetObj);
        LapBackOver(f.car, f.toLap);
        f.groundStep = f.steps;                                 // (back on the track)
    }
}

void NoteHazardCar(BYTE* car) {                          // from RegCar, once per car per step
    DWORD vt = *reinterpret_cast<DWORD*>(car);
    if (vt != 0x8A0384 && vt != 0x8A0144 && vt != 0x8A3FA4) return;  // race cars only
    DWORD now = GetTickCount();
    HazardCar* slot = nullptr;
    for (HazardCar& c : g_hzCars) {
        if (c.car == car) { c.ms = now; return; }
        if (!slot || now - c.ms > now - slot->ms) slot = &c;            // a free or the oldest slot
    }
    slot->car = car; slot->ms = now;
}
void QueueKick(BYTE* car, const HazardBox& b, float sp) {
    DWORD now = GetTickCount();
    HazardKick* k = nullptr;
    for (HazardKick& e : g_kicks) if (e.car == car) { k = &e; break; }
    if (k && (k->pending || now - k->ms < 700 || (k->safe && now - k->safe < 2000))) return;   // one hit per swipe; none 2 s after a put-back
    if (!k) {
        for (HazardKick& e : g_kicks) if (!e.pending && !e.reset && (!k || now - e.ms > now - k->ms)) k = &e;   // the oldest free one
        if (!k) return;
    }
    if (k->car != car) { k->safe = 0; k->flying = 0; k->watch = k->reset = false; }   // (another car's slot)
    k->car = car; k->ms = now;
    // The hands move 100-300 units/s at the box centre and often downwards (a slam):
    // their velocity as it is would fire a car into the ground at 400 units/s. And
    // a swipe along the track only sent the car down the track, into the walls'
    // reach again. So the car goes out sideways (its own right or left, whichever
    // the swipe leans to), with a set strength, and up.
    const float* right = reinterpret_cast<const float*>(car + 0x10);   // the car's row 0: its right
    float h[3] = { right[0], 0.0f, right[2] };
    float hl = sqrtf(Dot3(h, h));
    if (hl > 0.01f) for (int i = 0; i < 3; i++) h[i] /= hl;
    else { h[0] = b.v[0]; h[2] = b.v[2]; hl = sqrtf(Dot3(h, h)); if (hl > 0.01f) { h[0] /= hl; h[2] /= hl; } }
    // Away from the part (the vampire's hand comes from his side of the track: thrown
    // towards him, the car hit his body and stayed on the track).
    const float* cp = reinterpret_cast<const float*>(car + 0x10) + 12;
    if (h[0] * (cp[0] - b.pos[0]) + h[2] * (cp[2] - b.pos[2]) < 0.0f) { h[0] = -h[0]; h[2] = -h[2]; }
    k->dir[0] = h[0]; k->dir[1] = h[2];
    k->push = g_cfg.hazardPush; k->pushMs = (DWORD)(g_cfg.hazardPushTime * 1000.0f); k->weapon = false;
    k->dv[0] = h[0] * g_cfg.hazardPush;
    k->dv[1] = g_cfg.hazardLift;
    k->dv[2] = h[2] * g_cfg.hazardPush;
    k->w[0] = h[2] * g_cfg.hazardSpin;                                  // tumble about the horizontal axis
    k->w[1] = 0.0f;                                                     //   across the swipe
    k->w[2] = -h[0] * g_cfg.hazardSpin;
    InterlockedIncrement(&g_kicksPending);
    InterlockedExchange(&k->pending, 1);
    if (++g_hzKicks <= 30)
        Logf("hazards: box %p (moving %.1f units/s) hit car %p%s: velocity +(%.1f %.1f %.1f)\n", b.box, sp, car,
             *reinterpret_cast<DWORD*>(car) == 0x8A0384 ? " (you)" : "", k->dv[0], k->dv[1], k->dv[2]);
}

void InstallFallReset() {
    if (!g_cfg.fallReset) { Logf("fall reset: off\n"); return; }
    if (!g_sndStepHooked || !InstallCarSync()) { Logf("fall reset: patch FAILED\n"); return; }
    g_fallOn = true;
    Logf("fall reset: on - a car that falls off the track is put back\n");
}
