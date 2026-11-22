// What the other parts use from framerate.cpp.
#pragma once
#include "common.h"
#include "items.h"

typedef void (__cdecl *VoidFn_t)();
typedef void (__attribute__((thiscall)) *Limiter_t)(void*);
struct Track {
    BYTE* obj;            // nullptr = free
    void* vt;             // vtable when first seen: a destroyed object fails this check
    int   kind;           // 1 race car (physics), 2 camera or other drawn object
    LONG  seenStep;       // car: step whose physics sync saw it
    LONG  seenFrame;      // camera: frame whose 3D pass applied it
    LONG  snapStep;       // step of the last copy
    int   n;              // copies in a row (0..2)
    bool  written;        // blend is in the object right now
    bool  outOk;          // race car: out holds this frame's blend (outFrame)
    LONG  outFrame;       // race car: frame out was computed for
    LONG  trailStep;      // race car: step of its last tyre-trail draw
    float prev[16], cur[16], out[16];
};
extern Track g_tr[64];
extern LONG g_stepsDone, g_frameNo;
extern bool g_inRender;
extern bool g_interpOn;
static const float kCutDist = 25.0f;     // a jump this long in one step is a cut/respawn: no blend
extern volatile LONG g_kicksPending;
extern bool g_hzOn;
extern bool g_fallOn;
extern bool g_aiStuckOn;
extern bool g_carFxOn;
extern bool g_sndStepHooked;
extern bool g_raceClockOn;
extern BYTE* g_raceCar;
extern LONG g_raceSteps;
extern BYTE* g_applyCam;
extern "C" void CarSyncStub();
extern "C" void CamApplyStub();
extern "C" void VehDrawStub();
// Everything else drawn in the 3D pass (the car-select car, props, ...): any
// world matrix the pass reads through GSObject::GetTransform (slot 14, obj+0x10)
// or hands to the renderer's SetWorld (0x53C860), as long as it lives outside
// the stack. Tracked by address; same rules as above.
struct MTrack {
    float* p;
    LONG   seenFrame, snapStep, skipFrame;
    int    n;
    bool   written;
    BYTE   src;           // 1 SetWorld, 2 GetTransform
    DWORD  caller;        // diag: who handed it to SetWorld first
    LONG   redrawn, external;
    // computed while drawing (scene-graph nodes): history of the value handed to SetWorld
    LONG   fFrame, fStep; // frame and step of the last value taken
    LONG   fPrevStep;     // step of the value before it (fprev)
    int    fn;            // values in a row, one per step (0..2)
    LONG   fPerFrameStep; // last step it also changed between steps (animated per frame: left alone for a while)
    bool   drawnOnly;     // the drawing code rewrote it once: never blended in memory again (see HkFrameEnd)
    float  fprev[16], fcur[16];
    float  prev[16], cur[16], out[16];
};
bool PerFrame(const MTrack* t);
extern DWORD g_stackLo, g_stackHi;
MTrack* MFind(const float* p);
extern float g_alpha;
extern "C" void SetWorldStub();
extern "C" void GetXformStub();
extern "C" void BoneFetchStub();
void __cdecl HkStepTail();
float Len3(const float* v);
float Dot3(const float* a, const float* b);
bool Blend(const float* p, const float* c, float a, float* o);
LONGLONG Qpc();
void PerfFrameStart(LONGLONG waitTicks);
void InstallPerf();
void __cdecl HkRender3D();
void __attribute__((thiscall)) HkFrameEnd(void* renderer);
