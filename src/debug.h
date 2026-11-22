// What the other parts use from debug.cpp.
#pragma once
#include "common.h"
#include "window.h"

extern FILE* g_glog;
void GameLogRepeats(bool final);
void GameLogFinal();
void InstallGameLog();
