#include "common.h"
#include "debug.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "window.h"
#include "items.h"

// ---------------------------------------------------------------------------
// The game's own debug output. HOT.exe has about 1,200 calls to a debug printf
// (0x5890F0) - warnings, missing files, "not implemented on PC" - that the
// release build turned into an empty function. It's restored here and written
// to BTOverdrive_game.log, each message (by format string) at most 5 times, with a
// count of further repeats every minute. Arguments are formatted defensively:
// a bad string pointer prints as (bad) instead of crashing.
// ---------------------------------------------------------------------------
FILE* g_glog = nullptr;
static CRITICAL_SECTION g_glogLock;
struct GFmt { const char* fmt; LONG n, logged; };
static GFmt g_gf[2048]; static int g_gfN = 0;
static short g_gfHash[4096];                  // index + 1 by format pointer; at most half full
static DWORD g_gRepTick = 0;
static void SafeFormat(char* out, size_t cap, const char* fmt, va_list ap) {
    size_t o = 0;
    auto put = [&](const char* t) { while (*t && o + 1 < cap) out[o++] = *t++; };
    for (const char* f = fmt; *f && o + 1 < cap; f++) {
        if (*f != '%') { out[o++] = *f; continue; }
        const char* start = f++;
        if (*f == '%') { out[o++] = '%'; continue; }
        while (*f && strchr("-+ #0", *f)) f++;
        if (*f == '*') { (void)va_arg(ap, int); f++; } else while (*f >= '0' && *f <= '9') f++;
        if (*f == '.') { f++; if (*f == '*') { (void)va_arg(ap, int); f++; } else while (*f >= '0' && *f <= '9') f++; }
        size_t body = (size_t)(f - start);                     // "%", flags, width, precision (no length)
        bool i64 = false, wide = false;
        for (;;) {
            if (*f == 'h' || *f == 'L' || *f == 'z' || *f == 't' || *f == 'j') f++;
            else if (*f == 'l') { wide = true; f++; if (*f == 'l') { i64 = true; f++; } }
            else if (f[0] == 'I' && f[1] == '6' && f[2] == '4') { i64 = true; f += 3; }
            else break;
        }
        if (!*f) break;
        char spec[32], piece[256];
        size_t sl = (size_t)(f - start) + 1; if (sl >= sizeof(spec)) sl = sizeof(spec) - 1;
        memcpy(spec, start, sl); spec[sl] = 0;
        piece[0] = 0;
        switch (*f) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'c':
            if (i64) {                                          // the same spec with "ll" (MinGW's printf)
                char s64[40]; size_t b = body < 30 ? body : 30;
                memcpy(s64, start, b); s64[b] = 'l'; s64[b + 1] = 'l'; s64[b + 2] = *f; s64[b + 3] = 0;
                snprintf(piece, sizeof(piece), s64, va_arg(ap, long long));
            }
            else snprintf(piece, sizeof(piece), spec, va_arg(ap, int));
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
            snprintf(piece, sizeof(piece), spec, va_arg(ap, double)); break;
        case 'p': snprintf(piece, sizeof(piece), "%p", va_arg(ap, void*)); break;
        case 's': case 'S': {
            const void* p = va_arg(ap, const void*);
            bool w = wide || *f == 'S';
            if (!p) snprintf(piece, sizeof(piece), "(null)");
            else if (w ? IsBadStringPtrW(static_cast<const wchar_t*>(p), 200) : IsBadStringPtrA(static_cast<const char*>(p), 200))
                snprintf(piece, sizeof(piece), "(bad)");
            else if (w) snprintf(piece, sizeof(piece), "%.200ls", static_cast<const wchar_t*>(p));
            else snprintf(piece, sizeof(piece), "%.200s", static_cast<const char*>(p));
            break;
        }
        default: snprintf(piece, sizeof(piece), "%s", spec); break;   // unknown: print as is, no argument
        }
        put(piece);
    }
    out[o] = 0;
}
void GameLogRepeats(bool final) {
    DWORD now = GetTickCount();
    if (!final && now - g_gRepTick < 60000) return;
    g_gRepTick = now;
    for (int i = 0; i < g_gfN; i++) {
        GFmt& g = g_gf[i];
        if (g.n > g.logged) {
            fprintf(g_glog, "  (x%ld more) %.120s\n", g.n - g.logged, g.fmt);
            g.logged = g.n;
        }
    }
    fflush(g_glog);
}
void GameLogFinal() {
    if (!g_glog) return;
    EnterCriticalSection(&g_glogLock);
    GameLogRepeats(true);
    LeaveCriticalSection(&g_glogLock);
}
extern "C" void __cdecl GameDebugPrint(const char* fmt, ...) {
    if (!g_glog || !fmt) return;
    EnterCriticalSection(&g_glogLock);
    GFmt* g = nullptr;
    int h = (int)(((DWORD)(UINT_PTR)fmt * 2654435761u) >> 20);
    for (; g_gfHash[h]; h = (h + 1) & 4095)
        if (g_gf[g_gfHash[h] - 1].fmt == fmt) { g = &g_gf[g_gfHash[h] - 1]; break; }
    if (!g) {                                   // a new message: check its format string once
        if (IsBadStringPtrA(fmt, 1024)) { LeaveCriticalSection(&g_glogLock); return; }
        if (g_gfN >= 2048) { LeaveCriticalSection(&g_glogLock); return; }   // table full: no more new messages
        g = &g_gf[g_gfN++]; g->fmt = fmt; g->n = g->logged = 0; g_gfHash[h] = (short)g_gfN;
    }
    LONG n = ++g->n;
    if (n <= 5) {
        char buf[1024];
        va_list ap; va_start(ap, fmt); SafeFormat(buf, sizeof(buf), fmt, ap); va_end(ap);
        size_t l = strlen(buf);
        while (l && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) buf[--l] = 0;
        DWORD ret = (DWORD)(UINT_PTR)__builtin_return_address(0);
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(g_glog, "[%02d:%02d:%02d.%03d] %06lX %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, ret, buf);
        g->logged = n;
        fflush(g_glog);
    }
    GameLogRepeats(false);
    LeaveCriticalSection(&g_glogLock);
}
// Audit: every failed sound lookup (the game only reports some), with the
// bank's contents listed once, so missing or misnamed sounds show up.
extern "C" { DWORD g_soundFindCont = hot::soundFind + 8; }
extern "C" char SoundFindTramp(); 
asm(".text\n"
    ".globl _SoundFindTramp\n_SoundFindTramp:\n"     // the 8 bytes moved out of 0x5C36B0, then back
    "    pushl %ebx\n"
    "    pushl %ebp\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    movl 0x14(%esp), %edi\n"
    "    jmp *_g_soundFindCont\n");
typedef char (__attribute__((thiscall)) *SoundFind_t)(void*, BYTE*, const char*, int);
static char __attribute__((thiscall)) HkSoundFind(void* self, BYTE* bank, const char* name, int report) {
    char ok = reinterpret_cast<SoundFind_t>(reinterpret_cast<void*>(&SoundFindTramp))(self, bank, name, report);
    if (ok || !g_glog || !bank || IsBadReadPtr(bank, 0x100)) return ok;
    EnterCriticalSection(&g_glogLock);
    static const BYTE* listed[256]; static int nListed = 0;
    static char missed[256][48]; static int nMissed = 0;
    char key[48];
    snprintf(key, sizeof(key), "%p/%.32s", (void*)bank, (name && !IsBadStringPtrA(name, 64)) ? name : "?");
    bool seen = false;
    for (int i = 0; i < nMissed && !seen; i++) seen = !strcmp(missed[i], key);
    if (!seen) {
        if (nMissed < 256) lstrcpynA(missed[nMissed++], key, 48);
        const char* bankName = reinterpret_cast<const char*>(bank + 4);
        int count = *reinterpret_cast<int*>(bank + 0xE0);
        const BYTE* table = *reinterpret_cast<BYTE* const*>(bank + 0xF8);
        fprintf(g_glog, "[audit] sound \"%.40s\" not found in bank \"%.40s\" (%d sounds)\n",
                (name && !IsBadStringPtrA(name, 64)) ? name : "?", IsBadStringPtrA(bankName, 64) ? "?" : bankName, count);
        bool done = false;
        for (int i = 0; i < nListed && !done; i++) done = listed[i] == bank;
        if (!done && count > 0 && count < 4096 && table && !IsBadReadPtr(table, (UINT_PTR)count * 0x40)) {
            if (nListed < 256) listed[nListed++] = bank;
            fprintf(g_glog, "[audit]   bank contents:");
            for (int i = 0; i < count; i++) fprintf(g_glog, "%s%.16s", i % 8 ? ", " : "\n[audit]     ", table + i * 0x40);
            fprintf(g_glog, "\n");
        }
        fflush(g_glog);
    }
    LeaveCriticalSection(&g_glogLock);
    return ok;
}
void InstallGameLog() {
    if (!g_cfg.gameLog) return;
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%s" MOD_FILE "_game.log", g_asiDir);
    if (!(g_glog = fopen(path, "w"))) { Logf("game log: can't create %s\n", path); return; }
    InitializeCriticalSection(&g_glogLock);
    fprintf(g_glog, "HOT.exe debug messages (" MOD_NAME " v" HWBT_VERSION "): time, caller, message\n");
    if (!PatchJmp(hot::debugPrint, reinterpret_cast<DWORD>(&GameDebugPrint))) Logf("game log: patch FAILED\n");
    if (!PatchJmp(hot::soundFind, reinterpret_cast<DWORD>(&HkSoundFind))) Logf("game log: sound audit patch FAILED\n");
}
