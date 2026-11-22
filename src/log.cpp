#include "common.h"
#include "log.h"

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
FILE* g_log = nullptr;
CRITICAL_SECTION g_logLock;
bool g_logLockInit = false;
void Logf(const char* fmt, ...) {
    if (!g_log) return;
    if (g_logLockInit) EnterCriticalSection(&g_logLock);
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fflush(g_log);
    if (g_logLockInit) LeaveCriticalSection(&g_logLock);
}
