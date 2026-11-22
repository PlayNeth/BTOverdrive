#include "common.h"
#include "tuning.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "hazards.h"
#include "items.h"

// ---------------------------------------------------------------------------
// Tuning. The game's data (car physics, cameras, effects...) is in scripts:
// keywords followed by numbers, read with Script::ReadFloat / ReadInt (0x5C6B70,
// 0x5C6B90; about 1,800 call sites). Both are replaced: every number is logged
// with its keyword and file (tuningLog=1), and BTOverdrive_tuning.txt can set or
// scale it before the game gets it:
//     KEY = 2.5          set (every KEY, in every file)
//     KEY *= 1.3         scale
//     KEY#1 *= 1.3       the second number after KEY (KEY#0 = the first)
//     file.ext:KEY = 2   only in that file
//     @5F9FCF:MASS *= 0.8  only where that game code reads it (the @ address in
//                        the log: the same keyword can mean different things,
//                        e.g. a car's MASS and a wheel's MASS)
// ---------------------------------------------------------------------------
struct TOverride { char file[64]; DWORD caller; char key[48]; int idx; bool scale; float v; LONG hits; };
static TOverride g_tov[512]; static int g_tovN = 0;
FILE* g_tlog = nullptr;
static CRITICAL_SECTION g_tLock;
static bool g_tLockInit = false;
void TuningFlush() {
    if (!g_tlog || !g_tLockInit) return;
    EnterCriticalSection(&g_tLock); fflush(g_tlog); LeaveCriticalSection(&g_tLock);
}
static bool IsNumberTok(const char* s) {
    if (!s || !*s) return false;
    char* e; strtod(s, &e);
    return e != s && *e == 0;
}
// The tuning built in: with no BTOverdrive_tuning.txt these are the numbers, and
// the file is written with them (to edit; delete it to get these back).
static const char kTuningText[] =
    "; BTOverdrive_tuning.txt - a livelier camera, more tyre smoke (the car handles as in the game).\n"
    "; Lines starting with ; are off: remove the ; to try one, add it to switch one off.\n"
    ";   KEY = 2.5    KEY *= 1.3    KEY#1 *= 1.3 (2nd number after KEY)    @5F9FCF:KEY (only where that code reads it)\n"
    "; BTOverdrive_tuning.log marks every value a line here changed (-> new value).\n"
    "\n"
    "; --- handling: the game's (quicker steering is steerRate in BTOverdrive.ini) --\n"
    "; remove the ; to try one\n"
    "; rotational inertia: lower = snappier turn-in (stock 1500 / 1800 / 1200)\n"
    ";@5FA011:INERTIA *= 0.4\n"
    ";@5FA029:INERTIA#1 *= 0.4\n"
    ";@5FA041:INERTIA#2 *= 0.4\n"
    "; car body mass: lower = quicker launch, lighter feel (stock 299)\n"
    ";@5F9FCF:MASS *= 0.65\n"
    "; suspension damping: higher = the car settles instead of bouncing over waves and in\n"
    "; loops (stock 0.25 bump / 0.5 rebound; the springs are 40000)\n"
    ";@6B8820:DAMPING-RATIO *= 3.2\n"
    ";@6B8855:REBOUND-DAMPING-RATIO *= 1.6\n"
    "; spring stiffness: lower = the wheels follow curves and dips instead of skipping off them (stock 40000)\n"
    ";@6B87E7:SPRING-STIFFNESS *= 0.75\n"
    "\n"
    "; --- camera ----------------------------------------------------------------\n"
    "; field of view (stock 65; the speed FOV in BTOverdrive.ini adds to it at speed)\n"
    "@4246F0:FOV *= 1.15\n"
    "; distance behind the car (stock -11): closer\n"
    "@5BC6A5:OFFSET#2 *= 0.75\n"
    "; how much the camera reacts to acceleration and braking (it pulls back / dips\n"
    "; in): ACTIVE-SCALE is the amount, ACTIVE-LIMIT the most it can move\n"
    "ACTIVE-SCALE *= 3\n"
    "ACTIVE-LIMIT *= 2\n"
    "; sway (sideways swing in turns) and tilt (lean into turns)\n"
    "SWAY *= 1.6\n"
    "TILT *= 2\n"
    "; trailing behind the car (stock 0.7): lower = tighter\n"
    ";@5BC68C:LAG *= 0.8\n"
    "\n"
    "; --- tyre smoke and skids ----------------------------------------------------\n"
    "; per surface: TYRESMOKE's first number is how much smoke (0-255; stock 170 on\n"
    "; some surfaces, 64, 1 or 0 on others) -> the same strong smoke everywhere\n"
    "TYRESMOKE = 200\n"
    "; tyre particles spawned per slip (stock 15)\n"
    "TIRE_PART_SPAWN_SCALE *= 2\n"
    "; when the tyres count as skidding: SKID-DATA's first number is the slip where\n"
    "; skids (marks and smoke) start (stock 0.45 / 0.15 / 0.01). Lower = earlier,\n"
    "; so smoke shows when attacking turns, not only in big slides\n"
    "SKID-DATA *= 0.5\n";
static void TuningLine(char* line) {
    if (g_tovN >= 512) return;
    if (char* c = strpbrk(line, ";#\r\n")) {
        if (*c == '#' && c[1] >= '0' && c[1] <= '9') {              // KEY#1: not a comment
            if (char* c2 = strpbrk(c + 1, ";\r\n")) *c2 = 0;
        } else *c = 0;
    }
    char* eq = strchr(line, '=');
    if (!eq) return;
    bool scale = eq > line && eq[-1] == '*';
    char* ke = scale ? eq - 1 : eq;
    *ke = 0;
    char* k = line; while (*k == ' ' || *k == '\t') k++;
    char* kend = k + strlen(k); while (kend > k && (kend[-1] == ' ' || kend[-1] == '\t')) *--kend = 0;
    if (!*k) return;
    TOverride& o = g_tov[g_tovN];
    memset(&o, 0, sizeof(o));
    if (char* colon = strchr(k, ':')) {
        *colon = 0;
        if (k[0] == '@') o.caller = strtoul(k + 1, nullptr, 16);
        else lstrcpynA(o.file, k, sizeof(o.file));
        k = colon + 1;
    }
    if (char* hash = strchr(k, '#')) { *hash = 0; o.idx = atoi(hash + 1); }
    lstrcpynA(o.key, k, sizeof(o.key));
    o.scale = scale;
    o.v = (float)atof(eq + 1);
    g_tovN++;
}
static void LoadTuning() {
    CarryOver(MOD_FILE "_tuning.txt", "HWBTMod_tuning.txt");
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%s" MOD_FILE "_tuning.txt", g_asiDir);
    char line[256];
    if (FILE* f = fopen(path, "r")) {
        while (fgets(line, sizeof(line), f)) TuningLine(line);
        fclose(f);
        Logf("tuning: %d change(s) from %s\n", g_tovN, path);
        return;
    }
    for (const char* p = kTuningText; *p; ) {                       // (the built-in lines)
        const char* e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, p, n); line[n] = 0;
        TuningLine(line);
        p = e ? e + 1 : p + n;
    }
    bool wrote = false;
    if (FILE* f = fopen(path, "w")) { wrote = fputs(kTuningText, f) >= 0; fclose(f); }
    Logf("tuning: %d change(s) built in%s\n", g_tovN, wrote ? " (written to " MOD_FILE "_tuning.txt to edit)" : "");
}
// The script's file name (from the archive index), cached per script.
static const char* ScriptName(BYTE* scr) {
    static BYTE* last = nullptr; static int lastIdx = -2; static char name[512];
    int idx = *reinterpret_cast<int*>(scr + 0x20C);
    if (scr != last || idx != lastIdx) {
        last = scr; lastIdx = idx;
        name[0] = 0;
        if (idx >= 0) reinterpret_cast<void (__cdecl *)(int, char*)>(hot::scrFileName)(idx, name);
        name[sizeof(name) - 1] = 0;
        if (!name[0]) lstrcpynA(name, "?", sizeof(name));
    }
    return name;
}
static double Tune(BYTE* scr, const char* keyTok, const char* valTok, double v, bool isInt, DWORD caller) {
    static BYTE* lastScr = nullptr; static char lastKey[48]; static int lastIdx = 0;
    if (!g_tlog && !g_tovN) return v;
    EnterCriticalSection(&g_tLock);
    const char* key; int idx;
    if (scr == lastScr && IsNumberTok(keyTok)) { key = lastKey; idx = ++lastIdx; }   // KEY 1 2 3: the 2nd, 3rd...
    else {
        lstrcpynA(lastKey, keyTok && !IsBadStringPtrA(keyTok, 48) ? keyTok : "?", sizeof(lastKey));
        key = lastKey; idx = lastIdx = 0;
    }
    lastScr = scr;
    const char* file = ScriptName(scr);
    const char* base = strrchr(file, '\\'); base = base ? base + 1 : file;
    double out = v;
    for (int i = 0; i < g_tovN; i++) {
        TOverride& o = g_tov[i];
        if (o.idx != idx || _stricmp(o.key, key) || (o.file[0] && _stricmp(o.file, base)) ||
            (o.caller && o.caller != caller)) continue;
        out = o.scale ? v * o.v : o.v;
        if (isInt) out = (double)(long)(out + (out < 0 ? -0.5 : 0.5));
        o.hits++;
    }
    if (g_tlog) {
        char kk[64];
        if (idx) snprintf(kk, sizeof(kk), "%s#%d", key, idx); else lstrcpynA(kk, key, sizeof(kk));
        if (out != v) fprintf(g_tlog, "%-28.28s %-32s = %-12s -> %g%s  @%06lX\n", base, kk, valTok, out, isInt ? " (int)" : "", caller);
        else fprintf(g_tlog, "%-28.28s %-32s = %-12s%s  @%06lX\n", base, kk, valTok, isInt ? " (int)" : "", caller);
        static DWORD lastFlush = 0;                             // (the game can be killed without unloading us)
        if (GetTickCount() - lastFlush > 1000) { fflush(g_tlog); lastFlush = GetTickCount(); }
    }
    LeaveCriticalSection(&g_tLock);
    return out;
}
typedef const char* (__attribute__((thiscall)) *NextToken_t)(BYTE*);
static float __attribute__((thiscall)) HkReadFloat(BYTE* scr) {
    DWORD caller = (DWORD)(UINT_PTR)__builtin_return_address(0);
    const char* key = *reinterpret_cast<const char**>(scr + 0x214);
    reinterpret_cast<NextToken_t>(hot::scrNextToken)(scr);
    const char* tok = *reinterpret_cast<const char**>(scr + 0x214);
    float v = (float)atof(tok);                                  // as the original: atof, to float
    return (float)Tune(scr, key, tok, v, false, caller);
}
static int __attribute__((thiscall)) HkReadInt(BYTE* scr, char* ok) {
    DWORD caller = (DWORD)(UINT_PTR)__builtin_return_address(0);
    const char* key = *reinterpret_cast<const char**>(scr + 0x214);
    reinterpret_cast<NextToken_t>(hot::scrNextToken)(scr);
    if (ok) *ok = 1;
    const char* tok = *reinterpret_cast<const char**>(scr + 0x214);
    int v = atoi(tok);
    return (int)Tune(scr, key, tok, (double)v, true, caller);
}
void InstallTuning() {
    LoadTuning();
    if (g_cfg.tuningLog) {
        char path[MAX_PATH]; snprintf(path, sizeof(path), "%s" MOD_FILE "_tuning.log", g_asiDir);
        if ((g_tlog = fopen(path, "w"))) {
            setvbuf(g_tlog, nullptr, _IOFBF, 1 << 16);
            fprintf(g_tlog, MOD_NAME " v" HWBT_VERSION ": every number the game's scripts read, as it reads them.\n"
                            "file, keyword (#n = the n-th number after it), value as written, -> value used if\n"
                            "BTOverdrive_tuning.txt changed it, @ the game code reading it. BTOverdrive_tuning.txt lines:\n"
                            "  KEY = 2.5    KEY *= 1.3    KEY#1 *= 1.3    file.ext:KEY = 2    @5F9FCF:MASS *= 0.8\n\n");
        }
    }
    if (!g_tlog && !g_tovN) return;
    InitializeCriticalSection(&g_tLock); g_tLockInit = true;
    if (!PatchJmp(hot::scrReadFloat, reinterpret_cast<DWORD>(&HkReadFloat)) ||
        !PatchJmp(hot::scrReadInt, reinterpret_cast<DWORD>(&HkReadInt)))
        Logf("tuning: patch FAILED\n");
}
