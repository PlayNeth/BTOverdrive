// What the other parts use from camera.cpp.
#pragma once
#include "common.h"
#include "framerate.h"
#include "start.h"

void HdrOverride(const char* name, float v);
void CamShakeUndo();
bool TrackBetween(const float* a, const float* b);
void IntroCamClip(BYTE* cam);
void IntroCamClipUndo();
extern float g_maxCamSpeed;
extern "C" void __cdecl HkSetFov(float fov);
void InstallSpeedBlur();
