// What the other parts use from items.cpp.
#pragma once
#include "common.h"
struct IceSeen;
#include "icegun.h"

BYTE* RaceCarAt(int i);
void SlowHit(BYTE* car, float factor, float secs, DWORD weapon, DWORD sound);
void ItemFxStep(BYTE* car, IceSeen*, DWORD now);
void InstallItemFx();
void InstallToonFire();
static const struct { const char* name; int id; } kItemNames[] = {
    { "rocket", 0xFA1 }, { "mortar", 0xFA2 }, { "boost", 0xFA3 }, { "icegun", 0xFA4 }, { "mine", 0xFA5 },
    { "emp", 0xFA6 }, { "parachute", 0xFA7 }, { "airstrike", 0xFA8 }, { "smoke", 0xFA9 }, { "tesla", 0xFAA },
    { "shield", 0xFAB } };
bool ItemTableParse(const char* group, const char* v);
void InstallItemOdds();
