#include "common.h"
#include "saves.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"

// ---------------------------------------------------------------------------
// Save folder
// ---------------------------------------------------------------------------
static char g_saveDir[MAX_PATH] = {};    // full path, trailing backslash
static char g_oldSaveDir[MAX_PATH] = {}; // where v0.1 put it (next to HOT.exe), trailing backslash

static void MakeDirs(const char* path) {
    char tmp[MAX_PATH]; lstrcpynA(tmp, path, MAX_PATH);
    size_t n = strlen(tmp);
    for (size_t i = 3; i < n; i++)
        if (tmp[i] == '\\') { tmp[i] = 0; CreateDirectoryA(tmp, nullptr); tmp[i] = '\\'; }
    CreateDirectoryA(tmp, nullptr);
}
static bool DirHasFiles(const char* dir) {
    char pat[MAX_PATH]; snprintf(pat, sizeof(pat), "%s*", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool any = false;
    do { if (strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, "..")) any = true; } while (!any && FindNextFileA(h, &fd));
    FindClose(h);
    return any;
}
static int CopyTree(const char* src, const char* dst, int depth) {   // both with trailing backslash
    if (depth > 4) return 0;
    char pat[MAX_PATH]; snprintf(pat, sizeof(pat), "%s*", src);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char s2[MAX_PATH], d2[MAX_PATH];
        snprintf(s2, sizeof(s2), "%s%s", src, fd.cFileName); snprintf(d2, sizeof(d2), "%s%s", dst, fd.cFileName);
        if (strlen(s2) + 2 >= MAX_PATH || strlen(d2) + 2 >= MAX_PATH) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CreateDirectoryA(d2, nullptr);
            lstrcatA(s2, "\\"); lstrcatA(d2, "\\");
            n += CopyTree(s2, d2, depth + 1);
        } else if (CopyFileA(s2, d2, TRUE)) n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}
// Called (game thread, at startup) instead of SHGetSpecialFolderLocation in the
// path setup 0x40E280, with ebx = the path table: write our folder into slot 1,
// create it, bring old saves over once. The stub returns non-zero, so the game
// skips its own "Documents\Hot Wheels" code.
extern "C" void __cdecl SetSavePath(BYTE* table) {
    MakeDirs(g_saveDir);
    if (!DirHasFiles(g_saveDir) && g_oldSaveDir[0] && DirHasFiles(g_oldSaveDir))     // v0.1 kept them in Bin
        Logf("save folder: copied %d file(s) from %s\n", CopyTree(g_oldSaveDir, g_saveDir, 0), g_oldSaveDir);
    if (!DirHasFiles(g_saveDir)) {
        typedef HRESULT (WINAPI *SHGetFolderPathA_t)(HWND, int, HANDLE, DWORD, LPSTR);
        HMODULE sh = GetModuleHandleA("shell32.dll");
        auto get = sh ? reinterpret_cast<SHGetFolderPathA_t>(reinterpret_cast<void*>(GetProcAddress(sh, "SHGetFolderPathA")))
                      : nullptr;
        char docs[MAX_PATH] = {};
        if (get && SUCCEEDED(get(nullptr, 5 /* CSIDL_PERSONAL */, nullptr, 0, docs))) {
            char old[MAX_PATH]; snprintf(old, sizeof(old), "%s\\Hot Wheels\\", docs);
            if (DirHasFiles(old)) Logf("save folder: copied %d file(s) from %s\n", CopyTree(old, g_saveDir, 0), old);
        }
    }
    lstrcpynA(reinterpret_cast<char*>(table + hot::saveSlotOffset), g_saveDir, 0x104);
    Logf("save folder: %s\n", g_saveDir);
}
extern "C" void SaveFolderStub();
asm(".text\n"
    ".globl _SaveFolderStub\n"
    "_SaveFolderStub:\n"                  // replaces call [SHGetSpecialFolderLocation] (3 stdcall args)
    "    pushl %ebx\n"
    "    call _SetSavePath\n"
    "    addl $4, %esp\n"
    "    movl $1, %eax\n"                  // "failed": the game skips its own folder code
    "    ret $12\n");

void InstallSaveFolder() {
    if (!g_cfg.saveFolder[0]) { Logf("save folder: game default (Documents\\Hot Wheels)\n"); return; }
    const char* f = g_cfg.saveFolder;
    bool absolute = (f[0] && f[1] == ':') || (f[0] == '\\' && f[1] == '\\');
    snprintf(g_saveDir, sizeof(g_saveDir), "%s%s", absolute ? "" : g_rootDir, f);
    for (char* p = g_saveDir; *p; p++) if (*p == '/') *p = '\\';
    size_t n = strlen(g_saveDir);
    if (n == 0 || n > 0x104 - 32) { Logf("save folder: path too long - game default kept\n"); g_saveDir[0] = 0; return; }
    if (g_saveDir[n - 1] != '\\') { g_saveDir[n] = '\\'; g_saveDir[n + 1] = 0; }
    if (!absolute) {
        snprintf(g_oldSaveDir, sizeof(g_oldSaveDir), "%s%s", g_gameDir, g_saveDir + strlen(g_rootDir));
        if (!_stricmp(g_oldSaveDir, g_saveDir)) g_oldSaveDir[0] = 0;
    }
    if (!PatchCall(hot::callDocsFolder, reinterpret_cast<void*>(&SaveFolderStub), 6))
        Logf("save folder: patch FAILED - game default kept\n");
}
