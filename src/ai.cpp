#include "common.h"
#include "ai.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "items.h"

// ---------------------------------------------------------------------------
// AI opponents. Their rubber-banding is CatchUp (per car, 0x6681A0; the script's
// Catchup* values are loaded and never used): an AI more than 0/80/160 units
// (by difficulty) ahead of the player has its target speed capped at 26.7 (31.1
// level, 35.6 behind) and its grip and turning cut. aiLeadSlowdown=0 skips that
// case (0x66845D: jp -> jmp), so a leading AI races at the level pace; the
// boost when behind stays.
// Weapons: every AI can get and fire every item, but after any AI shot all AIs
// wait 5 s (gunner+0xAF4 x gunners, counted down by each: 5 s real time); only
// 40/50/60% of shots go for the player (the rest at the leaders); and area
// weapons (ice, EMP, airstrike, smoke, mortar 2, tesla 2) are held while 2 of
// them are out. Each is set from the INI here.
// ---------------------------------------------------------------------------
// Start boost: at GO the game gives each car its start boost if the player hit
// the green (0x479E30): the AI cars' version of it (vtable +0x204) is empty, so
// they never had one. After it, each AI car gets the boost (0x4545B0, the
// player's) with aiStartBoost percent chance.
static DWORD g_rng = 0;
DWORD Rand32() {                                         // xorshift, seeded from the clock
    if (!g_rng) { LARGE_INTEGER q; QueryPerformanceCounter(&q); g_rng = (DWORD)q.QuadPart | 1; }
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
    return g_rng;
}
typedef void (__attribute__((thiscall)) *ObjFn_t)(BYTE*);
bool g_startRed = false;                                 // the start dial pressed on its red end (see "Start boost"): slowed at GO
static void __attribute__((thiscall)) HkIntroGo(BYTE* state) {
    reinterpret_cast<ObjFn_t>(hot::fnIntroGo)(state);
    if (g_startRed) {                                           // (the player's car: the EMP's slow - the tesla ray's - for startRedSlow s)
        g_startRed = false;
        for (int i = 0; i < 8; i++) {
            BYTE* car = RaceCarAt(i);
            if (car && *reinterpret_cast<DWORD*>(car) == 0x8A0384)
                SlowHit(car, *reinterpret_cast<const float*>(0x8AAD84), g_cfg.startRedSlow, 0xFA6, 0);   // (as HkEmpFire does)
        }
    }
    if (g_cfg.aiStartBoost <= 0.0f) return;
    int boosted = 0, ai = 0;
    for (int i = 0; i < 8; i++) {
        BYTE* racer = reinterpret_cast<BYTE* const*>(0x953C90)[i];
        BYTE* car = racer ? *reinterpret_cast<BYTE**>(racer + 0x10) : nullptr;
        if (!car) continue;
        DWORD vt = *reinterpret_cast<DWORD*>(car);
        if (vt != 0x8A0144 && vt != 0x8A3FA4) continue;         // AI cars only
        ai++;
        if (Rand32() % 10000 < (DWORD)(g_cfg.aiStartBoost * 100.0f)) {
            reinterpret_cast<ObjFn_t>(hot::fnCarStartBoost)(car);
            boosted++;
        }
    }
    Logf("AI: start boost for %d of %d AI car(s)\n", boosted, ai);
}
static float  g_aiCooldown = 5.0f;              // per AI gunner: x gunners, counted down by each = seconds
static double g_aiTarget = 40.0;
void InstallAI() {
    bool ok = true;
    if (!g_cfg.aiLeadSlowdown) { BYTE j = 0xEB; ok = ok && PatchBytes(hot::catchupAhead, &j, 1); }
    if (g_cfg.aiStartBoost > 0.0f || g_cfg.startRedSlow > 0.0f) ok = ok && PatchCall(hot::callIntroGo, reinterpret_cast<void*>(&HkIntroGo), 5);
    if (g_cfg.aiWeaponCooldown > 0.0f && g_cfg.aiWeaponCooldown != 5.0f) {
        g_aiCooldown = g_cfg.aiWeaponCooldown;
        BYTE b[6] = { 0xD9, 0x05 };                             // fld dword ptr [g_aiCooldown]
        DWORD a = reinterpret_cast<DWORD>(&g_aiCooldown); memcpy(b + 2, &a, 4);
        ok = ok && PatchBytes(hot::aiFireCooldown, b, 6);
    }
    if (g_cfg.aiTargetPlayer >= 0.0f && g_cfg.aiTargetPlayer != 40.0f) {
        g_aiTarget = g_cfg.aiTargetPlayer;
        BYTE b[6] = { 0xDC, 0x05 };                             // fadd qword ptr [g_aiTarget]
        DWORD a = reinterpret_cast<DWORD>(&g_aiTarget); memcpy(b + 2, &a, 4);
        ok = ok && PatchBytes(hot::aiTargetChance, b, 6);
    }
    if (g_cfg.aiAreaWeaponLimit != 2) {
        BYTE n = (BYTE)(g_cfg.aiAreaWeaponLimit < 1 ? 1 : g_cfg.aiAreaWeaponLimit > 127 ? 127 : g_cfg.aiAreaWeaponLimit);
        ok = ok && PatchBytes(hot::aiAreaLimit, &n, 1);
    }
    Logf("AI: %s; lead slowdown %s, a shot every %.1f s, %.0f%%+ at the player, area weapons up to %d at once, start boost %.0f%%\n",
         ok ? "on" : "patch FAILED", g_cfg.aiLeadSlowdown ? "on" : "off", g_aiCooldown, g_aiTarget, g_cfg.aiAreaWeaponLimit, g_cfg.aiStartBoost);
}
