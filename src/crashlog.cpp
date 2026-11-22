#include "common.h"
#include "crashlog.h"
#include "log.h"

// ---------------------------------------------------------------------------
// Crash log: an access violation (or another fatal CPU exception) in HOT.exe's
// or this ASI's code is written to the log - where (with its offset), the
// registers, and the code addresses found on the stack - before the game's own
// handling. Each place is logged once (the game may catch some itself), 8 at most.
// ---------------------------------------------------------------------------
static BYTE* g_modBase; static DWORD g_modSize;
static void CrashWhere(char* out, size_t n, DWORD a) {
    if (a >= 0x401000 && a < 0x900000) snprintf(out, n, "HOT.exe %06lX", a);
    else if (a >= reinterpret_cast<DWORD>(g_modBase) && a < reinterpret_cast<DWORD>(g_modBase) + g_modSize)
        snprintf(out, n, MOD_FILE ".asi +%05lX", a - reinterpret_cast<DWORD>(g_modBase));
    else snprintf(out, n, "%08lX", a);
}
static bool CrashOurs(DWORD a) {                               // in HOT.exe or this ASI
    return (a >= 0x401000 && a < 0x900000) ||
           (a >= reinterpret_cast<DWORD>(g_modBase) && a < reinterpret_cast<DWORD>(g_modBase) + g_modSize);
}
static LONG CALLBACK CrashHandler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW) return EXCEPTION_CONTINUE_SEARCH;
    DWORD eip = ep->ContextRecord->Eip;
    // Only faults in the game's or the mod's own code: IsBadReadPtr and co. (used
    // here and there to probe pointers) fault inside Windows on purpose and
    // catch it themselves - those aren't crashes. (A crash inside a driver or
    // Windows itself isn't logged either.)
    if (!CrashOurs(eip)) return EXCEPTION_CONTINUE_SEARCH;
    static DWORD seen[8]; static LONG nSeen = 0;
    for (LONG i = 0; i < nSeen && i < 8; i++) if (seen[i] == eip) return EXCEPTION_CONTINUE_SEARCH;
    LONG i = InterlockedIncrement(&nSeen) - 1;
    if (i >= 8) return EXCEPTION_CONTINUE_SEARCH;
    seen[i] = eip;
    const CONTEXT* c = ep->ContextRecord;
    char buf[1024]; size_t n = 0; char where[64];
    auto add = [&](const char* fmt, auto... a) {                // (one Logf for the whole record: one line block, one timestamp)
        if (n < sizeof(buf)) { int k = snprintf(buf + n, sizeof(buf) - n, fmt, a...); if (k > 0) n += (size_t)k; }
    };
    CrashWhere(where, sizeof(where), eip);
    add("CRASH: exception %08lX at %s", code, where);
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
        add(" (%s %08lX)", ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
            static_cast<DWORD>(ep->ExceptionRecord->ExceptionInformation[1]));
    add("\n  eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX edi %08lX ebp %08lX esp %08lX\n  stack:",
        c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    const DWORD* sp = reinterpret_cast<const DWORD*>(c->Esp); int found = 0;
    MEMORY_BASIC_INFORMATION mbi;                               // (the stack's committed part: no probing faults)
    DWORD top = VirtualQuery(sp, &mbi, sizeof(mbi)) ? reinterpret_cast<DWORD>(mbi.BaseAddress) + mbi.RegionSize : c->Esp;
    for (int k = 0; k < 256 && found < 12; k++) {
        if (reinterpret_cast<DWORD>(sp + k) + 4 > top) break;
        DWORD v = sp[k];
        if (!CrashOurs(v) || (v >= 0x880000 && v < 0x900000)) continue;   // (code only: not HOT.exe's data)
        CrashWhere(where, sizeof(where), v); add(" [%d] %s", k, where); found++;
    }
    Logf("%s\n", buf);
    return EXCEPTION_CONTINUE_SEARCH;
}
void InstallCrashLog(HMODULE self) {
    g_modBase = reinterpret_cast<BYTE*>(self);
    const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(g_modBase + reinterpret_cast<const IMAGE_DOS_HEADER*>(g_modBase)->e_lfanew);
    g_modSize = nt->OptionalHeader.SizeOfImage;
    AddVectoredExceptionHandler(1, CrashHandler);
}
