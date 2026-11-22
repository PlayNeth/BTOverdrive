#include "common.h"
#include "rampage.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "framerate.h"
#include "items.h"
#include "start.h"

// ---------------------------------------------------------------------------
// Rampage (game mode 7: hunt down the opponents before the time runs out). The
// game starts it on the race grid and its AI simply races the track, so it was
// a walkover. The mod spreads the AI cars over the whole track at the start and
// makes them run from the player.
// Spawn points: the track's reset points (action points of type 0xB4, placed on
// the road and facing along it) and points every 30 units along the track's
// lines (the ones the AI drives, 0x9680D0). From those, each AI car gets the one
// farthest from the player and from the cars already placed, with spots behind
// the player counted as nearer (rampageBehind) and spots within rampageMinDist
// of them left till last. The player keeps the start.
// Fleeing: the AI brain refills each car's target points (0x648B50; one brain a
// frame, in turn). After it, an AI car with the player within rampageFleeRange
// gets its target speeds raised by rampageFleeSpeed, and its target points moved
// across the road (up to rampageDodge of the road's width) away from the player.
// Turning round isn't possible: the AI's routes only go forward.
// ---------------------------------------------------------------------------
struct SpawnPt { float m[16]; };
static SpawnPt g_spawns[1024];
static int g_spawnN = 0, g_spawnUsed = 0;
static DWORD g_spawnMs = 0;
static void AddSpawn(const float* pos, const float* fwd) {
    if (g_spawnN >= 1024) return;
    float f[3] = { fwd[0], 0.0f, fwd[2] };
    float l = sqrtf(Dot3(f, f));
    if (l < 0.01f) return;
    f[0] /= l; f[2] /= l;
    float* m = g_spawns[g_spawnN++].m;
    memset(m, 0, 64);
    m[0] = f[2]; m[2] = -f[0];                                  // right
    m[5] = 1.0f;                                                // up
    m[8] = f[0]; m[10] = f[2];                                  // forward (row 2: the heading 0x63E750 uses)
    m[12] = pos[0]; m[13] = pos[1] + 0.5f; m[14] = pos[2]; m[15] = 1.0f;
}
static void CollectSpawns() {
    g_spawnN = 0;
    const DWORD lists[2] = { hot::apUserList, hot::apList };
    int nAP = 0, types[8][2] = {}, nt = 0;                      // (type histogram, for the log)
    for (DWORD list : lists) {
        BYTE* const* aps = *reinterpret_cast<BYTE* const* const*>(list);
        int na = *reinterpret_cast<const int*>(list + 4);
        for (int i = 0; aps && i < na && i < 4096; i++) {
            const BYTE* ap = aps[i];
            if (!ap) continue;
            int t = *reinterpret_cast<const short*>(ap + 0x50);
            int k = 0; while (k < nt && types[k][0] != t) k++;
            if (k < nt) types[k][1]++; else if (nt < 8) { types[nt][0] = t; types[nt][1] = 1; nt++; }
            if (t != 0xB4) continue;
            const float* q = reinterpret_cast<const float*>(ap + 0x40);
            bool dup = false;                                   // (an AP in both lists)
            for (int j = 0; j < g_spawnN && !dup; j++) {
                const float* o = g_spawns[j].m + 12;
                dup = fabsf(o[0] - q[0]) < 1.0f && fabsf(o[2] - q[2]) < 1.0f;
            }
            if (!dup) { AddSpawn(q, reinterpret_cast<const float*>(ap + 0x30)); nAP = g_spawnN; }
        }
    }
    // The track's own lines (the AI drives these; loaded with the level, before
    // the grid): a spot every kSpotGap units along each. A line's nodes run one
    // way; which way the cars go comes from the reset points next to it (they
    // face along the track), else from its start junction.
    const float kSpotGap = 30.0f;
    int nLines = *reinterpret_cast<const int*>(hot::lineCount), nUsed = 0, nFlagged = 0, nRev = 0;
    const BYTE* lines = *reinterpret_cast<BYTE* const*>(hot::lineTable);
    BYTE* const* juncs = *reinterpret_cast<BYTE* const* const*>(hot::juncTable);
    int nJuncs = *reinterpret_cast<const int*>(hot::juncCount);
    for (int i = 0; lines && i < nLines && i < 1024; i++) {
        const BYTE* L = lines + i * 0x74;
        if (L[0x4A]) { nFlagged++; continue; }                  // (skipped by the game's own nearest-line search)
        int fmt = *reinterpret_cast<const int*>(L + 0x54);
        int stride = fmt == 1 ? 0x30 : fmt == 2 ? 0x40 : 0x10;
        int nn = *reinterpret_cast<const short*>(L + 0x3E);
        const BYTE* nodes = *reinterpret_cast<BYTE* const*>(L + 0x60);
        if (!nodes || nn < 2 || nn > 100000) continue;
        auto node = [&](int k) { return reinterpret_cast<const float*>(nodes + k * stride); };
        // which way: vote with the reset points within 40 units of a node
        float vote = 0.0f;
        for (int r = 0; r < nAP; r++) {
            const float* rm = g_spawns[r].m;
            for (int k = 0; k + 1 < nn; k++) {
                const float* a = node(k); const float* b = node(k + 1);
                float dx = a[0] - rm[12], dz = a[2] - rm[14];
                if (dx * dx + dz * dz > 40.0f * 40.0f) continue;
                vote += (b[0] - a[0]) * rm[8] + (b[2] - a[2]) * rm[10];
                break;
            }
        }
        bool rev = false;
        if (vote != 0.0f) rev = vote < 0.0f;
        else {                                                  // the start junction: > 0 = entered at node 0
            int j = *reinterpret_cast<const short*>(L + 0x34);
            const BYTE* J = (juncs && j >= 0 && j < nJuncs) ? juncs[j] : nullptr;
            if (J) {
                int nc = *reinterpret_cast<const short*>(J + 0x42);
                for (int c = 0; c < nc && c < 8; c++)
                    if (*reinterpret_cast<const short*>(J + 0x1C + c * 2) == i) {
                        rev = *reinterpret_cast<const short*>(J + 0x2C + c * 2) <= 0; break;
                    }
            }
        }
        nUsed++; if (rev) nRev++;
        float run = 0.0f;                                       // (the first one kSpotGap in: not at the junction)
        for (int k = 0; k + 1 < nn; k++) {
            const float* a = node(k); const float* b = node(k + 1);
            float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            run += sqrtf(Dot3(d, d));
            if (run < kSpotGap) continue;
            run = 0.0f;
            bool dupe = false;                                  // (parallel lanes: one spot)
            for (int j = 0; j < g_spawnN && !dupe; j++) {
                const float* o = g_spawns[j].m + 12;
                dupe = fabsf(o[0] - a[0]) < 10.0f && fabsf(o[2] - a[2]) < 10.0f && fabsf(o[1] - a[1]) < 10.0f;
            }
            if (dupe) continue;
            if (rev) { d[0] = -d[0]; d[2] = -d[2]; }
            AddSpawn(a, d);
        }
    }
    char hist[160] = ""; int hl = 0;
    for (int k = 0; k < nt && hl < 140; k++) hl += snprintf(hist + hl, sizeof(hist) - hl, " %X:%d", types[k][0], types[k][1]);
    Logf("rampage: %d spot(s): %d reset point(s), %d along %d of %d track line(s) (%d flagged, %d reversed; action point types%s)\n",
         g_spawnN, nAP, g_spawnN - nAP, nUsed, nLines, nFlagged, nRev, hist);
}
bool RampageSpawn(BYTE* car, const float*, const float*, int a3, float a4, int a5) {
    DWORD vt = *reinterpret_cast<DWORD*>(car);
    if (vt != 0x8A0144 && vt != 0x8A3FA4) return false;         // the player keeps the start
    DWORD now = GetTickCount();
    if (now - g_spawnMs > 3000) { CollectSpawns(); g_spawnUsed = 0; }   // a new start: the points afresh
    g_spawnMs = now;
    if (!g_spawnN) return false;
    // Where the player is: the start (they keep it), facing along it
    float ref[3] = { 0, 0, 0 }, fwd[3] = { 0, 0, 0 }; bool haveRef = false;
    if (const BYTE* start = FindStartAP(0)) { memcpy(ref, start + 0x40, 12); memcpy(fwd, start + 0x30, 12); haveRef = true; }
    fwd[1] = 0.0f;
    { float l = sqrtf(Dot3(fwd, fwd)); if (l > 0.01f) { fwd[0] /= l; fwd[2] /= l; } else fwd[0] = fwd[2] = 0.0f; }
    // Each AI car: the spot farthest from the player and from the AI cars placed
    // so far. Spots behind the player count as closer to them (x rampageBehind),
    // and spots nearer the player than rampageMinDist are used only if nothing
    // else is left.
    static float placed[16][3];
    float bestD = -1.0f; int best = -1;
    for (int pass = 0; pass < 2 && best < 0; pass++) {
        for (int i = 0; i < g_spawnN; i++) {
            const float* q = g_spawns[i].m + 12;
            bool taken = false;
            for (int j = 0; j < g_spawnUsed && j < 16 && !taken; j++)
                taken = fabsf(q[0] - placed[j][0]) < 1.0f && fabsf(q[2] - placed[j][2]) < 1.0f;
            if (taken) continue;
            float dmin = 1e30f;
            if (haveRef) {
                float d[3] = { q[0] - ref[0], q[1] - ref[1], q[2] - ref[2] };
                dmin = sqrtf(Dot3(d, d));
                if (d[0] * fwd[0] + d[2] * fwd[2] < 0.0f) dmin *= g_cfg.rampageBehind;
                if (pass == 0 && dmin < g_cfg.rampageMinDist) continue;
            }
            for (int j = 0; j < g_spawnUsed && j < 16; j++) {
                float d[3] = { q[0] - placed[j][0], q[1] - placed[j][1], q[2] - placed[j][2] };
                float dd = sqrtf(Dot3(d, d)); if (dd < dmin) dmin = dd;
            }
            if (dmin > bestD) { bestD = dmin; best = i; }
        }
    }
    if (best < 0) return false;
    const float* m = g_spawns[best].m;
    if (g_spawnUsed < 16) memcpy(placed[g_spawnUsed], m + 12, 12);
    g_spawnUsed++;
    Logf("rampage: AI car %p spawned at (%.1f %.1f %.1f), %.0f from you/the nearest car (of %d spots)\n",
         car, m[12], m[13], m[14], bestD, g_spawnN);
    reinterpret_cast<PlaceCar_t>(hot::fnPlaceCar)(car, m + 12, m, a3, a4, a5);
    return true;
}
typedef void (__attribute__((thiscall)) *Brain_t)(BYTE*);
static int g_rampageMode = -2; static DWORD g_rampageModeMs = 0;
static void __attribute__((thiscall)) HkBrainTargets(BYTE* brain) {
    reinterpret_cast<Brain_t>(hot::fnBrainTargets)(brain);
    DWORD now = GetTickCount();
    if (now - g_rampageModeMs > 1000) { g_rampageMode = GameMode(); g_rampageModeMs = now; }   // (once a second)
    if (g_rampageMode != 7) return;
    BYTE* car = *reinterpret_cast<BYTE**>(brain + 0xC);
    if (!car || *reinterpret_cast<DWORD*>(car) != 0x8A0144) return;    // AI cars only
    BYTE* player = nullptr;                                     // the player's car (GSPlayerRaceCar_c)
    for (int i = 0; i < 8 && !player; i++) {
        BYTE* r = reinterpret_cast<BYTE* const*>(0x953C90)[i];
        BYTE* c = r ? *reinterpret_cast<BYTE**>(r + 0x10) : nullptr;
        if (c && *reinterpret_cast<DWORD*>(c) == 0x8A0384) player = c;
    }
    if (!player) return;
    const float* cp = reinterpret_cast<const float*>(car + 0x10) + 12;
    const float* pp = reinterpret_cast<const float*>(player + 0x10) + 12;
    float rel[3] = { cp[0] - pp[0], 0.0f, cp[2] - pp[2] };     // from the player to the car
    float dist = sqrtf(Dot3(rel, rel));
    int n = *reinterpret_cast<int*>(car + 0xC60);
    BYTE* pts = *reinterpret_cast<BYTE**>(car + 0xC64);
    float* spd = *reinterpret_cast<float**>(car + 0xC6C);
    if (n <= 0 || n > 256 || !pts) return;
    if (spd && g_cfg.rampageSpeed != 1.0f)                      // all the time (1 = as the game drives them)
        for (int i = 0; i < n; i++) spd[i] *= g_cfg.rampageSpeed;
    if (dist > g_cfg.rampageFleeRange || dist < 0.01f) return;
    float close = 1.0f - dist / g_cfg.rampageFleeRange;         // 0 at the edge of range, 1 on top
    for (int i = 0; i < n; i++) {
        float* tp = reinterpret_cast<float*>(pts + i * 0x1C);    // target xyz; +0x10: road width vector (right - left)
        const float* side = tp + 4;
        float w2 = side[0] * side[0] + side[2] * side[2];
        if (w2 > 0.01f && g_cfg.rampageDodge > 0.0f) {
            float away = (rel[0] * side[0] + rel[2] * side[2]) >= 0.0f ? 1.0f : -1.0f;   // the side of the road away from the player
            float k = away * g_cfg.rampageDodge * close;
            tp[0] += side[0] * k; tp[2] += side[2] * k;
        }
        if (spd) spd[i] *= 1.0f + (g_cfg.rampageFleeSpeed - 1.0f) * close;
    }
}
void InstallRampage() {
    if (g_cfg.rampageTopSpeed > 0.0f && g_cfg.rampageTopSpeed != 0.45f) {
        if (!PatchBytes(hot::rampageTopSpd, &g_cfg.rampageTopSpeed, 4)) Logf("rampage: top speed patch FAILED\n");
        else Logf("rampage: AI top speed %.0f%% of yours\n", g_cfg.rampageTopSpeed * 100.0f);
    }
    if (!g_cfg.rampageFlee) return;
    if (!PatchCall(hot::callBrainTargets, reinterpret_cast<void*>(&HkBrainTargets), 5)) { Logf("rampage: patch FAILED\n"); return; }
    Logf("rampage: AI flees within %.0f (speed x%.2f, dodge %.2f)\n", g_cfg.rampageFleeRange, g_cfg.rampageFleeSpeed, g_cfg.rampageDodge);
}
