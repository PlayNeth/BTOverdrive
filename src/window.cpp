#include "common.h"
#include "window.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "debug.h"

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
void InstallCloseExits() {
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
