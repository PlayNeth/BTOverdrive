#include "common.h"
#include "menufixes.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "effects.h"

// ---------------------------------------------------------------------------
// Menu fixes.
//  - The 2D (menus, HUD) is drawn through an ortho projection (0x540C20:
//    2/W, 2/H, no flip) from vertices made screen-centred by three routines
//    (0x53C250, 0x53C310, 0x55A0B0): x - W/2 - 0.5 and H/2 - y - 0.5. The 0.5
//    is D3D9's half-pixel offset, right for x but with the wrong sign for y: on
//    screen everything lands half a pixel too low - and an edge meant for
//    y = 0 starts at 0.5, so the top row is never covered: the menu's black
//    band leaves a line of the background showing. fsubrp -> faddp: H/2 - y + 0.5.
//  - Track select: left/right (CSII_ScrollDialog 0x67E250 / 0x67E350) tells
//    the state (0x4806F0), which writes the new track's name into the text
//    shown on the centre card (state +0x5B4, at 0x48073C) at once; the cards
//    only start sliding (and the name hides) on the next update. For a frame
//    the old card has the new name. The write is held back: it's made two of
//    the dialog's updates later (whichever side of the update the input came
//    from, the slide is under way by then), and only by that same dialog.
static void* g_tnDst; static void* g_tnDlg; static const void* g_tnName; static DWORD g_tnAt; static DWORD g_dlgUpdates;
static bool g_tnPending;
static void* __cdecl HkTrackName(void* dst, const void* name) {   // (name: the string table's own string object, 0x59E730)
    BYTE* state = static_cast<BYTE*>(dst) - 0x5B4;
    g_tnDst = dst; g_tnDlg = *reinterpret_cast<void**>(state + 0x5CC);
    g_tnName = name;
    g_tnAt = g_dlgUpdates; g_tnPending = true;
    return dst;                                                 // (the caller ignores it)
}
static ThisVoid_t g_scrollDlgUpd;                               // (thiscall, BYTE* this)
static void __attribute__((thiscall)) HkScrollDlgUpd(BYTE* dlg) {
    if (dlg == g_tnDlg) g_dlgUpdates++;                        // (that dialog's updates only: others may update too)
    if (g_tnPending && dlg == g_tnDlg && g_dlgUpdates - g_tnAt >= 2 &&
        *reinterpret_cast<BYTE**>(dlg + 0xD8) == static_cast<BYTE*>(g_tnDst) - 0x5B4) {   // (still that state's dialog)
        g_tnPending = false;
        // (exactly as the game calls it: its "format" is a string object - the
        // string table's entry for the name, which stays put - not a char*)
        reinterpret_cast<void* (__cdecl *)(void*, const void*)>(hot::fnStrFormat)(g_tnDst, g_tnName);
    }
    g_scrollDlgUpd(dlg);
}
void InstallMenuFixes() {
    if (!g_cfg.menuFixes) return;
    static const BYTE faddp[2] = { 0xDE, 0xC1 };
    bool ok = true;
    for (DWORD at : hot::ui2DYSub) ok = ok && PatchBytes(at, faddp, 2);
    g_scrollDlgUpd = *reinterpret_cast<ThisVoid_t*>(hot::vtScrollDlgUpd);
    ok = ok && PatchCall(hot::callTrackName, reinterpret_cast<void*>(&HkTrackName), 5) &&
         PatchPointer(reinterpret_cast<void**>(hot::vtScrollDlgUpd), reinterpret_cast<void*>(&HkScrollDlgUpd));
    Logf(ok ? "menus: 2D half-pixel offset fixed, track names change with their cards\n" : "menus: patch FAILED\n");
}
