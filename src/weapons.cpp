#include "common.h"
#include "weapons.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "framerate.h"
#include "hazards.h"
#include "items.h"

// ---------------------------------------------------------------------------
// Weapon hits. A rocket, mine, mortar shell or airstrike bomb that hits a car
// calls its take-hit (0x457C70) with a hit of kind 2 - and kind 2 starts the
// car's stunt controller (car+0x810) on stunt 1 (0x4BACF0): for 0.8 s a torque
// about the car's own length (200 x mass) and a push toward the ground (75 x
// mass) each step - a barrel roll on the spot - then 0.5 s more and until it's
// settled, controls off. weaponImpact: the roll is started "quiet" instead (its
// second argument, +0x38C: the push and torque skipped, 0x4BAF1F; the loss of
// control and the timing as before), and the car is thrown like a hazard's
// throw (the same kick at its physics sync, the same put-back if it falls off
// the track; through the track's invisible walls - weaponWallMats - for
// weaponFlyTime, not the real ones): a mine or bomb weaponPush away from the
// hit - the hit's own direction (+0: from the mine to the car) along the
// ground - and weaponLift up, tumbling over away from it and spinning round; a
// rocket or mortar shell weaponForward along (the rocket's flight, the shell:
// the car's own way) and weaponForwardLift up, flipping end over end.
// ---------------------------------------------------------------------------
static LONG g_wpLogged = 0;
static DWORD g_wpRand = 0x2545F491;
static float WpRand() { g_wpRand = g_wpRand * 1664525u + 1013904223u; return (g_wpRand >> 8) / 16777216.0f; }   // 0..1
enum { kWpUp = 0, kWpForward = 1, kWpMortar = 2 };            // a blast's kind: thrown up and away / shoved along (x weaponMortar)
static const char* const kWpKind[3] = { "up", "forward", "mortar" };
static bool WpnThrow(BYTE* car, const float* hit, float strength, int kind, bool alongHit, const char* what) {
    DWORD vt = *reinterpret_cast<DWORD*>(car);
    if (vt != 0x8A0384 && vt != 0x8A0144 && vt != 0x8A3FA4) return false;   // race cars only
    DWORD now = GetTickCount();
    HazardKick* k = nullptr;
    for (HazardKick& e : g_kicks) if (e.car == car) { k = &e; break; }
    if (k && (k->pending || (k->weapon && k->flying && now - k->flying < 400))) return true;   // (one blast, one throw: quiet anyway)
    if (!k) {
        for (HazardKick& e : g_kicks) if (!e.pending && !e.reset && (!k || now - e.ms > now - k->ms)) k = &e;
        if (!k) return false;                                   // (no room: the game's roll)
    }
    if (k->car != car) { k->safe = 0; k->flying = 0; k->watch = k->reset = false; }
    // along the ground: away from the hit, or (shoved along) the hit's way - a rocket's flight -
    // or the car's own; none (a hit from straight above): to a random side
    const float* fwd = reinterpret_cast<const float*>(car + 0x30);   // the car's row 2: its front
    const float* src = kind != kWpUp && !alongHit ? fwd : hit;
    float h[3] = { src[0], 0.0f, src[2] };
    float hl = sqrtf(Dot3(h, h));
    if (hl > 0.2f) { h[0] /= hl; h[2] /= hl; }
    else {
        const float* right = reinterpret_cast<const float*>(car + 0x10);
        h[0] = right[0]; h[2] = right[2]; hl = sqrtf(h[0] * h[0] + h[2] * h[2]);
        if (hl < 0.01f) { h[0] = 1.0f; h[2] = 0.0f; hl = 1.0f; }
        float sgn = WpRand() < 0.5f ? -1.0f : 1.0f;
        h[0] *= sgn / hl; h[2] *= sgn / hl;
    }
    k->car = car; k->ms = now;
    k->weapon = true;
    k->dir[0] = h[0]; k->dir[1] = h[2];
    float vary = (0.85f + 0.3f * WpRand()) * strength;          // (no two hits quite the same)
    bool up = kind == kWpUp;
    float scale = kind == kWpMortar ? g_cfg.weaponMortar : 1.0f;
    float push = (up ? g_cfg.weaponPush : g_cfg.weaponForward * scale) * vary;
    float tumble = (up ? g_cfg.weaponTumble : g_cfg.weaponForwardTumble) * vary;   // (shoved along: end over end)
    k->push = push; k->pushMs = up ? 250 : 0;                   // (kept up for a moment, as a hazard's throw; along: no more than the hit)
    k->dv[0] = h[0] * push;
    k->dv[1] = (up ? g_cfg.weaponLift : g_cfg.weaponForwardLift * scale) * vary;
    k->dv[2] = h[2] * push;
    float spin = g_cfg.weaponSpin * strength * (up ? 1.0f : 0.5f) * (WpRand() < 0.5f ? -1.0f : 1.0f) * (0.5f + WpRand());
    k->w[0] = h[2] * tumble;                                    // over, away from the hit (the top goes along h)
    k->w[1] = spin;                                             // and round
    k->w[2] = -h[0] * tumble;
    InterlockedIncrement(&g_kicksPending);
    InterlockedExchange(&k->pending, 1);
    if (++g_wpLogged <= 30) {
        const float* v = reinterpret_cast<const float*>(car + 0x260);
        Logf("weapons: car %p%s %s, %s (direction %.2f %.2f %.2f, x%.2f): thrown +(%.1f %.1f %.1f), spinning (%.1f %.1f %.1f); "
             "it was doing (%.1f %.1f %.1f), facing (%.2f %.2f %.2f)\n",
             car, vt == 0x8A0384 ? " (you)" : "", what, kWpKind[kind], hit[0], hit[1], hit[2], strength, k->dv[0], k->dv[1], k->dv[2],
             k->w[0], k->w[1], k->w[2], v[0], v[1], v[2], fwd[0], fwd[1], fwd[2]);
    }
    return true;
}
extern "C" bool __cdecl WpnHitNote(BYTE* car, const float* hit, DWORD from) {   // a direct hit (its roll: quiet if thrown)
    if (!g_cfg.weaponImpact || !g_hzOn || !car || !hit) return false;
    if (from == hot::hitRetRocket) return WpnThrow(car, hit, 1.0f, kWpForward, true, "hit by a rocket");
    if (from == hot::hitRetMortar) return WpnThrow(car, hit, 1.0f, kWpMortar, false, "hit by a mortar shell");
    return WpnThrow(car, hit, 1.0f, kWpUp, false, "hit");
}
// Each explosion is tagged, as it's set off, with what set it off (its object is
// reused: the tag is the latest).
struct BlastTag { const BYTE* blast; int kind; };
static BlastTag g_blastTags[32];
static int g_blastTagNext = 0;
extern "C" { int g_blastKindNext = 0; }
extern "C" void __cdecl BlastTagNote(const BYTE* blast, int kind) {
    for (BlastTag& t : g_blastTags) if (t.blast == blast) { t.kind = kind; return; }
    g_blastTags[g_blastTagNext] = { blast, kind };
    g_blastTagNext = (g_blastTagNext + 1) & 31;
}
static int BlastKind(const BYTE* blast) {
    for (const BlastTag& t : g_blastTags) if (t.blast == blast) return t.kind;
    return kWpUp;
}
// A car in an explosion's reach without a direct hit got only a nudge (kind 1:
// 0.5 x mass x a force, 0x456290). It's thrown too, by how close it was to the
// centre: 100% there, 40% at the edge. Unless the game passes it over (shielded,
// or just hit - 0x457C70's own checks: +0x12D4, the hit-again timer's flag at
// +0x1300 unless the hit overrides it, +0x21); and a car thrown a moment ago by
// the same blast's direct hit isn't thrown twice (WpnThrow).
extern "C" void __cdecl BlastHitNote(BYTE* car, const BYTE* hit, const BYTE* blast) {
    if (!g_cfg.weaponImpact || !g_hzOn || !car || !hit || !blast) return;
    if (*reinterpret_cast<const int*>(hit + 0x10) != 1) return;
    if (car[0x12D4] || (car[0x1300] && !hit[0x21])) return;
    const float* c = reinterpret_cast<const float*>(car + 0x40);
    const float* o = reinterpret_cast<const float*>(blast + 0x2C);
    float d[3] = { c[0] - o[0], c[1] - o[1], c[2] - o[2] };
    float r2 = *reinterpret_cast<const float*>(blast + 0x10);
    float f = r2 > 0.0f ? 1.0f - sqrtf(Dot3(d, d) / r2) : 0.0f;
    f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    WpnThrow(car, reinterpret_cast<const float*>(hit), 0.4f + 0.6f * f, BlastKind(blast), false, "caught in a blast");
}
extern "C" { DWORD g_stuntStart = hot::fnStuntStart; DWORD g_takeHit = hot::fnTakeHit; DWORD g_blastMake = hot::fnBlastMake; }   // (the stub jumps on through it)
extern "C" void WpnRollStub(); extern "C" void BlastHitStub(); extern "C" void BlastMakeUpStub(); extern "C" void BlastMakeFwdStub(); extern "C" void BlastMakeMortarStub();
asm(".text\n"
    ".globl _WpnRollStub\n_WpnRollStub:\n"   // replaces call 0x4BACF0 at 0x457CF6 (ecx = the stunt controller; args 1, 0)
    "    pushl %ecx\n"
    "    pushl 40(%esp)\n"                     // where the take-hit returns to: who hit (its frame: 1, 0, esi, ebp, ebx, 0xC)
    "    pushl %esi\n"                         // the hit
    "    pushl %ebx\n"                         // the car
    "    call _WpnHitNote\n"
    "    addl $12, %esp\n"
    "    popl %ecx\n"
    "    testb %al, %al\n"
    "    jz 1f\n"
    "    movl $1, 8(%esp)\n"                   // thrown: the roll quiet
    "1:  jmp *_g_stuntStart\n"
    ".globl _BlastHitStub\n_BlastHitStub:\n"  // replaces call 0x457C70 at 0x4A38F0 (ecx = esi = the car; arg: the hit; edi = the explosion)
    "    pushl %ecx\n"
    "    pushl %edi\n"                         // the explosion
    "    pushl 12(%esp)\n"                     // the hit
    "    pushl %ecx\n"                         // the car
    "    call _BlastHitNote\n"
    "    addl $12, %esp\n"
    "    popl %ecx\n"
    "    jmp *_g_takeHit\n"
    ".globl _BlastMakeUpStub\n_BlastMakeUpStub:\n"       // replaces call 0x4A3950 (ecx = the explosion): a mine's, a bomb's
    "    movl $0, _g_blastKindNext\n"
    "    jmp 2f\n"
    ".globl _BlastMakeFwdStub\n_BlastMakeFwdStub:\n"     //   a rocket's
    "    movl $1, _g_blastKindNext\n"
    "    jmp 2f\n"
    ".globl _BlastMakeMortarStub\n_BlastMakeMortarStub:\n"   //   a mortar shell's
    "    movl $2, _g_blastKindNext\n"
    "2:  pushl %ecx\n"
    "    pushl _g_blastKindNext\n"
    "    pushl %ecx\n"
    "    call _BlastTagNote\n"
    "    addl $8, %esp\n"
    "    popl %ecx\n"
    "    jmp *_g_blastMake\n");
void InstallWeaponImpact() {
    if (!g_cfg.weaponImpact) { Logf("weapons: the game's roll on a hit (weaponImpact=0)\n"); return; }
    if (!g_hzOn) { Logf("weapons: the game's roll on a hit (weaponImpact needs hazardHits=1)\n"); return; }
    g_wpRand ^= GetTickCount();
    if (!PatchCall(hot::callHitRoll, reinterpret_cast<void*>(&WpnRollStub), 5) ||
        !PatchCall(hot::callBlastHit, reinterpret_cast<void*>(&BlastHitStub), 5)) { Logf("weapons: patch FAILED\n"); return; }
    for (int i = 0; i < 6; i++) {                               // (mortar, rocket, homing rocket: shoved along; the rest: up)
        void* stub = i == 0 ? reinterpret_cast<void*>(&BlastMakeMortarStub) : i < 3 ? reinterpret_cast<void*>(&BlastMakeFwdStub)
                                                                                    : reinterpret_cast<void*>(&BlastMakeUpStub);
        if (!PatchCall(hot::callBlastMake[i], stub, 5))
            Logf("weapons: explosion tag %d FAILED (its blasts: up)\n", i);
    }
    char mats[64] = ""; int at = 0;
    for (int i = 0; i < g_cfg.weaponWallMatCount && at < 56; i++) at += snprintf(mats + at, sizeof(mats) - at, i ? ",%d" : "%d", g_cfg.weaponWallMats[i]);
    Logf("weapons: a hit or a blast throws the car - mines and bombs %.1f out, %.1f up (tumble %.1f); rockets and "
         "mortar %.1f along, %.1f up (flip %.1f; mortar x%.2f); spin %.1f; through walls of material %s for %.1f s\n",
         g_cfg.weaponPush, g_cfg.weaponLift, g_cfg.weaponTumble, g_cfg.weaponForward, g_cfg.weaponForwardLift,
         g_cfg.weaponForwardTumble, g_cfg.weaponMortar, g_cfg.weaponSpin, at ? mats : "(none)", g_cfg.weaponFlyTime);
}
