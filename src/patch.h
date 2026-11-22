// What the other parts use from patch.cpp.
#pragma once
#include "common.h"

bool PatchBytes(DWORD at, const void* bytes, size_t len);
bool PatchCall(DWORD at, void* target, int len);
bool PatchJmp(DWORD at, DWORD target);
bool PatchPointer(void** slot, void* value);
template <typename Fn>
static void ForEachImport(const char* dll, Fn cb) {
    BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleA(nullptr));
    auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + reinterpret_cast<PIMAGE_DOS_HEADER>(base)->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return;
    for (auto imp = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + rva); imp->Name; ++imp) {
        if (_stricmp(reinterpret_cast<const char*>(base + imp->Name), dll)) continue;
        auto th = reinterpret_cast<PIMAGE_THUNK_DATA>(base + imp->FirstThunk);
        auto on = reinterpret_cast<PIMAGE_THUNK_DATA>(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk
                                                                                    : imp->FirstThunk));
        for (; th->u1.Function; ++th, ++on) {
            if (IMAGE_SNAP_BY_ORDINAL(on->u1.Ordinal)) continue;
            auto byName = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + on->u1.AddressOfData);
            cb(reinterpret_cast<void**>(&th->u1.Function), reinterpret_cast<const char*>(byName->Name));
        }
    }
}
bool CheckExe();
