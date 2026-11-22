#include "common.h"
#include "display.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"

// ---------------------------------------------------------------------------
// Display settings: the game's registry key answered from the ini
// ---------------------------------------------------------------------------
typedef LONG (WINAPI *RegOpenKeyExA_t)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
typedef LONG (WINAPI *RegQueryValueExA_t)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LONG (WINAPI *RegCloseKey_t)(HKEY);
static RegOpenKeyExA_t    g_oRegOpenKeyExA = nullptr;
static RegQueryValueExA_t g_oRegQueryValueExA = nullptr;
static RegCloseKey_t      g_oRegCloseKey = nullptr;
static HKEY const kFakeKey = reinterpret_cast<HKEY>(0x7E570001);
static const char kGameKey[] = "SOFTWARE\\Activision Value\\Hot Wheels";

int ClampInt(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static bool VDword(LPCSTR name, DWORD* out) {
    if (!name) return false;
    if (!_stricmp(name, "ScreenResW"))         { *out = (DWORD)g_cfg.width; return true; }
    if (!_stricmp(name, "ScreenResH"))         { *out = (DWORD)g_cfg.height; return true; }
    if (!_stricmp(name, "ModelDetail"))        { *out = (DWORD)ClampInt(g_cfg.modelDetail, 0, 2); return true; }
    if (!_stricmp(name, "DrawDistance"))       { *out = (DWORD)ClampInt(g_cfg.drawDistance, 0, 2); return true; }
    if (!_stricmp(name, "ShadowDetail"))       { *out = (DWORD)ClampInt(g_cfg.shadowDetail, 0, 3); return true; }
    if (!_stricmp(name, "Particles"))          { *out = (DWORD)ClampInt(g_cfg.particles, 0, 1); return true; }
    if (!_stricmp(name, "PostProcessEffects")) { *out = (DWORD)ClampInt(g_cfg.postProcessEffects, 0, 1); return true; }
    return false;
}
static LONG WINAPI HkRegOpenKeyExA(HKEY root, LPCSTR sub, DWORD o, REGSAM sam, PHKEY out) {
    if (root == HKEY_LOCAL_MACHINE && sub && !_stricmp(sub, kGameKey)) {
        if (!out) return ERROR_INVALID_PARAMETER;
        *out = kFakeKey;
        return ERROR_SUCCESS;
    }
    return g_oRegOpenKeyExA(root, sub, o, sam, out);
}
static LONG WINAPI HkRegQueryValueExA(HKEY h, LPCSTR name, LPDWORD r, LPDWORD type, LPBYTE data, LPDWORD cb) {
    if (h != kFakeKey) return g_oRegQueryValueExA(h, name, r, type, data, cb);
    DWORD v;
    if (VDword(name, &v)) {
        if (type) *type = REG_DWORD;
        if (!cb) return data ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
        if (!data) { *cb = 4; return ERROR_SUCCESS; }
        if (*cb < 4) { *cb = 4; return ERROR_MORE_DATA; }
        memcpy(data, &v, 4); *cb = 4;
        return ERROR_SUCCESS;
    }
    if (name && !_stricmp(name, "InstallDir")) {        // only used to find DisplayConfig.exe
        DWORD need = (DWORD)strlen(g_gameDir) + 1;
        if (type) *type = REG_SZ;
        if (!cb) return data ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
        if (!data) { *cb = need; return ERROR_SUCCESS; }
        if (*cb < need) { *cb = need; return ERROR_MORE_DATA; }
        memcpy(data, g_gameDir, need); *cb = need;
        return ERROR_SUCCESS;
    }
    return ERROR_FILE_NOT_FOUND;                         // e.g. "Installed": the game never uses it
}
static LONG WINAPI HkRegCloseKey(HKEY h) {
    if (h == kFakeKey) return ERROR_SUCCESS;
    return g_oRegCloseKey(h);
}

void InstallDisplaySettings() {
    if (g_cfg.width <= 0 || g_cfg.height <= 0) {
        g_cfg.width = GetSystemMetrics(SM_CXSCREEN);
        g_cfg.height = GetSystemMetrics(SM_CYSCREEN);
    }
    if (g_cfg.width < 640) g_cfg.width = 640;
    if (g_cfg.height < 480) g_cfg.height = 480;
    // any resolution: skip the 1024x768 .. 1600x1200 clamp, keep the detail clamps
    bool ok = PatchJmp(hot::resClamp, hot::resClampDone);
    // never launch DisplayConfig.exe at exit (Options > Display settings just quits)
    const BYTE nop5[5] = { 0x90, 0x90, 0x90, 0x90, 0x90 };
    ok &= PatchBytes(hot::callDisplayCfg, nop5, 5);
    if (!g_cfg.portableRegistry) {
        Logf("display: %s; settings from the Windows registry (portableRegistry=0)\n", ok ? "resolution unclamped" : "patch FAILED");
        return;
    }
    void* slots[3] = {};
    ForEachImport("ADVAPI32.dll", [&](void** slot, const char* nm) {
        if      (!strcmp(nm, "RegOpenKeyExA"))    slots[0] = slot;
        else if (!strcmp(nm, "RegQueryValueExA")) slots[1] = slot;
        else if (!strcmp(nm, "RegCloseKey"))      slots[2] = slot;
    });
    if (!slots[0] || !slots[1] || !slots[2]) { Logf("display: registry imports not found - Windows registry used\n"); return; }
    g_oRegOpenKeyExA    = *reinterpret_cast<RegOpenKeyExA_t*>(slots[0]);
    g_oRegQueryValueExA = *reinterpret_cast<RegQueryValueExA_t*>(slots[1]);
    g_oRegCloseKey      = *reinterpret_cast<RegCloseKey_t*>(slots[2]);
    ok &= PatchPointer(reinterpret_cast<void**>(slots[0]), reinterpret_cast<void*>(&HkRegOpenKeyExA));
    ok &= PatchPointer(reinterpret_cast<void**>(slots[1]), reinterpret_cast<void*>(&HkRegQueryValueExA));
    ok &= PatchPointer(reinterpret_cast<void**>(slots[2]), reinterpret_cast<void*>(&HkRegCloseKey));
    Logf("display: %dx%d, model %d, draw distance %d, shadows %d, particles %d, post-processing %d%s\n",
         g_cfg.width, g_cfg.height, g_cfg.modelDetail, g_cfg.drawDistance, g_cfg.shadowDetail,
         g_cfg.particles, g_cfg.postProcessEffects, ok ? "" : " (a patch FAILED)");
}
