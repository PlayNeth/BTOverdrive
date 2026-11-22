#include "common.h"
#include "pacing.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "display.h"
#include "framerate.h"
#include "effects.h"
#include "camera.h"
#include "items.h"

// ---------------------------------------------------------------------------
// Frame pacing, and the frame rate's installers
// ---------------------------------------------------------------------------
// Frame cap: wait for our next frame time, then let the game's limiter measure
// the frame without waiting (its minimum frame time set to 0 for the call).
static LONGLONG g_qpf = 0, g_period = 0, g_next = 0;
static void PaceWait() {
    LARGE_INTEGER t;
    if (!g_qpf) {
        QueryPerformanceFrequency(&t); g_qpf = t.QuadPart;
        timeBeginPeriod(1);                                     // Sleep(1) ~ 1 ms (game thread, not DllMain)
        int fps = g_cfg.fpsLimit;
        if (fps <= 0) {
            DEVMODEA dm = {}; dm.dmSize = sizeof(dm);
            fps = EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm) ? (int)dm.dmDisplayFrequency : 0;
            if (fps <= 1) { Logf("frame rate: refresh rate unknown, capping at 60 fps (set fpsLimit)\n"); fps = 60; }
        }
        fps = ClampInt(fps, 30, 1000);
        g_period = g_qpf / fps;
        Logf("frame rate: capped at %d fps, interpolation on\n", fps);
    }
    QueryPerformanceCounter(&t);
    if (g_next) {
        while (g_next - t.QuadPart > 0) {
            if (g_next - t.QuadPart > g_qpf / 500) Sleep(1);       // more than 2 ms left
            else YieldProcessor();
            QueryPerformanceCounter(&t);
        }
    }
    // keep the cadence unless we're more than a frame late
    g_next = (g_next && t.QuadPart - g_next < g_period) ? g_next + g_period : t.QuadPart + g_period;
}
static void __attribute__((thiscall)) HkLimiter(void* lim) {
    float* minMs = reinterpret_cast<float*>(static_cast<BYTE*>(lim) + 4);
    float saved = *minMs;
    LONGLONG pf0 = g_cfg.perfLog ? Qpc() : 0;
    PaceWait();
    if (pf0) PerfFrameStart(Qpc() - pf0);
    *minMs = 0.0f;
    reinterpret_cast<Limiter_t>(hot::fnLimiter)(lim);
    *minMs = saved;                                             // the loading-screen loop keeps its own cap
}

// The car physics-sync hook (interpolation tracks the cars with it).
static bool g_carSyncHooked = false;
bool InstallCarSync() {
    if (g_carSyncHooked) return true;
    BYTE jmp6[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
    DWORD rel = reinterpret_cast<DWORD>(&CarSyncStub) - (hot::carSync + 5); memcpy(jmp6 + 1, &rel, 4);
    g_carSyncHooked = PatchBytes(hot::carSync, jmp6, 6);
    return g_carSyncHooked;
}

void InstallFrameRate() {
    if (!g_cfg.interpolate) { Logf("frame rate: original (60 fps, interpolate=0)\n"); return; }
    if (*reinterpret_cast<DWORD*>(hot::fixedStepOn) != 1 ||
        fabsf(*reinterpret_cast<float*>(hot::stepDt) - 1.0f / 60.0f) > 1e-5f) {
        Logf("frame rate: unexpected step settings - original 60 fps kept\n");
        return;
    }
    const BYTE jmpShort = 0xEB;
    BYTE jmp6[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
    bool ok = true;
    // hooks first; the step patch last, so a failure never leaves 0-step frames without the cap
    ok = ok && InstallCarSync();
    DWORD rel = reinterpret_cast<DWORD>(&CamApplyStub) - (hot::camApply + 5); memcpy(jmp6 + 1, &rel, 4);
    ok = ok && PatchBytes(hot::camApply, jmp6, 6);
    rel = reinterpret_cast<DWORD>(&VehDrawStub) - (hot::vehDraw + 5); memcpy(jmp6 + 1, &rel, 4);
    ok = ok && PatchBytes(hot::vehDraw, jmp6, 6);
    BYTE jmp7[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
    rel = reinterpret_cast<DWORD>(&SetWorldStub) - (hot::setWorld + 5); memcpy(jmp7 + 1, &rel, 4);
    ok = ok && PatchBytes(hot::setWorld, jmp7, 7);
    rel = reinterpret_cast<DWORD>(&GetXformStub) - (hot::getXform + 5); memcpy(jmp7 + 1, &rel, 4);
    ok = ok && PatchBytes(hot::getXform, jmp7, 5);
    if (g_cfg.interpolateBones) {
        BYTE jmp15[15] = { 0xE9 }; memset(jmp15 + 5, 0x90, 10);
        rel = reinterpret_cast<DWORD>(&BoneFetchStub) - (hot::boneFetch + 5); memcpy(jmp15 + 1, &rel, 4);
        ok = ok && PatchBytes(hot::boneFetch, jmp15, 15);
    }
    ok = ok && PatchCall(hot::callVehLights, reinterpret_cast<void*>(&HkVehLights), 5);
    for (DWORD at : hot::callCarShadow) ok = ok && PatchCall(at, reinterpret_cast<void*>(&HkCarShadow), 5);
    ok = ok && PatchCall(hot::callCarTrails, reinterpret_cast<void*>(&HkCarTrails), 5);
    const struct { DWORD at; void (*stub)(); } effHooks[] = {
        { hot::effPos, &EffPosStub }, { hot::effPosObj18, &EffPosObj18Stub }, { hot::effPosNode, &EffPosNodeStub },
        { hot::effPosObj10, &EffPosObj10Stub }, { hot::effRow2, &EffRow2Stub }, { hot::effRow1, &EffRow1Stub },
        { hot::effRow0, &EffRow0Stub }, { hot::effRows, &EffRowsStub } };
    for (const auto& h : effHooks) ok = ok && PatchJmp(h.at, reinterpret_cast<DWORD>(h.stub));
    ok = ok && PatchCall(hot::garageLift, reinterpret_cast<void*>(&GarageLiftStub), 6);
    ok = ok && PatchCall(hot::callCamFov, reinterpret_cast<void*>(&HkSetFov), 5);   // (FOV blend + speed FOV)
    ok = ok && PatchCall(hot::callStepTail, reinterpret_cast<void*>(&HkStepTail), 5);
    ok = ok && PatchCall(hot::callFrameEnd, reinterpret_cast<void*>(&HkFrameEnd), 5);   // before the blend can happen
    ok = ok && PatchCall(hot::callRender3D, reinterpret_cast<void*>(&HkRender3D), 5);
    ok = ok && PatchCall(hot::callLimiter, reinterpret_cast<void*>(&HkLimiter), 5);
    ok = ok && PatchBytes(hot::forceOneStep, &jmpShort, 1);
    if (!ok) Logf("frame rate: a patch FAILED\n");
    else g_interpOn = true;
}
