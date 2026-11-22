// What the other parts use from icegun.cpp.
#pragma once
#include "common.h"
#include "framerate.h"
#include "items.h"

struct IceSeen { BYTE* car; bool frozen; bool lock; DWORD frozeMs, endMs, lockMs, seenMs; bool leftover;
                 DWORD lockUntil; bool ourLock; DWORD zapMs; };       // (a mega shield's lock on it; its last shock)
IceSeen* IceFind(BYTE* car, bool add);
void IceTrack(BYTE* car);
void InstallIceFix();
