// What the other parts use from effects.cpp.
#pragma once
#include "common.h"
#include "framerate.h"

const float* FollowCar(const float* v, float* o);
extern "C" { void EffPosStub(); void EffPosObj18Stub(); void EffPosNodeStub(); void EffPosObj10Stub(); void EffRow2Stub(); void EffRow1Stub(); void EffRow0Stub(); void EffRowsStub(); }
// The car's shadow light and headlight projector: the car's matrix holds its
// blend during the call only.
typedef void (__attribute__((thiscall)) *ThisVoid_t)(BYTE*);
void __attribute__((thiscall)) HkVehLights(BYTE* veh);
void __attribute__((thiscall)) HkCarShadow(BYTE* veh, DWORD a, DWORD c);
void __attribute__((thiscall)) HkCarTrails(BYTE* trails);
extern "C" void GarageLiftStub();
