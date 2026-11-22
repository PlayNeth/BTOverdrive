#include "common.h"
#include "framerate.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "effects.h"
#include "camera.h"
#include "hazards.h"
#include "ai_route.h"
#include "icegun.h"
#include "items.h"

// ---------------------------------------------------------------------------
// Frame rate: pacing + interpolation
//
// The game simulates in fixed 1/60 s steps (0x5A2360 turns the frame time into
// a step count, keeping the remainder in an accumulator) but forces at least one
// step per frame and caps the frame rate at 60 with a Sleep(0) spin (0x58DE90).
// With interpolate=1:
//  - frames with no step are allowed (0x5A23E9), the frame cap is ours (fpsLimit)
//    and waits with Sleep(1) + a short spin instead of Sleep(0)
//  - after every step the world matrices (object+0x10) of cameras and drawn
//    vehicles, and of other matrices the 3D pass uses, are copied; before the
//    3D pass each one is set to a blend of the last two steps, by how far the
//    accumulator is into the next step, and put back when the frame is drawn.
//    Cameras are found through Camera::Apply (0x5BA4C0), vehicles through their
//    draw (0x604B20), other matrices through SetWorld/GetTransform.
//  - an object is only blended if its matrix still holds exactly what was copied
//    at the step, and only put back if it still holds exactly the blend, so
//    anything else that moves it is left alone (and counted in the log).
//  - matrices the drawing code computes itself (scene-graph nodes: race cars and
//    their wheels) are blended as they're handed to SetWorld instead.
// ---------------------------------------------------------------------------

Track g_tr[64];
LONG  g_stepsDone = 0, g_frameNo = 0;
bool  g_inRender = false;
bool  g_interpOn = false;              // frame-rate hooks installed
static struct { LONG frames, steps, blended, external, redrawn, cuts, camOutside, full,
                 mBlended, mExternal, mRedrawn, mCuts, mFull, vehDrawn, vehOutside, fBlended; } g_st;
static DWORD g_statTick = 0;

extern "C" {
    DWORD g_carSyncCont = hot::carSync + 6;
    DWORD g_camApplyCont = hot::camApply + 6;
    DWORD g_setWorldCont = hot::setWorld + 7;
    DWORD g_vehDrawCont = hot::vehDraw + 6;
    DWORD g_boneFetchCont = hot::boneFetch + 15;
}

static Track* FindTrack(BYTE* obj, int kind) {
    void* vt = *reinterpret_cast<void**>(obj);
    Track* freeSlot = nullptr; Track* stale = nullptr;
    for (Track& t : g_tr) {
        if (t.obj == obj) {
            if (t.kind != kind || t.vt != vt) { t.kind = kind; t.vt = vt; t.n = 0; t.written = false; }
            return &t;
        }
        if (!t.obj) { if (!freeSlot) freeSlot = &t; }
        else if (!stale && t.seenStep < g_stepsDone - 120 && t.seenFrame < g_frameNo - 120) stale = &t;
    }
    Track* t = freeSlot ? freeSlot : stale;
    if (!t) { g_st.full++; return nullptr; }
    t->obj = obj; t->vt = vt; t->kind = kind; t->n = 0; t->written = false;
    t->seenStep = t->seenFrame = t->snapStep = t->outFrame = t->trailStep = LONG_MIN / 2;
    return t;
}
volatile LONG g_kicksPending = 0;              // hazard hits waiting for their car (see "Hazard hits")
bool g_hzOn = false;
bool g_fallOn = false;
bool g_aiStuckOn = false;
bool g_carFxOn = false;                       // IceTrack: the ice gun fix, a mega shield's lock, the shield's shock
bool g_sndStepHooked = false;                  // the sound update (once per step) is hooked: HkSndUpdate
// The race clock (empAfter): the race's steps since the countdown, counted on the
// player's car (the first one seen after it, in split screen), not while paused.
bool  g_raceClockOn = false;
BYTE* g_raceCar = nullptr;
static DWORD g_raceCarMs = 0;                         // (when it was last seen)
LONG  g_raceSteps = 0;
static void RaceClock(BYTE* car) {
    if (*reinterpret_cast<const DWORD*>(car) != 0x8A0384) return;   // (the player's car)
    DWORD now = GetTickCount();
    if (*reinterpret_cast<volatile BYTE*>(hot::introActive)) { g_raceCar = nullptr; g_raceSteps = 0; return; }   // (the countdown)
    if (car != g_raceCar) {
        if (g_raceCar && now - g_raceCarMs < 2000) return;    // (another player's car)
        g_raceCar = car; g_raceSteps = 0;                    // (a race with no countdown: from here)
    }
    g_raceCarMs = now;
    if (!*reinterpret_cast<volatile BYTE*>(hot::worldFrozen)) g_raceSteps++;
}
extern "C" void __cdecl RegCar(BYTE* car) {          // per car, per step, at the start of its physics sync
    if (!car) return;
    if (g_raceClockOn) RaceClock(car);
    if (g_hzOn) { NoteHazardCar(car); if (g_kicksPending) ApplyHazardKick(car); HazardFallCheck(car); }
    if (g_fallOn) FallTrack(car);
    if (g_aiStuckOn) AiStuckTrack(car);
    if (g_cfg.aiLoopSpeed > 0.0f) AiLoopAssist(car);
    if (g_carFxOn) IceTrack(car);
    if (!g_interpOn) return;
    if (Track* t = FindTrack(car, 1)) t->seenStep = g_stepsDone + 1;
}
BYTE* g_applyCam = nullptr;                   // the camera Camera::Apply is setting up (speed FOV)
extern "C" void __cdecl RegCam(BYTE* cam) {          // per camera, per frame
    g_applyCam = cam;
    if (!cam) return;
    if (!g_inRender) g_st.camOutside++;
    else IntroCamClip(cam);                          // (as the frame's view is set up from it)
    if (Track* t = FindTrack(cam, 2)) t->seenFrame = g_frameNo;
}
// Vehicles drawn without physics (the car-select cars driving in and out are
// VehicleMovers): registered when the 3D pass draws them, like cameras. A race
// car (already tracked through its physics sync) is left as it is.
extern "C" void __cdecl RegDrawnVehicle(BYTE* veh) {
    if (!veh) return;
    if (!g_inRender) { g_st.vehOutside++; return; }
    g_st.vehDrawn++;
    for (Track& t : g_tr)
        if (t.obj == veh && t.kind == 1) return;
    if (Track* t = FindTrack(veh, 2)) t->seenFrame = g_frameNo;
}
asm(".text\n"
    ".globl _CarSyncStub\n_CarSyncStub:\n"     // replaces sub esp,0xC4 at 0x5FB260 (thiscall, ecx = car)
    "    pushal\n"
    "    pushl %ecx\n"
    "    call _RegCar\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    subl $0xC4, %esp\n"
    "    jmp *_g_carSyncCont\n"
    ".globl _CamApplyStub\n_CamApplyStub:\n"   // replaces sub esp,0x80 at 0x5BA4C0 (thiscall, ecx = camera)
    "    pushal\n"
    "    pushl %ecx\n"
    "    call _RegCam\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    subl $0x80, %esp\n"
    "    jmp *_g_camApplyCont\n"
    ".globl _VehDrawStub\n_VehDrawStub:\n"     // replaces sub esp,0x108 at 0x604B20 (thiscall, ecx = vehicle)
    "    pushal\n"
    "    pushl %ecx\n"
    "    call _RegDrawnVehicle\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    subl $0x108, %esp\n"
    "    jmp *_g_vehDrawCont\n");
// Changed between steps within the last half second: animated per frame (or a
// one-off move, e.g. a car swapped in on car select), so not blended. Not for
// good: a matrix's memory is reused by later objects.
bool PerFrame(const MTrack* t) { return g_stepsDone - t->fPerFrameStep < 30; }
static const int kMaxM = 2048;
static MTrack g_mt[kMaxM];
static int    g_mtN = 0;
static short  g_mHash[4096];                       // index + 1, 0 = empty; at most half full
DWORD  g_stackLo = 0, g_stackHi = 0;        // game thread's stack (set on the first 3D pass)

static int MHash(const float* p) { return (int)(((DWORD)(UINT_PTR)p >> 4) * 2654435761u >> 20); }
MTrack* MFind(const float* p) {
    for (int h = MHash(p);; h = (h + 1) & 4095) {
        int i = g_mHash[h];
        if (!i) return nullptr;
        if (g_mt[i - 1].p == p) return &g_mt[i - 1];
    }
}
static void MRehash() {
    memset(g_mHash, 0, sizeof(g_mHash));
    for (int i = 0; i < g_mtN; i++) {
        int h = MHash(g_mt[i].p);
        while (g_mHash[h]) h = (h + 1) & 4095;
        g_mHash[h] = (short)(i + 1);
    }
}
// A world matrix the drawing code computes itself (a scene-graph node's world
// = parent x local, done while drawing, e.g. the car-select cars) can't be
// blended in memory beforehand. Instead the value handed to SetWorld is
// recorded once per frame; when it only ever changes on 60 Hz steps, SetWorld
// gets a blend of its last two step values. If it changes between steps too,
// it's animated per frame (or blended already) and passed through untouched.
bool Blend(const float* p, const float* c, float a, float* o);
float g_alpha = 1.0f;                       // this frame's blend factor (set before the 3D pass)
static const float* FilterWorld(MTrack* t, const float* v) {
    if (t->fFrame != g_frameNo) {                  // first time this frame
        t->fFrame = g_frameNo;
        if (t->fStep != g_stepsDone) {             // step(s) since the last value
            // Usually one step; a slow frame can have two or more, and then the
            // value in between was never seen. The older one is still a fine
            // neighbour: blend across the steps (below). Only a long gap (or the
            // first value) starts over.
            LONG gap = g_stepsDone - t->fStep;
            if (t->fn && gap >= 1 && gap <= 4) { memcpy(t->fprev, t->fcur, 64); t->fPrevStep = t->fStep; t->fn = 2; }
            else t->fn = 1;
            memcpy(t->fcur, v, 64);
            t->fStep = g_stepsDone;
        } else if (memcmp(t->fcur, v, 64)) {       // changed without a step
            t->fPerFrameStep = g_stepsDone;
        }
    }
    static float out[8][16]; static int k = 0;
    float* o = out[k = (k + 1) & 7];
    // Animated per frame (a car part like a spoiler, moved every frame on top
    // of the car): left at its raw position it sat a fraction of a step behind
    // the smoothed body, and since that mark wears off after half a second it
    // jumped back and forth. On a race car it follows the car's blend instead.
    if (PerFrame(t)) { const float* f = FollowCar(v, o); return f ? f : v; }
    if (t->fn < 2 || memcmp(t->fcur, v, 64) || !memcmp(t->fprev, t->fcur, 64)) return v;
    // the drawn moment is step (fStep - 1 + alpha); fprev is from fPrevStep
    float span = (float)(t->fStep - t->fPrevStep);
    float f = span > 1.0f ? (span - 1.0f + g_alpha) / span : g_alpha;
    if (!Blend(t->fprev, t->fcur, f, o)) { g_st.mCuts++; return v; }
    g_st.fBlended++;
    return o;
}
extern "C" const float* __cdecl RegMat(float* p, int src, DWORD caller) {
    if (!g_inRender || !p) return p;
    DWORD a = (DWORD)(UINT_PTR)p;
    if (a >= g_stackLo && a < g_stackHi) return p; // a temporary on the stack
    MTrack* t = MFind(p);
    if (!t) {
        if (g_mtN >= kMaxM) { g_st.mFull++; return p; }
        t = &g_mt[g_mtN++];
        t->p = p; t->n = 0; t->written = false; t->src = 0; t->caller = caller; t->redrawn = t->external = 0;
        t->snapStep = t->skipFrame = LONG_MIN / 2;
        t->fFrame = t->fStep = t->fPrevStep = LONG_MIN / 2; t->fn = 0; t->fPerFrameStep = LONG_MIN / 2; t->drawnOnly = false;
        int h = MHash(p);
        while (g_mHash[h]) h = (h + 1) & 4095;
        g_mHash[h] = (short)g_mtN;
    }
    t->seenFrame = g_frameNo;
    t->src |= (BYTE)src;
    return src == 1 && !t->written ? FilterWorld(t, p) : p;     // (written: already blended in memory)
}
asm(".text\n"
    ".globl _BoneFetchStub\n_BoneFetchStub:\n"   // replaces the bone fetch at 0x54AF55 (skinned models: the ghoul...)
    "    movl 8(%ebp), %ecx\n"
    "    movl (%ecx,%eax,4), %esi\n"
    "    shll $6, %esi\n"
    "    addl 0x1D0(%ebx), %esi\n"             // esi = the bone's matrix (in the model's bone array)
    "    pushal\n"
    "    pushl $0x54AF55\n"                    // (caller tag, for the log)
    "    pushl $1\n"
    "    pushl %esi\n"
    "    call _RegMat\n"                       // blended like a SetWorld matrix (FilterWorld)
    "    addl $12, %esp\n"
    "    movl %eax, 4(%esp)\n"                 // esi after popal
    "    popal\n"
    "    jmp *_g_boneFetchCont\n"
    ".globl _SetWorldStub\n_SetWorldStub:\n"   // replaces sub esp,0x40 / mov eax,[esp+0x44] at 0x53C860
    "    pushal\n"
    "    pushl 32(%esp)\n"                     // the caller (return address, above pushal)
    "    pushl $1\n"
    "    pushl 44(%esp)\n"                     // the matrix argument: 32 (pushal) + 4 (ret) + 8 (two pushes)
    "    call _RegMat\n"
    "    addl $12, %esp\n"
    "    movl %eax, 36(%esp)\n"                // the matrix SetWorld will use (the caller pops it unread)
    "    popal\n"
    "    subl $0x40, %esp\n"
    "    movl 0x44(%esp), %eax\n"
    "    jmp *_g_setWorldCont\n"
    ".globl _GetXformStub\n_GetXformStub:\n"   // replaces the whole getter at 0x416270 (thiscall)
    "    pushal\n"
    "    pushl 32(%esp)\n"                     // the caller
    "    pushl $2\n"
    "    leal 0x10(%ecx), %eax\n"
    "    pushl %eax\n"
    "    call _RegMat\n"
    "    addl $12, %esp\n"
    "    popal\n"
    "    leal 0x10(%ecx), %eax\n"
    "    ret\n");

// Last call of each 60 Hz step: copy the matrices of everything seen this step.
void __cdecl HkStepTail() {
    reinterpret_cast<VoidFn_t>(hot::fnStepTail)();
    g_stepsDone++; g_st.steps++;
    for (Track& t : g_tr) {
        if (!t.obj) continue;
        bool live = t.kind == 1 ? t.seenStep == g_stepsDone : t.seenFrame == g_frameNo;
        if (!live) { t.n = 0; continue; }
        if (*reinterpret_cast<void**>(t.obj) != t.vt) { t.obj = nullptr; continue; }
        if (t.snapStep != g_stepsDone - 1) t.n = 0;       // a gap: the old copy is no neighbour
        if (t.n) memcpy(t.prev, t.cur, 64);
        memcpy(t.cur, t.obj + 0x10, 64);
        t.snapStep = g_stepsDone;
        if (t.n < 2) t.n++;
    }
    int keep = 0;                                  // matrices drawn in the last 3D pass; drop the rest
    for (int i = 0; i < g_mtN; i++) {
        MTrack& m = g_mt[i];
        if (m.seenFrame != g_frameNo || IsBadReadPtr(m.p, 64)) continue;
        if (m.snapStep != g_stepsDone - 1) m.n = 0;
        if (m.n) memcpy(m.prev, m.cur, 64);
        memcpy(m.cur, m.p, 64);
        m.snapStep = g_stepsDone;
        if (m.n < 2) m.n++;
        if (keep != i) g_mt[keep] = m;
        keep++;
    }
    if (keep != g_mtN) { g_mtN = keep; MRehash(); }
}

float Len3(const float* v) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
float Dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
// Blend two row-major world matrices (rows 0-2 rotation*scale, row 3 position).
// The rotation rows are lerped and re-orthonormalised (Gram-Schmidt, so the
// handedness is kept), each scaled by its lerped length. The w column is kept.
// A position jump is a cut (respawn, camera cut): no blend. A rotation of more
// than 60 degrees in one step can't be told apart from its opposite (a fast
// spinning wheel), so then only the position is blended.
bool Blend(const float* p, const float* c, float a, float* o) {
    memcpy(o, c, 64);
    float d[3] = { c[12] - p[12], c[13] - p[13], c[14] - p[14] };
    if (Dot3(d, d) > kCutDist * kCutDist) return false;
    for (int i = 0; i < 3; i++) o[12 + i] = p[12 + i] + d[i] * a;
    float v[3][3], len[3];
    for (int r = 0; r < 3; r++) {
        const float* pr = p + 4 * r; const float* cr = c + 4 * r;
        float lp = Len3(pr), lc = Len3(cr);
        if (lp < 1e-6f || lc < 1e-6f) return true;        // degenerate: position only
        if (Dot3(pr, cr) < 0.5f * lp * lc) return true;   // turned over 60 degrees: position only
        for (int i = 0; i < 3; i++) v[r][i] = pr[i] + (cr[i] - pr[i]) * a;
        len[r] = lp + (lc - lp) * a;
    }
    for (int r = 0; r < 3; r++) {
        for (int q = 0; q < r; q++) {                     // v[q] is already unit length
            float k = Dot3(v[r], v[q]);
            for (int i = 0; i < 3; i++) v[r][i] -= k * v[q][i];
        }
        float l = Len3(v[r]);
        if (l < 1e-6f) return true;                       // (o still has the current rotation)
        for (int i = 0; i < 3; i++) v[r][i] /= l;
    }
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < 3; i++) o[4 * r + i] = v[r][i] * len[r];
    return true;
}

static void LogStats() {
    if (!g_cfg.gameLog) return;                    // diagnostics only
    DWORD now = GetTickCount();
    if (!g_statTick) { g_statTick = now; return; }
    if (now - g_statTick < 10000) return;
    double sec = (now - g_statTick) / 1000.0;
    g_statTick = now;
    if (g_st.frames > 0) {
        int cars = 0, cams = 0;
        for (const Track& t : g_tr)
            if (t.obj && t.n == 2 && t.snapStep == g_stepsDone) (t.kind == 1 ? cars : cams)++;
        Logf("frame rate: %.0f fps, %.2f steps/frame | tracking %d race car(s), %d camera(s)/other vehicle(s), %.2f blended/frame | "
             "skipped: moved outside a step %ld, cut %ld, changed during drawing %ld%s%s\n",
             g_st.frames / sec, (double)g_st.steps / g_st.frames, cars, cams, (double)g_st.blended / g_st.frames,
             g_st.external, g_st.cuts, g_st.redrawn,
             g_st.camOutside ? " | camera applied outside drawing" : "", g_st.full ? " | table full" : "");
        Logf("  vehicle draws: %.2f/frame while drawing, %ld outside\n", (double)g_st.vehDrawn / g_st.frames, g_st.vehOutside);
        int moving = 0, viaSet = 0, viaGet = 0;
        for (int i = 0; i < g_mtN; i++) {
            const MTrack& m = g_mt[i];
            if (m.src & 1) viaSet++;
            if (m.src & 2) viaGet++;
            if (m.n == 2 && memcmp(m.prev, m.cur, 64)) moving++;
        }
        int perFrame = 0;
        for (int i = 0; i < g_mtN; i++) if (PerFrame(&g_mt[i])) perFrame++;
        Logf("  computed while drawing: %.2f blended/frame, %d animated per frame (left alone)\n",
             (double)g_st.fBlended / g_st.frames, perFrame);
        Logf("  other objects: %d matrices (%d via SetWorld, %d via GetTransform), %d moving, %.2f blended/frame | "
             "skipped: moved outside a step %ld, cut %ld, changed during drawing %ld%s\n",
             g_mtN, viaSet, viaGet, moving, (double)g_st.mBlended / g_st.frames, g_st.mExternal, g_st.mCuts,
             g_st.mRedrawn, g_st.mFull ? " | table full" : "");
        struct CC { DWORD caller; LONG n, redrawn, external; } cc[8] = {};
        for (int i = 0; i < g_mtN; i++) {
            MTrack& m = g_mt[i];
            for (int k = 0; k < 8; k++) {
                if (cc[k].caller == m.caller || !cc[k].caller) {
                    cc[k].caller = m.caller; cc[k].n++; cc[k].redrawn += m.redrawn; cc[k].external += m.external;
                    break;
                }
            }
            m.redrawn = m.external = 0;
        }
        for (int k = 0; k < 8 && cc[k].caller; k++)
            Logf("    via 0x%06lX: %ld matrices, changed during drawing %ld, moved outside a step %ld\n",
                 cc[k].caller, cc[k].n, cc[k].redrawn, cc[k].external);
    }
    memset(&g_st, 0, sizeof(g_st));
}

// The 3D pass: blend in (put back in HkFrameEnd), then draw.
// Frame timing (perfLog=1): where a frame's time goes, logged every 10 s. The
// main loop (0x40EC1A on): the frame cap (waiting), the 60 Hz steps (0x59B3F0
// each: the game's simulation, AI, physics), the 3D pass (0x421160: walks the
// scene and queues the draws), the 2D pass (0x421100, 0x421150: HUD), the
// renderer's flush (0x54DA00: every Direct3D draw call, shadows, the
// post-processing - CPU time spent feeding the driver, plus waits for the GPU)
// and the frame end (0x54DC30: Present). What's left over is "other".
enum { PF_FRAME, PF_WAIT, PF_SIM, PF_3D, PF_2D, PF_DRAW, PF_PRESENT, PF_OTHER, PF_N };
static LONGLONG g_pfQpf = 0, g_pfFrameStart = 0, g_pfCur[PF_N];
static double g_pfSum[PF_N], g_pfMax[PF_N]; static LONG g_pfN = 0, g_pfSlow = 0, g_pfSteps = 0; static DWORD g_pfLogMs = 0;
LONGLONG Qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
void PerfFrameStart(LONGLONG waitTicks) {                // the limiter: one frame ends, the next starts
    LONGLONG now = Qpc();
    if (!g_pfQpf) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_pfQpf = f.QuadPart; }
    if (g_pfFrameStart) {
        double k = 1000.0 / (double)g_pfQpf, v[PF_N];
        g_pfCur[PF_FRAME] = now - waitTicks - g_pfFrameStart;   // (from this frame's wait start back to the last one's)
        LONGLONG other = g_pfCur[PF_FRAME];
        for (int i = PF_WAIT; i < PF_OTHER; i++) other -= g_pfCur[i];
        g_pfCur[PF_OTHER] = other;
        for (int i = 0; i < PF_N; i++) { v[i] = g_pfCur[i] * k; g_pfSum[i] += v[i]; if (v[i] > g_pfMax[i]) g_pfMax[i] = v[i]; }
        g_pfN++;
        if (v[PF_FRAME] - v[PF_WAIT] > 16.7) g_pfSlow++;        // (work alone over a 60 Hz frame)
    }
    g_pfFrameStart = now - waitTicks;
    memset(g_pfCur, 0, sizeof(g_pfCur));
    g_pfCur[PF_WAIT] = waitTicks;
    DWORD ms = GetTickCount();
    if (!g_pfLogMs) g_pfLogMs = ms;
    if (ms - g_pfLogMs >= 10000 && g_pfN > 0) {
        double n = (double)g_pfN;
        static const char* nm[PF_N] = { "frame", "waiting", "game steps", "3D queue", "2D", "draw (flush)", "present", "other" };
        char line[600]; int l = snprintf(line, sizeof(line), "perf: %.0f fps, %.2f steps/frame; avg/max ms:", 1000.0 / (g_pfSum[PF_FRAME] / n),
                                         (double)g_pfSteps / n);
        for (int i = PF_WAIT; i < PF_N && l < 560; i++)
            l += snprintf(line + l, sizeof(line) - l, " %s %.1f/%.1f%s", nm[i], g_pfSum[i] / n, g_pfMax[i], i + 1 < PF_N ? "," : "");
        Logf("%s; %ld of %ld frames over 16.7 ms of work\n", line, g_pfSlow, g_pfN);
        memset(g_pfSum, 0, sizeof(g_pfSum)); memset(g_pfMax, 0, sizeof(g_pfMax)); g_pfN = g_pfSlow = g_pfSteps = 0; g_pfLogMs = ms;
    }
}
static void __cdecl HkPfStep() {
    LONGLONG t = Qpc(); reinterpret_cast<VoidFn_t>(hot::fnGameStep)(); g_pfCur[PF_SIM] += Qpc() - t; g_pfSteps++;
}
static void __cdecl HkPf2D1() { LONGLONG t = Qpc(); reinterpret_cast<VoidFn_t>(hot::fn2D1)(); g_pfCur[PF_2D] += Qpc() - t; }
static void __cdecl HkPf2D2() { LONGLONG t = Qpc(); reinterpret_cast<VoidFn_t>(hot::fn2D2)(); g_pfCur[PF_2D] += Qpc() - t; }
static void __attribute__((thiscall)) HkPfFlush(void* r) {
    LONGLONG t = Qpc(); reinterpret_cast<Limiter_t>(hot::fnFlush)(r); g_pfCur[PF_DRAW] += Qpc() - t;
}
void InstallPerf() {
    if (!g_cfg.perfLog || !g_cfg.interpolate) return;           // (the frame start comes from the frame cap)
    if (!PatchCall(hot::callGameStep, reinterpret_cast<void*>(&HkPfStep), 5) ||
        !PatchCall(hot::call2D1, reinterpret_cast<void*>(&HkPf2D1), 5) ||
        !PatchCall(hot::call2D2, reinterpret_cast<void*>(&HkPf2D2), 5) ||
        !PatchCall(hot::callFlush, reinterpret_cast<void*>(&HkPfFlush), 5)) Logf("perf: patch FAILED\n");
    else Logf("perf: frame timing in the log every 10 s\n");
}
void __cdecl HkRender3D() {
    g_frameNo++; g_st.frames++;
    float a = 1.0f, dt = *reinterpret_cast<float*>(hot::stepDt);
    if (dt > 0.0f) a = *reinterpret_cast<float*>(hot::stepAccum) / dt;
    if (!(a >= 0.0f)) a = 0.0f; else if (a > 1.0f) a = 1.0f;     // also catches NaN
    g_alpha = a;
    for (Track& t : g_tr) {
        t.written = false;
        if (!t.obj || t.n < 2 || t.snapStep != g_stepsDone) continue;
        // Race cars aren't blended in memory: the scene graph recomputes their
        // wheels from the body only on some frames, so a blended body showed the
        // wheels jumping between blended and unblended positions (a flicker).
        // Body and wheels are blended together where they're handed to SetWorld
        // (FilterWorld) instead. Their step copies are still kept: effects,
        // shadows and trails that follow a car get its blend (CarBlend).
        if (t.kind == 1) continue;
        if (*reinterpret_cast<void**>(t.obj) != t.vt) continue;
        float* live = reinterpret_cast<float*>(t.obj + 0x10);
        if (memcmp(live, t.cur, 64)) { g_st.external++; continue; }
        if (!Blend(t.prev, t.cur, a, t.out)) { g_st.cuts++; continue; }
        memcpy(live, t.out, 64);
        t.written = true; g_st.blended++;
    }
    for (Track& t : g_tr)                          // already handled above: don't blend twice
        if (t.obj) if (MTrack* m = MFind(reinterpret_cast<float*>(t.obj + 0x10))) m->skipFrame = g_frameNo;
    for (int i = 0; i < g_mtN; i++) {
        MTrack& m = g_mt[i];
        m.written = false;
        if (m.n < 2 || m.snapStep != g_stepsDone || m.skipFrame == g_frameNo || m.drawnOnly) continue;
        if (!memcmp(m.prev, m.cur, 64)) continue;  // not moving
        if (memcmp(m.p, m.cur, 64)) { g_st.mExternal++; m.external++; continue; }
        if (!Blend(m.prev, m.cur, a, m.out)) { g_st.mCuts++; continue; }
        memcpy(m.p, m.out, 64);
        m.written = true; g_st.mBlended++;
    }
    if (!g_stackHi) {
        asm volatile("movl %%fs:4, %0" : "=r"(g_stackHi));   // TIB StackBase: top of this thread's stack
        g_stackLo = g_stackHi > 0x100000 ? g_stackHi - 0x100000 : 0;  // HOT.exe reserves 1 MB of stack
    }
    g_inRender = true;
    LONGLONG pf0 = g_cfg.perfLog ? Qpc() : 0;
    reinterpret_cast<VoidFn_t>(hot::fnRender3D)();
    if (pf0) g_pfCur[PF_3D] += Qpc() - pf0;
}

// End of the frame's drawing (0x54DC30, after the 2D pass and the renderer's
// flush 0x54DA00): put the step state back. The renderer queues draws and
// reads many matrices only when it flushes, so the blend has to stay in until
// then, not just through the 3D pass.

void __attribute__((thiscall)) HkFrameEnd(void* renderer) {
    LONGLONG pf0 = g_cfg.perfLog ? Qpc() : 0;
    reinterpret_cast<Limiter_t>(hot::fnFrameEnd)(renderer);
    if (pf0) g_pfCur[PF_PRESENT] += Qpc() - pf0;
    CamShakeUndo();                                // (before the check below: a shaken camera isn't "moved by the pass")
    IntroCamClipUndo();
    if (!g_inRender) return;                       // no blend this frame
    g_inRender = false;
    for (Track& t : g_tr) {
        if (!t.written) continue;
        t.written = false;
        float* live = reinterpret_cast<float*>(t.obj + 0x10);
        if (!memcmp(live, t.out, 64)) memcpy(live, t.cur, 64);
        else { g_st.redrawn++; t.n = 0; }                       // the 3D pass moved it itself: leave it
    }
    for (int i = 0; i < g_mtN; i++) {              // (entries added during the pass have written = false)
        MTrack& m = g_mt[i];
        if (!m.written) continue;
        m.written = false;
        if (!memcmp(m.p, m.out, 64)) memcpy(m.p, m.cur, 64);
        else {
            // The drawing code recomputes this one (a scene-graph node's world), but
            // not every frame: a blend written into it would be shown on some frames
            // and the real value on others - a flicker (the race-car wheels). From
            // now on it's only blended where it's handed to SetWorld (FilterWorld).
            g_st.mRedrawn++; m.redrawn++; m.n = 0; m.drawnOnly = true;
        }
    }
    LogStats();
}
