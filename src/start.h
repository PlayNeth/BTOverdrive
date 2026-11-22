// What the other parts use from start.cpp.
#pragma once
#include "common.h"

void InstallStartBoost();
const BYTE* FindStartAP(int idx);
typedef void (__attribute__((thiscall)) *PlaceCar_t)(BYTE*, const float*, const float*, int, float, int);
int GameMode();
void InstallGrid();
