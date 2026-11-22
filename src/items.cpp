#include "common.h"
#include "items.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "framerate.h"
#include "pacing.h"
#include "icegun.h"

// ---------------------------------------------------------------------------
// Items. The race cars: 8 slots at 0x953C90, each +0x10 its car (as the
// missile's aim walks them, 0x471350). A slow-down is the parachute's and the
// tesla ray's hit: kind 4 (0x457C70: car+0x1330 its timer = +0x2C seconds, the
// controller's +0x2D8 = +0x28 - the parachute 40, the tesla ray 15), set to
// pass the just-hit check (+0x21) and leave none of its own (+0x24 = 0, so the
// next rocket still lands). A shielded car (+0x12D4) is passed over, as always.
//
// Smoke (smokeHide). A missile picks its target each step of its flight (call
// 0x473370 from 0x473630): a car ahead (+0x40, steered at when +0x1F7), or for
// a homing one (+0x1F6) one of the racers' track followers (+0x1F8: [it]+4 its
// car). A car whose smoke (GSAmmoSmoke / SmokeFlash, +0x44 the car; its update,
// vtable +0x28, runs while it pours out) poured out in the last 0.4 s is taken
// off either: the missile flies on straight.
//
// Parachute (parachuteHoming): the plain parachute is a missile with neither
// flag (the big parachute has both); it's launched with both, as the homing
// rocket is - the launch (0x4710D0) takes the homing off again for an AI's, as
// it does the homing rocket's.
//
// Shields: car+0x12B0 on (0x4A7D20, from the shield's start 0x49D5D0) takes
// (radius, time, mega, hits, 1): the radius the car's +0x1340 x 1.4 (+0x28, its
// square +0x2C), the time the ammo's +0xBC - 5 s, the mega shield's 13 - (the
// timer at +0x18), +0x30 mega, +0x38 the hits it takes (3; the mega's -1). The
// radius is scaled by shieldSize / megaShieldSize, the time set to shieldSecs
// / megaShieldSecs. (Up to 0.5.4 the radius was taken for the time: the time
// was scaled, the shield grew 1.5x / 1.75x, and it still ended at 5 / 13 s.)
// shieldZap: a
// race car within the radius + 1 (at most shieldTouch) of a shielded car - its
// centre to the shielded car's - is shocked - the tesla
// ray's sparks on it for a second (GSTeslaParams 0x4F3FA0, +0x40 the car, +0x7E
// the super ray's look; 0x42C6E0 makes it, 0x428B00 ends it) - and slowed for
// shieldSlow s (the parachute's slow; the tesla ray's sound), a mega shield's
// unable to steer for shieldLock s; again only after 3 s.
//
// EMP (empSlow): once set off (vtable +0x8, 0x49EEA0), every other race car is
// slowed (the tesla ray's slow) for empSlow s.
// ---------------------------------------------------------------------------
typedef void (__attribute__((thiscall)) *ThisFn_t)(BYTE*);
BYTE* RaceCarAt(int i) {
    BYTE* slot = reinterpret_cast<BYTE**>(0x953C90)[i];
    if (!slot) return nullptr;
    BYTE* car = *reinterpret_cast<BYTE**>(slot + 0x10);
    if (!car) return nullptr;
    DWORD vt = *reinterpret_cast<DWORD*>(car);
    if (vt != 0x8A0384 && vt != 0x8A0144 && vt != 0x8A3FA4) return nullptr;
    return car;
}
void SlowHit(BYTE* car, float factor, float secs, DWORD weapon, DWORD sound) {
    alignas(4) BYTE hit[0x40] = {};
    reinterpret_cast<ThisFn_t>(0x497190)(hit);                  // (its defaults)
    *reinterpret_cast<int*>(hit + 0x10) = 4;                    // slow down
    *reinterpret_cast<DWORD*>(hit + 0x14) = weapon;
    *reinterpret_cast<DWORD*>(hit + 0x1C) = sound;              // (0: none)
    hit[0x20] = 0;
    hit[0x21] = 1;                                              // (not passed over for a hit a moment ago)
    *reinterpret_cast<float*>(hit + 0x24) = 0.0f;               // (and no "just hit" of its own)
    *reinterpret_cast<float*>(hit + 0x28) = factor;
    *reinterpret_cast<float*>(hit + 0x2C) = secs;
    reinterpret_cast<void (__attribute__((thiscall)) *)(BYTE*, BYTE*)>(hot::fnTakeHit)(car, hit);
}
static LONG g_itemFxLogged = 0;
#define ITEMLOG(...) do { if (InterlockedIncrement(&g_itemFxLogged) <= 80) Logf(__VA_ARGS__); } while (0)
static const char* CarTag(const BYTE* car) {
    DWORD vt = *reinterpret_cast<const DWORD*>(car);
    return vt == 0x8A0384 ? " (you)" : vt == 0x8A0144 ? " (AI)" : "";
}
// smoke
struct SmokeSeen { BYTE* car; DWORD ms; };
static SmokeSeen g_smoke[8];
static bool Smoked(const BYTE* car) {
    if (!car) return false;
    DWORD now = GetTickCount();
    for (const SmokeSeen& x : g_smoke) if (x.car == car && now - x.ms < 400) return true;
    return false;
}
typedef bool (__attribute__((thiscall)) *AmmoUpd_t)(BYTE*);
static bool __attribute__((thiscall)) HkSmokeUpd(BYTE* ammo) {
    BYTE* car = *reinterpret_cast<BYTE**>(ammo + 0x44);
    if (car) {
        DWORD now = GetTickCount();
        SmokeSeen* slot = &g_smoke[0];
        for (SmokeSeen& x : g_smoke) { if (x.car == car) { slot = &x; break; } if (now - x.ms > now - slot->ms) slot = &x; }
        if (slot->car != car || now - slot->ms >= 400) ITEMLOG("smoke: car %p%s pouring out smoke - hidden from homing\n", car, CarTag(car));
        slot->car = car; slot->ms = now;
    }
    return reinterpret_cast<AmmoUpd_t>(0x49DA50)(ammo);
}
struct FollowerCar { BYTE* f; BYTE* car; };
static FollowerCar g_followers[32];
static int g_followerNext = 0;
static BYTE* FollowerCarOf(BYTE* f, bool fresh) {             // (fresh: just picked by the game, so readable now)
    if (fresh) {
        BYTE* a = *reinterpret_cast<BYTE**>(f);
        BYTE* car = a ? *reinterpret_cast<BYTE**>(a + 4) : nullptr;
        for (FollowerCar& x : g_followers) if (x.f == f) { x.car = car; return car; }
        g_followers[g_followerNext] = { f, car };
        g_followerNext = (g_followerNext + 1) & 31;
        return car;
    }
    for (const FollowerCar& x : g_followers) if (x.f == f) return x.car;
    return nullptr;
}
static void __attribute__((thiscall)) HkMslTarget(BYTE* m) {   // replaces call 0x473370 at 0x473749
    BYTE** tgt = reinterpret_cast<BYTE**>(m + 0x40);
    BYTE** fol = reinterpret_cast<BYTE**>(m + 0x1F8);
    if (Smoked(*tgt)) *tgt = nullptr;
    BYTE* before = *fol;
    static BYTE* logged = nullptr;                              // (a missile's lost target: logged once, not each step)
    reinterpret_cast<ThisFn_t>(0x473370)(m);
    if (Smoked(*tgt)) {
        if (logged != m) ITEMLOG("smoke: missile %p lost car %p%s in the smoke\n", m, *tgt, CarTag(*tgt));
        *tgt = nullptr;
        logged = m;
    }
    if (*fol) {
        BYTE* car = FollowerCarOf(*fol, *fol != before);
        if (Smoked(car)) {
            if (logged != m) ITEMLOG("smoke: homing missile %p lost car %p%s in the smoke\n", m, car, CarTag(car));
            *fol = nullptr;
            logged = m;
        }
    }
}
// parachute
typedef void (__attribute__((thiscall)) *Launch_t)(BYTE*, DWORD, DWORD);
static void __attribute__((thiscall)) HkParaLaunch(BYTE* ammo, DWORD a, DWORD b) {
    ammo[0x1F6] = 1;                                            // homing
    ammo[0x1F7] = 1;                                            // steered
    reinterpret_cast<Launch_t>(0x4710D0)(ammo, a, b);
    BYTE* by = *reinterpret_cast<BYTE**>(ammo + 0x44);
    if (by) ITEMLOG("parachute: launched by car %p%s, %s\n", by, CarTag(by), ammo[0x1F6] ? "homing" : "aimed at a car ahead (an AI's: no homing, as its homing rockets)");
}
// shields
typedef void (__attribute__((thiscall)) *ShieldOn_t)(BYTE*, float, float, int, int, int);
static void __attribute__((thiscall)) HkShieldOn(BYTE* sh, float radius, float secs, int mega, int hits, int one) {
    bool m = (mega & 0xFF) != 0;
    float t = m ? g_cfg.megaShieldSecs : g_cfg.shieldSecs;
    float k = m ? g_cfg.megaShieldSize : g_cfg.shieldSize;
    if (t <= 0.0f) t = secs;                                    // (0: the game's)
    if (k <= 0.0f) k = 1.0f;
    BYTE* car = sh - 0x12B0;
    ITEMLOG("shield: car %p%s %s shield up for %.1f s (the game's %.1f), radius %.1f (the game's %.1f)\n", car, CarTag(car),
            m ? "mega" : "plain", t, secs, radius * k, radius);
    reinterpret_cast<ShieldOn_t>(0x4A7D20)(sh, radius * k, t, mega, hits, one);
}
struct Shock { int fx; DWORD endMs; };
static Shock g_shocks[8];
static void ShockFx(BYTE* car, bool super) {
    alignas(8) BYTE prm[0x100] = {};
    reinterpret_cast<ThisFn_t>(0x4F3FA0)(prm);                  // GSTeslaParams
    *reinterpret_cast<BYTE**>(prm + 0x40) = car;
    prm[0x7E] = super ? 1 : 0;
    int fx = reinterpret_cast<int (__cdecl *)(BYTE*, int, int)>(0x42C6E0)(prm, -1, 0);
    if (fx == -1) return;
    DWORD now = GetTickCount();
    Shock* slot = &g_shocks[0];
    for (Shock& x : g_shocks) { if (!x.endMs) { slot = &x; break; } if ((LONG)(x.endMs - slot->endMs) < 0) slot = &x; }
    if (slot->endMs) reinterpret_cast<void (__cdecl *)(int, int)>(0x428B00)(slot->fx, 0);   // (out of room: the oldest ends now)
    slot->fx = fx; slot->endMs = now + 1000;
}
static void ShockEnds(DWORD now) {
    for (Shock& x : g_shocks) {
        if (!x.endMs || (LONG)(now - x.endMs) < 0) continue;
        if (now - x.endMs < 2000) reinterpret_cast<void (__cdecl *)(int, int)>(0x428B00)(x.fx, 0);   // (long overdue: a race since -
        x.endMs = 0;                                                                              //   its handle may be another's now)
    }
}
void ItemFxStep(BYTE* car, IceSeen*, DWORD now) {        // from IceTrack, once per car per step
    ShockEnds(now);
    if (!g_cfg.shieldZap || !car[0x12D4]) return;               // (no shield up)
    float radius = *reinterpret_cast<const float*>(car + 0x12D8) + 1.0f;   // (the shield's own reach, 0x4A7D20's +0x28)
    if (radius > g_cfg.shieldTouch) radius = g_cfg.shieldTouch;
    bool mega = car[0x12E0] != 0;
    const float* p = reinterpret_cast<const float*>(car + 0x40);
    for (int i = 0; i < 8; i++) {
        BYTE* o = RaceCarAt(i);
        if (!o || o == car || o[0x12D4]) continue;
        const float* q = reinterpret_cast<const float*>(o + 0x40);
        float d[3] = { q[0] - p[0], q[1] - p[1], q[2] - p[2] };
        float dist = sqrtf(Dot3(d, d));
        if (dist >= radius) continue;
        IceSeen* v = IceFind(o, true);
        if (v->zapMs && now - v->zapMs < 3000) continue;
        v->zapMs = now;
        v->seenMs = now;
        SlowHit(o, *reinterpret_cast<const float*>(0x8AAE6C), g_cfg.shieldSlow, mega ? 0xFB7 : 0xFAB, 5);   // (the parachute's slow; the tesla's sound)
        if (mega && g_cfg.shieldLock > 0.0f) v->lockUntil = now + (DWORD)(g_cfg.shieldLock * 1000.0f);
        ShockFx(o, mega);
        ITEMLOG("shield: car %p%s touched car %p%s's %s shield (%.1f apart): shocked, slowed for %.1f s%s\n", o, CarTag(o), car, CarTag(car),
                mega ? "mega" : "plain", dist, g_cfg.shieldSlow, mega ? ", no steering" : "");
    }
}
// EMP
static void __attribute__((thiscall)) HkEmpFire(BYTE* ammo) {
    reinterpret_cast<ThisFn_t>(0x49EEA0)(ammo);
    BYTE* by = *reinterpret_cast<BYTE**>(ammo + 0x44);
    if (!by || g_cfg.empSlow <= 0.0f) return;
    int n = 0;
    for (int i = 0; i < 8; i++) {
        BYTE* o = RaceCarAt(i);
        if (!o || o == by || o[0x12D4]) continue;
        SlowHit(o, *reinterpret_cast<const float*>(0x8AAD84), g_cfg.empSlow, *reinterpret_cast<DWORD*>(ammo + 0x4C), 0);   // (the tesla ray's slow)
        n++;
    }
    ITEMLOG("emp: set off by car %p%s: %d other car(s) slowed for %.1f s\n", by, CarTag(by), n, g_cfg.empSlow);
}
void InstallItemFx() {
    char on[160] = "";
    auto add = [&on](const char* w) { lstrcatA(on, on[0] ? ", " : " "); lstrcatA(on, w); };
    if (g_cfg.smokeHide) {
        if (PatchCall(hot::callMslTarget, reinterpret_cast<void*>(&HkMslTarget), 5) &&
            PatchPointer(reinterpret_cast<void**>(hot::vtSmokeUpd), reinterpret_cast<void*>(&HkSmokeUpd)) &&
            PatchPointer(reinterpret_cast<void**>(hot::vtSmokeFlUpd), reinterpret_cast<void*>(&HkSmokeUpd))) add("smoke hides");
        else Logf("items: smoke patch FAILED\n");
    }
    if (g_cfg.parachuteHoming) {
        if (PatchPointer(reinterpret_cast<void**>(hot::vtParaLaunch), reinterpret_cast<void*>(&HkParaLaunch))) add("homing parachute");
        else Logf("items: parachute patch FAILED\n");
    }
    if (g_cfg.shieldSecs > 0.0f || g_cfg.megaShieldSecs > 0.0f || g_cfg.shieldSize != 1.0f || g_cfg.megaShieldSize != 1.0f) {
        if (PatchCall(hot::callShieldOn, reinterpret_cast<void*>(&HkShieldOn), 5)) add("shield time and size");
        else Logf("items: shield time and size patch FAILED\n");
    }
    if (g_cfg.shieldZap) {
        if (InstallCarSync()) { g_carFxOn = true; add("shield shocks"); }
        else Logf("items: shield shock patch FAILED\n");
    }
    if (g_cfg.empSlow > 0.0f) {
        if (PatchPointer(reinterpret_cast<void**>(hot::vtEmpFire), reinterpret_cast<void*>(&HkEmpFire)) &&
            PatchPointer(reinterpret_cast<void**>(hot::vtEmpUpFire), reinterpret_cast<void*>(&HkEmpFire))) add("EMP slows");
        else Logf("items: EMP patch FAILED\n");
    }
    Logf("items:%s\n", on[0] ? on : " as the game");
}

// ---------------------------------------------------------------------------
// Firing on Spooky Sprint (shieldPowerFire). The fire button's checks (0x41E3C0)
// ask the car's toon state (car+0xF44: [+0] its state; the state table 0x954730,
// 0x30 each, +4 may fire, +5 may hold an item out) whether it may fire. State 4
// is a track's shield power-up's (GSShieldToonPower_c, 0x4DB110) - and a stretch
// of Spooky Sprint's road before the ghoul's ramp carries it as a surface
// property (0x443180 sets the state from what the car touches), so nothing could
// be fired there. State 4 now may fire and hold its item out, at the fire button,
// the other way items are set off (0x46D210) and the gunner's update (0x42F070).
// ---------------------------------------------------------------------------
static bool __attribute__((thiscall)) HkToonCanFire(BYTE* toon) {   // replaces call 0x451370 at 0x41E593
    if (*reinterpret_cast<int*>(toon) == 4) return true;       // (a shield power-up: fire anyway)
    return reinterpret_cast<bool (__attribute__((thiscall)) *)(BYTE*)>(0x451370)(toon);
}
static bool __attribute__((thiscall)) HkToonShowWpn(BYTE* toon) {   // replaces the calls of 0x451380 in 0x42F070
    if (*reinterpret_cast<int*>(toon) == 4) return true;
    return reinterpret_cast<bool (__attribute__((thiscall)) *)(BYTE*)>(0x451380)(toon);
}
void InstallToonFire() {
    if (g_cfg.shieldPowerFire) {
        if (PatchCall(hot::callToonCanFire, reinterpret_cast<void*>(&HkToonCanFire), 5) &&
            PatchCall(hot::callToonCanFire2, reinterpret_cast<void*>(&HkToonCanFire), 5) &&
            PatchCall(hot::callToonShowWpn[0], reinterpret_cast<void*>(&HkToonShowWpn), 5) &&
            PatchCall(hot::callToonShowWpn[1], reinterpret_cast<void*>(&HkToonShowWpn), 5)) Logf("items: can be fired in toon state 4 (a shield power-up, or road marked with it - Spooky Sprint)\n");
        else Logf("items: shield power-up patch FAILED\n");
    }
}
// Items by position (itemOdds). A crate's item is rolled (0x4C3360) from the
// weapon data's table: 22 rows (item id +0x24, 10 chance columns from +0x28),
// a column picked by the car count and the car's position (0x95A2F4; with 8
// cars only 1st, 2nd-7th and last differ), blue crates columns 8-9. For a
// normal crate the roll is made here instead, from four lines - 1st, the front
// third, the middle, the last two - of item:weight pairs (the ini). Blue
// crates, and a line left empty, keep the game's roll. No crate gives the EMP
// (0xFA6; a blue crate's super EMP 0xFB2) in the race's first empAfter s (the
// race clock): it's left off the line, a blue crate's is rolled again.
struct ItemW { int id; float w; };
static ItemW g_items[4][16];
static int g_itemsN[4] = { 0, 0, 0, 0 };
static bool g_itemsSet[4] = { false, false, false, false };
static const char* ItemName(int id) {
    for (const auto& n : kItemNames) if (n.id == id) return n.name;
    return "?";
}
bool ItemTableParse(const char* group, const char* v) {
    static const char* const groups[4] = { "First", "Front", "Mid", "Back" };
    int g = -1;
    for (int i = 0; i < 4; i++) if (!_stricmp(group, groups[i])) g = i;
    if (g < 0) return false;
    g_itemsN[g] = 0; g_itemsSet[g] = true;
    char buf[320]; lstrcpynA(buf, v, sizeof(buf));
    for (char* tok = strtok(buf, ","); tok && g_itemsN[g] < 16; tok = strtok(nullptr, ",")) {
        while (*tok == ' ' || *tok == '\t') tok++;
        char* colon = strchr(tok, ':');
        if (!colon) continue;
        *colon = 0;
        char* ne = colon; while (ne > tok && (ne[-1] == ' ' || ne[-1] == '\t')) *--ne = 0;
        float w = (float)atof(colon + 1);
        if (w <= 0.0f) continue;
        for (const auto& n : kItemNames)
            if (!_stricmp(tok, n.name)) { g_items[g][g_itemsN[g]++] = { n.id, w }; break; }
    }
    return true;
}
static void ItemDefaults() {                                    // (the ini's defaults, for an ini without the lines)
    static const char* const def[4] = {
        "smoke:30, shield:30, mine:25, rocket:15",
        "rocket:25, mine:15, shield:15, mortar:12, parachute:10, icegun:10, boost:8, tesla:5",
        "rocket:18, mortar:15, parachute:15, icegun:12, boost:12, tesla:10, airstrike:8, mine:5, smoke:5",
        "tesla:22, airstrike:20, boost:18, parachute:12, mortar:10, icegun:10.5, emp:7.5" };
    static const char* const names[4] = { "First", "Front", "Mid", "Back" };
    for (int g = 0; g < 4; g++) if (!g_itemsSet[g]) ItemTableParse(names[g], def[g]);
}
static DWORD g_itemRand = 0x6A09E667;
static LONG g_itemLogged = 0;
typedef int (__attribute__((thiscall)) *WeaponRoll_t)(BYTE*, int, int, float, int);
static int __attribute__((thiscall)) HkWeaponRoll(BYTE* table, int pos, int cars, float roll, int blue) {
    WeaponRoll_t orig = reinterpret_cast<WeaponRoll_t>(0x4C3360);
    bool empOk = g_cfg.empAfter <= 0.0f || (g_raceCar && g_raceSteps >= (LONG)(g_cfg.empAfter * 60.0f));   // (60 steps a second)
    if (blue == 1 && cars >= 1 && pos >= 1) {                  // a blue crate: the game's roll, the EMP held back
        int id = orig(table, pos, cars, roll, blue);
        if (id != 0xFB2 && id != 0xFA6) return id;
        bool back = cars >= 4 ? pos >= cars - 1 : pos == cars;
        g_itemRand = g_itemRand * 1664525u + 1013904223u;
        if (back && empOk && (g_itemRand >> 8) / 16777216.0f < g_cfg.blueEmp) {
            if (InterlockedIncrement(&g_itemLogged) <= 40) Logf("items: position %d of %d got a blue crate's EMP\n", pos, cars);
            return id;
        }
        int was = id;
        for (int i = 0; i < 12 && (id == 0xFB2 || id == 0xFA6); i++) {
            g_itemRand = g_itemRand * 1664525u + 1013904223u;
            id = orig(table, pos, cars, (g_itemRand >> 8) / 16777216.0f * 100.0f, blue);
        }
        if (id == 0xFB2 || id == 0xFA6) id = was == 0xFB2 ? 0xFAD : 0xFA1;   // (still: a homing rocket)
        if (InterlockedIncrement(&g_itemLogged) <= 40)
            Logf("items: position %d of %d: a blue crate's EMP held back%s, item %X instead\n", pos, cars, empOk ? "" : " (too early in the race)", id);
        return id;
    }
    if (blue == 1 || cars < 1 || pos < 1) return orig(table, pos, cars, roll, blue);
    int g;
    if (pos == 1) g = 0;
    else if (cars >= 4 && pos >= cars - 1) g = 3;               // (the last two)
    else if ((pos - 1) * 3 < cars) g = 1;                       // (the front third)
    else g = 2;
    if (g == 3 && cars < 4) g = 2;
    float sum = 0.0f;
    int last = -1;
    for (int i = 0; i < g_itemsN[g]; i++) {
        if (!empOk && g_items[g][i].id == 0xFA6) continue;      // (no EMP yet: as if it weren't on the line)
        sum += g_items[g][i].w; last = i;
    }
    if (sum <= 0.0f) return orig(table, pos, cars, roll, blue);
    g_itemRand = g_itemRand * 1664525u + 1013904223u;
    float r = (g_itemRand >> 8) / 16777216.0f * sum;
    int id = g_items[g][last].id;
    for (int i = 0; i < g_itemsN[g]; i++) {
        if (!empOk && g_items[g][i].id == 0xFA6) continue;
        if (r < g_items[g][i].w) { id = g_items[g][i].id; break; }
        r -= g_items[g][i].w;
    }
    if (InterlockedIncrement(&g_itemLogged) <= 40)
        Logf("items: position %d of %d (%s) got %s\n", pos, cars, g == 0 ? "1st" : g == 1 ? "front" : g == 2 ? "middle" : "back", ItemName(id));
    return id;
}
void InstallItemOdds() {
    if (!g_cfg.itemOdds) { Logf("items: the game's odds\n"); return; }
    ItemDefaults();
    g_itemRand ^= GetTickCount();
    if (!PatchCall(hot::callWeaponRoll, reinterpret_cast<void*>(&HkWeaponRoll), 5)) { Logf("items: patch FAILED\n"); return; }
    if (g_cfg.empAfter > 0.0f) {
        if (InstallCarSync()) g_raceClockOn = true;
        else { g_cfg.empAfter = 0.0f; Logf("items: race clock patch FAILED - the EMP from the start\n"); }
    }
    Logf("items: by position - 1st %d items, front %d, middle %d, back %d (blue crates: the game's, EMP only for the last two); EMP after %.0f s\n",
         g_itemsN[0], g_itemsN[1], g_itemsN[2], g_itemsN[3], g_cfg.empAfter);
}
