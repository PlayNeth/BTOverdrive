#include "common.h"
#include "music.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"

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
void InstallRaceMusic() {
    if (!g_cfg.raceMusic) { Logf("music: original (every race plays song 0, no results music)\n"); return; }
    if (!PatchJmp(hot::getTrackName, reinterpret_cast<DWORD>(&HkGetTrackName)) ||
        !PatchJmp(hot::playByNameCmp, reinterpret_cast<DWORD>(&PlayByNameStub)) ||
        !PatchCall(hot::raceMusicMiss, reinterpret_cast<void*>(&MusicFallbackStub), 7))
        Logf("music: patch FAILED\n");
}
