#include "common.h"
#include "patch.h"
#include "game.h"
#include "log.h"

// ---------------------------------------------------------------------------
// Patch helpers
// ---------------------------------------------------------------------------
bool PatchBytes(DWORD at, const void* bytes, size_t len) {
    void* p = reinterpret_cast<void*>(at); DWORD old;
    if (!VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(p, bytes, len);
    DWORD tmp; VirtualProtect(p, len, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    return true;
}
bool PatchCall(DWORD at, void* target, int len) {     // E8 rel32, rest NOP
    BYTE b[8] = { 0xE8 };
    DWORD rel = reinterpret_cast<DWORD>(target) - (at + 5); memcpy(b + 1, &rel, 4);
    for (int i = 5; i < len; i++) b[i] = 0x90;
    return PatchBytes(at, b, len);
}
bool PatchJmp(DWORD at, DWORD target) {
    BYTE b[5] = { 0xE9 };
    DWORD rel = target - (at + 5); memcpy(b + 1, &rel, 4);
    return PatchBytes(at, b, 5);
}
bool PatchPointer(void** slot, void* value) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
    *slot = value;
    DWORD tmp; VirtualProtect(slot, sizeof(void*), old, &tmp);
    return true;
}
bool CheckExe() {
    for (const auto& s : hot::kSigs) {
        if (IsBadReadPtr(reinterpret_cast<void*>(s.addr), s.len) ||
            memcmp(reinterpret_cast<void*>(s.addr), s.bytes, s.len) != 0) {
            Logf("exe check: bytes at 0x%06lX differ - not the analysed HOT.exe, or already patched (HWBTFix\n"
                 "  loaded as well?): all changes off\n",
                 (unsigned long)s.addr);
            return false;
        }
    }
    Logf("exe check: HOT.exe matches\n");
    return true;
}
