#include "common.h"
#include "hazard_parts.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "framerate.h"
#include "pacing.h"
#include "hazards.h"
#include "tuning.h"

// ---------------------------------------------------------------------------
// Hazards' moving parts: what they hit (box sets, gate panels), thrown cars and walls
// ---------------------------------------------------------------------------
// A part's size (its mesh's, +0x88), or null.
static const float* PartSize(BYTE* box) {
    BYTE* obj = *reinterpret_cast<BYTE**>(box + 0x6C);
    BYTE* x = obj ? *reinterpret_cast<BYTE**>(obj + 0x174) : nullptr;
    BYTE* mesh = x ? *reinterpret_cast<BYTE**>(x + 0x10) : nullptr;
    return mesh ? reinterpret_cast<const float*>(mesh + 0x88) : nullptr;
}
// A gate panel: thin (under 2 units) and upright (third axis vertical).
static bool IsPanel(const float* size, const float* m) {
    return size && size[0] > 0.01f && size[0] < 2.0f && fabsf(m[9]) > 0.95f;
}
// After the game moves a box set's boxes (once per step): their speed, and the hit test.
static void HazardStep(BYTE* set) {                             // (after the game moved the boxes: +0x14 is current)
    BYTE* boxes = *reinterpret_cast<BYTE**>(set + 0xC);
    DWORD n = *reinterpret_cast<DWORD*>(set + 0x10);
    DWORD now = GetTickCount();
    float dt = *reinterpret_cast<const float*>(hot::stepDt);           // 1/60
    for (DWORD i = 0; boxes && i < n; i++) {
        BYTE* box = boxes + i * 0x7C;
        if (!box[8] || !*reinterpret_cast<void**>(box)) continue;       // (as 0x4E6630: inactive or unused)
        const float* m = reinterpret_cast<const float*>(box + 0x14);   // rows: axes; row 3: position
        const float* lo = reinterpret_cast<const float*>(box + 0x54);
        const float* hi = reinterpret_cast<const float*>(box + 0x60);
        if (hi[0] - lo[0] < 0.01f) continue;                            // a part without a size
        if (IsPanel(PartSize(box), m)) continue;                        // the gate: it blocks, it doesn't throw
        HazardBox* b = nullptr; HazardBox* slot = nullptr;
        for (HazardBox& e : g_hzBoxes) {
            if (e.box == box) { b = &e; break; }
            if (!slot || now - e.ms > now - slot->ms) slot = &e;
        }
        if (!b) { b = slot; memset(b, 0, sizeof(*b)); b->box = box; b->ms = now - 1000; }
        bool fresh = now - b->ms < 100;                                 // last step's position is usable
        for (int k = 0; k < 3; k++) {
            b->v[k] = fresh ? (m[12 + k] - b->pos[k]) / dt : 0.0f;
            b->pos[k] = m[12 + k];
        }
        b->ms = now;
        float sp = sqrtf(Dot3(b->v, b->v));
        if (sp > 1000.0f) { b->v[0] = b->v[1] = b->v[2] = 0.0f; continue; }   // a jump (a reset), not a swipe
        if (sp > b->maxSpeed) b->maxSpeed = sp;
        if (sp < g_cfg.hazardMinSpeed) continue;
        for (const HazardCar& c : g_hzCars) {
            if (!c.car || now - c.ms > 200) continue;
            const float* cp = reinterpret_cast<const float*>(c.car + 0x10) + 12;
            float d[3] = { cp[0] - m[12], cp[1] - m[13], cp[2] - m[14] };
            bool in = true;
            for (int a = 0; a < 3 && in; a++)                           // within reach on each of the box's axes
                in = fabsf(Dot3(d, m + 4 * a)) <= hi[a] * g_cfg.hazardReach + 1.5f;   // (+ about half a car)
            if (in) { b->hits++; QueueKick(c.car, *b, sp); }
        }
    }
}
// A thrown car flies through walls (the track's invisible walls keep it on the
// track otherwise) for hazardFlyTime seconds: the collision code asks the contact
// filter (0x489090) about each contact, and contacts of a flying car whose normal
// is level (a wall, not the ground: |y| < 0.6) are deleted. The rest is 0x489090
// as it was.
static LONG g_hzWallsLogged = 0;
typedef void (__cdecl *BoxHit_t)(BYTE* box, BYTE* car);
extern "C" bool __cdecl HkContactFilter(BYTE* c) {
    BYTE* car = *reinterpret_cast<BYTE**>(c + 0x30);
    if (!car || *reinterpret_cast<DWORD*>(c + 0x34)) return false;
    int idA = *reinterpret_cast<int*>(c + 0x28), idB = *reinterpret_cast<int*>(c + 0x2C);
    const float* n = reinterpret_cast<const float*>(c + 0xC);          // the contact normal
    if (fabsf(n[1]) < 0.6f && idA != idB) {                   // (a wall, not another car)
        DWORD now = GetTickCount();
        for (HazardKick& k : g_kicks) {
            if (k.car != car || !k.flying || !k.weapon || now - k.flying >= (DWORD)(g_cfg.weaponFlyTime * 1000.0f)) continue;
            bool pass = false;
            for (int i = 0; i < g_cfg.weaponWallMatCount && !pass; i++)
                pass = idA == g_cfg.weaponWallMats[i] || idB == g_cfg.weaponWallMats[i];
            if (pass) return true;
        }
    }
    if (g_cfg.hazardFlyTime > 0.0f && fabsf(n[1]) < 0.6f) {
        DWORD now = GetTickCount();
        for (HazardKick& k : g_kicks)
            if (k.car == car && k.flying && !k.weapon && now - k.flying < (DWORD)(g_cfg.hazardFlyTime * 1000.0f)) {
                if (++g_hzWallsLogged <= 5)
                    Logf("hazards: thrown car %p through a wall (materials %d, %d)\n", car, idA, idB);
                return true;
            }
    }
    const int* noIds = reinterpret_cast<const int*>(hot::contactNoIds);
    bool noId = false;
    for (int i = 0; i < 2 && !noId; i++) noId = noIds[i] != -1 && (idA == noIds[i] || idB == noIds[i]);
    if (BYTE* box = *reinterpret_cast<BYTE**>(c + 0x38)) {
        if (!noId) {
            if (!box[0x10]) return false;                               // a solid box: a normal contact
            if (BoxHit_t cb = *reinterpret_cast<BoxHit_t*>(box + 4)) cb(box, car);
        }
    } else if (!noId) return false;
    reinterpret_cast<void (__cdecl *)(const char*, ...)>(hot::debugPrint)(
        reinterpret_cast<const char*>(hot::contactDelMsg), idA, idB);
    return true;
}
typedef void (__attribute__((thiscall)) *KinStep_t)(BYTE*);
static void __attribute__((thiscall)) HkKinStep(BYTE* set) {
    reinterpret_cast<KinStep_t>(hot::fnKinStep)(set);
    HazardResets();
    HazardStep(set);
}
// Gate panels (thin, upright parts: the graveyard's swinging gate). Each part's
// collision box (a CollisionBox, obj +0x174 -> +0x10: +0x88 its half-size, +0x24
// its frame) is authored in the level file, centred and at the right size, and
// the game's PhysX box matches it (0x5F21D0/0x5F2260). But a panel is only
// 1.6-2.6 thick and swings shut a unit a step: cars slipped through it. With
// hazardGateFix=1 a panel's box is made 3 units thick.
typedef void (__attribute__((thiscall)) *KinCreate_t)(BYTE*);
static void __attribute__((thiscall)) HkKinCreate(BYTE* box) {  // a box's actor: halved, or a gate panel's made thicker
    // box +0x54: min x, y, z / max x, y, z, = -/+ half the half-size; the actor's
    // NxBox gets max - min as its dimensions (= half-sizes).
    float* c = reinterpret_cast<float*>(box + 0x54);
    float saved[6];
    memcpy(saved, c, sizeof(saved));
    if (IsPanel(PartSize(box), reinterpret_cast<const float*>(box + 0x14))) {   // (a gate panel: never halved)
        if (g_cfg.hazardGateFix && c[3] - c[0] < 1.5f) { c[0] = -0.75f; c[3] = 0.75f; }   // (3 thick: half-size 1.5)
    } else {
        for (int i = 0; i < 6; i++) c[i] *= 0.5f;               // (the slabs' and hands' boxes, see above)
    }
    reinterpret_cast<KinCreate_t>(hot::fnKinCreate)(box);
    memcpy(c, saved, sizeof(saved));
}
typedef void (__attribute__((thiscall)) *KinSetup_t)(BYTE*, void*, void*, DWORD);
static void __attribute__((thiscall)) HkKinSetup(BYTE* set, void* model, void* a2, DWORD flag) {
    reinterpret_cast<KinSetup_t>(hot::fnKinSetup)(set, model, a2, flag);
    TuningFlush();                                              // (a track is loading: its numbers are in)
    BYTE* boxes = *reinterpret_cast<BYTE**>(set + 0xC);
    DWORD n = *reinterpret_cast<DWORD*>(set + 0x10);
    if (++g_hzLogged > 40) return;
    Logf("hazards: animated object %p: %lu part(s)%s\n", model, (unsigned long)n, (flag & 0xFF) ? ", pass-through" : "");
    for (DWORD i = 0; boxes && i < n && i < 12; i++) {
        const BYTE* b = boxes + i * 0x7C;
        const float* p = reinterpret_cast<const float*>(b + 0x44);
        const float* lo = reinterpret_cast<const float*>(b + 0x54);
        const float* hi = reinterpret_cast<const float*>(b + 0x60);
        if (hi[0] - lo[0] < 0.01f) Logf("  box %p: no size (no collision)\n", b);
        else Logf("  box %p at (%.1f %.1f %.1f), size %.1f x %.1f x %.1f\n", b, p[0], p[1], p[2],
                  hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
    }
}
void HazardReport() {                                    // at quitting: how fast each part moved
    for (const HazardBox& b : g_hzBoxes)
        if (b.box && b.maxSpeed > 0.5f)
            Logf("hazards: box %p: fastest %.1f units/s, %ld hit(s)\n", b.box, b.maxSpeed, b.hits);
}

void InstallHazards() {
    if (!g_cfg.hazardHits) { Logf("hazards: original (hazardHits=0)\n"); return; }
    if (!InstallCarSync() ||
        !PatchCall(hot::callKinSetup, reinterpret_cast<void*>(&HkKinSetup), 5) ||
        !PatchCall(hot::callKinCreate, reinterpret_cast<void*>(&HkKinCreate), 5) ||
        !PatchCall(hot::callKinStep, reinterpret_cast<void*>(&HkKinStep), 5) ||
        !PatchJmp(hot::contactFilter, reinterpret_cast<DWORD>(&HkContactFilter))) {
        Logf("hazards: patch FAILED\n");
        return;
    }
    g_hzOn = true;
    Logf("hazards: on (min speed %.1f, reach x%.2f, push %.1f, lift %.1f, spin %.1f, fly time %.1f, fall reset %.1f, smoke %d)\n",
         g_cfg.hazardMinSpeed, g_cfg.hazardReach, g_cfg.hazardPush, g_cfg.hazardLift, g_cfg.hazardSpin,
         g_cfg.hazardFlyTime, g_cfg.hazardFallReset, g_cfg.hazardSmoke);
}
