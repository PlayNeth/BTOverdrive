// What the other parts use from log.cpp.
#pragma once
#include "common.h"

extern FILE* g_log;
extern CRITICAL_SECTION g_logLock;
extern bool g_logLockInit;
void Logf(const char* fmt, ...);
