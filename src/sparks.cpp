#include "common.h"
#include "sparks.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "framerate.h"
#include "ai.h"
#include "start.h"

// ---------------------------------------------------------------------------
// Sparks. The game's spark particles (0x563xxx-0x566xxx: 50 types, 650
// particles) are drawn by type flags (0x5668F9): flag 4 draws each spark as one
// quad stretched along its velocity (0x5654D0, length P+0x48 = 0.25 s of
// travel); flag 1 as a chain of up to 8 little squares over just 0.04 s of
// travel (0x564BE0) - with a width of 0.035, the orange "line of dots" of a
// car-on-car hit (type 7, flags 1|2|8). Every type with flag 1 is switched to
// flag 4 here (flag 8, whose history the PC version never sets up, cleared
// too), made sparkWidth x as wide, and every streak is sparkLength seconds of
// travel long instead of 0.25 (0x563CB7 reads our value). Done each time a
// level places its cars, after it has loaded its spark types; a type already
// switched is left alone.
// Variety: as each streak spark (a flag-4 type) is born (0x563D23, the end of
// the spawn 0x563AF0, every field set; ecx = the particle, ebp = its type) its
// length and width are scaled by a random amount in sparkVary, and a share of
// them (sparkHot) burn hotter: yellow to near white, full alpha. Colours are 7
// bits a channel there (P+0x28: A R G B, 0-0x7F).
// More of them: the game throws only 1-2 sparks a hit. Each car-hit spark (a
// type switched above; not the wall-scrape sparks, which come every step while
// scraping) brings sparkCount more of its type with it, sparkRate a second at
// most, through the game's own spawn (0x566A40: type, position, velocity,
// gravity), from the same spot, each sent off in its own direction: the first one's velocity, scattered by sparkSpread
// and thrown upward by sparkUp - the fountain of the intro.
// ---------------------------------------------------------------------------
static float g_sparkLength = 0.25f;
static float Rand01() { return (Rand32() & 0xFFFFFF) / 16777216.0f; }
typedef void (__cdecl *SparkSpawn_t)(int, const float*, const float*, const float*, float);
static int g_sparkDepth = 0;
static DWORD g_sparkSwitched[50];                                // types switched from dots to streaks (car hits): their texture then
                                                                 // (a slot a later level fills with another type no longer matches)
static DWORD g_sparkBudgetMs = 0; static float g_sparkBudget = 0.0f;
// The hit's strength: set while the game throws a collision's sparks (HkCollSparks), 0 otherwise.
static float g_sparkImpact = 0.0f;
// ... and the car (0: no collision) and the contact's vector for it (unit, or 0).
static BYTE* g_sparkMover = nullptr;
static float g_sparkN[3] = { 0, 0, 0 };
// How big this hit's sparks are: impact / sparkImpactRef, 0.5 to sparkImpactMax (1 outside a collision).
// (Hits measured in a race ran about 3300 to 5300, averaging 3800.)
static float SparkScale() {
    if (g_sparkImpact <= 0.0f || g_cfg.sparkImpactRef <= 0.0f) return 1.0f;
    float k = g_sparkImpact / g_cfg.sparkImpactRef;
    float mx = g_cfg.sparkImpactMax > 1.0f ? g_cfg.sparkImpactMax : 1.0f;
    return k < 0.5f ? 0.5f : (k > mx ? mx : k);
}
extern "C" void __cdecl SparkSpawned(BYTE* P, int type, BYTE* sys) {
    if (!P || !sys || type < 0 || type >= 50) return;
    const BYTE* E = sys + 0x1CC3C28 + type * 0x3C;
    if (!(*reinterpret_cast<const DWORD*>(E + 0x28) & 4)) return;   // streak sparks only (not smoke)
    bool carHit = g_sparkSwitched[type] && g_sparkSwitched[type] == *reinterpret_cast<const DWORD*>(E);
    float hit = SparkScale();
    if (g_sparkDepth == 0 && g_cfg.sparkCount > 0 && carHit) {  // (the extras' own births don't bring more)
        DWORD now = GetTickCount();                             // budget: sparkRate extras a second, a quarter second's worth banked
        g_sparkBudget += (now - g_sparkBudgetMs) * 0.001f * g_cfg.sparkRate;
        g_sparkBudgetMs = now;
        if (g_sparkBudget > g_cfg.sparkRate * 0.25f) g_sparkBudget = g_cfg.sparkRate * 0.25f;
    }
    if (g_sparkDepth == 0 && g_cfg.sparkCount > 0 && carHit && g_sparkBudget >= 1.0f) {
        g_sparkDepth = 1;
        float pos[3], v0[3], acc[3];
        memcpy(pos, P + 4, 12); memcpy(v0, P + 0x10, 12); memcpy(acc, P + 0x1C, 12);
        float sp = sqrtf(Dot3(v0, v0));
        if (sp < 8.0f) sp = 8.0f;                               // (a light scrape still throws them)
        int extra = (int)(g_cfg.sparkCount * hit + 0.5f);          // harder hits throw more
        for (int i = 0; i < extra && i < 32 && g_sparkBudget >= 1.0f; i++, g_sparkBudget -= 1.0f) {
            float v[3];
            float k = 0.5f + Rand01();                          // 0.5 - 1.5 x as fast
            for (int a = 0; a < 3; a++) v[a] = v0[a] * k + sp * g_cfg.sparkSpread * (2.0f * Rand01() - 1.0f);
            v[1] += sp * g_cfg.sparkUp * (0.3f + Rand01());
            reinterpret_cast<SparkSpawn_t>(hot::fnSparkSpawn)(type, pos, v, acc, 0.0f);
        }
        g_sparkDepth = 0;
    }
    // Its own shape: a size, and somewhere between a long thin needle and a short
    // fat flake; a stronger hit's sparks are longer, wider and faster.
    float v = g_cfg.sparkVary, len = hit, wid = sqrtf(hit);
    if (v > 0.0f) {
        float size = 1.0f + v * (2.0f * Rand01() - 1.0f);       // e.g. 0.4 - 1.6 for 0.6
        float u = 2.0f * Rand01() - 1.0f;                       // -1 flake ... +1 needle
        len *= size * (1.0f + 0.5f * u);
        wid *= size * (1.0f - 0.4f * u);
    }
    *reinterpret_cast<float*>(P + 0x48) *= len;
    *reinterpret_cast<float*>(P + 0x30) *= wid;
    *reinterpret_cast<float*>(P + 0x34) *= wid;
    // Its own flight: the streak lies along its velocity, so turning that turns
    // the spark - up to sparkTwist degrees off its course - and a heavier or
    // lighter fall (gravity x 0.5-1.5) bends some into arcs; it lives 0.7-1.3 x
    // as long.
    float* vel = reinterpret_cast<float*>(P + 0x10);
    float sp = sqrtf(Dot3(vel, vel));
    if (sp > 1e-3f) {
        float sk = sqrtf(hit);
        if (g_cfg.sparkTwist > 0.0f) {
            float r[3] = { 2.0f * Rand01() - 1.0f, 2.0f * Rand01() - 1.0f, 2.0f * Rand01() - 1.0f };
            float tw = g_cfg.sparkTwist < 80.0f ? g_cfg.sparkTwist : 80.0f;   // (tan: keep well short of 90)
            float t = tanf(Rand01() * tw * (3.14159265f / 180.0f));
            float rd = Dot3(r, vel) / sp;                               // r made square to the course: turns it, never
            for (int a = 0; a < 3; a++) r[a] -= vel[a] / sp * rd;       //   back on itself
            float rl = sqrtf(Dot3(r, r));
            if (rl > 1e-3f) {
                for (int a = 0; a < 3; a++) vel[a] = vel[a] / sp + r[a] / rl * t;
                float nl = sqrtf(Dot3(vel, vel));
                for (int a = 0; a < 3; a++) vel[a] *= sp * sk / nl;
            }
        } else for (int a = 0; a < 3; a++) vel[a] *= sk;
        float* acc = reinterpret_cast<float*>(P + 0x1C);
        float gk = 0.5f + Rand01();
        for (int a = 0; a < 3; a++) acc[a] *= gk;
        *reinterpret_cast<float*>(P) *= 0.7f + 0.6f * Rand01();
    }
    if (g_cfg.sparkHot > 0.0f) {
        float h = Rand01(); h = h * h * g_cfg.sparkHot;         // most a little, a few a lot
        if (h > 1.0f) h = 1.0f;
        DWORD c = *reinterpret_cast<DWORD*>(P + 0x28);
        int r = (c >> 16) & 0x7F, g = (c >> 8) & 0x7F, b = c & 0x7F;
        r += (int)((0x7F - r) * h); g += (int)((0x7F - g) * h); b += (int)((0x70 - b) * h * 0.8f);
        if (b < 0) b = 0;
        *reinterpret_cast<DWORD*>(P + 0x28) = 0x7Fu << 24 | (DWORD)r << 16 | (DWORD)g << 8 | (DWORD)b;
    }
    // Moving with the car (sparkInherit). The game sends a collision's sparks off
    // at -0.2 x the car's velocity (0x441BF0): backward in the world, so with the
    // camera following the car they all streamed straight back. Instead, inside a
    // collision, each one keeps the car's velocity and bursts off the contact:
    // back along the surface (against the car's sliding along it), out from the
    // car and up, scattered; as fast as sparkBurst x that sliding speed (a hard hit
    // a bit faster). The streak - drawn along the spark's velocity, P+0x48 seconds
    // of it - is cut to show the burst only, what the camera sees.
    if (g_sparkMover && g_cfg.sparkInherit > 0.0f) {
        BYTE* car = g_sparkMover;
        const float* cv = reinterpret_cast<const float*>(car + 0x260);
        const float* cp = reinterpret_cast<const float*>(car + 0x40);
        float* pos = reinterpret_cast<float*>(P + 4);
        float t[3] = { cv[0], cv[1], cv[2] };                   // the sliding along the surface: the car's velocity, its
        float vn = Dot3(t, g_sparkN);                           //   part into the contact taken out
        for (int a = 0; a < 3; a++) t[a] -= g_sparkN[a] * vn;
        float tl = sqrtf(Dot3(t, t));
        float o[3] = { pos[0] - cp[0], 0.0f, pos[2] - cp[2] };  // out from the car, level
        float ol = sqrtf(Dot3(o, o));
        if (ol > 1e-3f) { o[0] /= ol; o[2] /= ol; }
        float dir[3];
        for (int a = 0; a < 3; a++)
            dir[a] = (tl > 1.0f ? -t[a] / tl : 0.0f) + o[a] * g_cfg.sparkOut * (0.3f + 0.7f * Rand01())
                   + g_cfg.sparkSpread * (2.0f * Rand01() - 1.0f);
        dir[1] += g_cfg.sparkLift * Rand01();
        float dl = sqrtf(Dot3(dir, dir));
        if (dl > 1e-3f) {
            float bs = g_cfg.sparkBurst * tl;
            if (bs < g_cfg.sparkBurstMin) bs = g_cfg.sparkBurstMin;
            bs *= (0.5f + Rand01()) * sqrtf(hit);
            float* vel = reinterpret_cast<float*>(P + 0x10);
            for (int a = 0; a < 3; a++) vel[a] = cv[a] * g_cfg.sparkInherit + dir[a] / dl * bs;
            float vl = sqrtf(Dot3(vel, vel));
            float k = vl > 1e-3f ? bs / vl : 1.0f;              // (the tail: the burst's share of it)
            *reinterpret_cast<float*>(P + 0x48) *= k < 0.05f ? 0.05f : k > 1.0f ? 1.0f : k;
        }
        *reinterpret_cast<float*>(P) *= g_cfg.sparkLife;
    }
}
extern "C" void SparkSpawnStub();
asm(".text\n"
    ".globl _SparkSpawnStub\n_SparkSpawnStub:\n"   // call from 0x563D23 (replaces fld dword ptr [ebx+0x1CB66D8]); ecx = particle, ebp = type, ebx = system
    "    pushal\n"
    "    pushl %ebx\n"
    "    pushl %ebp\n"
    "    pushl %ecx\n"
    "    call _SparkSpawned\n"
    "    addl $12, %esp\n"
    "    popal\n"
    "    flds 0x1CB66D8(%ebx)\n"
    "    ret\n");
void SparkSetup() {
    if (!g_cfg.sparkStreaks) return;
    BYTE* sys = reinterpret_cast<BYTE* (__cdecl *)()>(hot::fnSparkSys)();
    if (!sys) return;
    for (int i = 0; i < 50; i++) {
        BYTE* E = sys + 0x1CC3C28 + i * 0x3C;
        if (!*reinterpret_cast<DWORD*>(E)) continue;            // no texture: unused
        DWORD& flags = *reinterpret_cast<DWORD*>(E + 0x28);
        if (!(flags & 1) || (flags & 4)) continue;              // (flag 4 wins the dispatch anyway)
        flags = (flags & ~0x9u) | 4u;
        g_sparkSwitched[i] = *reinterpret_cast<DWORD*>(E);
        *reinterpret_cast<float*>(E + 0x2C) *= g_cfg.sparkWidth;
        *reinterpret_cast<float*>(E + 0x18) *= g_cfg.sparkWidth;
        Logf("sparks: type %d drawn as streaks (flags now %X, width %.3f, colour %08X)\n", i, flags,
             *reinterpret_cast<float*>(E + 0x2C), *reinterpret_cast<DWORD*>(E + 0x34));
    }
}
// Only walls and cars: the game throws collision sparks (0x441BF0, thiscall:
// contact, mover, impact; called from 0x442714 and 0x442CC2) for any hard
// contact - landing a jump or bottoming out on a ramp too. The contact's vector
// for the car (0x620B00: the same one the sparks fly along) is compared with
// the car's up: mostly along it (more than sparkFloor of it) is the road under
// the car, and no sparks are thrown; mostly across it is a wall or a car.
typedef void (__attribute__((thiscall)) *CollSparks_t)(BYTE*, BYTE*, BYTE*, float);
typedef void (__attribute__((thiscall)) *ContactVec_t)(BYTE*, float*, BYTE*, int);
static LONG g_sparkFloorSkipped = 0, g_sparkSideKept = 0, g_sparkImpactN = 0;
static float g_sparkImpactMax = 0.0f, g_sparkImpactMin = 0.0f, g_sparkImpactSum = 0.0f;
static void __attribute__((thiscall)) HkCollSparks(BYTE* self, BYTE* contact, BYTE* mover, float impact) {
    bool skip = false;
    float n[3] = { 0, 0, 0 }, nl = 0.0f;
    if (contact && mover) {
        reinterpret_cast<ContactVec_t>(hot::fnContactVec)(contact, n, mover, 1);
        nl = sqrtf(Dot3(n, n));
    }
    if (contact && mover && g_cfg.sparkFloor < 1.0f) {   // (the road filter)
        const float* up = reinterpret_cast<const float*>(mover + 0x10) + 4;   // the car's matrix, row 1
        if (nl > 1e-4f) {
            skip = fabsf(Dot3(n, up)) / nl > g_cfg.sparkFloor;
            (skip ? g_sparkFloorSkipped : g_sparkSideKept)++;
            LONG all = g_sparkFloorSkipped + g_sparkSideKept;
            if (all == 50 || all == 500)                        // (for the log)
                Logf("sparks: collision sparks: %ld from walls/cars, %ld from the road skipped\n", g_sparkSideKept, g_sparkFloorSkipped);
        }
    }
    if (skip) return;
    if (impact > 0.0f) {                                        // (for the log: the hits' range, to set sparkImpactRef by)
        if (impact > g_sparkImpactMax) g_sparkImpactMax = impact;
        if (g_sparkImpactMin <= 0.0f || impact < g_sparkImpactMin) g_sparkImpactMin = impact;
        g_sparkImpactSum += impact; g_sparkImpactN++;
        if (g_sparkImpactN == 20 || g_sparkImpactN == 200)
            Logf("sparks: hit strength over %ld hits: weakest %.0f, average %.0f, strongest %.0f (sparkImpactRef %.0f)\n",
                 g_sparkImpactN, g_sparkImpactMin, g_sparkImpactSum / g_sparkImpactN, g_sparkImpactMax, g_cfg.sparkImpactRef);
    }
    g_sparkImpact = impact;
    g_sparkMover = mover;
    for (int a = 0; a < 3; a++) g_sparkN[a] = nl > 1e-4f ? n[a] / nl : 0.0f;
    reinterpret_cast<CollSparks_t>(hot::fnCollSparks)(self, contact, mover, impact);
    g_sparkImpact = 0.0f;
    g_sparkMover = nullptr;
}
void InstallSparks() {
    if (g_cfg.sparkFloor < 1.0f || (g_cfg.sparkStreaks && (g_cfg.sparkImpactRef > 0.0f || g_cfg.sparkInherit > 0.0f))) {
        if (!PatchCall(hot::callCollSparks1, reinterpret_cast<void*>(&HkCollSparks), 5) ||
            !PatchCall(hot::callCollSparks2, reinterpret_cast<void*>(&HkCollSparks), 5)) Logf("sparks: road filter patch FAILED\n");
        else Logf("sparks: collision sparks hooked (road filter %.2f, size by hit strength / %.0f)\n", g_cfg.sparkFloor, g_cfg.sparkImpactRef);
    }
    if (!g_cfg.sparkStreaks) return;
    g_sparkLength = g_cfg.sparkLength > 0.0f ? g_cfg.sparkLength : 0.25f;
    BYTE fld[6] = { 0xD9, 0x05 };                               // fld dword ptr [&g_sparkLength]
    DWORD a = reinterpret_cast<DWORD>(&g_sparkLength); memcpy(fld + 2, &a, 4);
    if (!PatchBytes(hot::sparkStreakLen, fld, 6) ||
         ((g_cfg.sparkVary > 0.0f || g_cfg.sparkHot > 0.0f || g_cfg.sparkCount > 0 ||
           g_cfg.sparkImpactRef > 0.0f || g_cfg.sparkTwist > 0.0f || g_cfg.sparkInherit > 0.0f) &&
         !PatchCall(hot::sparkSpawnEnd, reinterpret_cast<void*>(&SparkSpawnStub), 6))) { Logf("sparks: patch FAILED\n"); return; }
    Logf("sparks: dotted sparks drawn as streaks, %.2f s of travel long, x%.1f wide; size +-%.0f%%, hot %.2f; "
         "%d more a car-hit spark, %.0f/s at most (spread %.2f, up %.2f)\n", g_sparkLength, g_cfg.sparkWidth, g_cfg.sparkVary * 100.0f,
         g_cfg.sparkHot, g_cfg.sparkCount, g_cfg.sparkRate, g_cfg.sparkSpread, g_cfg.sparkUp);
    if (g_cfg.sparkInherit > 0.0f)
        Logf("sparks: collision sparks move with the car (x%.2f) and burst off the contact (x%.2f of the sliding, %.1f at least; "
             "out %.2f, up %.2f), life x%.2f\n", g_cfg.sparkInherit, g_cfg.sparkBurst, g_cfg.sparkBurstMin, g_cfg.sparkOut,
             g_cfg.sparkLift, g_cfg.sparkLife);
}
