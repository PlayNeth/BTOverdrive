#include "common.h"
#include "camera.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "framerate.h"
#include "items.h"
#include "start.h"

// ---------------------------------------------------------------------------
// Speed FOV. Camera::Apply hands the camera's field of view to the renderer
// (0x5BA4ED); it's widened by up to speedFov degrees with the speed of the car
// it follows, between speedFovMin and speedFovMax: the player's car nearest the
// camera (else the nearest AI race car; car-select cars don't count) within 40 units, from its last two step
// positions (physics: exactly once a step; the camera itself may move per frame,
// which made a speed taken from it jump), smoothed over about a third of a second.
// Cameras with no race car near (menus, car select) are left alone; across a
// cut (a jump of over 25 units in a step) the last speed is kept.
// ---------------------------------------------------------------------------
// Each camera keeps its own smoothed speed: the game sets up more than one
// camera a frame, and a shared value was pulled to and fro between the moving
// race camera and the still ones, so the field of view jumped every frame.
// Speed blur. The game's post-processing has a "speed ghosting" blur: every
// frame a small copy of the scene goes into a 3-frame history (0x6A85E0), and
// when the post-process flag g_MotionBlur (+0x31C) is on, the three are
// averaged and Gaussian-blurred (0x6A8F08) and the final pass mixes that in by
// g_MotionBlurAmount. The game works the amount out from the field of view -
// (fov in radians - 1.3) x 2, 0 to 0.7 - so it only showed when a boost widened
// the view past about 75 degrees, and a level's own settings can switch the
// flag off (0x5726C0). So:
//  - the level's g_MotionBlur no longer overrides the flag (0x57287C: always
//    skipped; it starts at 1)
//  - just before the amount goes to the shader (0x575F65), the shader's own
//    g_MotionBlur is set to 1 and the amount raised to the speed blur: up to
//    speedBlur, from speedBlurMin to speedBlurMax (the player's speed, as the
//    speed FOV sees it), whichever is more.
// Not a directional blur: the whole frame trails a little. The car mostly
// stays put on screen, so it's the road and the surroundings that smear.
static float g_blurSpeed = 0.0f; static DWORD g_blurSpeedMs = 0;
static float g_blurMax = 0.0f; static LONG g_blurOn = 0; static bool g_blurLogged = false;
typedef HRESULT (__stdcall *EffSetFloat_t)(void*, const char*, float);
// HDR. Each region of a level has HDR settings ("HDR Region File Settings":
// g_OverBrightScale, g_Bloom, g_BloomScale, g_brightPassThreshold,
// g_brightnessClamp...) that the post-processing (0x5726C0) hands to its
// effect as they are, printing each one through the game's debug output
// (0x5727EE: "+%s: %f"). That print is hooked: the values are kept (and logged
// once each), and every frame the ones with an INI scale other than 1 are set
// again, scaled: hdrBrightness x g_OverBrightScale (the tone-mapped image's
// brightness: 1.5 on the car select), hdrBloom x g_BloomScale, hdrBloomThreshold
// x g_brightPassThreshold (only what's brighter than it blooms). Any setting
// can also be given outright as hdr.<name>=value (applied after the scales, so
// it wins). And hdrGlare x g_StarScale and g_brightnessClampStar: the "star"
// glare streaks the race regions draw from anything above 30% brightness
// (g_brightPassThresholdStar 0.3) at 0.5 strength, up to 3.5x - the overdone
// highlights on paint and floors. At 0.5 they're half as strong, up to 1.75x.
struct HdrParam { const char* name; float base; bool have; float* scale; };
static HdrParam g_hdr[5] = {
    { "g_OverBrightScale", 0, false, &g_cfg.hdrBrightness },
    { "g_BloomScale", 0, false, &g_cfg.hdrBloom },
    { "g_brightPassThreshold", 0, false, &g_cfg.hdrBloomThreshold },
    { "g_StarScale", 0, false, &g_cfg.hdrGlare },               // the glare streaks' strength
    { "g_brightnessClampStar", 0, false, &g_cfg.hdrGlare },     // and how bright they can get
};
struct HdrOv { char name[40]; float v; };
static HdrOv g_hdrOv[32]; static int g_hdrOvN = 0;
void HdrOverride(const char* name, float v) {
    if (g_hdrOvN >= 32) return;
    snprintf(g_hdrOv[g_hdrOvN].name, sizeof(g_hdrOv[0].name), "%s", name); g_hdrOv[g_hdrOvN++].v = v;
}
static bool HdrActive() { if (g_hdrOvN) return true; for (const HdrParam& h : g_hdr) if (*h.scale != 1.0f) return true; return false; }
extern "C" void __cdecl HdrParamNote(const char* fmt, const char* name, double v) {
    reinterpret_cast<void (__cdecl *)(const char*, const char*, double)>(hot::debugPrint)(fmt, name, v);   // (the game log, if on)
    if (!name || IsBadStringPtrA(name, 64)) return;
    static char seen[48][40]; static float seenV[48]; static int nSeen = 0;
    bool fresh = true;
    for (int i = 0; i < nSeen && fresh; i++) if (!strcmp(seen[i], name) && seenV[i] == (float)v) fresh = false;
    if (fresh && nSeen < 48) {
        snprintf(seen[nSeen], sizeof(seen[nSeen]), "%s", name); seenV[nSeen++] = (float)v;
        Logf("hdr: region setting %s = %.3f\n", name, v);
    }
    for (HdrParam& h : g_hdr) if (!strcmp(h.name, name)) { h.base = (float)v; h.have = true; }
}
static void HdrApply(void* eff) {
    if (!eff) return;
    void** vt = *reinterpret_cast<void***>(eff);
    for (const HdrParam& h : g_hdr)
        if (h.have && *h.scale != 1.0f)
            reinterpret_cast<EffSetFloat_t>(vt[0x78 / 4])(eff, h.name, h.base * *h.scale);
    static bool logged = false;
    for (int i = 0; i < g_hdrOvN; i++) {
        HRESULT hr = reinterpret_cast<EffSetFloat_t>(vt[0x78 / 4])(eff, g_hdrOv[i].name, g_hdrOv[i].v);
        if (!logged) Logf("hdr: %s set to %.3f%s\n", g_hdrOv[i].name, g_hdrOv[i].v, hr < 0 ? " - FAILED (no such parameter in the post-processing effect)" : "");
    }
    logged = true;
}
extern "C" void __cdecl SpeedBlur(float* amount, BYTE* pp) {
    float game = *amount, ours = 0.0f;
    if (GetTickCount() - g_blurSpeedMs < 500) {                 // (a race camera on the player's car just now)
        float span = g_cfg.speedBlurMax - g_cfg.speedBlurMin;
        float x = span > 0.0f ? (g_blurSpeed - g_cfg.speedBlurMin) / span : 0.0f;
        if (x < 0.0f) x = 0.0f; else if (x > 1.0f) x = 1.0f;
        ours = g_cfg.speedBlur * x * x * (3.0f - 2.0f * x);     // smoothstep
    }
    BYTE* fx = pp ? *reinterpret_cast<BYTE**>(pp + 0x234) : nullptr;
    void* eff = fx ? *reinterpret_cast<void**>(fx + 0x1C) : nullptr;   // the ID3DXEffect
    HdrApply(eff);
    if (g_cfg.speedBlur <= 0.0f) return;
    if (eff) {
        void** vt = *reinterpret_cast<void***>(eff);
        reinterpret_cast<EffSetFloat_t>(vt[0x78 / 4])(eff, "g_MotionBlur", 1.0f);   // (no such parameter: does nothing)
    }
    if (ours > game) *amount = ours;
    if (ours > 0.0f) {                                          // (for the log: once, ~10 s into the first speed blur)
        if (*amount > g_blurMax) g_blurMax = *amount;
        if (!g_blurLogged && ++g_blurOn == 600) {
            g_blurLogged = true;
            Logf("speed blur: on in a race; strongest so far %.2f (game's own now %.2f)\n", g_blurMax, game);
        }
    }
}
extern "C" void SpeedBlurStub();
asm(".text\n"
    ".globl _SpeedBlurStub\n_SpeedBlurStub:\n"   // call from 0x575F65 (replaces mov ecx,[ebx+0x234]); ebx = post-process, the amount at the caller's [esp+0x10]
    "    pushal\n"
    "    leal 0x34(%esp), %eax\n"                // 0x10 + 4 (return address) + 0x20 (pushal)
    "    pushl %ebx\n"
    "    pushl %eax\n"
    "    call _SpeedBlur\n"
    "    addl $8, %esp\n"
    "    popal\n"
    "    movl 0x234(%ebx), %ecx\n"
    "    ret\n");

// Speed shake. Camera::Apply builds the view from the camera's matrix right
// after it sets the field of view, so the shake goes into the camera's matrix
// here and comes out again at the end of the frame's drawing (HkFrameEnd),
// before the step state goes back: the game never sees it. It's a small turn
// (yaw and pitch, up to speedShake degrees), not a move: moving a camera that
// close to the car slid the car about on screen against a still background,
// which read as jitter. Strength builds from speedShakeMin to speedShakeMax;
// a few sine waves of 1.7-5 Hz that never line up make the rumble.
struct CamShakeSave { BYTE* cam; float orig[16], shaken[16]; };
static CamShakeSave g_shake[4];
static int g_shakeN = 0;
static LONG g_shakeDone = 0, g_shakeOutside = 0, g_shakeFrames = 0; static bool g_shakeLogged = false;
static void CamShake(BYTE* cam, float speed, double t) {
    float span = g_cfg.speedShakeMax - g_cfg.speedShakeMin;
    float x = span > 0.0f ? (speed - g_cfg.speedShakeMin) / span : (speed >= g_cfg.speedShakeMin ? 1.0f : 0.0f);
    if (x <= 0.0f || *reinterpret_cast<volatile BYTE*>(hot::worldFrozen)) return;   // (not while paused)
    if (x > 1.0f) x = 1.0f;
    float a = g_cfg.speedShake * x * x * (3.14159265f / 180.0f);   // radians
    float* m = reinterpret_cast<float*>(cam + 0x10);
    CamShakeSave* e = nullptr;                                  // (the same camera twice in a frame: from its own matrix)
    for (int i = 0; i < g_shakeN; i++) if (g_shake[i].cam == cam) { e = &g_shake[i]; break; }
    if (e) { if (memcmp(m, e->shaken, 64)) return; memcpy(m, e->orig, 64); }
    else { if (g_shakeN >= 4) return; e = &g_shake[g_shakeN++]; e->cam = cam; memcpy(e->orig, m, 64); }
    float yaw   = a * (0.5f * (float)sin(t * 10.7) + 0.3f * (float)sin(t * 18.2 + 1.3) + 0.2f * (float)sin(t * 28.9 + 4.0));
    float pitch = a * (0.5f * (float)sin(t * 12.3 + 0.7) + 0.3f * (float)sin(t * 20.6 + 2.1) + 0.2f * (float)sin(t * 31.4 + 5.2));
    // small-angle turn of the rows (0 right, 1 up, 2 forward): still square to
    // within angle^2 (under 1e-4), and it's put back after the frame anyway
    for (int k = 0; k < 3; k++) {
        float r = m[k], u = m[4 + k], f = m[8 + k];
        m[k] = r - f * yaw; m[4 + k] = u - f * pitch; m[8 + k] = f + r * yaw + u * pitch;
    }
    memcpy(e->shaken, m, 64);
    g_shakeDone++;
}
void CamShakeUndo() {
    for (int i = 0; i < g_shakeN; i++) {
        float* m = reinterpret_cast<float*>(g_shake[i].cam + 0x10);
        if (!memcmp(m, g_shake[i].shaken, 64)) memcpy(m, g_shake[i].orig, 64);   // (unless something else moved it since)
    }
    g_shakeN = 0;
    if ((g_shakeDone || g_shakeOutside) && !g_shakeLogged && ++g_shakeFrames >= 600) {   // (once, ~10 s into the first shaking)
        g_shakeLogged = true;
        Logf("camera shake: %ld shakes in 600 frames, %ld camera setups outside drawing skipped\n", g_shakeDone, g_shakeOutside);
    }
}
// Intro camera (introCamClip). While a race intro is on (0x959F80), a camera
// with a race car within 40 units is checked against the track (the game's line
// test, 0x635580: from the car, 1 unit up, to the camera, both sides of every
// triangle, its hit globals kept): scenery in between moves the camera in along
// that line to just short of it (halving the line 8 times). Only for the frame:
// the camera's own position is put back after the frame is drawn (the intro's
// path goes on as it was).
struct CamClipSave { BYTE* cam; float orig[3], moved[3]; };
static CamClipSave g_camClip[4];
static int g_camClipN = 0;
static LONG g_camClipLogged = 0;
bool TrackBetween(const float* a, const float* b) {     // (game coordinates)
    BYTE save[0x3C]; memcpy(save, reinterpret_cast<void*>(hot::lineHitGlobals), sizeof(save));
    int mode = *reinterpret_cast<int*>(hot::lineSides);
    *reinterpret_cast<int*>(hot::lineSides) = 2;
    bool hit = reinterpret_cast<bool (__cdecl *)(const float*, const float*, void*)>(hot::fnLineCollide)(a, b, nullptr);
    *reinterpret_cast<int*>(hot::lineSides) = mode;
    memcpy(reinterpret_cast<void*>(hot::lineHitGlobals), save, sizeof(save));
    return hit;
}
void IntroCamClip(BYTE* cam) {
    if (!g_cfg.introCamClip || !cam || !*reinterpret_cast<volatile BYTE*>(hot::introActive) || !g_interpOn) return;
    for (int i = 0; i < g_camClipN; i++) if (g_camClip[i].cam == cam) return;   // (done this frame)
    if (g_camClipN >= 4) return;
    float* cp = reinterpret_cast<float*>(cam + 0x10) + 12;
    const float* nearCar = nullptr; float best = 40.0f * 40.0f;
    for (const Track& t : g_tr) {
        if (!t.obj || t.kind != 1 || t.n != 2) continue;
        DWORD vt = reinterpret_cast<DWORD>(t.vt);
        if (vt != 0x8A0384 && vt != 0x8A0144 && vt != 0x8A3FA4) continue;
        float d[3] = { t.cur[12] - cp[0], t.cur[13] - cp[1], t.cur[14] - cp[2] };
        float dd = Dot3(d, d);
        if (dd < best) { best = dd; nearCar = t.cur + 12; }
    }
    if (!nearCar) return;
    float a[3] = { nearCar[0], nearCar[1] + 1.0f, nearCar[2] };
    if (!TrackBetween(a, cp)) return;
    float lo = 0.0f, hi = 1.0f;                                 // lo: free up to here
    for (int i = 0; i < 8; i++) {
        float m = (lo + hi) * 0.5f;
        float p[3] = { a[0] + (cp[0] - a[0]) * m, a[1] + (cp[1] - a[1]) * m, a[2] + (cp[2] - a[2]) * m };
        if (TrackBetween(a, p)) hi = m; else lo = m;
    }
    float k = lo * 0.9f;
    if (k < 0.15f) return;                                      // (the car itself in the scenery: leave it)
    CamClipSave& e = g_camClip[g_camClipN++];
    e.cam = cam; memcpy(e.orig, cp, 12);
    for (int i = 0; i < 3; i++) cp[i] = a[i] + (cp[i] - a[i]) * k;
    memcpy(e.moved, cp, 12);
    if (InterlockedIncrement(&g_camClipLogged) <= 10)
        Logf("intro camera: %p was in the scenery - moved in to %.0f%% of the way from the car\n", cam, k * 100.0f);
}
void IntroCamClipUndo() {                                // after the frame
    for (int i = 0; i < g_camClipN; i++) {
        float* cp = reinterpret_cast<float*>(g_camClip[i].cam + 0x10) + 12;
        if (!memcmp(cp, g_camClip[i].moved, 12)) memcpy(cp, g_camClip[i].orig, 12);   // (unless something else moved it since)
    }
    g_camClipN = 0;
}
struct CamFov { BYTE* cam; float speed; LONGLONG qpc; float fovPrev, fovCur; LONG fovStep; };
static CamFov g_camFov[8];
float g_maxCamSpeed = 0.0f;           // for the log (calibration)
extern "C" void __cdecl HkSetFov(float fov) {
    const Track* car = nullptr;                                 // the car this camera follows
    if (g_interpOn && g_applyCam) {
        const float* cp = reinterpret_cast<const float*>(g_applyCam + 0x10) + 12;   // the camera's position
        float best = 40.0f * 40.0f; bool bestPlayer = false;
        for (const Track& t : g_tr) {
            if (!t.obj || t.kind != 1 || t.n != 2 || t.snapStep != g_stepsDone) continue;
            float d[3] = { t.cur[12] - cp[0], t.cur[13] - cp[1], t.cur[14] - cp[2] };
            float dd = Dot3(d, d);
            DWORD vt = reinterpret_cast<DWORD>(t.vt);
            bool player = vt == 0x8A0384;                       // GSPlayerRaceCar_c
            if (!player && vt != 0x8A0144 && vt != 0x8A3FA4) continue;   // GSAIRaceCar_c, GSRaceCar_c only
            if (dd > 40.0f * 40.0f) continue;
            if ((player && !bestPlayer) || (player == bestPlayer && dd < best)) { car = &t; best = dd; bestPlayer = player; }
        }
    }
    CamFov* c = nullptr;
    if (g_applyCam) for (CamFov& e : g_camFov) if (e.cam == g_applyCam) { c = &e; break; }
    bool moving = false;                                        // the camera itself is tracked (it moves)
    if (g_interpOn && g_applyCam)
        for (const Track& t : g_tr) if (t.obj == g_applyCam && t.n == 2) { moving = true; break; }
    if (!car && !c && !moving) {                                // still and no car (menus): as it is
        reinterpret_cast<void (__cdecl *)(float)>(hot::fnSetFov)(fov);
        return;
    }
    if (!c) {                                                   // new camera: take a free slot or the oldest
        c = &g_camFov[0];
        for (CamFov& e : g_camFov) if (!e.cam || e.qpc < c->qpc) c = &e;
        c->cam = g_applyCam; c->speed = 0.0f; c->qpc = 0; c->fovStep = LONG_MIN / 2;
    }
    // The camera's own field of view changes on steps (the game's camera effects
    // set it every step) while its position is interpolated: blend it the same way.
    if (c->fovStep != g_stepsDone) {
        c->fovPrev = c->fovStep == g_stepsDone - 1 ? c->fovCur : fov;
        c->fovCur = fov;
        c->fovStep = g_stepsDone;
    }
    if (fov == c->fovCur && fabsf(c->fovCur - c->fovPrev) < 20.0f)   // (a jump: a cut, not blended)
        fov = c->fovPrev + (c->fovCur - c->fovPrev) * g_alpha;
    float speed = 0.0f;                                         // (no car near: ease back to none)
    if (car) {
        float d[3] = { car->cur[12] - car->prev[12], car->cur[13] - car->prev[13], car->cur[14] - car->prev[14] };
        float step = Len3(d);
        speed = step < kCutDist ? step / *reinterpret_cast<float*>(hot::stepDt) : c->speed;   // (a cut: keep)
    }
    static LONGLONG qpf = 0;
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    if (!qpf) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpf = f.QuadPart; }
    float dt = c->qpc ? (float)(q.QuadPart - c->qpc) / (float)qpf : 0.0f;
    c->qpc = q.QuadPart;
    if (dt > 0.25f) dt = 0.25f;
    // While the world is frozen (pause menu, countdown) the cars don't move or
    // aren't tracked: the speed would ease to 0 and the view narrow under the
    // pause menu. It's held as it was.
    if (!*reinterpret_cast<volatile BYTE*>(hot::worldFrozen))
        c->speed += (speed - c->speed) * (1.0f - expf(-dt / 0.33f));   // ~1/3 s smoothing
    if (c->speed > g_maxCamSpeed) g_maxCamSpeed = c->speed;
    float span = g_cfg.speedFovMax - g_cfg.speedFovMin;
    float x = span > 0.0f ? (c->speed - g_cfg.speedFovMin) / span : 0.0f;
    if (x < 0.0f) x = 0.0f; else if (x > 1.0f) x = 1.0f;
    x = x * x * (3.0f - 2.0f * x);                              // smoothstep
    reinterpret_cast<void (__cdecl *)(float)>(hot::fnSetFov)(fov + g_cfg.speedFov * x);
    if (car && reinterpret_cast<DWORD>(car->vt) == 0x8A0384) { g_blurSpeed = c->speed; g_blurSpeedMs = GetTickCount(); }
    if (car && g_cfg.speedShake > 0.0f) {
        if (g_inRender) CamShake(g_applyCam, c->speed, q.QuadPart / (double)qpf);
        else g_shakeOutside++;
    }
}

void InstallSpeedBlur() {
    // (the HDR region settings are always noted, for the log)
    if (!PatchCall(hot::callHdrPrint, reinterpret_cast<void*>(&HdrParamNote), 5)) Logf("hdr: patch FAILED\n");
    bool blur = g_cfg.speedBlur > 0.0f;
    if (blur && !g_cfg.interpolate) { Logf("speed blur: needs interpolate=1 (it uses the speed FOV's car speed)\n"); blur = false; }
    if (!blur && !HdrActive()) return;
    const BYTE jmp = 0xEB;
    if (!PatchCall(hot::blurAmount, reinterpret_cast<void*>(&SpeedBlurStub), 6) ||
        (blur && !PatchBytes(hot::blurFlagLevel, &jmp, 1))) { Logf("speed blur: patch FAILED\n"); return; }
    if (blur) Logf("speed blur: up to %.2f, from %.0f to %.0f units/s\n", g_cfg.speedBlur, g_cfg.speedBlurMin, g_cfg.speedBlurMax);
    if (HdrActive()) Logf("hdr: brightness x%.2f, bloom x%.2f, bloom threshold x%.2f\n", g_cfg.hdrBrightness, g_cfg.hdrBloom, g_cfg.hdrBloomThreshold);
}
