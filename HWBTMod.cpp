// HWBTFix.asi  v0.3.8
// Fixes for Hot Wheels: Beat That! (PC, HOT.exe)
//
// Analysed exe: HOT.exe SHA-1 0c6c388cd9a8326bfea4da56049ad58a270d4830
// (PE timestamp 0x4721CA06, image base 0x400000, no ASLR). Every patch site is
// byte-checked first; if anything differs the ASI leaves the game alone.
//
// What the game does:
//  - Display settings (resolution, model/shadow detail, draw distance,
//    particles, post-processing) are only read from the registry,
//    HKLM\SOFTWARE\Activision Value\Hot Wheels (key opened at 0x40CABB, values
//    read by 0x54FA90 as DWORDs). The external DisplayConfig.exe writes them.
//    Without the key the game runs at its defaults (1280x720 -> 1280x768).
//  - The resolution is then clamped to 1024x768 .. 1600x1200 (0x54FB30).
//  - Options > Display settings ("EXITTODISPLAYSETTINGS") sets a flag; at exit
//    the game reads InstallDir from the registry and execs DisplayConfig.exe
//    (0x40C1C0, called at 0x40C666).
//  - Saves and Settings.cfg go to My Documents\Hot Wheels\ (path table slot 1,
//    built in 0x40E280 with SHGetSpecialFolderLocation at 0x40E3E1).
//
// What this ASI does:
//  - portableRegistry: the game's registry key is answered from HWBTFix.ini
//    (width/height/detail keys); nothing is read from or written to the
//    Windows registry for it. Other keys (CPU info) are untouched.
//  - width/height: any resolution (0 = desktop resolution); the 1024..1600
//    clamp is removed.
//  - saveFolder: saves and Settings.cfg in a folder in the game root, above
//    Bin (default "save"); existing ones are copied over from Documents on first run.
//  - DisplayConfig.exe is never launched (settings are in the ini).
//  - interpolate/fpsLimit: frames above 60 fps, with cars, cameras and other
//    moving objects drawn between their last two 60 Hz simulation steps (see
//    "Frame rate"); effects, shadows and trails that follow a car included.
//  - closeExits: closing the window quits the game cleanly (like Alt+F4).
//  - raceMusic: each race plays its own song, results music plays (the PC
//    port's song-name lookup was a stub).
//  - gameLog: diagnostics - the game's own debug messages (an empty function in
//    the release build) and failed sound lookups to HWBTFix_game.log.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cmath>
#include <mmsystem.h>

#define HWBT_VERSION "0.3.8"

// ---------------------------------------------------------------------------
// Game addresses
// ---------------------------------------------------------------------------
namespace hot {
    const DWORD callDocsFolder = 0x40E3E1;   // call [SHGetSpecialFolderLocation] (6 bytes), ebx = path table
    const DWORD saveSlotOffset = 0x20C;      // path table slot 1 (0x104 bytes): saves + Settings.cfg
    const DWORD resClamp       = 0x54FB30;   // cmp [ecx], 0x400 (6 bytes): start of the 1024..1600 x 768..1200 clamp
    const DWORD resClampDone   = 0x54FB66;   // detail clamps that follow
    const DWORD callDisplayCfg = 0x40C666;   // call 0x401695 -> exec DisplayConfig.exe at exit
    // frame rate (see "Frame rate" below)
    const DWORD callLimiter    = 0x40EC1A;   // call 0x58DE90: frame limiter (thiscall, ecx = limiter)
    const DWORD fnLimiter      = 0x58DE90;
    const DWORD forceOneStep   = 0x5A23E9;   // jne 0x5A23F9; falling through makes a 0-step frame 1 step
    const DWORD callStepTail   = 0x40EC58;   // call 0x4210B0: last call of a 60 Hz step
    const DWORD fnStepTail     = 0x4210B0;
    const DWORD callRender3D   = 0x40EC98;   // call 0x421160: the 3D pass
    const DWORD fnRender3D     = 0x421160;
    const DWORD callFrameEnd   = 0x40ECB0;   // call 0x54DC30 (thiscall, renderer): after the 2D pass and the flush
    const DWORD fnFrameEnd     = 0x54DC30;
    const DWORD carSync        = 0x5FB260;   // car -> physics bodies, once per car per step
    const DWORD camApply       = 0x5BA4C0;   // Camera::Apply (sets the view from camera+0x10)
    const DWORD vehDraw        = 0x604B20;   // GSVehicle draw (vtable slot 3 of VehicleMover, via GSVehicle_c)
    const DWORD setWorld       = 0x53C860;   // renderer: set world matrix, cdecl(matrix*) (sub esp,0x40 / mov eax,[esp+0x44])
    const DWORD getXform       = 0x416270;   // GS object vtable slot 14: lea eax,[ecx+0x10] / ret (+ int3 padding)
    const DWORD stepAccum      = 0x960B28;   // float: time left over after this frame's steps
    const DWORD stepDt         = 0x935898;   // float: 1/60
    const DWORD fixedStepOn    = 0x9374AC;   // DWORD: 1 = fixed steps (always, in this exe)
    const DWORD getMainWnd     = 0x703906;   // jmp [MFC80 #1091]: the main CWnd* (hwnd at +0x20), as used at 0x40C46E
    const DWORD quitFlag       = 0x953A3C;   // DWORD: 1 = quit (the game's own Alt+F4 handler sets it, 0x40C3E7)
    const DWORD debugPrint     = 0x5890F0;   // the game's debug printf (cdecl, varargs): an empty ret in release
    const DWORD soundFind      = 0x5C36B0;   // Sound::Find(bank, name, report) thiscall: sample by name in a sound bank
    // music
    const DWORD getTrackName   = 0x538B20;   // Music::GetTrackName(i): PC stub, always "track1"
    const DWORD musicPlayer    = 0x953A60;   // PCMusicPlayer singleton
    const DWORD playByNameCmp  = 0x538F91;   // Music::PlayTrack(name) loop: the same stub inlined, compares with "track1"
    const DWORD playByNameNext = 0x538FB0;   //   no match: next song
    const DWORD playByNameHit  = 0x538FB5;   //   match: play song ebx
    const DWORD raceMusicMiss  = 0x47C5E6;   // push 0 / call 0x538DD0: no song matched the race - play song 0
    const DWORD fnPlaySong     = 0x538DD0;   // Music::Play(index), cdecl (caller pops)
    // things drawn with a race car (see "Effects and shadows")
    const DWORD callVehLights  = 0x604C4A;   // call 0x6029B0 in the vehicle draw: headlight projector from vehicle+0x10
    const DWORD fnVehLights    = 0x6029B0;   //   (thiscall, ecx = vehicle, no arguments)
    const DWORD fnCarShadow    = 0x602F40;   // shadow light from vehicle+0x10 (thiscall, ecx = vehicle, 2 arguments)
    const DWORD callCarShadow[3] = { 0x455D12, 0x455D2F, 0x455D46 };   // its calls in GSRaceCar slot 125 (0x455C40)
    const DWORD callCarTrails  = 0x459094;   // call 0x4BED80 in the race-car draw (thiscall, ecx = car+0x1070)
    const DWORD fnCarTrails    = 0x4BED80;
    const DWORD trailFade      = 0x95A308;   // float: trail fade per call of 0x4BE580, set once (static)
    const DWORD trailFadeInit  = 0x95A30C;   // bit 0: trailFade is set
    // GSBaseEffect getters for what an effect follows: its node (+0x14) or object (+0x10 / +0x18)
    const DWORD effPos         = 0x425D30;   // slot 1: position (node, object, path, own)
    const DWORD effPosObj18    = 0x425E20;   // position of the +0x18 object, else slot 1
    const DWORD effPosNode     = 0x425E50;   // position of the node, else slot 1
    const DWORD effPosObj10    = 0x425EB0;   // position of the +0x10 object, else slot 1
    const DWORD effRow2        = 0x425F00;   // slot 3: matrix row 2 (bool)
    const DWORD effRow1        = 0x425FA0;   // slot 4: row 1 (bool)
    const DWORD effRow0        = 0x426040;   // slot 5: row 0 (bool)
    const DWORD effRows        = 0x4260E0;   // slot 6: rows 0-2 (bool)
    const DWORD garageLift     = 0x4EE61F;   // fld [ebp+0x97C]: car select/garage car's ride height, added to its node
                                             //   (ebp = GSHotGarageState; eax = the node's world, used right after)
    struct Sig { DWORD addr; const char* bytes; int len; };
    const Sig kSigs[] = {
        { callDocsFolder, "\xFF\x15\x20\x74\x97\x00", 6 },
        { 0x40E3E7,       "\x85\xC0\x0F\x85\xA8\x00\x00\x00", 8 },   // test eax,eax / jne 0x40E497
        { resClamp,       "\x81\x39\x00\x04\x00\x00", 6 },
        { resClampDone,   "\x8B\x41\x08", 3 },                       // mov eax, [ecx+8]
        { callDisplayCfg, "\xE8\x2A\x50\xFF\xFF", 5 },
        { 0x40CAB1,       "\x68\xC0\x9C\x89\x00", 5 },               // push "SOFTWARE\Activision Value\Hot Wheels"
        { 0x54FAA3,       "\x68\xB4\x3F\x8B\x00", 5 },               // push "ScreenResW"
        { callLimiter,    "\xE8\x71\xF2\x17\x00", 5 },               // call 0x58DE90 (frame limiter)
        { callStepTail,   "\xE8\x53\x24\x01\x00", 5 },               // call 0x4210B0 (end of a step)
        { callRender3D,   "\xE8\xC3\x24\x01\x00", 5 },               // call 0x421160 (3D pass)
        { callFrameEnd,   "\xE8\x7B\xEF\x13\x00", 5 },               // call 0x54DC30
        { soundFind,      "\x53\x55\x56\x57\x8B\x7C\x24\x14", 8 }, // push ebx/ebp/esi/edi; mov edi,[esp+0x14]
        { debugPrint,     "\xC3\xCC\xCC\xCC\xCC", 5 },               // ret + padding
        { forceOneStep,   "\x75\x0E", 2 },                           // jne 0x5A23F9
        { carSync,        "\x81\xEC\xC4\x00\x00\x00", 6 },           // sub esp, 0xC4
        { camApply,       "\x81\xEC\x80\x00\x00\x00", 6 },           // sub esp, 0x80
        { vehDraw,        "\x81\xEC\x08\x01\x00\x00", 6 },           // sub esp, 0x108
        { raceMusicMiss,  "\x6A\x00\xE8\xE3\xC7\x0B\x00", 7 },
        { getTrackName,   "\x68\x68\x35\x8B\x00", 5 },               // push "Music::GetTrackName not implemented..."
        { playByNameCmp,  "\x68\x68\x35\x8B\x00\xE8\x55\x01\x05\x00\x83\xC4\x04\x8B\xFD\xBE\x60\x35\x8B\x00"
                          "\xB9\x07\x00\x00\x00\x33\xD2\xF3\xA6\x74\x05\x83\xC3\x01\xEB\x8B", 36 },
        { setWorld,       "\x83\xEC\x40\x8B\x44\x24\x44", 7 },       // sub esp,0x40 / mov eax,[esp+0x44]
        { getXform,       "\x8D\x41\x10\xC3\xCC", 5 },               // lea eax,[ecx+0x10] / ret / int3
        { callVehLights,  "\xE8\x61\xDD\xFF\xFF", 5 },               // call 0x6029B0
        { 0x455D12,       "\xE8\x29\xD2\x1A\x00", 5 },               // call 0x602F40 (x3)
        { 0x455D2F,       "\xE8\x0C\xD2\x1A\x00", 5 },
        { 0x455D46,       "\xE8\xF5\xD1\x1A\x00", 5 },
        { callCarTrails,  "\xE8\xE7\x5C\x06\x00", 5 },               // call 0x4BED80
        { 0x4BE59A,       "\xF6\x05\x0C\xA3\x95\x00\x01", 7 },       // test byte [trailFadeInit], 1
        { 0x4BE5B6,       "\xD9\x1D\x08\xA3\x95\x00", 6 },           // fstp [trailFade]
        { effPos,         "\x81\xEC\x80\x00\x00\x00", 6 },           // sub esp, 0x80
        { effPosObj18,    "\x8B\x41\x18\x85\xC0\x74\x18", 7 },       // mov eax,[ecx+0x18] / test / je
        { effPosNode,     "\x8B\x41\x14\x81\xEC\x80\x00\x00\x00", 9 },
        { effPosObj10,    "\x8B\x41\x10\x85\xC0\x74\x18", 7 },
        { effRow2,        "\x8B\x41\x14\x81\xEC\x80\x00\x00\x00", 9 },
        { effRow1,        "\x8B\x41\x14\x81\xEC\x80\x00\x00\x00", 9 },
        { effRow0,        "\x8B\x41\x14\x81\xEC\x80\x00\x00\x00", 9 },
        { effRows,        "\x81\xEC\x80\x00\x00\x00", 6 },
        { garageLift,     "\xD9\x85\x7C\x09\x00\x00", 6 },       // fld dword ptr [ebp+0x97C]
    };
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
struct Config {
    int  width = 0, height = 0;          // 0 = desktop resolution
    int  modelDetail = 2;                // 0..2
    int  drawDistance = 2;               // 0..2
    int  shadowDetail = 3;               // 0..3
    int  particles = 1;                  // 0..1
    int  postProcessEffects = 1;         // 0..1
    char saveFolder[MAX_PATH] = "save";  // relative to the game root (above Bin), or a full path; empty = Documents
    int  portableRegistry = 1;
    int  interpolate = 1;                // 1 = above 60 fps with in-between frames
    int  fpsLimit = 0;                   // 0 = refresh rate
    int  closeExits = 1;                 // closing the window quits like Alt+F4
    int  raceMusic = 1;                  // each race plays its own song (the PC port always played song 0)
    int  logToFile = 1;
    int  gameLog = 0;                    // diagnostics: the game's own debug messages to HWBTFix_game.log
};
static Config g_cfg;
static char g_asiDir[MAX_PATH] = {};     // trailing backslash
static char g_gameDir[MAX_PATH] = {};    // HOT.exe's folder (Bin), trailing backslash
static char g_rootDir[MAX_PATH] = {};    // the game's root (parent of Bin, with Data, videos...), trailing backslash
static bool g_knownExe = false;

static void InitDirs() {
    HMODULE self = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&InitDirs), &self) &&
        GetModuleFileNameA(self, g_asiDir, MAX_PATH)) {
        char* s = strrchr(g_asiDir, '\\'); if (s) s[1] = 0; else g_asiDir[0] = 0;
    }
    if (GetModuleFileNameA(nullptr, g_gameDir, MAX_PATH)) {
        char* s = strrchr(g_gameDir, '\\'); if (s) s[1] = 0; else g_gameDir[0] = 0;
    }
    // root = the folder above Bin (the game finds its data as ..\Data from Bin)
    lstrcpynA(g_rootDir, g_gameDir, MAX_PATH);
    size_t n = strlen(g_rootDir);
    if (n > 1) {
        g_rootDir[n - 1] = 0;                                  // drop the trailing backslash
        char* s = strrchr(g_rootDir, '\\');
        if (s && s >= g_rootDir + 2) s[1] = 0; else lstrcpynA(g_rootDir, g_gameDir, MAX_PATH);
    }
}

static const char kIniText[] =
    "; HWBTFix.ini (v" HWBT_VERSION ") - Hot Wheels: Beat That!\n"
    "; --- display (replaces DisplayConfig.exe) ---\n"
    "; resolution; 0 = desktop resolution\n"
    "width=0\n"
    "height=0\n"
    "; model detail 0-2, draw distance 0-2, shadow detail 0-3, particles 0-1, post-processing 0-1\n"
    "modelDetail=2\n"
    "drawDistance=2\n"
    "shadowDetail=3\n"
    "particles=1\n"
    "postProcessEffects=1\n"
    "; --- portable ---\n"
    "; folder for saves and Settings.cfg, relative to the game's root folder (the one with Bin and\n"
    ";   Data) or a full path; empty = the game's default (Documents\\Hot Wheels)\n"
    "saveFolder=save\n"
    "; answer the game's registry key from this file (1) or use the Windows registry (0)\n"
    "portableRegistry=1\n"
    "; --- frame rate ---\n"
    "; the game runs its simulation at 60 Hz; interpolate=1 draws in-between frames, so it runs\n"
    ";   smoothly above 60 fps. 0 = original (locked to 60 fps)\n"
    "interpolate=1\n"
    "; frame cap with interpolate=1: 0 = monitor refresh rate, or a number (30-1000)\n"
    "fpsLimit=0\n"
    "; closing the window (title bar, window manager, taskbar) quits the game the way Alt+F4 does\n"
    "closeExits=1\n"
    "; each race plays its own music and the results screen gets its music, as on consoles (1),\n"
    ";   or the PC original: always the same song, no results music (0)\n"
    "raceMusic=1\n"
    "logToFile=1\n"
    "; diagnostics: the game's own debug messages (errors, missing files, unfinished PC code) and\n"
    ";   failed sound lookups to HWBTFix_game.log, and frame-rate statistics to this log\n"
    "gameLog=0\n";

static void LoadConfig() {
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%sHWBTFix.ini", g_asiDir);
    FILE* f = fopen(path, "r");
    if (!f) {
        if ((f = fopen(path, "w"))) { fputs(kIniText, f); fclose(f); }
        return;
    }
    char line[MAX_PATH + 64];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = line; while (*key == ' ' || *key == '\t') key++;
        char* ke = eq; while (ke > key && (ke[-1] == ' ' || ke[-1] == '\t')) *--ke = 0;
        char* val = eq + 1; while (*val == ' ' || *val == '\t') val++;
        if (char* c = strchr(val, ';')) *c = 0;                     // trailing comment
        char* ve = val + strlen(val);
        while (ve > val && (ve[-1] == '\r' || ve[-1] == '\n' || ve[-1] == ' ' || ve[-1] == '\t')) *--ve = 0;
        int n = atoi(val);
        if      (!_stricmp(key, "width"))              g_cfg.width = n;
        else if (!_stricmp(key, "height"))             g_cfg.height = n;
        else if (!_stricmp(key, "modelDetail"))        g_cfg.modelDetail = n;
        else if (!_stricmp(key, "drawDistance"))       g_cfg.drawDistance = n;
        else if (!_stricmp(key, "shadowDetail"))       g_cfg.shadowDetail = n;
        else if (!_stricmp(key, "particles"))          g_cfg.particles = n;
        else if (!_stricmp(key, "postProcessEffects")) g_cfg.postProcessEffects = n;
        else if (!_stricmp(key, "saveFolder"))         lstrcpynA(g_cfg.saveFolder, val, MAX_PATH);
        else if (!_stricmp(key, "portableRegistry"))   g_cfg.portableRegistry = n;
        else if (!_stricmp(key, "interpolate"))        g_cfg.interpolate = n;
        else if (!_stricmp(key, "fpsLimit"))           g_cfg.fpsLimit = n;
        else if (!_stricmp(key, "closeExits"))         g_cfg.closeExits = n;
        else if (!_stricmp(key, "raceMusic"))          g_cfg.raceMusic = n;
        else if (!_stricmp(key, "gameLog"))            g_cfg.gameLog = n;
        else if (!_stricmp(key, "logToFile"))          g_cfg.logToFile = n;
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
static FILE* g_log = nullptr;
static CRITICAL_SECTION g_logLock;
static bool g_logLockInit = false;
static void Logf(const char* fmt, ...) {
    if (!g_log) return;
    if (g_logLockInit) EnterCriticalSection(&g_logLock);
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fflush(g_log);
    if (g_logLockInit) LeaveCriticalSection(&g_logLock);
}

// ---------------------------------------------------------------------------
// Patch helpers
// ---------------------------------------------------------------------------
static bool PatchBytes(DWORD at, const void* bytes, size_t len) {
    void* p = reinterpret_cast<void*>(at); DWORD old;
    if (!VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(p, bytes, len);
    DWORD tmp; VirtualProtect(p, len, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    return true;
}
static bool PatchCall(DWORD at, void* target, int len) {     // E8 rel32, rest NOP
    BYTE b[8] = { 0xE8 };
    DWORD rel = reinterpret_cast<DWORD>(target) - (at + 5); memcpy(b + 1, &rel, 4);
    for (int i = 5; i < len; i++) b[i] = 0x90;
    return PatchBytes(at, b, len);
}
static bool PatchJmp(DWORD at, DWORD target) {
    BYTE b[5] = { 0xE9 };
    DWORD rel = target - (at + 5); memcpy(b + 1, &rel, 4);
    return PatchBytes(at, b, 5);
}
static bool PatchPointer(void** slot, void* value) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
    *slot = value;
    DWORD tmp; VirtualProtect(slot, sizeof(void*), old, &tmp);
    return true;
}
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
static bool CheckExe() {
    for (const auto& s : hot::kSigs) {
        if (IsBadReadPtr(reinterpret_cast<void*>(s.addr), s.len) ||
            memcmp(reinterpret_cast<void*>(s.addr), s.bytes, s.len) != 0) {
            Logf("exe check: bytes at 0x%06lX differ - not the analysed HOT.exe, all fixes off\n",
                 (unsigned long)s.addr);
            return false;
        }
    }
    Logf("exe check: HOT.exe matches\n");
    return true;
}

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

static void InstallSaveFolder() {
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

static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
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

static void InstallDisplaySettings() {
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

// ---------------------------------------------------------------------------
// Frame rate: pacing + interpolation
//
// The game simulates in fixed 1/60 s steps (0x5A2360 turns the frame time into
// a step count, keeping the remainder in an accumulator) but forces at least one
// step per frame and caps the frame rate at 60 with a Sleep(0) spin (0x58DE90).
// With interpolate=1:
//  - frames with no step are allowed (0x5A23E9), the frame cap is ours (fpsLimit)
//    and waits with Sleep(1) + a short spin instead of Sleep(0)
//  - after every step the world matrices (object+0x10) of cameras and drawn
//    vehicles, and of other matrices the 3D pass uses, are copied; before the
//    3D pass each one is set to a blend of the last two steps, by how far the
//    accumulator is into the next step, and put back when the frame is drawn.
//    Cameras are found through Camera::Apply (0x5BA4C0), vehicles through their
//    draw (0x604B20), other matrices through SetWorld/GetTransform.
//  - an object is only blended if its matrix still holds exactly what was copied
//    at the step, and only put back if it still holds exactly the blend, so
//    anything else that moves it is left alone (and counted in the log).
//  - matrices the drawing code computes itself (scene-graph nodes: race cars and
//    their wheels) are blended as they're handed to SetWorld instead.
// ---------------------------------------------------------------------------
typedef void (__cdecl *VoidFn_t)();
typedef void (__attribute__((thiscall)) *Limiter_t)(void*);

struct Track {
    BYTE* obj;            // nullptr = free
    void* vt;             // vtable when first seen: a destroyed object fails this check
    int   kind;           // 1 race car (physics), 2 camera or other drawn object
    LONG  seenStep;       // car: step whose physics sync saw it
    LONG  seenFrame;      // camera: frame whose 3D pass applied it
    LONG  snapStep;       // step of the last copy
    int   n;              // copies in a row (0..2)
    bool  written;        // blend is in the object right now
    bool  outOk;          // race car: out holds this frame's blend (outFrame)
    LONG  outFrame;       // race car: frame out was computed for
    LONG  trailStep;      // race car: step of its last tyre-trail draw
    float prev[16], cur[16], out[16];
};
static Track g_tr[64];
static LONG  g_stepsDone = 0, g_frameNo = 0;
static bool  g_inRender = false;
static bool  g_interpOn = false;              // frame-rate hooks installed
static struct { LONG frames, steps, blended, external, redrawn, cuts, camOutside, full,
                 mBlended, mExternal, mRedrawn, mCuts, mFull, vehDrawn, vehOutside, fBlended; } g_st;
static DWORD g_statTick = 0;
static const float kCutDist = 25.0f;     // a jump this long in one step is a cut/respawn: no blend

extern "C" {
    DWORD g_carSyncCont = hot::carSync + 6;
    DWORD g_camApplyCont = hot::camApply + 6;
    DWORD g_setWorldCont = hot::setWorld + 7;
    DWORD g_vehDrawCont = hot::vehDraw + 6;
}

static Track* FindTrack(BYTE* obj, int kind) {
    void* vt = *reinterpret_cast<void**>(obj);
    Track* freeSlot = nullptr; Track* stale = nullptr;
    for (Track& t : g_tr) {
        if (t.obj == obj) {
            if (t.kind != kind || t.vt != vt) { t.kind = kind; t.vt = vt; t.n = 0; t.written = false; }
            return &t;
        }
        if (!t.obj) { if (!freeSlot) freeSlot = &t; }
        else if (!stale && t.seenStep < g_stepsDone - 120 && t.seenFrame < g_frameNo - 120) stale = &t;
    }
    Track* t = freeSlot ? freeSlot : stale;
    if (!t) { g_st.full++; return nullptr; }
    t->obj = obj; t->vt = vt; t->kind = kind; t->n = 0; t->written = false;
    t->seenStep = t->seenFrame = t->snapStep = t->outFrame = t->trailStep = LONG_MIN / 2;
    return t;
}
extern "C" void __cdecl RegCar(BYTE* car) {          // per car, per step, at the start of its physics sync
    if (!car) return;
    if (!g_interpOn) return;
    if (Track* t = FindTrack(car, 1)) t->seenStep = g_stepsDone + 1;
}
extern "C" void __cdecl RegCam(BYTE* cam) {          // per camera, per frame
    if (!cam) return;
    if (!g_inRender) g_st.camOutside++;
    if (Track* t = FindTrack(cam, 2)) t->seenFrame = g_frameNo;
}
// Vehicles drawn without physics (the car-select cars driving in and out are
// VehicleMovers): registered when the 3D pass draws them, like cameras. A race
// car (already tracked through its physics sync) is left as it is.
extern "C" void __cdecl RegDrawnVehicle(BYTE* veh) {
    if (!veh) return;
    if (!g_inRender) { g_st.vehOutside++; return; }
    g_st.vehDrawn++;
    for (Track& t : g_tr)
        if (t.obj == veh && t.kind == 1) return;
    if (Track* t = FindTrack(veh, 2)) t->seenFrame = g_frameNo;
}
extern "C" void CarSyncStub(); extern "C" void CamApplyStub(); extern "C" void VehDrawStub();
asm(".text\n"
    ".globl _CarSyncStub\n_CarSyncStub:\n"     // replaces sub esp,0xC4 at 0x5FB260 (thiscall, ecx = car)
    "    pushal\n"
    "    pushl %ecx\n"
    "    call _RegCar\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    subl $0xC4, %esp\n"
    "    jmp *_g_carSyncCont\n"
    ".globl _CamApplyStub\n_CamApplyStub:\n"   // replaces sub esp,0x80 at 0x5BA4C0 (thiscall, ecx = camera)
    "    pushal\n"
    "    pushl %ecx\n"
    "    call _RegCam\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    subl $0x80, %esp\n"
    "    jmp *_g_camApplyCont\n"
    ".globl _VehDrawStub\n_VehDrawStub:\n"     // replaces sub esp,0x108 at 0x604B20 (thiscall, ecx = vehicle)
    "    pushal\n"
    "    pushl %ecx\n"
    "    call _RegDrawnVehicle\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    subl $0x108, %esp\n"
    "    jmp *_g_vehDrawCont\n");

// Everything else drawn in the 3D pass (the car-select car, props, ...): any
// world matrix the pass reads through GSObject::GetTransform (slot 14, obj+0x10)
// or hands to the renderer's SetWorld (0x53C860), as long as it lives outside
// the stack. Tracked by address; same rules as above.
struct MTrack {
    float* p;
    LONG   seenFrame, snapStep, skipFrame;
    int    n;
    bool   written;
    BYTE   src;           // 1 SetWorld, 2 GetTransform
    DWORD  caller;        // diag: who handed it to SetWorld first
    LONG   redrawn, external;
    // computed while drawing (scene-graph nodes): history of the value handed to SetWorld
    LONG   fFrame, fStep; // frame and step of the last value taken
    int    fn;            // values in a row, one per step (0..2)
    LONG   fPerFrameStep; // last step it also changed between steps (animated per frame: left alone for a while)
    bool   drawnOnly;     // the drawing code rewrote it once: never blended in memory again (see HkFrameEnd)
    float  fprev[16], fcur[16];
    float  prev[16], cur[16], out[16];
};
// Changed between steps within the last half second: animated per frame (or a
// one-off move, e.g. a car swapped in on car select), so not blended. Not for
// good: a matrix's memory is reused by later objects.
static bool PerFrame(const MTrack* t) { return g_stepsDone - t->fPerFrameStep < 30; }
static const int kMaxM = 2048;
static MTrack g_mt[kMaxM];
static int    g_mtN = 0;
static short  g_mHash[4096];                       // index + 1, 0 = empty; at most half full
static DWORD  g_stackLo = 0, g_stackHi = 0;        // game thread's stack (set on the first 3D pass)

static int MHash(const float* p) { return (int)(((DWORD)(UINT_PTR)p >> 4) * 2654435761u >> 20); }
static MTrack* MFind(const float* p) {
    for (int h = MHash(p);; h = (h + 1) & 4095) {
        int i = g_mHash[h];
        if (!i) return nullptr;
        if (g_mt[i - 1].p == p) return &g_mt[i - 1];
    }
}
static void MRehash() {
    memset(g_mHash, 0, sizeof(g_mHash));
    for (int i = 0; i < g_mtN; i++) {
        int h = MHash(g_mt[i].p);
        while (g_mHash[h]) h = (h + 1) & 4095;
        g_mHash[h] = (short)(i + 1);
    }
}
// A world matrix the drawing code computes itself (a scene-graph node's world
// = parent x local, done while drawing, e.g. the car-select cars) can't be
// blended in memory beforehand. Instead the value handed to SetWorld is
// recorded once per frame; when it only ever changes on 60 Hz steps, SetWorld
// gets a blend of its last two step values. If it changes between steps too,
// it's animated per frame (or blended already) and passed through untouched.
static bool Blend(const float* p, const float* c, float a, float* o);
static float g_alpha = 1.0f;                       // this frame's blend factor (set before the 3D pass)
static const float* FilterWorld(MTrack* t, const float* v) {
    if (t->fFrame != g_frameNo) {                  // first time this frame
        t->fFrame = g_frameNo;
        if (t->fStep != g_stepsDone) {             // step(s) since the last value
            if (t->fStep == g_stepsDone - 1 && t->fn) { memcpy(t->fprev, t->fcur, 64); if (t->fn < 2) t->fn++; }
            else t->fn = 1;                        // a gap (or the first value): no neighbour yet
            memcpy(t->fcur, v, 64);
            t->fStep = g_stepsDone;
        } else if (memcmp(t->fcur, v, 64)) {       // changed without a step
            t->fPerFrameStep = g_stepsDone;
        }
    }
    if (PerFrame(t) || t->fn < 2 || memcmp(t->fcur, v, 64) || !memcmp(t->fprev, t->fcur, 64)) return v;
    static float out[4][16]; static int k = 0;
    float* o = out[k = (k + 1) & 3];
    if (!Blend(t->fprev, t->fcur, g_alpha, o)) { g_st.mCuts++; return v; }
    g_st.fBlended++;
    return o;
}
extern "C" const float* __cdecl RegMat(float* p, int src, DWORD caller) {
    if (!g_inRender || !p) return p;
    DWORD a = (DWORD)(UINT_PTR)p;
    if (a >= g_stackLo && a < g_stackHi) return p; // a temporary on the stack
    MTrack* t = MFind(p);
    if (!t) {
        if (g_mtN >= kMaxM) { g_st.mFull++; return p; }
        t = &g_mt[g_mtN++];
        t->p = p; t->n = 0; t->written = false; t->src = 0; t->caller = caller; t->redrawn = t->external = 0;
        t->snapStep = t->skipFrame = LONG_MIN / 2;
        t->fFrame = t->fStep = LONG_MIN / 2; t->fn = 0; t->fPerFrameStep = LONG_MIN / 2; t->drawnOnly = false;
        int h = MHash(p);
        while (g_mHash[h]) h = (h + 1) & 4095;
        g_mHash[h] = (short)g_mtN;
    }
    t->seenFrame = g_frameNo;
    t->src |= (BYTE)src;
    return src == 1 && !t->written ? FilterWorld(t, p) : p;     // (written: already blended in memory)
}
extern "C" void SetWorldStub(); extern "C" void GetXformStub();
asm(".text\n"
    ".globl _SetWorldStub\n_SetWorldStub:\n"   // replaces sub esp,0x40 / mov eax,[esp+0x44] at 0x53C860
    "    pushal\n"
    "    pushl 32(%esp)\n"                     // the caller (return address, above pushal)
    "    pushl $1\n"
    "    pushl 44(%esp)\n"                     // the matrix argument: 32 (pushal) + 4 (ret) + 8 (two pushes)
    "    call _RegMat\n"
    "    addl $12, %esp\n"
    "    movl %eax, 36(%esp)\n"                // the matrix SetWorld will use (the caller pops it unread)
    "    popal\n"
    "    subl $0x40, %esp\n"
    "    movl 0x44(%esp), %eax\n"
    "    jmp *_g_setWorldCont\n"
    ".globl _GetXformStub\n_GetXformStub:\n"   // replaces the whole getter at 0x416270 (thiscall)
    "    pushal\n"
    "    pushl 32(%esp)\n"                     // the caller
    "    pushl $2\n"
    "    leal 0x10(%ecx), %eax\n"
    "    pushl %eax\n"
    "    call _RegMat\n"
    "    addl $12, %esp\n"
    "    popal\n"
    "    leal 0x10(%ecx), %eax\n"
    "    ret\n");

// Last call of each 60 Hz step: copy the matrices of everything seen this step.
static void __cdecl HkStepTail() {
    reinterpret_cast<VoidFn_t>(hot::fnStepTail)();
    g_stepsDone++; g_st.steps++;
    for (Track& t : g_tr) {
        if (!t.obj) continue;
        bool live = t.kind == 1 ? t.seenStep == g_stepsDone : t.seenFrame == g_frameNo;
        if (!live) { t.n = 0; continue; }
        if (*reinterpret_cast<void**>(t.obj) != t.vt) { t.obj = nullptr; continue; }
        if (t.snapStep != g_stepsDone - 1) t.n = 0;       // a gap: the old copy is no neighbour
        if (t.n) memcpy(t.prev, t.cur, 64);
        memcpy(t.cur, t.obj + 0x10, 64);
        t.snapStep = g_stepsDone;
        if (t.n < 2) t.n++;
    }
    int keep = 0;                                  // matrices drawn in the last 3D pass; drop the rest
    for (int i = 0; i < g_mtN; i++) {
        MTrack& m = g_mt[i];
        if (m.seenFrame != g_frameNo || IsBadReadPtr(m.p, 64)) continue;
        if (m.snapStep != g_stepsDone - 1) m.n = 0;
        if (m.n) memcpy(m.prev, m.cur, 64);
        memcpy(m.cur, m.p, 64);
        m.snapStep = g_stepsDone;
        if (m.n < 2) m.n++;
        if (keep != i) g_mt[keep] = m;
        keep++;
    }
    if (keep != g_mtN) { g_mtN = keep; MRehash(); }
}

static float Len3(const float* v) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
static float Dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
// Blend two row-major world matrices (rows 0-2 rotation*scale, row 3 position).
// The rotation rows are lerped and re-orthonormalised (Gram-Schmidt, so the
// handedness is kept), each scaled by its lerped length. The w column is kept.
// A position jump is a cut (respawn, camera cut): no blend. A rotation of more
// than 60 degrees in one step can't be told apart from its opposite (a fast
// spinning wheel), so then only the position is blended.
static bool Blend(const float* p, const float* c, float a, float* o) {
    memcpy(o, c, 64);
    float d[3] = { c[12] - p[12], c[13] - p[13], c[14] - p[14] };
    if (Dot3(d, d) > kCutDist * kCutDist) return false;
    for (int i = 0; i < 3; i++) o[12 + i] = p[12 + i] + d[i] * a;
    float v[3][3], len[3];
    for (int r = 0; r < 3; r++) {
        const float* pr = p + 4 * r; const float* cr = c + 4 * r;
        float lp = Len3(pr), lc = Len3(cr);
        if (lp < 1e-6f || lc < 1e-6f) return true;        // degenerate: position only
        if (Dot3(pr, cr) < 0.5f * lp * lc) return true;   // turned over 60 degrees: position only
        for (int i = 0; i < 3; i++) v[r][i] = pr[i] + (cr[i] - pr[i]) * a;
        len[r] = lp + (lc - lp) * a;
    }
    for (int r = 0; r < 3; r++) {
        for (int q = 0; q < r; q++) {                     // v[q] is already unit length
            float k = Dot3(v[r], v[q]);
            for (int i = 0; i < 3; i++) v[r][i] -= k * v[q][i];
        }
        float l = Len3(v[r]);
        if (l < 1e-6f) return true;                       // (o still has the current rotation)
        for (int i = 0; i < 3; i++) v[r][i] /= l;
    }
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < 3; i++) o[4 * r + i] = v[r][i] * len[r];
    return true;
}

static void LogStats() {
    if (!g_cfg.gameLog) return;                    // diagnostics only
    DWORD now = GetTickCount();
    if (!g_statTick) { g_statTick = now; return; }
    if (now - g_statTick < 10000) return;
    double sec = (now - g_statTick) / 1000.0;
    g_statTick = now;
    if (g_st.frames > 0) {
        int cars = 0, cams = 0;
        for (const Track& t : g_tr)
            if (t.obj && t.n == 2 && t.snapStep == g_stepsDone) (t.kind == 1 ? cars : cams)++;
        Logf("frame rate: %.0f fps, %.2f steps/frame | tracking %d race car(s), %d camera(s)/other vehicle(s), %.2f blended/frame | "
             "skipped: moved outside a step %ld, cut %ld, changed during drawing %ld%s%s\n",
             g_st.frames / sec, (double)g_st.steps / g_st.frames, cars, cams, (double)g_st.blended / g_st.frames,
             g_st.external, g_st.cuts, g_st.redrawn,
             g_st.camOutside ? " | camera applied outside drawing" : "", g_st.full ? " | table full" : "");
        Logf("  vehicle draws: %.2f/frame while drawing, %ld outside\n", (double)g_st.vehDrawn / g_st.frames, g_st.vehOutside);
        int moving = 0, viaSet = 0, viaGet = 0;
        for (int i = 0; i < g_mtN; i++) {
            const MTrack& m = g_mt[i];
            if (m.src & 1) viaSet++;
            if (m.src & 2) viaGet++;
            if (m.n == 2 && memcmp(m.prev, m.cur, 64)) moving++;
        }
        int perFrame = 0;
        for (int i = 0; i < g_mtN; i++) if (PerFrame(&g_mt[i])) perFrame++;
        Logf("  computed while drawing: %.2f blended/frame, %d animated per frame (left alone)\n",
             (double)g_st.fBlended / g_st.frames, perFrame);
        Logf("  other objects: %d matrices (%d via SetWorld, %d via GetTransform), %d moving, %.2f blended/frame | "
             "skipped: moved outside a step %ld, cut %ld, changed during drawing %ld%s\n",
             g_mtN, viaSet, viaGet, moving, (double)g_st.mBlended / g_st.frames, g_st.mExternal, g_st.mCuts,
             g_st.mRedrawn, g_st.mFull ? " | table full" : "");
        struct CC { DWORD caller; LONG n, redrawn, external; } cc[8] = {};
        for (int i = 0; i < g_mtN; i++) {
            MTrack& m = g_mt[i];
            for (int k = 0; k < 8; k++) {
                if (cc[k].caller == m.caller || !cc[k].caller) {
                    cc[k].caller = m.caller; cc[k].n++; cc[k].redrawn += m.redrawn; cc[k].external += m.external;
                    break;
                }
            }
            m.redrawn = m.external = 0;
        }
        for (int k = 0; k < 8 && cc[k].caller; k++)
            Logf("    via 0x%06lX: %ld matrices, changed during drawing %ld, moved outside a step %ld\n",
                 cc[k].caller, cc[k].n, cc[k].redrawn, cc[k].external);
    }
    memset(&g_st, 0, sizeof(g_st));
}

// The 3D pass: blend in (put back in HkFrameEnd), then draw.
static void __cdecl HkRender3D() {
    g_frameNo++; g_st.frames++;
    float a = 1.0f, dt = *reinterpret_cast<float*>(hot::stepDt);
    if (dt > 0.0f) a = *reinterpret_cast<float*>(hot::stepAccum) / dt;
    if (!(a >= 0.0f)) a = 0.0f; else if (a > 1.0f) a = 1.0f;     // also catches NaN
    g_alpha = a;
    for (Track& t : g_tr) {
        t.written = false;
        if (!t.obj || t.n < 2 || t.snapStep != g_stepsDone) continue;
        // Race cars aren't blended in memory: the scene graph recomputes their
        // wheels from the body only on some frames, so a blended body showed the
        // wheels jumping between blended and unblended positions (a flicker).
        // Body and wheels are blended together where they're handed to SetWorld
        // (FilterWorld) instead. Their step copies are still kept: effects,
        // shadows and trails that follow a car get its blend (CarBlend).
        if (t.kind == 1) continue;
        if (*reinterpret_cast<void**>(t.obj) != t.vt) continue;
        float* live = reinterpret_cast<float*>(t.obj + 0x10);
        if (memcmp(live, t.cur, 64)) { g_st.external++; continue; }
        if (!Blend(t.prev, t.cur, a, t.out)) { g_st.cuts++; continue; }
        memcpy(live, t.out, 64);
        t.written = true; g_st.blended++;
    }
    for (Track& t : g_tr)                          // already handled above: don't blend twice
        if (t.obj) if (MTrack* m = MFind(reinterpret_cast<float*>(t.obj + 0x10))) m->skipFrame = g_frameNo;
    for (int i = 0; i < g_mtN; i++) {
        MTrack& m = g_mt[i];
        m.written = false;
        if (m.n < 2 || m.snapStep != g_stepsDone || m.skipFrame == g_frameNo || m.drawnOnly) continue;
        if (!memcmp(m.prev, m.cur, 64)) continue;  // not moving
        if (memcmp(m.p, m.cur, 64)) { g_st.mExternal++; m.external++; continue; }
        if (!Blend(m.prev, m.cur, a, m.out)) { g_st.mCuts++; continue; }
        memcpy(m.p, m.out, 64);
        m.written = true; g_st.mBlended++;
    }
    if (!g_stackHi) {
        asm volatile("movl %%fs:4, %0" : "=r"(g_stackHi));   // TIB StackBase: top of this thread's stack
        g_stackLo = g_stackHi > 0x100000 ? g_stackHi - 0x100000 : 0;  // HOT.exe reserves 1 MB of stack
    }
    g_inRender = true;
    reinterpret_cast<VoidFn_t>(hot::fnRender3D)();
}

// End of the frame's drawing (0x54DC30, after the 2D pass and the renderer's
// flush 0x54DA00): put the step state back. The renderer queues draws and
// reads many matrices only when it flushes, so the blend has to stay in until
// then, not just through the 3D pass.
static void __attribute__((thiscall)) HkFrameEnd(void* renderer) {
    reinterpret_cast<Limiter_t>(hot::fnFrameEnd)(renderer);
    if (!g_inRender) return;                       // no blend this frame
    g_inRender = false;
    for (Track& t : g_tr) {
        if (!t.written) continue;
        t.written = false;
        float* live = reinterpret_cast<float*>(t.obj + 0x10);
        if (!memcmp(live, t.out, 64)) memcpy(live, t.cur, 64);
        else { g_st.redrawn++; t.n = 0; }                       // the 3D pass moved it itself: leave it
    }
    for (int i = 0; i < g_mtN; i++) {              // (entries added during the pass have written = false)
        MTrack& m = g_mt[i];
        if (!m.written) continue;
        m.written = false;
        if (!memcmp(m.p, m.out, 64)) memcpy(m.p, m.cur, 64);
        else {
            // The drawing code recomputes this one (a scene-graph node's world), but
            // not every frame: a blend written into it would be shown on some frames
            // and the real value on others - a flicker (the race-car wheels). From
            // now on it's only blended where it's handed to SetWorld (FilterWorld).
            g_st.mRedrawn++; m.redrawn++; m.n = 0; m.drawnOnly = true;
        }
    }
    LogStats();
}

// ---------------------------------------------------------------------------
// Effects and shadows that follow a race car
//
// Race cars aren't blended in memory (see HkRender3D), so whatever reads a
// car's matrix (car+0x10) while drawing gets its 60 Hz step value. Three kinds
// of drawing do, and get the car's blend instead:
//  - the car's shadow and headlights: the race car's draw sets its shadow
//    light (0x602F40, through GSRaceCar slot 125) and the vehicle draw its
//    headlight projector (0x6029B0) from the car's matrix; the car's matrix
//    holds the blend during those calls only
//  - effects attached to a car (parachute, booster flames, shields...): every
//    GSBaseEffect reads what it follows through a few getters (0x425D30...),
//    from a scene node (+0x14) or an object (+0x10 / +0x18). They're replaced:
//    an object that is a race car gives its blend, a node gives the blend
//    FilterWorld keeps for it, or follows its car's blend if it's the car's
//    body node. Everything else gives what the original gives.
//  - tyre trails fade in and out by a fixed amount per draw (0x4BE580), so
//    above 60 fps they faded faster. The amount is now scaled by the steps
//    since the car's last trail draw (0 on frames without a step).
// ---------------------------------------------------------------------------
static Track* FindCar(BYTE* car) {
    for (Track& t : g_tr)
        if (t.obj == car && t.kind == 1) return &t;
    return nullptr;
}
// This frame's blend of a race car's matrix, or nullptr (not a race car, not
// drawing, no two steps in a row, a cut, or moved since the step).
static const float* CarBlend(BYTE* car) {
    if (!g_inRender || !car) return nullptr;
    Track* t = FindCar(car);
    if (!t || t->n < 2 || t->snapStep != g_stepsDone || *reinterpret_cast<void**>(car) != t->vt) return nullptr;
    if (memcmp(car + 0x10, t->cur, 64)) return nullptr;
    if (t->outFrame != g_frameNo) {
        t->outFrame = g_frameNo;
        t->outOk = Blend(t->prev, t->cur, g_alpha, t->out);
    }
    return t->outOk ? t->out : nullptr;
}

// Row-major affine matrices (row 3 = position, w column 0 0 0 1).
static void MulAffine(const float* a, const float* b, float* o) {        // o = a x b (o may not be a or b)
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 3; c++)
            o[4 * r + c] = a[4 * r] * b[c] + a[4 * r + 1] * b[4 + c] + a[4 * r + 2] * b[8 + c] + (r == 3 ? b[12 + c] : 0.0f);
    o[3] = o[7] = o[11] = 0.0f; o[15] = 1.0f;
}
static bool InvAffine(const float* m, float* o) {
    float c00 = m[5] * m[10] - m[6] * m[9], c01 = m[6] * m[8] - m[4] * m[10], c02 = m[4] * m[9] - m[5] * m[8];
    float det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (fabsf(det) < 1e-12f) return false;
    float k = 1.0f / det;
    o[0] = c00 * k; o[1] = (m[2] * m[9] - m[1] * m[10]) * k; o[2] = (m[1] * m[6] - m[2] * m[5]) * k;
    o[4] = c01 * k; o[5] = (m[0] * m[10] - m[2] * m[8]) * k; o[6] = (m[2] * m[4] - m[0] * m[6]) * k;
    o[8] = c02 * k; o[9] = (m[1] * m[8] - m[0] * m[9]) * k; o[10] = (m[0] * m[5] - m[1] * m[4]) * k;
    for (int c = 0; c < 3; c++) o[12 + c] = -(m[12] * o[c] + m[13] * o[4 + c] + m[14] * o[8 + c]);
    o[3] = o[7] = o[11] = 0.0f; o[15] = 1.0f;
    return true;
}

// A node's world matrix, blended if possible. buf: 2 x 16 floats of scratch.
typedef float* (__attribute__((thiscall)) *NodeWorld_t)(BYTE* node, float* buf);
static const float* NodeWorld(BYTE* node, float* buf) {
    NodeWorld_t get = *reinterpret_cast<NodeWorld_t*>(*reinterpret_cast<BYTE**>(node) + 0x218);
    const float* p = get(node, buf);                            // (fills buf or returns its own)
    if (!p) return nullptr;
    DWORD a = (DWORD)(UINT_PTR)p;
    if (p != buf && !(a >= g_stackLo && a < g_stackHi)) {
        if (MTrack* m = MFind(p)) {
            if (m->written) return p;                           // blended in memory already
            if (m->fn == 2 && m->fStep == g_stepsDone && !PerFrame(m) &&
                !memcmp(m->fcur, p, 64) && memcmp(m->fprev, m->fcur, 64))
                return Blend(m->fprev, m->fcur, g_alpha, buf + 16) ? buf + 16 : p;
        }
    }
    for (Track& t : g_tr) {                                     // a car's body node: follow the car
        if (t.obj && t.kind == 1 && t.snapStep == g_stepsDone && *reinterpret_cast<BYTE**>(t.obj + 0xE0) == node) {
            const float* b = CarBlend(t.obj);
            float inv[16], d[16];
            if (!b || !InvAffine(t.cur, inv)) return p;
            MulAffine(inv, b, d);                               // the car's step -> blend
            if (p != buf) { memcpy(buf, p, 64); p = buf; }
            MulAffine(p, d, buf + 16);
            return buf + 16;
        }
    }
    return p;
}

// The getters. id: 0 position, 1 position of [+0x18] only, 2 position of the
// node only, 3 position of [+0x10] only, 4-6 row 2/1/0, 7 rows 0-2.
// 1 = answered (out set), 0 = let the original run.
extern "C" int __cdecl EffGet(BYTE* eff, int id, float* out) {
    static const signed char kRow[8] = { 3, 3, 3, 3, 2, 1, 0, 0 };
    BYTE* node = (id == 1 || id == 3) ? nullptr : *reinterpret_cast<BYTE**>(eff + 0x14);
    BYTE* obj = nullptr;
    if (!node) {
        if (id == 2) return 0;
        if (id == 1) obj = *reinterpret_cast<BYTE**>(eff + 0x18);
        else if (id == 3) obj = *reinterpret_cast<BYTE**>(eff + 0x10);
        else if (!(obj = *reinterpret_cast<BYTE**>(eff + 0x10))) obj = *reinterpret_cast<BYTE**>(eff + 0x18);
        if (!obj) return 0;
    }
    const float* m;
    float buf[32];
    if (node) {
        if (!g_inRender) return 0;
        if (!(m = NodeWorld(node, buf))) return 0;
    } else if (!(m = CarBlend(obj))) {
        if (id != 1 && id != 3) return 0;
        m = reinterpret_cast<const float*>(obj + 0x10);         // (these two have no other path to keep)
    }
    if (id == 7) memcpy(out, m, 48);
    else memcpy(out, m + 4 * kRow[id], 12);
    return 1;
}
extern "C" {
    DWORD g_effPosCont = hot::effPos + 6, g_effPosNodeCont = hot::effPosNode + 9, g_effRow2Cont = hot::effRow2 + 9,
          g_effRow1Cont = hot::effRow1 + 9, g_effRow0Cont = hot::effRow0 + 9, g_effRowsCont = hot::effRows + 6;
    void EffPosStub(); void EffPosObj18Stub(); void EffPosNodeStub(); void EffPosObj10Stub();
    void EffRow2Stub(); void EffRow1Stub(); void EffRow0Stub(); void EffRowsStub();
}
// thiscall(ecx = effect, [esp+4] = out), ret 4; the bool ones return al = 1.
#define EFF_STUB(name, id, fallback)                                                   \
    ".globl _" name "\n_" name ":\n"                                                   \
    "    pushl %ecx\n"                                                                 \
    "    pushl 8(%esp)\n"           /* out (above the saved ecx and the return) */     \
    "    pushl $" #id "\n"                                                             \
    "    pushl %ecx\n"                                                                 \
    "    call _EffGet\n"                                                               \
    "    addl $12, %esp\n"                                                             \
    "    popl %ecx\n"                                                                  \
    "    testl %eax, %eax\n"                                                           \
    "    jz 1f\n"                                                                      \
    "    ret $4\n"                  /* (eax = 1) */                                    \
    "1:\n" fallback
#define FALLBACK_SLOT1  "    movl (%ecx), %edx\n    jmp *4(%edx)\n"       /* what those originals do without the object */
#define FALLBACK_SUB(cont)  "    subl $0x80, %esp\n    jmp *_" cont "\n"
#define FALLBACK_NODE(cont) "    movl 0x14(%ecx), %eax\n    subl $0x80, %esp\n    jmp *_" cont "\n"
asm(".text\n"
    EFF_STUB("EffPosStub",      0, FALLBACK_SUB("g_effPosCont"))
    EFF_STUB("EffPosObj18Stub", 1, FALLBACK_SLOT1)
    EFF_STUB("EffPosNodeStub",  2, FALLBACK_NODE("g_effPosNodeCont"))
    EFF_STUB("EffPosObj10Stub", 3, FALLBACK_SLOT1)
    EFF_STUB("EffRow2Stub",     4, FALLBACK_NODE("g_effRow2Cont"))
    EFF_STUB("EffRow1Stub",     5, FALLBACK_NODE("g_effRow1Cont"))
    EFF_STUB("EffRow0Stub",     6, FALLBACK_NODE("g_effRow0Cont"))
    EFF_STUB("EffRowsStub",     7, FALLBACK_SUB("g_effRowsCont")));

// The car's shadow light and headlight projector: the car's matrix holds its
// blend during the call only.
typedef void (__attribute__((thiscall)) *ThisVoid_t)(BYTE*);
typedef void (__attribute__((thiscall)) *ThisVoid2_t)(BYTE*, DWORD, DWORD);
static const float* CarBlendIn(BYTE* veh) {                     // put the blend in; nullptr = nothing to do
    const float* b = CarBlend(veh);
    if (b) memcpy(veh + 0x10, b, 64);
    return b;
}
static void CarBlendOut(BYTE* veh, const float* b) {            // put the step value back if still the blend
    if (b && !memcmp(veh + 0x10, b, 64)) memcpy(veh + 0x10, FindCar(veh)->cur, 64);   // (b: the car is tracked)
}
static void __attribute__((thiscall)) HkVehLights(BYTE* veh) {
    const float* b = CarBlendIn(veh);
    reinterpret_cast<ThisVoid_t>(hot::fnVehLights)(veh);
    CarBlendOut(veh, b);
}
static void __attribute__((thiscall)) HkCarShadow(BYTE* veh, DWORD a, DWORD c) {
    const float* b = CarBlendIn(veh);
    reinterpret_cast<ThisVoid2_t>(hot::fnCarShadow)(veh, a, c);
    CarBlendOut(veh, b);
}
// Tyre trails: the fade step scaled by the steps since the car's last trail draw.
static void __attribute__((thiscall)) HkCarTrails(BYTE* trails) {
    Track* t = g_inRender ? FindCar(trails - 0x1070) : nullptr;
    float* fade = reinterpret_cast<float*>(hot::trailFade);
    if (!t || !(*reinterpret_cast<BYTE*>(hot::trailFadeInit) & 1)) {   // (not set yet: the first call sets it)
        if (t) t->trailStep = g_stepsDone;
        reinterpret_cast<ThisVoid_t>(hot::fnCarTrails)(trails);
        return;
    }
    LONG k = g_stepsDone - t->trailStep;
    if (k < 0 || k > 4) k = 1;                                  // first draw or a long gap: one step
    t->trailStep = g_stepsDone;
    float saved = *fade;
    *fade = saved * (float)k;
    reinterpret_cast<ThisVoid_t>(hot::fnCarTrails)(trails);
    *fade = saved;
}

// Car select and the garage show the selected car by reading its node's world
// matrix, adding the car's ride height (+0x97C, set per car) and writing it
// back (0x4EE330, every frame). The animation resets the node once per step,
// so with one step per frame the height is added once; on frames without a
// step it was added again, lifting the car (0.28 for some cars, 0 for others)
// until the next step: the car jumped up and down above 60 fps. Now it's added
// once per step, on the first frame after it.
static const float kNoLift = 0.0f;
extern "C" const float* __cdecl GarageLift(BYTE* state) {
    static BYTE* lastState = nullptr;
    static LONG  lastStep = LONG_MIN / 2;
    const float* lift = reinterpret_cast<const float*>(state + 0x97C);
    if (!g_interpOn) return lift;
    if (state == lastState && g_stepsDone == lastStep) return &kNoLift;   // added since the last reset
    lastState = state; lastStep = g_stepsDone;
    return lift;
}
extern "C" void GarageLiftStub();
asm(".text\n"
    ".globl _GarageLiftStub\n_GarageLiftStub:\n"   // replaces fld [ebp+0x97C] at 0x4EE61F (call + nop)
    "    pushl %eax\n"                             // eax (the node's world) is used right after
    "    pushl %ecx\n"
    "    pushl %edx\n"
    "    pushl %ebp\n"
    "    call _GarageLift\n"
    "    addl $4, %esp\n"
    "    flds (%eax)\n"
    "    popl %edx\n"
    "    popl %ecx\n"
    "    popl %eax\n"
    "    ret\n");

// Frame cap: wait for our next frame time, then let the game's limiter measure
// the frame without waiting (its minimum frame time set to 0 for the call).
static LONGLONG g_qpf = 0, g_period = 0, g_next = 0;
static void PaceWait() {
    LARGE_INTEGER t;
    if (!g_qpf) {
        QueryPerformanceFrequency(&t); g_qpf = t.QuadPart;
        timeBeginPeriod(1);                                     // Sleep(1) ~ 1 ms (game thread, not DllMain)
        int fps = g_cfg.fpsLimit;
        if (fps <= 0) {
            DEVMODEA dm = {}; dm.dmSize = sizeof(dm);
            fps = EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm) ? (int)dm.dmDisplayFrequency : 0;
            if (fps <= 1) { Logf("frame rate: refresh rate unknown, capping at 60 fps (set fpsLimit)\n"); fps = 60; }
        }
        fps = ClampInt(fps, 30, 1000);
        g_period = g_qpf / fps;
        Logf("frame rate: capped at %d fps, interpolation on\n", fps);
    }
    QueryPerformanceCounter(&t);
    if (g_next) {
        while (g_next - t.QuadPart > 0) {
            if (g_next - t.QuadPart > g_qpf / 500) Sleep(1);       // more than 2 ms left
            else YieldProcessor();
            QueryPerformanceCounter(&t);
        }
    }
    // keep the cadence unless we're more than a frame late
    g_next = (g_next && t.QuadPart - g_next < g_period) ? g_next + g_period : t.QuadPart + g_period;
}
static void __attribute__((thiscall)) HkLimiter(void* lim) {
    float* minMs = reinterpret_cast<float*>(static_cast<BYTE*>(lim) + 4);
    float saved = *minMs;
    PaceWait();
    *minMs = 0.0f;
    reinterpret_cast<Limiter_t>(hot::fnLimiter)(lim);
    *minMs = saved;                                             // the loading-screen loop keeps its own cap
}

// The car physics-sync hook (interpolation tracks the cars with it).
static bool g_carSyncHooked = false;
static bool InstallCarSync() {
    if (g_carSyncHooked) return true;
    BYTE jmp6[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
    DWORD rel = reinterpret_cast<DWORD>(&CarSyncStub) - (hot::carSync + 5); memcpy(jmp6 + 1, &rel, 4);
    g_carSyncHooked = PatchBytes(hot::carSync, jmp6, 6);
    return g_carSyncHooked;
}

static void InstallFrameRate() {
    if (!g_cfg.interpolate) { Logf("frame rate: original (60 fps, interpolate=0)\n"); return; }
    if (*reinterpret_cast<DWORD*>(hot::fixedStepOn) != 1 ||
        fabsf(*reinterpret_cast<float*>(hot::stepDt) - 1.0f / 60.0f) > 1e-5f) {
        Logf("frame rate: unexpected step settings - original 60 fps kept\n");
        return;
    }
    const BYTE jmpShort = 0xEB;
    BYTE jmp6[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
    bool ok = true;
    // hooks first; the step patch last, so a failure never leaves 0-step frames without the cap
    ok = ok && InstallCarSync();
    DWORD rel = reinterpret_cast<DWORD>(&CamApplyStub) - (hot::camApply + 5); memcpy(jmp6 + 1, &rel, 4);
    ok = ok && PatchBytes(hot::camApply, jmp6, 6);
    rel = reinterpret_cast<DWORD>(&VehDrawStub) - (hot::vehDraw + 5); memcpy(jmp6 + 1, &rel, 4);
    ok = ok && PatchBytes(hot::vehDraw, jmp6, 6);
    BYTE jmp7[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
    rel = reinterpret_cast<DWORD>(&SetWorldStub) - (hot::setWorld + 5); memcpy(jmp7 + 1, &rel, 4);
    ok = ok && PatchBytes(hot::setWorld, jmp7, 7);
    rel = reinterpret_cast<DWORD>(&GetXformStub) - (hot::getXform + 5); memcpy(jmp7 + 1, &rel, 4);
    ok = ok && PatchBytes(hot::getXform, jmp7, 5);
    ok = ok && PatchCall(hot::callVehLights, reinterpret_cast<void*>(&HkVehLights), 5);
    for (DWORD at : hot::callCarShadow) ok = ok && PatchCall(at, reinterpret_cast<void*>(&HkCarShadow), 5);
    ok = ok && PatchCall(hot::callCarTrails, reinterpret_cast<void*>(&HkCarTrails), 5);
    const struct { DWORD at; void (*stub)(); } effHooks[] = {
        { hot::effPos, &EffPosStub }, { hot::effPosObj18, &EffPosObj18Stub }, { hot::effPosNode, &EffPosNodeStub },
        { hot::effPosObj10, &EffPosObj10Stub }, { hot::effRow2, &EffRow2Stub }, { hot::effRow1, &EffRow1Stub },
        { hot::effRow0, &EffRow0Stub }, { hot::effRows, &EffRowsStub } };
    for (const auto& h : effHooks) ok = ok && PatchJmp(h.at, reinterpret_cast<DWORD>(h.stub));
    ok = ok && PatchCall(hot::garageLift, reinterpret_cast<void*>(&GarageLiftStub), 6);
    ok = ok && PatchCall(hot::callStepTail, reinterpret_cast<void*>(&HkStepTail), 5);
    ok = ok && PatchCall(hot::callFrameEnd, reinterpret_cast<void*>(&HkFrameEnd), 5);   // before the blend can happen
    ok = ok && PatchCall(hot::callRender3D, reinterpret_cast<void*>(&HkRender3D), 5);
    ok = ok && PatchCall(hot::callLimiter, reinterpret_cast<void*>(&HkLimiter), 5);
    ok = ok && PatchBytes(hot::forceOneStep, &jmpShort, 1);
    if (!ok) Logf("frame rate: a patch FAILED\n");
    else g_interpOn = true;
}
// ---------------------------------------------------------------------------
// Closing the window. The game only quits through its own Alt+F4 check in the
// message loop (0x40C380 sets the quit flag); a close request from outside
// (window manager, title bar, taskbar) reaches MFC's window procedure, which
// destroys the window while the game keeps running without it and later
// crashes. Instead, a close request sets the same quit flag, so the game shuts
// down its own way:
//  - posted ones are caught in the loop's DispatchMessageA (IAT hook)
//  - sent ones by subclassing the game's main window (MFC's main CWnd), done
//    from that hook once it exists (on the game thread, after MFC set it up)
// The game ends its own quit by posting WM_CLOSE to its window (0x40C48C); that
// one is seen through its PostMessageA import and let through, as is anything
// after it.
// ---------------------------------------------------------------------------
typedef LRESULT (WINAPI *DispatchMessageA_t)(const MSG*);
static DispatchMessageA_t g_oDispatchMessageA = nullptr;
typedef BOOL (WINAPI *PostMessageA_t)(HWND, UINT, WPARAM, LPARAM);
static PostMessageA_t g_oPostMessageA = nullptr;
static bool    g_gameClosing = false;          // the game posted its own WM_CLOSE
static HWND    g_subWnd = nullptr;
static WNDPROC g_subOld = nullptr;
static bool    g_subUnicode = false;
static bool IsCloseRequest(UINT msg, WPARAM wp) {
    return msg == WM_CLOSE || (msg == WM_SYSCOMMAND && (wp & 0xFFF0) == SC_CLOSE);
}
static void RequestQuit() {
    if (*reinterpret_cast<DWORD*>(hot::quitFlag) != 1) Logf("close request: quitting the way Alt+F4 does\n");
    *reinterpret_cast<DWORD*>(hot::quitFlag) = 1;
}
static void GameLogFinal();
static BOOL WINAPI HkPostMessageA(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) { g_gameClosing = true; GameLogFinal(); }
    return g_oPostMessageA(h, msg, wp, lp);
}
static LRESULT CALLBACK CloseWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {   // only installed with g_subOld set
    if (h == g_subWnd && !g_gameClosing && IsCloseRequest(msg, wp)) { RequestQuit(); return 0; }
    return g_subUnicode ? CallWindowProcW(g_subOld, h, msg, wp, lp) : CallWindowProcA(g_subOld, h, msg, wp, lp);
}
static LRESULT WINAPI HkDispatchMessageA(const MSG* m) {
    if (!m) return g_oDispatchMessageA(m);
    if (!g_subWnd && !g_gameClosing) {
        BYTE* main = reinterpret_cast<BYTE* (*)()>(hot::getMainWnd)();
        HWND w = main ? *reinterpret_cast<HWND*>(main + 0x20) : nullptr;
        if (w && IsWindow(w)) {
            g_subUnicode = IsWindowUnicode(w) != FALSE;
            LONG_PTR old = g_subUnicode
                ? SetWindowLongPtrW(w, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&CloseWndProc))
                : SetWindowLongPtrA(w, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&CloseWndProc));
            g_subWnd = w;                                       // also on failure: don't retry every message
            if (old) g_subOld = reinterpret_cast<WNDPROC>(old);
            else { Logf("close: subclassing the window FAILED\n"); g_subWnd = HWND(-1); }
        }
    }
    if (!g_gameClosing && IsCloseRequest(m->message, m->wParam) && m->hwnd &&
        GetWindowThreadProcessId(GetAncestor(m->hwnd, GA_ROOT), nullptr) == GetCurrentThreadId()) {
        RequestQuit();
        return 0;
    }
    return g_oDispatchMessageA(m);
}
static void InstallCloseExits() {
    if (!g_cfg.closeExits) return;
    void** slot = nullptr; void** post = nullptr;
    ForEachImport("USER32.dll", [&](void** sl, const char* nm) {
        if (!strcmp(nm, "DispatchMessageA")) slot = sl;
        else if (!strcmp(nm, "PostMessageA")) post = sl;
    });
    if (!slot || !post) { Logf("close: USER32 imports not found\n"); return; }
    g_oDispatchMessageA = *reinterpret_cast<DispatchMessageA_t*>(slot);
    g_oPostMessageA = *reinterpret_cast<PostMessageA_t*>(post);
    // PostMessageA first: the close hook must never run without it
    if (!PatchPointer(post, reinterpret_cast<void*>(&HkPostMessageA)) ||
        !PatchPointer(slot, reinterpret_cast<void*>(&HkDispatchMessageA)))
        Logf("close: patch FAILED\n");
}

// ---------------------------------------------------------------------------
// Race music. The race-music selection (0x47C535) asks Music::GetTrackName(i)
// for every song in the in-race music bank and plays the one matching the race
// ("zone12" plays the bank's "zone12"; no match plays song 0). On PC that
// function was never written and always answers "track1", so every race got
// song 0. It now answers the song's name from its 64-byte bank entry (the name
// is the entry's first 16 bytes), reached the way the player itself does:
// PCMusicPlayer -> bank (+0x30698) -> count (+0xE0), entries (+0xF8).
// The same stub is inlined in Music::PlayTrack(name) (0x538F20), used for the
// results screen ("zone1r".."zone4r", by placing) and the title ("TITLE"):
// it never found them ("%s DOES NOT EXIST!!"), so the results music never
// played. Its comparison now uses the real names too.
// ---------------------------------------------------------------------------
static bool BankSongName(int i, char (&name)[17]) {
    BYTE* player = *reinterpret_cast<BYTE**>(hot::musicPlayer);
    if (!player || IsBadReadPtr(player + 0x30698, 4)) return false;
    BYTE* bank = *reinterpret_cast<BYTE**>(player + 0x30698);
    if (!bank || IsBadReadPtr(bank, 0xFC)) return false;
    int count = *reinterpret_cast<int*>(bank + 0xE0);
    BYTE* table = *reinterpret_cast<BYTE**>(bank + 0xF8);
    if (i < 0 || i >= count || !table || IsBadReadPtr(table + i * 0x40, 16)) return false;
    memcpy(name, table + i * 0x40, 16);
    name[16] = 0;
    return name[0] != 0;
}
extern "C" const char* __cdecl HkGetTrackName(int i) {
    static char name[17];
    return BankSongName(i, name) ? name : reinterpret_cast<const char*>(0x8B3560);   // else the original "track1"
}
extern "C" int __cdecl MatchSongName(int i, const char* wanted) {
    char name[17];
    return wanted && !IsBadStringPtrA(wanted, 64) && BankSongName(i, name) && !_stricmp(name, wanted);
}
extern "C" { DWORD g_playNext = hot::playByNameNext, g_playHit = hot::playByNameHit; }
extern "C" void PlayByNameStub();
asm(".text\n"
    ".globl _PlayByNameStub\n_PlayByNameStub:\n"   // at 0x538F91: ebx = song index, ebp = wanted name
    "    pushal\n"
    "    pushl %ebp\n"
    "    pushl %ebx\n"
    "    call _MatchSongName\n"
    "    addl $8, %esp\n"
    "    testl %eax, %eax\n"
    "    popal\n"                                   // (popal leaves the flags alone)
    "    movl $0, %edx\n"                           // as the original xor edx,edx
    "    jz 1f\n"
    "    jmp *_g_playHit\n"
    "1:  jmp *_g_playNext\n");
// When no song matches the race exactly (some tracks are named so that only a
// prefix of their name is a song: the game then played song 0, zone 1's music,
// even in other zones), pick the song whose name is the longest prefix of the
// race's name instead.
extern "C" int __cdecl PickFallbackSong(const char* race) {
    int best = 0; size_t bestLen = 0;
    char name[17];
    if (race && !IsBadStringPtrA(race, 64)) {
        for (int i = 0; i < 256 && BankSongName(i, name); i++) {
            size_t l = strlen(name);
            if (l > bestLen && !_strnicmp(race, name, l)) { best = i; bestLen = l; }
        }
    }
    char picked[17] = "?";
    BankSongName(best, picked);
    Logf("music: race \"%.40s\" has no song of its own - playing \"%s\"%s\n",
         race && !IsBadStringPtrA(race, 64) ? race : "?", picked, bestLen ? "" : " (song 0, as the original)");
    return best;
}
extern "C" { DWORD g_mfRet = 0, g_fnPlaySong = hot::fnPlaySong; }
extern "C" void MusicFallbackStub();
asm(".text\n"
    ".globl _MusicFallbackStub\n_MusicFallbackStub:\n"   // replaces push 0 / call 0x538DD0 at 0x47C5E6
    "    popl _g_mfRet\n"                       // esp is the race-music code's again
    "    pushal\n"
    "    leal 0x0F8(%esp), %eax\n"              // its [esp+0xD8] (the race name) + 32 for pushal
    "    pushl %eax\n"
    "    call _PickFallbackSong\n"
    "    addl $4, %esp\n"
    "    movl %eax, 28(%esp)\n"                 // into the saved eax
    "    popal\n"
    "    pushl %eax\n"                          // the argument, left on the stack as the original's push 0
    "    call *_g_fnPlaySong\n"
    "    jmp *_g_mfRet\n");
static void InstallRaceMusic() {
    if (!g_cfg.raceMusic) { Logf("music: original (every race plays song 0, no results music)\n"); return; }
    if (!PatchJmp(hot::getTrackName, reinterpret_cast<DWORD>(&HkGetTrackName)) ||
        !PatchJmp(hot::playByNameCmp, reinterpret_cast<DWORD>(&PlayByNameStub)) ||
        !PatchCall(hot::raceMusicMiss, reinterpret_cast<void*>(&MusicFallbackStub), 7))
        Logf("music: patch FAILED\n");
}

// ---------------------------------------------------------------------------
// The game's own debug output. HOT.exe has about 1,200 calls to a debug printf
// (0x5890F0) - warnings, missing files, "not implemented on PC" - that the
// release build turned into an empty function. It's restored here and written
// to HWBTFix_game.log, each message (by format string) at most 5 times, with a
// count of further repeats every minute. Arguments are formatted defensively:
// a bad string pointer prints as (bad) instead of crashing.
// ---------------------------------------------------------------------------
static FILE* g_glog = nullptr;
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
static void GameLogRepeats(bool final) {
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
static void GameLogFinal() {
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
static void InstallGameLog() {
    if (!g_cfg.gameLog) return;
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%sHWBTFix_game.log", g_asiDir);
    if (!(g_glog = fopen(path, "w"))) { Logf("game log: can't create %s\n", path); return; }
    InitializeCriticalSection(&g_glogLock);
    fprintf(g_glog, "HOT.exe debug messages (HWBTFix v" HWBT_VERSION "): time, caller, message\n");
    if (!PatchJmp(hot::debugPrint, reinterpret_cast<DWORD>(&GameDebugPrint))) Logf("game log: patch FAILED\n");
    if (!PatchJmp(hot::soundFind, reinterpret_cast<DWORD>(&HkSoundFind))) Logf("game log: sound audit patch FAILED\n");
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        InitializeCriticalSection(&g_logLock); g_logLockInit = true;
        InitDirs();
        LoadConfig();
        if (g_cfg.logToFile) {
            char path[MAX_PATH]; snprintf(path, sizeof(path), "%sHWBTFix.log", g_asiDir);
            g_log = fopen(path, "w");
        }
        Logf("HWBTFix v" HWBT_VERSION " | exe folder %s | root %s\n", g_gameDir, g_rootDir);
        g_knownExe = CheckExe();
        if (g_knownExe) {
            InstallSaveFolder();
            InstallDisplaySettings();
            InstallFrameRate();
            InstallRaceMusic();
            InstallGameLog();
            InstallCloseExits();
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        if (reserved) g_logLockInit = false;
        if (g_log) { Logf("=== shutdown ===\n"); fclose(g_log); g_log = nullptr; }
        if (g_glog && !reserved) { GameLogRepeats(true); fclose(g_glog); g_glog = nullptr; }   // (FreeLibrary only)
    }
    return TRUE;
}
