#include "common.h"
#include "start.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "framerate.h"
#include "camera.h"
#include "ai.h"
#include "rampage.h"
#include "sparks.h"

// ---------------------------------------------------------------------------
// Start boost. The 3-2-1 dial (GSSingle321_c) has four Flash animations: the
// text (3-2-1-GO: it times the start), the gauge, the wheel and the fire. After
// its intro the gauge loops; pressing accelerate while the gauge is on frames
// 127-144 (the green light) gives the boost start - one try (0x4E8619). The
// gauge and the wheel are run boostMeterSpeed times as fast: each update they get
// the extra time on top of the game's own step. The text, and so the countdown
// and GO, keep their timing.
// The gauge loops over frames 110-186, one full swing of the needle: the
// green (127-144) is around frame 135, the needle at 100%; the red end, 0%,
// around 174. A press there (startRedFrom-startRedTo) slows the player's car
// for startRedSlow s at GO (HkIntroGo; the EMP's slow).
// ---------------------------------------------------------------------------
typedef void (__attribute__((thiscall)) *Upd321_t)(BYTE*);
typedef bool (__attribute__((thiscall)) *PastMark_t)(BYTE*, const char*);
static void __attribute__((thiscall)) Hk321Update(BYTE* self) {
    bool before = self[0x494] != 0;                             // (pressed already)
    if (!before) g_startRed = false;
    float k = g_cfg.boostMeterSpeed;
    if (k > 1.0f && !self[0x494] && *reinterpret_cast<int*>(self + 0x30) < 5) {   // no press yet, before GO
        float dt = *reinterpret_cast<const float*>(hot::frameDt);
        BYTE* anims[2] = { self + 0x1B0, self + 0x2C4 };        // the gauge, the wheel
        for (BYTE* a : anims)
            if (*reinterpret_cast<int*>(a + 0x18) == 2 &&       // playing, past its intro: looping
                reinterpret_cast<PastMark_t>(hot::fnAnimPastMark)(a, reinterpret_cast<const char*>(hot::strIntroEnd)))
                *reinterpret_cast<float*>(a + 0x14) += (k - 1.0f) * dt * (a[0x23] ? 1.0f : -1.0f);
    }
    reinterpret_cast<Upd321_t>(hot::fn321Update)(self);
    if (!before && self[0x494] && !self[0x495] && g_cfg.startRedSlow > 0.0f) {   // pressed just now, not on the green
        int f = *reinterpret_cast<const int*>(self + 0x1B0 + 0x1C);   // (the gauge's frame, as 0x4E8669 reads it)
        if (f >= g_cfg.startRedFrom && f <= g_cfg.startRedTo) {
            g_startRed = true;
            Logf("start boost: pressed on the red (frame %d): slowed for %.1f s at GO\n", f, g_cfg.startRedSlow);
        }
    }
}
void InstallStartBoost() {
    if (g_cfg.boostMeterSpeed <= 1.0f && g_cfg.startRedSlow <= 0.0f) return;
    if (!PatchPointer(reinterpret_cast<void**>(hot::vt321Update), reinterpret_cast<void*>(&Hk321Update))) {
        Logf("start boost: patch FAILED\n");
        g_cfg.startRedSlow = 0.0f;
        return;
    }
    Logf("start boost: meter x%.2f, a press on the red (frames %d-%d) slowed %.1f s at GO\n",
         g_cfg.boostMeterSpeed, g_cfg.startRedFrom, g_cfg.startRedTo, g_cfg.startRedSlow);
}

// ---------------------------------------------------------------------------
// Starting grid. Each track has one start action point per grid slot (type 1,
// index = slot; position at +0x40, heading = matrix row 2 at +0x30), laid one
// behind the other ~15 apart: the cars start in single file. GSWorldInitState_c
// (and GSWorldResetState_c on a restart) puts each car on its slot's point with
// 0x63E750. The mod puts them in a compact grid instead, as in the trailer:
// from the front of the game's line, facing the way the start points face,
// gridColumns abreast gridWidth apart, rows gridRowGap apart, each car to the
// right gridStagger further back (the rows descend; with gridRowGap = gridColumns
// x gridStagger every car is further back than the one before, as on an F1
// grid across four lanes). The car order is kept.
// ---------------------------------------------------------------------------
const BYTE* FindStartAP(int idx) {
    const DWORD lists[2] = { hot::apUserList, hot::apList };
    for (DWORD list : lists) {
        BYTE* const* aps = *reinterpret_cast<BYTE* const* const*>(list);
        int n = *reinterpret_cast<const int*>(list + 4);
        for (int i = 0; aps && i < n && i < 4096; i++) {
            const BYTE* ap = aps[i];
            if (ap && *reinterpret_cast<const short*>(ap + 0x50) == 1 && *reinterpret_cast<const short*>(ap + 0x52) == idx) return ap;
        }
    }
    return nullptr;
}
int GameMode() {
    BYTE* ev = *reinterpret_cast<BYTE**>(hot::curEvent);
    return ev ? reinterpret_cast<int (__attribute__((thiscall)) *)(BYTE*)>(hot::fnGameMode)(ev) : -1;
}
// Narrow starts: the grid is fitted to the track. Some start lines run between
// scenery (walls, pillars, props of the track) with no room for gridColumns
// abreast: the outer cars started in it, and the intro camera swept through it.
// Along the grid's length, the room either side of the start line is measured
// with the game's line test against the track (from 1.5 up, out to 25 units,
// the nearest of a few points along each row); the grid is moved sideways into
// the middle of that room and given as many columns as fit, gridWidth apart
// (down to 5.5 apart), with 3 units to spare beside the outer cars - single file
// if two don't fit. Worked out once per race.

static void GridFit(const float* fp, const float* ah, const float* rt, int n, int& cols, float& width, float& shift) {
    static const float* lastFp = nullptr; static DWORD lastMs = 0;
    static int fitCols = 1; static float fitWidth = 0.0f, fitShift = 0.0f;
    DWORD now = GetTickCount();
    if (fp == lastFp && now - lastMs < 10000) { cols = fitCols < cols ? fitCols : cols; width = fitWidth; shift = fitShift; return; }
    const float reach = 25.0f, margin = 3.0f;
    float minL = reach, minR = reach;
    int rows = (n + cols - 1) / cols;
    float length = (rows - 1) * g_cfg.gridRowGap + (cols - 1) * g_cfg.gridStagger;
    for (int i = 0; i <= 4; i++) {                              // points along the grid's length
        float back = length * i / 4.0f;
        float c[3] = { fp[0] - ah[0] * back, fp[1] + 1.5f, fp[2] - ah[2] * back };
        for (int side = 0; side < 2; side++) {
            float sg = side ? 1.0f : -1.0f;
            float e[3] = { c[0] + rt[0] * reach * sg, c[1], c[2] + rt[2] * reach * sg };
            float free = reach;
            if (TrackBetween(c, e)) {
                float lo = 0.0f, hi = 1.0f;
                for (int k = 0; k < 8; k++) {
                    float m = (lo + hi) * 0.5f;
                    float p[3] = { c[0] + (e[0] - c[0]) * m, c[1], c[2] + (e[2] - c[2]) * m };
                    if (TrackBetween(c, p)) hi = m; else lo = m;
                }
                free = reach * lo;
            }
            float& mn = side ? minR : minL;
            if (free < mn) mn = free;
        }
    }
    float room = minL + minR - 2.0f * margin;                   // centre to centre of the outer cars
    int want = cols;
    float w = g_cfg.gridWidth;
    int c = want;
    while (c > 1 && (c - 1) * w > room) {
        float squeezed = room / (c - 1);
        if (squeezed >= 5.5f) { w = squeezed; break; }
        c--;
    }
    fitCols = c; fitWidth = w;
    fitShift = (minR - minL) * 0.5f;                            // the middle of the room
    float half = (c - 1) * w * 0.5f;
    float lim = minR - margin - half, limL = -(minL - margin - half);   // (keep the outer cars clear)
    if (fitShift > lim) fitShift = lim;
    if (fitShift < limL) fitShift = limL;
    if (lim < limL) fitShift = 0.0f;
    lastFp = fp; lastMs = now;
    Logf("grid: room beside the start line %.1f left, %.1f right: %d abreast, %.1f apart, moved %.1f %s\n",
         minL, minR, c, w, fabsf(fitShift), fitShift >= 0.0f ? "right" : "left");
    cols = c < cols ? c : cols; width = fitWidth; shift = fitShift;
}
static void __attribute__((thiscall)) HkGridPlace(BYTE* car, const float* pos, const float* mat, int a3, float a4, int a5) {
    SparkSetup();
    int mode = GameMode();
    if (mode == 7 && g_cfg.rampageSpread && RampageSpawn(car, pos, mat, a3, a4, a5)) return;
    if (mode != 6 && mode != 8 && mode != 10) {                 // the grid is for races (race, eliminator, tournament)
        reinterpret_cast<PlaceCar_t>(hot::fnPlaceCar)(car, pos, mat, a3, a4, a5);
        return;
    }
    const BYTE* ap = reinterpret_cast<const BYTE*>(pos) - 0x40;
    int slot = *reinterpret_cast<const short*>(ap + 0x52);
    const BYTE* ap0 = FindStartAP(0);
    int n = 0;                                                  // start points on this track
    while (n < 16 && FindStartAP(n)) n++;
    if (!g_cfg.gridLayout || slot < 0 || slot >= n || n < 2 || !ap0) {
        reinterpret_cast<PlaceCar_t>(hot::fnPlaceCar)(car, pos, mat, a3, a4, a5);
        return;
    }
    // ahead: the way the cars face (the start point's row 2), level; right: across
    const float* h = reinterpret_cast<const float*>(ap0 + 0x30);
    float ah[3] = { h[0], 0.0f, h[2] };
    float al = sqrtf(Dot3(ah, ah));
    if (al < 0.01f) { reinterpret_cast<PlaceCar_t>(hot::fnPlaceCar)(car, pos, mat, a3, a4, a5); return; }
    ah[0] /= al; ah[2] /= al;
    float rt[3] = { ah[2], 0.0f, -ah[0] };
    // the front of the game's line: the start point furthest ahead; the car order
    // (slot 0 first) is kept, front to back
    const BYTE* front = ap0; float best = -1e30f;
    for (int i = 0; i < n; i++) {
        const float* p = reinterpret_cast<const float*>(FindStartAP(i) + 0x40);
        float d = p[0] * ah[0] + p[2] * ah[2];
        if (d > best) { best = d; front = FindStartAP(i); }
    }
    const float* p0 = reinterpret_cast<const float*>(ap0 + 0x40);
    const float* pn = reinterpret_cast<const float*>(FindStartAP(n - 1) + 0x40);
    bool slot0Front = p0[0] * ah[0] + p0[2] * ah[2] >= pn[0] * ah[0] + pn[2] * ah[2];
    int rank = slot0Front ? slot : n - 1 - slot;                // 0 = pole
    int cols = g_cfg.gridColumns < 1 ? 1 : g_cfg.gridColumns > 8 ? 8 : g_cfg.gridColumns;
    const float* fp = reinterpret_cast<const float*>(front + 0x40);
    float width = g_cfg.gridWidth, shift = 0.0f;
    GridFit(fp, ah, rt, n, cols, width, shift);
    int row = rank / cols, col = rank % cols;
    float across = shift + (col - (cols - 1) * 0.5f) * width;
    float back = row * g_cfg.gridRowGap + col * g_cfg.gridStagger;   // each car further right, further back
    float np[3] = { fp[0] + rt[0] * across - ah[0] * back, fp[1], fp[2] + rt[2] * across - ah[2] * back };
    Logf("grid: slot %d -> row %d column %d (%.1f %.1f %.1f)\n", slot, row, col, np[0], np[1], np[2]);
    reinterpret_cast<PlaceCar_t>(hot::fnPlaceCar)(car, np, reinterpret_cast<const float*>(front + 0x10), a3, a4, a5);
}

void InstallGrid() {
    if (!g_cfg.gridLayout && !g_cfg.rampageSpread && !g_cfg.sparkStreaks) { Logf("grid: original (gridLayout=0)\n"); return; }   // (the hook also sets up the sparks)
    if (!PatchCall(hot::callGridPlace, reinterpret_cast<void*>(&HkGridPlace), 5) ||
        !PatchCall(hot::callGridReset, reinterpret_cast<void*>(&HkGridPlace), 5)) { Logf("grid: patch FAILED\n"); return; }
    if (g_cfg.gridLayout) Logf("grid: %d abreast, %.1f apart\n", g_cfg.gridColumns, g_cfg.gridWidth);
    else Logf("grid: original (gridLayout=0)\n");
}
