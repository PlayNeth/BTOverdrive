// BTOverdrive.asi - Beat That! Overdrive (formerly HWBTMod)
// Gameplay and rendering mod for Hot Wheels: Beat That! (PC, HOT.exe), forked from HWBTFix
// 0.3.8 (the vanilla fixes: portable install, high frame rate, music...). Use
// one or the other: whichever loads second finds the game already patched and
// stays off.
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
//  - portableRegistry: the game's registry key is answered from BTOverdrive.ini
//    (width/height/detail keys); nothing is read from or written to the
//    Windows registry for it. Other keys (CPU info) are untouched.
//  - width/height: any resolution (0 = desktop resolution); the 1024..1600
//    clamp is removed.
//  - saveFolder: saves and Settings.cfg in a folder in the game root, above
//    Bin (default "save"); existing ones are copied over from Documents on first run.
//  - DisplayConfig.exe is never launched (settings are in the ini).
//  - interpolate/fpsLimit: frames above 60 fps, with cars, cameras, other
//    moving objects and animated models' bones drawn between their last two 60 Hz simulation steps (see
//    "Frame rate"); effects, shadows and trails that follow a car included.
//  - closeExits: closing the window quits the game cleanly (like Alt+F4).
//  - raceMusic: each race plays its own song, results music plays (the PC
//    port's song-name lookup was a stub).
//  - gameLog: diagnostics - the game's own debug messages (an empty function in
//    the release build) and failed sound lookups to BTOverdrive_game.log.
//  - and the mod's own (each section below has the details): the ghoul throws
//    cars, the gate's collision, the F1-style grid, the start boost dial, the
//    AI, Rampage; speed FOV, blur and shake; sparks; sun shadows, glass, HDR
//    glare, the sky; animated models interpolated; the scrape sound, race music
//    at GO; the 2D half-pixel offset and the track select's name; the tuning
//    log/file (its defaults built in), frame timing and a crash log in
//    BTOverdrive.log; weapon hits that throw cars, items by race position and
//    reworked items (ice gun, smoke, parachute, shields, EMP, firing on Spooky
//    Sprint), falls and stuck AI put back, the AI on loops, quicker steering,
//    car shadows that stay on their road, sparks that move with the car,
//    looping menu music and smoothed engine pitch.

#include "src/common.h"
#include "src/config.h"
#include "src/log.h"
#include "src/patch.h"
#include "src/saves.h"
#include "src/display.h"
#include "src/framerate.h"
#include "src/camera.h"
#include "src/pacing.h"
#include "src/window.h"
#include "src/music.h"
#include "src/debug.h"
#include "src/hazards.h"
#include "src/ai_route.h"
#include "src/icegun.h"
#include "src/items.h"
#include "src/weapons.h"
#include "src/hazard_parts.h"
#include "src/steering.h"
#include "src/ai.h"
#include "src/start.h"
#include "src/rampage.h"
#include "src/sparks.h"
#include "src/scrape.h"
#include "src/menufixes.h"
#include "src/sound.h"
#include "src/musicstream.h"
#include "src/render.h"
#include "src/tuning.h"
#include "src/exit.h"
#include "src/crashlog.h"

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
            char path[MAX_PATH]; snprintf(path, sizeof(path), "%s" MOD_FILE ".log", g_asiDir);
            g_log = fopen(path, "w");
        }
        Logf(MOD_NAME " v" HWBT_VERSION " (based on " HWBT_BASE ") | exe folder %s | root %s\n", g_gameDir, g_rootDir);
        InstallCrashLog(h);
        g_knownExe = CheckExe();
        if (g_knownExe) {
            InstallSaveFolder();
            InstallDisplaySettings();
            InstallFrameRate();
            InstallSpeedBlur();
            InstallSparks();
            InstallRendering();
            InstallScrapeSound();
            InstallMusicAtGo();
            InstallMusicStream();
            InstallMusicLoop();
            InstallSoundFx();
            InstallExitFix();
            InstallMenuFixes();
            InstallPerf();
            InstallRaceMusic();
            InstallGameLog();
            InstallCloseExits();
            InstallTuning();
            InstallHazards();
            InstallWeaponImpact();
            InstallFallReset();
            InstallAiUnstuck();
            InstallItemOdds();
            InstallIceFix();
            InstallItemFx();
            InstallToonFire();
            InstallCarShadowDepth();
            InstallSteering();
            InstallGrid();
            InstallStartBoost();
            InstallAI();
            InstallRampage();
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        if (reserved) g_logLockInit = false;
        if (g_log) {
            Logf("speed FOV: fastest camera speed seen %.1f units/s\n", g_maxCamSpeed);
            if (g_hzOn) HazardReport();
            Logf("=== shutdown ===\n"); fclose(g_log); g_log = nullptr; }
        if (g_glog && !reserved) { GameLogRepeats(true); fclose(g_glog); g_glog = nullptr; }   // (FreeLibrary only)
        if (g_tlog) fflush(g_tlog);
    }
    return TRUE;
}
