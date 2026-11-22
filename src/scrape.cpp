#include "common.h"
#include "scrape.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"

// ---------------------------------------------------------------------------
// Scrape sound. Each car's collision-audio helper plays its surface's looping
// "contact" sound while it grinds along a wall (0x65B290) or another car
// (0x65B4A0): it copies the surface material's sample into the car's sound at
// car+0x13C (0x5C41C0, called at 0x65B2F1 / 0x65B501), then plays it at the
// car, its volume following the speed, kept alive while the contact lasts.
// But no PC surface names a contact sample, so it stayed silent - while the
// "materials" sound bank ([0x968914]) has one: "metalscrape". Those two copies
// are hooked: an empty sample becomes metalscrape (bound once, 0x5C38E0), and
// the game does the rest, for every car.
// ---------------------------------------------------------------------------
static BYTE g_scrapeSnd[0x14];                                  // the game's sound handle (+0 the sample)
static BYTE* g_scrapeBank = nullptr; static int g_scrapeTries = 0;
typedef void (__attribute__((thiscall)) *SndCopy_t)(BYTE*, const BYTE*);
typedef void (__attribute__((thiscall)) *SndBind_t)(BYTE*, BYTE*, const char*, int);
static void __attribute__((thiscall)) HkContactCopy(BYTE* dst, const BYTE* src) {
    if (!src || !*reinterpret_cast<const DWORD*>(src)) {       // the surface has no contact sound
        BYTE* bank = *reinterpret_cast<BYTE**>(hot::sndMaterialsBank);
        if (bank && (bank != g_scrapeBank || (!*reinterpret_cast<DWORD*>(g_scrapeSnd) && g_scrapeTries < 3))) {
            if (bank != g_scrapeBank) { memset(g_scrapeSnd, 0, sizeof(g_scrapeSnd)); g_scrapeTries = 0; g_scrapeBank = bank; }
            g_scrapeTries++;
            reinterpret_cast<SndBind_t>(hot::fnSndBind)(g_scrapeSnd, bank, "metalscrape", 1);
            Logf("scrape sound: metalscrape %s\n", *reinterpret_cast<DWORD*>(g_scrapeSnd) ? "found" : "NOT found in the materials bank");
        }
        if (*reinterpret_cast<DWORD*>(g_scrapeSnd)) src = g_scrapeSnd;
    }
    reinterpret_cast<SndCopy_t>(hot::fnSndCopy)(dst, src);
    // metalscrape plays as a one-shot of a few hundredths of a second (its loop
    // flag isn't set in the PC bank): while the contact lasts its voice is kept
    // going as a loop (voice +0x18 loop, +0x34 time left = +0x30 its length)
    if (src == g_scrapeSnd) {
        BYTE* v = *reinterpret_cast<BYTE**>(dst + 4);
        if (v) { v[0x18] = 1; *reinterpret_cast<float*>(v + 0x34) = *reinterpret_cast<float*>(v + 0x30); }
    }
}
void InstallScrapeSound() {
    if (!g_cfg.scrapeSound) return;
    if (!PatchCall(hot::callContactCopy1, reinterpret_cast<void*>(&HkContactCopy), 5) ||
        !PatchCall(hot::callContactCopy2, reinterpret_cast<void*>(&HkContactCopy), 5)) { Logf("scrape sound: patch FAILED\n"); return; }
    Logf("scrape sound: the game's metalscrape where a surface has no contact sound\n");
}
