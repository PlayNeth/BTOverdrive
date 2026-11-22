#include "common.h"
#include "sound.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "framerate.h"
#include "hazards.h"
#include "ai_route.h"
#include "items.h"
#include "fastload.h"

// ---------------------------------------------------------------------------
// Sound effects (DirectSound 8; one new buffer per playing sound, 16-bit mono).
//  - Voices: the number of 3D sounds playing at once is capped from the card's
//    hardware 3D buffer count, at least 16 (0x53A21B/0x53A226); a sound past the
//    cap is stopped dead (and a loop restarted from its start when it's heard
//    again). Windows Vista and later report no hardware buffers: 16, on a card
//    of the time 64. soundVoices: at least 65 - so the cap is the game's own 64
//    (0x539280), and its 3D mode stays plain panning (HRTF only when the card
//    reports exactly 64, 0x5392AC).
//  - Doppler: as each sound starts, its type's Doppler factor is set on the
//    listener (0x537DF2), whose velocity is your car's; sounds placed in the
//    world have no velocity of their own. So every opponent and world sound is
//    pitch-shifted by your speed alone - wrong, and it warbles as you speed up
//    and slow down. soundDoppler=0: factor 0 there and at startup (0x53A2BF).
//  - Headroom: the gain-to-attenuation table (0x935650, 17 steps of -6000..0
//    hundredths of dB) is lowered by soundHeadroom dB, so a pile of full-volume
//    sounds clips less in the software mix.
// 3D sound hooks: the methods are DirectSound's (or a wrapper's), found on a 3D
// buffer made for the purpose once the game's sound is up, and hooked in their
// vtables: the 3D buffer's SetPosition (+0x4C), SetVelocity (+0x50), Release,
// the listener's SetPosition (+0x38) and SetDopplerFactor (+0x30). Each 3D sound
// is tracked by its 3D interface from its first SetPosition until it's released.
//  - Doppler from movement (soundDoppler=2). Every 3D sound's velocity is
//    measured from how its position moves from one sound update to the next
//    (0x537F70, once per 60 Hz step, where the game positions them) and set
//    with the position; the game's own SetVelocity calls (always 0) are
//    dropped, a sound not moved for 2 updates is set still, and a jump of over
//    10 units in a step (a respawn, a sound reused elsewhere) isn't a speed.
//    The listener's velocity is the game's (your car's), its Doppler factor
//    soundDopplerScale (the game set a per-sound value on it as each sound
//    started).
//  - Occlusion (soundOcclusion, with EAX on): the game gets every 3D sound's
//    EAX controls (its IKsPropertySet) and never uses them. Each step a few
//    sounds (round robin) are checked for a wall between them and the listener
//    with the game's own line test against the track (0x635580, as its replay
//    cameras use it: both-sided, the hit globals kept); one behind a wall gets
//    EAX 4 source occlusion soundOcclusionDb (muffled: mostly highs), eased in
//    and out. Positions here are DirectSound's: the game's with x negated.
typedef HRESULT (__stdcall *Set3_t)(void*, float, float, float, DWORD);
typedef HRESULT (__stdcall *SetF_t)(void*, float, DWORD);
typedef ULONG (__stdcall *Rel_t)(void*);
typedef HRESULT (__stdcall *QI_t)(void*, const GUID&, void**);
typedef HRESULT (__stdcall *KsSet_t)(void*, const GUID&, ULONG, void*, ULONG, void*, ULONG);
static Set3_t g_ds3SetPos, g_ds3SetVel, g_dsLsnSetPos, g_dsLsnSetVel;
static SetF_t g_dsLsnDoppler;
static Rel_t g_ds3Release;
struct SndSrc { void* buf; float pos[3]; LONG step; bool moving; void* ks; bool ksTried; LONG occ, occTarget;
                LONG nearSteps; };                                   // (Doppler: positions in a row near the listener)
// A sound that stays near the listener - your own car's engine, skids, scrapes -
// rides with it: it's given the listener's velocity every update, so it has none
// relative to you. Measured on its own, each step the two moved differently (the
// camera swinging in, the sound not placed one update and taken as still, a jump)
// gave it a speed toward you, and the engine screamed for as long as that lasted.
static const float kRideDist = 30.0f;                           // (the camera is about 10 behind the car)
static const LONG  kRideSteps = 60;                             // (near for a second: riding along, not passing)
static inline bool SndRiding(const SndSrc& e) { return e.nearSteps > kRideSteps; }
static SndSrc g_sndSrc[256];
static LONG g_sndStep = 0;
static float g_sndDt = 1.0f / 60.0f;
static float g_lsnPos[3];
static float g_lsnLast[3];                                      // (Doppler: the listener's position at its last update)
static float g_lsnVel[3];                                       // (Doppler: the listener's velocity, as set)
static LONG g_lsnStep = LONG_MIN / 2;
static bool g_lsnMoving = false;
static int g_snd3dState = 0;                                    // 0 not yet, 1 hooked, -1 gave up
static int g_snd3dTries = 0;
static int g_occNext = 0;
static LONG g_occFailLogged = 0;
static const GUID kIidKsPropertySet = { 0x31EFAC30, 0x515C, 0x11D0, { 0xA9, 0xAA, 0x00, 0xAA, 0x00, 0x61, 0xBE, 0x93 } };
static const GUID kEax40Source = { 0x1B86B823, 0x22DF, 0x4EAE, { 0x8B, 0x3C, 0x12, 0x78, 0xCE, 0x54, 0x42, 0x27 } };
static const ULONG kEaxSourceOcclusion = 11;                    // EAXSOURCE_OCCLUSION: long, hundredths of dB (-10000..0)
static bool EaxOn() { BYTE* ds = *reinterpret_cast<BYTE**>(hot::dsSingleton); return ds && ds[0xDA]; }
static SndSrc* SndFind(void* buf, bool add) {
    SndSrc* free = nullptr;
    for (SndSrc& e : g_sndSrc) {
        if (e.buf == buf) return &e;
        if (!free && !e.buf) free = &e;
    }
    if (!add || !free) return nullptr;
    free->buf = buf; free->step = LONG_MIN / 2; free->moving = false;
    free->ks = nullptr; free->ksTried = false; free->occ = free->occTarget = 0;
    free->nearSteps = 0;
    return free;
}
static void SndMeasure(const float* last, LONG lastStep, float x, float y, float z, float* v) {   // (a speed from two positions)
    v[0] = v[1] = v[2] = 0.0f;
    LONG steps = g_sndStep - lastStep;
    if (steps < 1 || steps > 4) return;
    float d[3] = { x - last[0], y - last[1], z - last[2] };
    if (Dot3(d, d) >= 100.0f * steps * steps) return;            // (over 10 units a step: a jump, not a speed)
    float k = 1.0f / (steps * g_sndDt);
    for (int a = 0; a < 3; a++) v[a] = d[a] * k;
}
static HRESULT __stdcall HkDs3SetPos(void* b, float x, float y, float z, DWORD apply) {
    HRESULT hr = g_ds3SetPos(b, x, y, z, apply);
    SndSrc* e = SndFind(b, true);
    if (!e) return hr;
    if (g_sndStep == e->step) return hr;                        // (twice in one update: keep the first)
    float v[3];
    SndMeasure(e->pos, e->step, x, y, z, v);
    e->pos[0] = x; e->pos[1] = y; e->pos[2] = z; e->step = g_sndStep;
    if (g_cfg.soundDoppler == 2) {
        float d[3] = { x - g_lsnPos[0], y - g_lsnPos[1], z - g_lsnPos[2] };
        if (Dot3(d, d) < kRideDist * kRideDist) { if (e->nearSteps < 100000) e->nearSteps++; } else e->nearSteps = 0;
        if (SndRiding(*e)) { v[0] = g_lsnVel[0]; v[1] = g_lsnVel[1]; v[2] = g_lsnVel[2]; }
        e->moving = v[0] != 0.0f || v[1] != 0.0f || v[2] != 0.0f;
        g_ds3SetVel(b, v[0], v[1], v[2], apply);
    }
    return hr;
}
static HRESULT __stdcall HkDs3SetVel(void* b, float x, float y, float z, DWORD apply) {
    if (g_cfg.soundDoppler == 2) return S_OK;                   // (the game's: always 0 - ours is set with the position)
    return g_ds3SetVel(b, x, y, z, apply);
}
static HRESULT __stdcall HkDsLsnSetPos(void* l, float x, float y, float z, DWORD apply) {
    g_lsnPos[0] = x; g_lsnPos[1] = y; g_lsnPos[2] = z;
    HRESULT hr = g_dsLsnSetPos(l, x, y, z, apply);
    if (g_cfg.soundDoppler == 2 && g_dsLsnSetVel && g_sndStep != g_lsnStep) {
        // The listener's velocity measured as the sounds' are (the game set your car's): a sound
        // riding with the listener - your own engine - then has none relative to it. With the
        // game's, the step your car's speed jumped (a hit, a boost, a landing) the listener had
        // the new speed and the engine (measured) still the old: a squeal for a moment.
        float v[3];
        SndMeasure(g_lsnLast, g_lsnStep, x, y, z, v);
        g_lsnLast[0] = x; g_lsnLast[1] = y; g_lsnLast[2] = z; g_lsnStep = g_sndStep;
        g_lsnMoving = v[0] != 0.0f || v[1] != 0.0f || v[2] != 0.0f;
        g_lsnVel[0] = v[0]; g_lsnVel[1] = v[1]; g_lsnVel[2] = v[2];
        g_dsLsnSetVel(l, v[0], v[1], v[2], apply);
    }
    return hr;
}
static HRESULT __stdcall HkDsLsnSetVel(void* l, float x, float y, float z, DWORD apply) {
    if (g_cfg.soundDoppler == 2) return S_OK;                   // (ours is set with the position)
    return g_dsLsnSetVel(l, x, y, z, apply);
}
static HRESULT __stdcall HkDsLsnDoppler(void* l, float f, DWORD apply) {
    return g_dsLsnDoppler(l, g_cfg.soundDoppler == 2 ? g_cfg.soundDopplerScale : f, apply);
}
static ULONG __stdcall HkDs3Release(void* b) {
    // (dropped at any release, not only the last: the game releases a sound's 3D
    // interface as it tears it down (0x5367C0), and whether that's the object's
    // last reference depends on the DirectSound in use - never touch one after.
    // Our own reference to its EAX controls goes first.)
    SndSrc* e = SndFind(b, false);
    if (e) {
        void* ks = e->ks; e->buf = nullptr; e->ks = nullptr;
        if (ks) reinterpret_cast<Rel_t>((*reinterpret_cast<void***>(ks))[2])(ks);
    }
    return g_ds3Release(b);
}
// Pitch (soundPitchRise). Every buffer's SetFrequency (IDirectSoundBuffer +0x44) is
// watched. An engine's pitch follows its revs, and its revs its driven wheels: a
// wheel off the road for a step or two spins up free and the engine screams for a
// moment. A sound whose pitch is set again within 0.1 s (one held on: an engine,
// a skid) may rise at most soundPitchRise per 1/60 s from what it last played at;
// a drop, or a sound (re)started after a pause, is as the game sets it.
typedef HRESULT (__stdcall *SetFreq_t)(void*, DWORD);
static SetFreq_t g_dsSetFreq;
struct FreqSeen { void* buf; DWORD freq, ms; };
static FreqSeen g_freqSeen[64];
static HRESULT __stdcall HkDsSetFreq(void* b, DWORD f) {
    DWORD now = GetTickCount();
    FreqSeen* e = nullptr; FreqSeen* slot = &g_freqSeen[0];
    for (FreqSeen& x : g_freqSeen) { if (x.buf == b) { e = &x; break; } if (now - x.ms > now - slot->ms) slot = &x; }
    if (e && f && e->freq && now - e->ms < 100 && g_cfg.soundPitchRise > 0.0f && f > e->freq) {
        float steps = (now - e->ms) / 16.7f;
        if (steps < 1.0f) steps = 1.0f;
        float cap = (float)e->freq * (1.0f + g_cfg.soundPitchRise * steps);
        if ((float)f > cap) f = (DWORD)cap;
    }
    if (!e) { e = slot; e->buf = b; }
    e->freq = f; e->ms = now;
    return g_dsSetFreq(b, f);
}
static void Sound3dHook() {                                     // (the game's sound is up: find the methods)
    BYTE* ds = *reinterpret_cast<BYTE**>(hot::dsSingleton);
    void* dev = ds ? *reinterpret_cast<void**>(ds + 0x30) : nullptr;
    void* lsn = ds ? *reinterpret_cast<void**>(ds + 0xD0) : nullptr;
    if (!dev || !lsn) return;                                   // (not yet)
    if (!g_snd3dTries)
        Logf("sound: the device reports %lu hardware 3D voices; EAX %s\n", *reinterpret_cast<DWORD*>(ds + 0x60), ds[0xDA] ? "on" : "off");
    if (++g_snd3dTries > 3) { g_snd3dState = -1; Logf("sound: 3D sound hook FAILED (no 3D buffer)\n"); return; }
    WAVEFORMATEX wf = {}; wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1; wf.nSamplesPerSec = 22050;
    wf.wBitsPerSample = 16; wf.nBlockAlign = 2; wf.nAvgBytesPerSec = 44100;
    struct { DWORD size, flags, bytes, reserved; WAVEFORMATEX* fmt; GUID alg; } desc = { 36, 0xB0, 4096, 0, &wf, GUID() };
    void* buf = nullptr;                                        // (as the game's 3D sounds: CTRL3D | CTRLFREQUENCY | CTRLVOLUME)
    typedef HRESULT (__stdcall *Create_t)(void*, void*, void**, void*);
    if (reinterpret_cast<Create_t>((*reinterpret_cast<void***>(dev))[0x0C / 4])(dev, &desc, &buf, nullptr) < 0 || !buf) return;
    static const GUID iid3d = { 0x279AFA86, 0x4981, 0x11CE, { 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60 } };
    void* b3 = nullptr;
    reinterpret_cast<QI_t>((*reinterpret_cast<void***>(buf))[0])(buf, iid3d, &b3);
    bool ok = false;
    if (b3) {
        void** vt3 = *reinterpret_cast<void***>(b3);
        void** vtl = *reinterpret_cast<void***>(lsn);
        g_ds3Release = reinterpret_cast<Rel_t>(vt3[2]);
        g_ds3SetPos = reinterpret_cast<Set3_t>(vt3[0x4C / 4]);
        g_ds3SetVel = reinterpret_cast<Set3_t>(vt3[0x50 / 4]);
        g_dsLsnSetPos = reinterpret_cast<Set3_t>(vtl[0x38 / 4]);
        g_dsLsnDoppler = reinterpret_cast<SetF_t>(vtl[0x30 / 4]);
        g_dsLsnSetVel = reinterpret_cast<Set3_t>(vtl[0x40 / 4]);
        ok = PatchPointer(&vt3[0x4C / 4], reinterpret_cast<void*>(&HkDs3SetPos)) &&
             PatchPointer(&vt3[0x50 / 4], reinterpret_cast<void*>(&HkDs3SetVel)) &&
             PatchPointer(&vt3[2], reinterpret_cast<void*>(&HkDs3Release)) &&
             PatchPointer(&vtl[0x38 / 4], reinterpret_cast<void*>(&HkDsLsnSetPos)) &&
             PatchPointer(&vtl[0x30 / 4], reinterpret_cast<void*>(&HkDsLsnDoppler)) &&
             PatchPointer(&vtl[0x40 / 4], reinterpret_cast<void*>(&HkDsLsnSetVel));
        reinterpret_cast<Rel_t>(vt3[2])(b3);                    // (through the hook if it's in: not tracked anyway)
        if (g_cfg.soundDoppler == 2) g_dsLsnDoppler(lsn, g_cfg.soundDopplerScale, 0);   // (DS3D_IMMEDIATE)
    }
    {
        void** vtb = *reinterpret_cast<void***>(buf);
        g_dsSetFreq = reinterpret_cast<SetFreq_t>(vtb[0x44 / 4]);
        if (!PatchPointer(&vtb[0x44 / 4], reinterpret_cast<void*>(&HkDsSetFreq))) Logf("pitch: watch patch FAILED\n");
    }
    reinterpret_cast<Rel_t>((*reinterpret_cast<void***>(buf))[2])(buf);
    g_snd3dState = ok ? 1 : -1;
    if (!ok) { Logf("sound: 3D sound hook FAILED\n"); return; }
    if (g_cfg.soundPitchRise > 0.0f) Logf("sound: a held sound's pitch rises at most %.0f%% per 1/60 s (engine blips)\n", g_cfg.soundPitchRise * 100.0f);
    if (g_cfg.soundDoppler == 2) Logf("sound: Doppler from the sounds' movement (x%.2f)\n", g_cfg.soundDopplerScale);
    if (g_cfg.soundOcclusion) Logf("sound: occlusion %s\n", ds[0xDA] ? "on (EAX)" : "off (no EAX)");
}
// Is there track between two DirectSound positions? (the game's line test, both-sided)
static bool WallBetween(const float* a, const float* b) {
    float s[3] = { -a[0], a[1], a[2] }, t[3] = { -b[0], b[1], b[2] };
    BYTE save[0x3C]; memcpy(save, reinterpret_cast<void*>(hot::lineHitGlobals), sizeof(save));
    int mode = *reinterpret_cast<int*>(hot::lineSides);
    *reinterpret_cast<int*>(hot::lineSides) = 2;
    bool hit = reinterpret_cast<bool (__cdecl *)(const float*, const float*, void*)>(hot::fnLineCollide)(s, t, nullptr);
    *reinterpret_cast<int*>(hot::lineSides) = mode;
    memcpy(reinterpret_cast<void*>(hot::lineHitGlobals), save, sizeof(save));
    return hit;
}
static void OcclusionStep() {
    if (!EaxOn()) return;
    int rays = 0;
    for (int n = 0; n < 256 && rays < 4; n++) {                 // a few line tests a step, round robin
        SndSrc& e = g_sndSrc[g_occNext]; g_occNext = (g_occNext + 1) & 255;
        if (!e.buf || g_sndStep - e.step > 30) continue;        // (gone, or not placed for half a second)
        float d[3] = { e.pos[0] - g_lsnPos[0], e.pos[1] - g_lsnPos[1], e.pos[2] - g_lsnPos[2] };
        float dd = Dot3(d, d);
        if (dd < 25.0f || dd > 400.0f * 400.0f) { e.occTarget = 0; continue; }   // (at you - your car - or out of earshot)
        // the line stops 2 units short of the sound: one at the road (wheels, skids,
        // landings) would otherwise hit the very surface it sits on
        float l = sqrtf(dd), k = (l - 2.0f) / l;
        float end[3] = { g_lsnPos[0] + d[0] * k, g_lsnPos[1] + d[1] * k, g_lsnPos[2] + d[2] * k };
        e.occTarget = WallBetween(g_lsnPos, end) ? -(LONG)(g_cfg.soundOcclusionDb * 100.0f) : 0;
        rays++;
    }
    for (SndSrc& e : g_sndSrc) {                                // ease toward it: 6 dB a step
        if (!e.buf || e.occ == e.occTarget) continue;
        if (!e.ksTried) {
            e.ksTried = true;
            reinterpret_cast<QI_t>((*reinterpret_cast<void***>(e.buf))[0])(e.buf, kIidKsPropertySet, &e.ks);
        }
        if (!e.ks) { e.occ = e.occTarget; continue; }
        LONG v = e.occ + (e.occTarget > e.occ ? 600 : -600);
        if ((e.occTarget - v) * (e.occTarget - e.occ) <= 0) v = e.occTarget;   // (passed it)
        HRESULT hr = reinterpret_cast<KsSet_t>((*reinterpret_cast<void***>(e.ks))[0x10 / 4])(e.ks, kEax40Source, kEaxSourceOcclusion,
                                                                                           nullptr, 0, &v, sizeof(v));
        if (hr < 0 && InterlockedIncrement(&g_occFailLogged) <= 3) Logf("sound: EAX occlusion not taken (%08lX)\n", (unsigned long)hr);
        e.occ = v;
    }
}
// Fade-out (soundFade). A sound is stopped - to make room for another, when its
// voice is torn down (3D 0x5367C0, 2D 0x536860), or a 2D loop that timed out
// (0x5379F8) - with Stop, then released: a cut mid-wave, a click. Those Stops
// (6 bytes each: mov edx,[ecx+0x48] / push eax / call edx -> push eax / call)
// come here instead: a playing buffer is kept (AddRef) and turned down 12 dB at
// once and then each sound update (60 Hz), stopped and let go from -36 dB -
// about 50 ms. The game releases its references at once as before; the voice
// slot is free at once. Stopping every voice at once (0x537060: the window
// losing focus, a sound reset - the updates may stop then) stays instant, and
// ends the fades still going first; so does a fade that's had no update for
// 150 ms, at the next.
typedef ULONG (__stdcall *Ref_t)(void*);
struct SndFade { void* buf; LONG vol0; LONG step; DWORD ms; };
static SndFade g_fades[64];
static void FadeEnd(SndFade& f) {
    void** vt = *reinterpret_cast<void***>(f.buf);
    reinterpret_cast<HRESULT (__stdcall *)(void*)>(vt[0x48 / 4])(f.buf);     // Stop
    reinterpret_cast<Ref_t>(vt[2])(f.buf);                                    // our reference
    f.buf = nullptr;
}
static HRESULT __stdcall FadeStop(void* buf) {
    void** vt = *reinterpret_cast<void***>(buf);
    typedef HRESULT (__stdcall *Stop_t)(void*);
    DWORD status = 0, now = GetTickCount();
    SndFade* slot = nullptr;
    for (SndFade& f : g_fades) {
        if (f.buf && now - f.ms > 150) FadeEnd(f);              // (no sound updates for a while: finish them now)
        if (!f.buf && !slot) slot = &f;
    }
    reinterpret_cast<HRESULT (__stdcall *)(void*, DWORD*)>(vt[0x24 / 4])(buf, &status);   // GetStatus
    LONG vol = 0;
    if (!slot || !(status & 1 /* DSBSTATUS_PLAYING */) ||
        reinterpret_cast<HRESULT (__stdcall *)(void*, LONG*)>(vt[0x18 / 4])(buf, &vol) < 0)   // GetVolume
        return reinterpret_cast<Stop_t>(vt[0x48 / 4])(buf);     // (not playing, or no room: as the game)
    reinterpret_cast<Ref_t>(vt[1])(buf);                        // AddRef: ours until the fade ends
    slot->buf = buf; slot->vol0 = vol; slot->step = g_sndStep; slot->ms = now;
    LONG v = vol - 1200; if (v < -10000) v = -10000;
    reinterpret_cast<HRESULT (__stdcall *)(void*, LONG)>(vt[0x3C / 4])(buf, v);   // SetVolume: the first step now
    return S_OK;
}
static void FadeFlushAll() { for (SndFade& f : g_fades) if (f.buf) FadeEnd(f); }
static void __cdecl HkStopAll() {
    FadeFlushAll();
    reinterpret_cast<void (__cdecl *)()>(hot::fnStopAll)();
}
static void FadeStep() {
    for (SndFade& f : g_fades) {
        if (!f.buf) continue;
        LONG k = g_sndStep - f.step;
        if (k < 1) continue;
        LONG v = f.vol0 - 1200 * (k + 1);                       // (-12 dB came with the stop: -24, -36, then off)
        if (k >= 3 || v <= -10000) { FadeEnd(f); continue; }
        reinterpret_cast<HRESULT (__stdcall *)(void*, LONG)>((*reinterpret_cast<void***>(f.buf))[0x3C / 4])(f.buf, v);   // SetVolume
    }
}
// Volumes. The sound menu's volumes are kept in the player's profile (+0xC0..)
// and put back when it's loaded (0x45C577); until then - the front end, the
// first menus - they're the boot default, 50% (0x92D0F0 = 0x1FFF each). (The
// game's Settings.cfg would have kept settings too, but it's never written:
// its writer is a stub with no live callers - nothing a player sets lives
// there.) soundKeepVolume: the menu's confirm (0x50A35F, values changed) is
// noted in BTOverdrive_volume.txt, and at the first sound update those are
// applied (0x4335B0). A profile loaded later still puts in its own.
static bool g_volRestored = false;
static void VolPath(char* out, size_t n) { snprintf(out, n, "%s" MOD_FILE "_volume.txt", g_asiDir); }
static void __attribute__((thiscall)) HkVolConfirm(BYTE* vols) {
    reinterpret_cast<void (__attribute__((thiscall)) *)(BYTE*)>(hot::fnVolApply)(vols);
    const int* v = reinterpret_cast<const int*>(vols);
    char path[MAX_PATH]; VolPath(path, sizeof(path));
    if (FILE* f = fopen(path, "w")) {
        fprintf(f, "%d %d %d\n; sound effects, music, speech (0-16383): the sound menu's, applied at start\n", v[0], v[1], v[2]);
        fclose(f);
        Logf("sound: volumes kept (effects %d, music %d, speech %d)\n", v[0], v[1], v[2]);
    }
}
static void VolRestore() {
    g_volRestored = true;
    char path[MAX_PATH]; VolPath(path, sizeof(path));
    FILE* f = fopen(path, "r");
    if (!f) return;
    int v[3];
    bool ok = fscanf(f, "%d %d %d", &v[0], &v[1], &v[2]) == 3;
    fclose(f);
    if (!ok) return;
    for (int& x : v) x = x < 0 ? 0 : x > 16383 ? 16383 : x;
    int* live = reinterpret_cast<int*>(hot::volumes);
    for (int i = 0; i < 3; i++) live[i] = v[i];
    reinterpret_cast<void (__attribute__((thiscall)) *)(int*)>(hot::fnVolApply)(live);
    Logf("sound: volumes applied (effects %d, music %d, speech %d)\n", v[0], v[1], v[2]);
}
static bool Sound3dWanted() { return g_cfg.soundDoppler == 2 || g_cfg.soundOcclusion || g_cfg.soundPitchRise > 0.0f; }
static void __cdecl HkSndUpdate(float dt) {
    if (g_snd3dState == 0 && Sound3dWanted()) Sound3dHook();
    if (!g_volRestored && g_cfg.soundKeepVolume && *reinterpret_cast<BYTE**>(hot::dsSingleton)) VolRestore();
    if (dt > 0.0f) g_sndDt = dt;
    g_sndStep++;
    reinterpret_cast<void (__cdecl *)(float)>(hot::fnSndUpdate)(dt);
    if (g_cfg.soundFade) FadeStep();
    if (g_fallOn) FallResets();
    if (g_aiStuckOn) AiStuckResets();
    if (g_snd3dState != 1) return;
    if (g_cfg.soundDoppler == 2 && g_lsnMoving && g_sndStep - g_lsnStep >= 2) {   // (the listener: not moved for 2 updates)
        void* lsn = *reinterpret_cast<BYTE**>(hot::dsSingleton) ? *reinterpret_cast<void**>(*reinterpret_cast<BYTE**>(hot::dsSingleton) + 0xD0) : nullptr;
        g_lsnMoving = false;
        g_lsnVel[0] = g_lsnVel[1] = g_lsnVel[2] = 0.0f;
        if (lsn) g_dsLsnSetVel(lsn, 0, 0, 0, 0);
    }
    if (g_cfg.soundDoppler == 2)
        for (SndSrc& e : g_sndSrc) {
            if (!e.buf) continue;
            if (SndRiding(e) && g_sndStep - e.step < 30) {       // riding with the listener: its velocity, every update
                g_ds3SetVel(e.buf, g_lsnVel[0], g_lsnVel[1], g_lsnVel[2], 0);
                e.moving = g_lsnVel[0] != 0.0f || g_lsnVel[1] != 0.0f || g_lsnVel[2] != 0.0f;
            } else if (e.moving && g_sndStep - e.step >= 2) {   // not moved for 2 updates: still
                e.moving = false; g_ds3SetVel(e.buf, 0, 0, 0, 0);
            }
        }
    if (g_cfg.soundOcclusion) OcclusionStep();
}
// EAX. The game has zone-driven reverb: each frame the trigger zones the camera
// is in (0x610EA0) give a reverb id (0x6105A0, type-1 zone tags; none: 3), and a
// change sets EAX 4's FX slot 0 reverb to one of 9 presets tuned for the game
// (0x537420: 0x935220 + id x 0x70, EAXREVERB_ALLPARAMETERS). At startup it
// probes the device for EAX 4 (0x5398C0) - and then closes it again unless its
// sound settings say "EAX on" (+0x14), which default to 0 (0x5391F0) and are
// only ever set from a saved settings record the PC game never writes: EAX was
// off on every PC, with any card. soundEax=1: the default is 1. And ids 1-13 are
// passed on, but there are 9 presets: one above 8 read past the table (the
// sound volume table after it) - it gets the default, 3.
static LONG g_envLogged = 0;
static void __cdecl HkEnvChange(int id) {
    int use = id >= 0 && id <= 8 ? id : 3;
    if (InterlockedIncrement(&g_envLogged) <= 40)
        Logf("sound: reverb zone %d%s\n", id, use != id ? " (no such preset: 3)" : "");
    reinterpret_cast<void (__cdecl *)(int)>(hot::fnEnvChange)(use);
}
void InstallSoundFx() {
    bool ok = true;
    if (g_cfg.soundEax) {
        static const BYTE one = 1;
        ok = ok && PatchBytes(hot::eaxDefaultOff + 3, &one, 1);
        for (DWORD at : hot::callEnvChange) ok = ok && PatchCall(at, reinterpret_cast<void*>(&HkEnvChange), 5);
    }
    if (g_cfg.soundVoices) {
        static const BYTE n = 0x41;
        ok = ok && PatchBytes(hot::sndHwVoicesCmp + 2, &n, 1) && PatchBytes(hot::sndHwVoicesMin + 6, &n, 1);
    }
    if (g_cfg.soundDoppler == 0) {
        static const BYTE fldz3[3] = { 0xD9, 0xEE, 0x90 }, fldz2[2] = { 0xD9, 0xEE };
        ok = ok && PatchBytes(hot::sndDopplerLd, fldz3, 3) && PatchBytes(hot::sndDopplerInit, fldz2, 2);
    }
    if (g_cfg.soundKeepVolume)
        ok = ok && PatchCall(hot::callVolConfirm, reinterpret_cast<void*>(&HkVolConfirm), 5);
    if (Sound3dWanted() || g_cfg.soundFade || g_cfg.soundKeepVolume || g_cfg.fallReset || g_cfg.aiUnstuck) {   // (the per-step hook they use)
        g_sndStepHooked = PatchCall(hot::callSndUpdate, reinterpret_cast<void*>(&HkSndUpdate), 5);
        ok = ok && g_sndStepHooked;
    }
    if (g_cfg.soundFade)
        for (DWORD at : hot::callStopAll) ok = ok && PatchCall(at, reinterpret_cast<void*>(&HkStopAll), 5);
    if (g_cfg.soundFade)
        for (DWORD at : hot::voiceStop) {
            BYTE b[6] = { 0x50, 0xE8 };                             // push eax / call FadeStop (stdcall: pops it)
            DWORD rel = reinterpret_cast<DWORD>(&FadeStop) - (at + 6); memcpy(b + 2, &rel, 4);
            ok = ok && PatchBytes(at, b, 6);
        }
    if (g_cfg.fastLoad) {
        static const BYTE one = 1;
        ok = ok && PatchBytes(hot::ioThreadWait + 1, &one, 1) &&
             PatchCall(hot::callJobsDone, reinterpret_cast<void*>(&HkJobsDone), 5);
    }
    if (g_cfg.soundHeadroom > 0.0f) {
        float t[17]; memcpy(t, reinterpret_cast<void*>(hot::sndVolTable), sizeof(t));
        float d = g_cfg.soundHeadroom * 100.0f;
        for (float& v : t) { v -= d; if (v < -10000.0f) v = -10000.0f; }
        ok = ok && PatchBytes(hot::sndVolTable, t, sizeof(t));
    }
    Logf(ok ? "sound: %s voices, Doppler %s, %.1f dB headroom, EAX reverb %s, fade-outs %s; loading polls %s\n" : "sound: patch FAILED\n",
         g_cfg.soundVoices ? "64" : "the game's",
         g_cfg.soundDoppler == 2 ? "from the sounds' movement" : g_cfg.soundDoppler ? "the game's" : "off", g_cfg.soundHeadroom,
         g_cfg.soundEax ? "on (if the device has EAX)" : "off", g_cfg.soundFade ? "on" : "off", g_cfg.fastLoad ? "every 1 ms" : "every 10 ms");
}
