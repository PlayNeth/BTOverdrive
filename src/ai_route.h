// What the other parts use from ai_route.cpp.
#pragma once
#include "common.h"
#include "framerate.h"
#include "hazards.h"

bool ResetAPAhead(const float* p, const float* fwd, float* m);
void AiStuckTrack(BYTE* car);
void AiStuckResets();
void InstallAiUnstuck();
void AiLoopAssist(BYTE* car);
