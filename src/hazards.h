// What the other parts use from hazards.cpp.
#pragma once
#include "common.h"
#include "framerate.h"
#include "items.h"

struct HazardKick {
    BYTE* car; float dv[3]; float w[3]; volatile LONG pending; DWORD ms;
    volatile DWORD flying;                      // when it was thrown (0: not flying)
    float m0[16];                               // the car's matrix then: on the track, where it's put back
    LONG lap0;                                  // its lap count then (LapBackOver)
    bool watch, reset;                          // watching it fall / a put-back is due
    DWORD safe;                                 // when it was put back: no new hit for a while
    float dir[2];                               // the throw's horizontal direction (x, z)
    float push; DWORD pushMs;                   // its speed along dir kept up to push for pushMs
    bool weapon;                                // a weapon hit's: no smoke, walls stay walls
};
extern HazardKick g_kicks[16];
struct HazardCar { BYTE* car; DWORD ms; };                      // race cars seen at their physics sync
extern HazardCar g_hzCars[16];
struct HazardBox { BYTE* box; float pos[3]; DWORD ms; float v[3]; float maxSpeed; LONG hits; };
extern HazardBox g_hzBoxes[64];
extern LONG g_hzKicks, g_hzLogged;
typedef void (__attribute__((thiscall)) *SetVec_t)(BYTE*, const float*);
void ApplyHazardKick(BYTE* car);
void HazardFallCheck(BYTE* car);
alignas(16) extern BYTE g_resetObj[0x88];
void HazardResets();
void FallTrack(BYTE* car);
void FallResets();
void NoteHazardCar(BYTE* car);
void QueueKick(BYTE* car, const HazardBox& b, float sp);
void InstallFallReset();
