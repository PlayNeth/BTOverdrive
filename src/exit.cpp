#include "common.h"
#include "exit.h"
#include "game.h"
#include "log.h"
#include "patch.h"

// ---------------------------------------------------------------------------
// Quitting. After the game's main loop returns, the C runtime's exit runs its
// static destructors - and GSNxAllocator_c's (the PhysX memory pool, 0x4483C0)
// frees its pool's block, which is gone by then: an access violation at
// 0x5CF89B as the game quits. Once exit has been called (the game's import of
// it, hooked), that release only clears the pool's fields: the process is
// ending, its memory goes with it.
static volatile bool g_exiting = false;
typedef void (__cdecl *Exit_t)(int);
static Exit_t g_crtExit;
static void __cdecl HkExit(int code) { g_exiting = true; g_crtExit(code); }
static void __attribute__((thiscall)) HkPoolRelease(BYTE* pool) {
    if (g_exiting) {
        memset(pool, 0, 0xC); *reinterpret_cast<DWORD*>(pool + 0x18) = 0;   // (+0, +4, +8, +0x18, as it does)
        return;
    }
    reinterpret_cast<void (__attribute__((thiscall)) *)(BYTE*)>(hot::fnPoolRelease)(pool);
}
void InstallExitFix() {
    bool hooked = false;
    ForEachImport("MSVCR80.dll", [&](void** slot, const char* nm) {
        if (strcmp(nm, "exit")) return;
        g_crtExit = reinterpret_cast<Exit_t>(*slot);
        hooked = PatchPointer(slot, reinterpret_cast<void*>(&HkExit));
    });
    bool ok = hooked;
    for (DWORD at : hot::callPoolRelease) ok = ok && PatchCall(at, reinterpret_cast<void*>(&HkPoolRelease), 5);
    if (!ok) Logf("exit fix: patch FAILED\n");
}
