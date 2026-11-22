#include "common.h"
#include "effects.h"
#include "game.h"
#include "framerate.h"

// ---------------------------------------------------------------------------
// Effects and shadows that follow a race car
//
// Race cars aren't blended in memory (see HkRender3D), so whatever reads a
// car's matrix (car+0x10) while drawing gets its 60 Hz step value. Three kinds
// of drawing do, and get the car's blend instead:
//  - the car's shadow and headlights: the race car's draw sets its shadow
//    light (0x602F40, through GSRaceCar slot 125) and the vehicle draw its
//    headlight projector (0x6029B0) from the car's matrix; the car's matrix
//    holds the blend during those calls only
//  - effects attached to a car (parachute, booster flames, shields...): every
//    GSBaseEffect reads what it follows through a few getters (0x425D30...),
//    from a scene node (+0x14) or an object (+0x10 / +0x18). They're replaced:
//    an object that is a race car gives its blend, a node gives the blend
//    FilterWorld keeps for it, or follows its car's blend if it's the car's
//    body node. Everything else gives what the original gives.
//  - tyre trails fade in and out by a fixed amount per draw (0x4BE580), so
//    above 60 fps they faded faster. The amount is now scaled by the steps
//    since the car's last trail draw (0 on frames without a step).
// ---------------------------------------------------------------------------
static Track* FindCar(BYTE* car) {
    for (Track& t : g_tr)
        if (t.obj == car && t.kind == 1) return &t;
    return nullptr;
}
// This frame's blend of a race car's matrix, or nullptr (not a race car, not
// drawing, no two steps in a row, a cut, or moved since the step).
static const float* CarBlend(BYTE* car) {
    if (!g_inRender || !car) return nullptr;
    Track* t = FindCar(car);
    if (!t || t->n < 2 || t->snapStep != g_stepsDone || *reinterpret_cast<void**>(car) != t->vt) return nullptr;
    if (memcmp(car + 0x10, t->cur, 64)) return nullptr;
    if (t->outFrame != g_frameNo) {
        t->outFrame = g_frameNo;
        t->outOk = Blend(t->prev, t->cur, g_alpha, t->out);
    }
    return t->outOk ? t->out : nullptr;
}

// Row-major affine matrices (row 3 = position, w column 0 0 0 1).
static void MulAffine(const float* a, const float* b, float* o) {        // o = a x b (o may not be a or b)
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 3; c++)
            o[4 * r + c] = a[4 * r] * b[c] + a[4 * r + 1] * b[4 + c] + a[4 * r + 2] * b[8 + c] + (r == 3 ? b[12 + c] : 0.0f);
    o[3] = o[7] = o[11] = 0.0f; o[15] = 1.0f;
}
static bool InvAffine(const float* m, float* o) {
    float c00 = m[5] * m[10] - m[6] * m[9], c01 = m[6] * m[8] - m[4] * m[10], c02 = m[4] * m[9] - m[5] * m[8];
    float det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (fabsf(det) < 1e-12f) return false;
    float k = 1.0f / det;
    o[0] = c00 * k; o[1] = (m[2] * m[9] - m[1] * m[10]) * k; o[2] = (m[1] * m[6] - m[2] * m[5]) * k;
    o[4] = c01 * k; o[5] = (m[0] * m[10] - m[2] * m[8]) * k; o[6] = (m[2] * m[4] - m[0] * m[6]) * k;
    o[8] = c02 * k; o[9] = (m[1] * m[8] - m[0] * m[9]) * k; o[10] = (m[0] * m[5] - m[1] * m[4]) * k;
    for (int c = 0; c < 3; c++) o[12 + c] = -(m[12] * o[c] + m[13] * o[4 + c] + m[14] * o[8 + c]);
    o[3] = o[7] = o[11] = 0.0f; o[15] = 1.0f;
    return true;
}

// A node's world matrix, blended if possible. buf: 2 x 16 floats of scratch.
typedef float* (__attribute__((thiscall)) *NodeWorld_t)(BYTE* node, float* buf);
static const float* NodeWorld(BYTE* node, float* buf) {
    NodeWorld_t get = *reinterpret_cast<NodeWorld_t*>(*reinterpret_cast<BYTE**>(node) + 0x218);
    const float* p = get(node, buf);                            // (fills buf or returns its own)
    if (!p) return nullptr;
    DWORD a = (DWORD)(UINT_PTR)p;
    if (p != buf && !(a >= g_stackLo && a < g_stackHi)) {
        if (MTrack* m = MFind(p)) {
            if (m->written) return p;                           // blended in memory already
            if (m->fn == 2 && m->fStep == g_stepsDone && !PerFrame(m) &&
                !memcmp(m->fcur, p, 64) && memcmp(m->fprev, m->fcur, 64))
            {
                float span = (float)(m->fStep - m->fPrevStep);       // (as FilterWorld: across 2+ steps)
                float f = span > 1.0f ? (span - 1.0f + g_alpha) / span : g_alpha;
                return Blend(m->fprev, m->fcur, f, buf + 16) ? buf + 16 : p;
            }
        }
    }
    for (Track& t : g_tr) {                                     // a car's body node: follow the car
        if (t.obj && t.kind == 1 && t.snapStep == g_stepsDone && *reinterpret_cast<BYTE**>(t.obj + 0xE0) == node) {
            const float* b = CarBlend(t.obj);
            float inv[16], d[16];
            if (!b || !InvAffine(t.cur, inv)) return p;
            MulAffine(inv, b, d);                               // the car's step -> blend
            if (p != buf) { memcpy(buf, p, 64); p = buf; }
            MulAffine(p, d, buf + 16);
            return buf + 16;
        }
    }
    return p;
}

// A matrix on a race car (within 4 units of its position) moved by the car's
// step -> blend transform: whatever the part does on its own is kept, the car's
// motion is smoothed. nullptr = no race car there (or no blend this frame).
const float* FollowCar(const float* v, float* o) {
    Track* best = nullptr; float bestD = 4.0f * 4.0f;
    for (Track& t : g_tr) {
        if (!t.obj || t.kind != 1 || t.n != 2 || t.snapStep != g_stepsDone) continue;
        float d[3] = { v[12] - t.cur[12], v[13] - t.cur[13], v[14] - t.cur[14] };
        float dd = Dot3(d, d);
        if (dd < bestD) { bestD = dd; best = &t; }
    }
    if (!best) return nullptr;
    const float* b = CarBlend(best->obj);
    float inv[16], d[16];
    if (!b || !InvAffine(best->cur, inv)) return nullptr;
    MulAffine(inv, b, d);                                       // the car's step -> blend
    MulAffine(v, d, o);
    return o;
}

// The getters. id: 0 position, 1 position of [+0x18] only, 2 position of the
// node only, 3 position of [+0x10] only, 4-6 row 2/1/0, 7 rows 0-2.
// 1 = answered (out set), 0 = let the original run.
extern "C" int __cdecl EffGet(BYTE* eff, int id, float* out) {
    static const signed char kRow[8] = { 3, 3, 3, 3, 2, 1, 0, 0 };
    BYTE* node = (id == 1 || id == 3) ? nullptr : *reinterpret_cast<BYTE**>(eff + 0x14);
    BYTE* obj = nullptr;
    if (!node) {
        if (id == 2) return 0;
        if (id == 1) obj = *reinterpret_cast<BYTE**>(eff + 0x18);
        else if (id == 3) obj = *reinterpret_cast<BYTE**>(eff + 0x10);
        else if (!(obj = *reinterpret_cast<BYTE**>(eff + 0x10))) obj = *reinterpret_cast<BYTE**>(eff + 0x18);
        if (!obj) return 0;
    }
    const float* m;
    float buf[32];
    if (node) {
        if (!g_inRender) return 0;
        if (!(m = NodeWorld(node, buf))) return 0;
    } else if (!(m = CarBlend(obj))) {
        if (id != 1 && id != 3) return 0;
        m = reinterpret_cast<const float*>(obj + 0x10);         // (these two have no other path to keep)
    }
    if (id == 7) memcpy(out, m, 48);
    else memcpy(out, m + 4 * kRow[id], 12);
    return 1;
}
extern "C" {
    DWORD g_effPosCont = hot::effPos + 6, g_effPosNodeCont = hot::effPosNode + 9, g_effRow2Cont = hot::effRow2 + 9,
          g_effRow1Cont = hot::effRow1 + 9, g_effRow0Cont = hot::effRow0 + 9, g_effRowsCont = hot::effRows + 6;
    void EffPosStub(); void EffPosObj18Stub(); void EffPosNodeStub(); void EffPosObj10Stub();
    void EffRow2Stub(); void EffRow1Stub(); void EffRow0Stub(); void EffRowsStub();
}
// thiscall(ecx = effect, [esp+4] = out), ret 4; the bool ones return al = 1.
#define EFF_STUB(name, id, fallback)                                                   \
    ".globl _" name "\n_" name ":\n"                                                   \
    "    pushl %ecx\n"                                                                 \
    "    pushl 8(%esp)\n"           /* out (above the saved ecx and the return) */     \
    "    pushl $" #id "\n"                                                             \
    "    pushl %ecx\n"                                                                 \
    "    call _EffGet\n"                                                               \
    "    addl $12, %esp\n"                                                             \
    "    popl %ecx\n"                                                                  \
    "    testl %eax, %eax\n"                                                           \
    "    jz 1f\n"                                                                      \
    "    ret $4\n"                  /* (eax = 1) */                                    \
    "1:\n" fallback
#define FALLBACK_SLOT1  "    movl (%ecx), %edx\n    jmp *4(%edx)\n"       /* what those originals do without the object */
#define FALLBACK_SUB(cont)  "    subl $0x80, %esp\n    jmp *_" cont "\n"
#define FALLBACK_NODE(cont) "    movl 0x14(%ecx), %eax\n    subl $0x80, %esp\n    jmp *_" cont "\n"
asm(".text\n"
    EFF_STUB("EffPosStub",      0, FALLBACK_SUB("g_effPosCont"))
    EFF_STUB("EffPosObj18Stub", 1, FALLBACK_SLOT1)
    EFF_STUB("EffPosNodeStub",  2, FALLBACK_NODE("g_effPosNodeCont"))
    EFF_STUB("EffPosObj10Stub", 3, FALLBACK_SLOT1)
    EFF_STUB("EffRow2Stub",     4, FALLBACK_NODE("g_effRow2Cont"))
    EFF_STUB("EffRow1Stub",     5, FALLBACK_NODE("g_effRow1Cont"))
    EFF_STUB("EffRow0Stub",     6, FALLBACK_NODE("g_effRow0Cont"))
    EFF_STUB("EffRowsStub",     7, FALLBACK_SUB("g_effRowsCont")));
typedef void (__attribute__((thiscall)) *ThisVoid2_t)(BYTE*, DWORD, DWORD);
static const float* CarBlendIn(BYTE* veh) {                     // put the blend in; nullptr = nothing to do
    const float* b = CarBlend(veh);
    if (b) memcpy(veh + 0x10, b, 64);
    return b;
}
static void CarBlendOut(BYTE* veh, const float* b) {            // put the step value back if still the blend
    if (b && !memcmp(veh + 0x10, b, 64)) memcpy(veh + 0x10, FindCar(veh)->cur, 64);   // (b: the car is tracked)
}
void __attribute__((thiscall)) HkVehLights(BYTE* veh) {
    const float* b = CarBlendIn(veh);
    reinterpret_cast<ThisVoid_t>(hot::fnVehLights)(veh);
    CarBlendOut(veh, b);
}
void __attribute__((thiscall)) HkCarShadow(BYTE* veh, DWORD a, DWORD c) {
    const float* b = CarBlendIn(veh);
    reinterpret_cast<ThisVoid2_t>(hot::fnCarShadow)(veh, a, c);
    CarBlendOut(veh, b);
}
// Tyre trails: the fade step scaled by the steps since the car's last trail draw.
void __attribute__((thiscall)) HkCarTrails(BYTE* trails) {
    Track* t = g_inRender ? FindCar(trails - 0x1070) : nullptr;
    float* fade = reinterpret_cast<float*>(hot::trailFade);
    if (!t || !(*reinterpret_cast<BYTE*>(hot::trailFadeInit) & 1)) {   // (not set yet: the first call sets it)
        if (t) t->trailStep = g_stepsDone;
        reinterpret_cast<ThisVoid_t>(hot::fnCarTrails)(trails);
        return;
    }
    LONG k = g_stepsDone - t->trailStep;
    if (k < 0 || k > 4) k = 1;                                  // first draw or a long gap: one step
    t->trailStep = g_stepsDone;
    float saved = *fade;
    *fade = saved * (float)k;
    reinterpret_cast<ThisVoid_t>(hot::fnCarTrails)(trails);
    *fade = saved;
}

// Car select and the garage show the selected car by reading its node's world
// matrix, adding the car's ride height (+0x97C, set per car) and writing it
// back (0x4EE330, every frame). The animation resets the node once per step,
// so with one step per frame the height is added once; on frames without a
// step it was added again, lifting the car (0.28 for some cars, 0 for others)
// until the next step: the car jumped up and down above 60 fps. Now it's added
// once per step, on the first frame after it.
static const float kNoLift = 0.0f;
extern "C" const float* __cdecl GarageLift(BYTE* state) {
    static BYTE* lastState = nullptr;
    static LONG  lastStep = LONG_MIN / 2;
    const float* lift = reinterpret_cast<const float*>(state + 0x97C);
    if (!g_interpOn) return lift;
    if (state == lastState && g_stepsDone == lastStep) return &kNoLift;   // added since the last reset
    lastState = state; lastStep = g_stepsDone;
    return lift;
}
asm(".text\n"
    ".globl _GarageLiftStub\n_GarageLiftStub:\n"   // replaces fld [ebp+0x97C] at 0x4EE61F (call + nop)
    "    pushl %eax\n"                             // eax (the node's world) is used right after
    "    pushl %ecx\n"
    "    pushl %edx\n"
    "    pushl %ebp\n"
    "    call _GarageLift\n"
    "    addl $4, %esp\n"
    "    flds (%eax)\n"
    "    popl %edx\n"
    "    popl %ecx\n"
    "    popl %eax\n"
    "    ret\n");
