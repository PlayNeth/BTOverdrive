// ---------------------------------------------------------------------------
// Game addresses
// ---------------------------------------------------------------------------
#pragma once
#include "common.h"
#include "items.h"

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
    const DWORD boneFetch      = 0x54AF55;   // skinned draw: esi = &bones[index[i]] (15 bytes), then copied to g_BoneMatrices
    const DWORD callFrameEnd   = 0x40ECB0;   // call 0x54DC30 (thiscall, renderer): after the 2D pass and the flush
    const DWORD fnFrameEnd     = 0x54DC30;
    const DWORD callGameStep   = 0x40EC53;   // call 0x59B3F0: a 60 Hz step's update (perf timing)
    const DWORD fnGameStep     = 0x59B3F0;
    const DWORD call2D1        = 0x40EC9D;   // call 0x421100 (2D pass)
    const DWORD fn2D1          = 0x421100;
    const DWORD call2D2        = 0x40ECA2;   // call 0x421150
    const DWORD fn2D2          = 0x421150;
    const DWORD callFlush      = 0x40ECA9;   // call 0x54DA00 (thiscall, renderer): the renderer's flush - every draw call
    const DWORD fnFlush        = 0x54DA00;
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
    // tuning (see "Tuning")
    const DWORD scrReadFloat   = 0x5C6B70;   // Script::ReadFloat (thiscall): next token, atof
    const DWORD scrReadInt     = 0x5C6B90;   // Script::ReadInt(bool* ok) (thiscall): next token, atoi
    const DWORD scrNextToken   = 0x5C6450;   // Script::NextToken (thiscall); current token at +0x214
    const DWORD scrFileName    = 0x5B2130;   // cdecl (index, char* out): archive file name; the script's index at +0x20C
    // speed field of view (see "Speed FOV")
    const DWORD callCamFov     = 0x5BA4ED;   // call 0x53C980 in Camera::Apply: set the field of view (cdecl, degrees)
    const DWORD fnSetFov       = 0x53C980;
    const DWORD worldFrozen    = 0x960B24;
    const DWORD frameDt        = 0x960B14;   // float: this step's time (set by 0x5A2330; the Flash UI's clock)   // byte: 1 = the world is frozen (pause menu, countdown; set by 0x5A2420)
    // hazard hits (see "Hazard hits")
    const DWORD callHitRoll    = 0x457CF6;   // call 0x4BACF0 (push 0 / push 1 before it): a weapon hit's roll, in a car's take-hit
                                             //   (0x457C70, thiscall car, hit; here ebx = the car, esi = the hit: +0 its push direction)
    const DWORD callBlastHit   = 0x4A38F0;   // call 0x457C70 in an explosion's update (0x4A3610): each car in its reach, a hit of kind 1
                                             //   (a small push, 0x456290); here esi = the car, edi = the explosion (+0x2C centre, +0x10 reach squared)
    const DWORD fnTakeHit      = 0x457C70;
    const DWORD callIceHit     = 0x4A341B;   // call 0x478E40 (freeze, thiscall car+0x1360): the upgraded ice gun's shot hit a car
    const DWORD callIceFull    = 0x4790C7;   // call 0x478E40 in the freeze update (0x479050): the ice built up to 10 (the plain ice gun)
    const DWORD callMslTarget  = 0x473749;   // call 0x473370 in a missile's flight (0x473630): pick its target (+0x40 a car ahead;
                                             //   homing, +0x1F6: +0x1F8 a racer's track follower)
    const DWORD callShieldOn   = 0x49D65B;   // call 0x4A7D20 in a shield's start (0x49D5D0): car+0x12B0 on (radius, time, mega, hits, 1)
    const DWORD vtSmokeUpd     = 0x8AACDC;   // GSAmmoSmoke's vtable (0x8AACB4) +0x28: its update (0x49DA50, while pouring out)
    const DWORD vtSmokeFlUpd   = 0x8AAD44;   // GSAmmoSmokeFlash's (0x8AAD1C) +0x28
    const DWORD vtParaLaunch   = 0x8AAEBC;   // GSAmmoParachute's (0x8AAEA4) +0x18: launch (0x4710D0, thiscall firer, ?)
    const DWORD vtEmpFire      = 0x8AAFB4;   // GSAmmoEMP's (0x8AAFAC) +0x8: set off (0x49EEA0)
    const DWORD vtEmpUpFire    = 0x8AB01C;   // GSAmmoEMPUpgrade's (0x8AB014) +0x8
    const DWORD callToonCanFire = 0x41E593;  // call 0x451370 (thiscall the car's toon state, car+0xF44): may this state fire? State 4
                                             //   is a track's shield power-up's (GSShieldToonPower_c, 0x4DB110), which says no
    const DWORD callToonCanFire2 = 0x46D22D; // call 0x451370: the same check on the other way an item is set off (0x46D210)
    const DWORD callToonShowWpn[2] = { 0x42F08A, 0x42F0C4 };   // call 0x451380 (+5: may this state hold its item out) in the
                                             //   gunner's update (0x42F070) - no: it's put away (0x42E780)
    const DWORD callWeaponRoll = 0x4620BC;   // call 0x4C3360: a crate's item (thiscall table; position 1.., cars, roll, blue)
    const DWORD carShadowFar   = 0x603D25;   // fadd qword [0x8A4268] (3.0): the car shadow projector's far plane = 10 + 3 + 1.4
    const DWORD musEndNoLoop   = 0x53B041;   // je (6 bytes): the music stream at its end, not set to loop - stop for good
    const DWORD vtSteerSetup   = 0x8C8F94;   // RaceCarSteering's vtable (0x8C8F3C) +0x58: its setup (0x6C6D30, thiscall): +0x74 how fast
                                             //   the wheels turn (0.96 rad/s, 0x8C8FC0), +0x78 the lock (+0x7C: 1.13 rad at most)
    const DWORD fnBlastMake    = 0x4A3950;   // set off an explosion (thiscall: the explosion object, its centre, ...)
    const DWORD callBlastMake[6] = { 0x470B38, 0x4731E2, 0x49D352, 0x49EAA0, 0x4A1128, 0x4A1FE3 };
                                             //   by: a mortar shell, a rocket (plain, bottle, airstrike), a homing rocket,
                                             //   a parachute bomb, an airstrike bomb, a mine
    const DWORD hitRetMortar   = 0x470B14;   // where a take-hit returns to: a mortar shell's direct hit (0x4708B0)
    const DWORD hitRetRocket   = 0x47109D;   //   a rocket's (0x470F80: plain, bottle, homing, airstrike)
    const DWORD fnStuntStart   = 0x4BACF0;   // the car's stunt controller (car+0x810): start a stunt (thiscall, kind, quiet)
    const DWORD contactFilter  = 0x489090;   // collision: delete a contact? (cdecl, contact*): true = delete it
    const DWORD contactNoIds   = 0x95A464;   //   int[2]: material ids whose contacts are always deleted (-1 = none)
    const DWORD contactDelMsg  = 0x8A89C0;   //   "Deleted contact between material IDs %i, %i\n"
    const DWORD fnCarReset     = 0x433F90;   // GSResetOnTrackAP_c: put car +0x60 on the track at matrix +0x10 (thiscall)
    const DWORD fnCarDustBall  = 0x453A20;   // race car: a "DustBall" smoke burst on it (thiscall; the hazards' own hit effect)
    // starting grid (see "Starting grid")
    const DWORD callGridPlace  = 0x47BF43;   // call 0x63E750 in GSWorldInitState_c: put racer on its grid action point
    const DWORD callGridReset  = 0x47D146;   //   the same in GSWorldResetState_c (race restart)
    const DWORD fnPlaceCar     = 0x63E750;   // car: put on the ground at pos facing mat row 2 (thiscall; pos*, mat*, int, float, int)
    const DWORD apUserList     = 0x9680C0;   // ActionPoint* list (count at +4): searched first (0x60F6F0)
    const DWORD apList         = 0x96821C;   // ActionPoint* list (count at +4): then this one (0x60E0A0)
    const DWORD curEvent       = 0x953D80;   // GSEvent_c*: the event being played
    const DWORD fnGameMode     = 0x436E60;   // GSEvent_c: game mode (thiscall): 6 race, 7 rampage, 8 eliminator, 9 battle, 10 tournament
    const DWORD lineTable      = 0x9680D0;   // -> the level's track lines (0x74 bytes each; 0x610000), count at 0x9680D4
    const DWORD lineCount      = 0x9680D4;
    const DWORD juncTable      = 0x9681B4;   // -> junction* table (count 0x9681B8): +0x1C short[8] lines, +0x2C entry, +0x42 count
    const DWORD juncCount      = 0x9681B8;
    const DWORD rampageTopSpd  = 0x92EA20;   // float 0.45: a Rampage AI car's TOP-SPEED = the player's x this (0x456FEA, its only user)
    const DWORD callBrainTargets = 0x670EAE; // call 0x648B50 in the AI brain update: fill the car's target points
    const DWORD blurFlagLevel  = 0x57287C;   // 0x5726C0: jne past "level's g_MotionBlur sets +0x31C" (75 18 -> EB 18: always past)
    const DWORD callHdrPrint   = 0x5727EE;   // call 0x5890F0 ("+%s: %f": each HDR region setting, after it's set)
    const DWORD blurAmount     = 0x575F65;   // post-processing: mov ecx,[ebx+0x234] right before g_MotionBlurAmount is set (amount at [esp+0x10])
    const DWORD shadowTexMake  = 0x57D7BA;   // shadow map: mov eax,[edi+0xB8] ... device->CreateTexture(8, 8, 1, RT, R32F) ... (to 0x57D7E7)
    const DWORD shadowTexEnd   = 0x57D7E7;
    const DWORD shadowDepthMake = 0x57D7F8;  // mov edi,[edi+0xB8] ... device->CreateDepthStencilSurface(8, 8, D24X8) (to 0x57D815)
    const DWORD shadowDepthEnd = 0x57D815;
    const DWORD shadowViewport = 0x57C28F;   // shadow caster pass: mov edi,8 (viewport width/height)
    const DWORD shadowSizeParam = 0x54A70D;  // fld dword ptr [0x8A57CC] (8.0; 14 other users) -> g_ShadowMapSize
    const DWORD shadowDepthK   = 0x8A0988;   // float 64: the sun shadow's depth along the camera (one user, 0x43606E)
    const DWORD shadowFadeK    = 0x8A0984;   // float 16: its fade width / back-off (one user, 0x43608B)
    const DWORD shadowWideK    = 0x8A0978;   // float 128: its width (one user, 0x4360E3)
    const DWORD alphaSort      = 0x54B8F8;   // alpha pass: cmp eax,4 / mov ecx,[esi+0x10] - its own sort by pass only (to 0x54BB17)
    const DWORD alphaSortEnd   = 0x54BB17;   // ...where the first mesh list's sort starts (to 0x54BD77)
    const DWORD meshSort2      = 0x54BD77;   // the second mesh list's sort (to 0x54BFE7)
    const DWORD meshSortEnd    = 0x54BFE7;
    const DWORD callBlendMode  = 0x547204;   // call 0x5420F0 (thiscall renderer, mode): a material's blend mode
    const DWORD fnBlendMode    = 0x5420F0;
    const DWORD fnZWrite       = 0x541FE0;   // renderer: depth writing on/off (thiscall, byte) - through its state cache
    const DWORD skyFarLd       = 0x53E7B9;   // sky pass: fld dword ptr [0x8AA444] (5000: its far plane)
    const DWORD skySetWorld1   = 0x53E84F;   // mov edx,[eax+0x224] (the renderer's SetWorld, called at 0x53E907 for the sky)
    const DWORD skySetWorld2   = 0x53EA33;   //   the same for the second sky instance (called at 0x53EAEB)
    const DWORD shadowBackOffLd = 0x64D084;  // fld dword ptr [0x9698F0]: the shadow light's back-off toward the sun
    const DWORD shadowFarLd    = 0x64DDF9;   // fld dword ptr [0x969910]: its far plane
    const DWORD shadowFadeLd   = 0x64DE17;   // fld dword ptr [0x969924]: the fade width for the shaders
    const DWORD callContactCopy1 = 0x65B2F1; // call 0x5C41C0: the surface's contact sample into car+0x13C (world scrape)
    const DWORD callContactCopy2 = 0x65B501; //   the same, car-vs-car scrape
    const DWORD ui2DYSub[3]    = { 0x53C28A, 0x55A0EA, 0x53C349 };   // fsubrp st(1),st: 2D vertex y = H/2 - y - 0.5
    const DWORD callTrackName  = 0x48073C;   // call 0x59D840: the track select's name text, on left/right
    const DWORD fnStrFormat    = 0x59D840;   // string object: set from a format (cdecl: dst, fmt, ...; fmt itself a string object)
    const DWORD vtScrollDlgUpd = 0x8C53D8;   // CSII_ScrollDialog vtable +0x14: its update (0x681160, thiscall)
    const DWORD fnMusicPlay    = 0x538C20;   // Music: play the opened track (cdecl, no args)
    const DWORD sndHwVoicesCmp = 0x53A21B;   // cmp eax,0x10: the card's 3D buffer count, at least 16
    const DWORD sndHwVoicesMin = 0x53A226;   // mov [esi+0xC8],0x10 (10 bytes)
    const DWORD sndDopplerLd   = 0x537DF2;   // fld [esi+0x24]: a sound's Doppler factor, set on the listener as it starts
    const DWORD eaxDefaultOff  = 0x53926F;   // mov [ecx+0x14],0: the sound settings' "EAX on" default (never set to 1 on PC)
    const DWORD callEnvChange[2] = { 0x6105DC, 0x6105E8 };   // call 0x537420: a trigger zone's reverb preset (cdecl, id)
    const DWORD fnEnvChange    = 0x537420;   //   (EAX 4 FX slot 0 reverb = preset 0x935220 + id x 0x70: 9 presets, 0-8)
    const DWORD voiceStop[3]   = { 0x5367D0, 0x53686C, 0x5379F8 };   // mov edx,[ecx+0x48] / push eax / call edx: Stop a voice's
                                             //   buffer before releasing it (3D teardown, 2D teardown, a 2D loop timed out)
    const DWORD callStopAll[3] = { 0x537100, 0x537125, 0x539FF9 };   // call 0x537060: stop every 3D voice at once
    const DWORD fnStopAll      = 0x537060;   //   (cdecl; losing focus 0x40EA64.., a sound reset)
    const DWORD callOvRead     = 0x53B012;   // call 0x708820: ov_read in the music decode loop
    const DWORD fnOvRead       = 0x708820;
    const DWORD ioThreadWait   = 0x58E3EB;   // push 0xA: the archive I/O thread's poll, 10 ms
    const DWORD fnLineCollide  = 0x635580;   // bool cdecl (start, end, exclude kd): a segment vs the track's static geometry
    const DWORD lineHitGlobals = 0x969818;   //   its hit results, 0x3C bytes (kept around ours)
    const DWORD lineSides      = 0x96A2D0;   //   int: triangles' sides it tests (0 front, 1 back, 2 both)
    const DWORD volumes        = 0x92D0F0;   // the live volumes: SFX, music, speech (ints 0..16383; 0x1FFF at boot)
    const DWORD fnVolApply     = 0x4335B0;   //   apply them (thiscall, the volumes)
    const DWORD callVolConfirm = 0x50A35F;   // call 0x4335B0: the sound menu's confirm, values changed
    const DWORD callPoolRelease[2] = { 0x4483EE, 0x44853E };   // call 0x5CF890: GSNxAllocator_c's destructors free its pool
    const DWORD fnPoolRelease  = 0x5CF890;   //   (thiscall, pool: +0 block, +4/+8/+0x18 cleared)
    const DWORD callJobsDone   = 0x47B7E3;   // call 0x431F20: race load step 17, are the drivers' and gunners' models in?
    const DWORD fnJobsDone     = 0x431F20;   //   (thiscall, the job table [0x953D68]: 2 x { manager, jobs, count }; bool)
    const DWORD fnArcService   = 0x5B6F90;   // advance every archive being streamed by one state (cdecl; once a frame)
    const DWORD fnLoadMgrTick  = 0x63C9F0;   // a model load manager's update: its pending loads, one state each (thiscall)
    const DWORD callSndUpdate  = 0x4210EB;   // call 0x537F70 (cdecl, float dt): the sound update, once per 60 Hz step
    const DWORD fnSndUpdate    = 0x537F70;
    const DWORD dsSingleton    = 0x953A50;   // the DirectSound object (+0x30 IDirectSound8, +0xD0 the 3D listener)
    const DWORD sndDopplerInit = 0x53A2BF;   // fld1: the listener's Doppler factor at startup (listener +0x30; 0x53A298 is its distance factor)
    const DWORD sndVolTable    = 0x935650;   // 17 floats: gain 0..1 -> hundredths of dB (-6000 .. 0), read at 0x536722
    const DWORD musChunkMov    = 0x53B2C7;   // mov [esi+0x301B0],0x72D8: the music stream's segment (3 in its DS buffer)
    const DWORD musReadCbMov   = 0x53AC11;   // mov [esi+0x305E8],0x5DE3F0: the stream's ogg read callback
    const DWORD fnMusReadCb    = 0x5DE3F0;   //   (cdecl ptr, size, n, source: the game's file via the archive I/O queue)
    const DWORD callMusThread  = 0x53B7A9;   // call [CreateThread]: the music stream's refill thread
    const DWORD vtPCArcFile    = 0x8B6714;   // archive file: +0x2C position, +0x30 start in the archive, +0x34 size
    const DWORD arcHandle      = 0x936F44;   // the archive's file handle (the I/O thread's)
    const DWORD callIntroPlay  = 0x479780;   // call 0x538C20: race intro, every frame of the 3-2-1 countdown
    const DWORD movIntroGo     = 0x4797BF;   // mov byte ptr [0x959F80],0: the countdown's GO (7 bytes)
    const DWORD callPauseResume1 = 0x47E6D2; // call 0x538C20: leaving the pause menu, music not paused
    const DWORD callPauseResume2 = 0x47EEF0;
    const DWORD introActive    = 0x959F80;   // byte: 1 while a race intro / countdown is on
    const DWORD fnSndCopy      = 0x5C41C0;   // sound handle: copy another (thiscall, src; ret 4)
    const DWORD fnSndBind      = 0x5C38E0;   // sound handle: bind by name (thiscall: bank, name, warn; ret 0xC)
    const DWORD sndMaterialsBank = 0x968914; // -> the "materials" sound bank
    const DWORD fnCollSparks   = 0x441BF0;   // collision sparks (thiscall: contact, mover, impact; ret 0xC)
    const DWORD callCollSparks1 = 0x442714;  // call 0x441BF0
    const DWORD callCollSparks2 = 0x442CC2;  // call 0x441BF0
    const DWORD fnContactVec   = 0x620B00;   // contact (thiscall: out vec3*, body, byte 1): the contact's vector for that body
    const DWORD fnSparkSpawn   = 0x566A40;   // cdecl (type, pos*, vel*, gravity*, float): a spark with its type's colour and width
    const DWORD sparkSpawnEnd  = 0x563D23;   // spark spawn, every particle field set: fld dword ptr [ebx+0x1CB66D8] (now -> P+0x38)
    const DWORD sparkStreakLen = 0x563CB7;   // spark spawn: fld dword ptr [0x89E1AC] (0.25: a streak's length in seconds of travel -> P+0x48)
    const DWORD fnSparkSys     = 0x401424;   // cdecl: the spark system singleton (jmp 0x40D620)
    const DWORD fnBrainTargets = 0x648B50;   //   (thiscall, brain; brain+0xC = car; points at car+0xC64, speeds at car+0xC6C)
    // start boost (see "Start boost")
    const DWORD vt321Update    = 0x8AF4DC;   // GSSingle321_c vtable slot 1: its update, 0x4E8500 (thiscall, no arguments)
    const DWORD fn321Update    = 0x4E8500;
    const DWORD fnAnimPastMark = 0x5C0DC0;   // FlashDialog: past a marker? (thiscall, const char* name) -> bool
    const DWORD strIntroEnd    = 0x8A3A98;   // "intro_end"
    // AI opponents (see "AI opponents")
    const DWORD callIntroGo    = 0x483960;   // call 0x479E30 in GSRaceIntroState_c (vtable +0xBC): GO - start boosts, cars released
    const DWORD fnIntroGo      = 0x479E30;   //   (thiscall, intro state): each car's start boost if the player hit the green
    const DWORD fnCarStartBoost = 0x4545B0;  // race car: the start boost (a boost, fired on it; thiscall, no arguments)
    const DWORD catchupAhead   = 0x66845D;   // jp 0x66847B in CatchUp::Update: an AI ahead of the player -> slowed down
    const DWORD aiFireCooldown = 0x430B65;   // fld [esi+0xAF4] (5.0 s x AI gunners): the wait between any two AI shots
    const DWORD aiTargetChance = 0x46D2F2;   // fadd [0x89EFF0] (40.0): % chance (+10 per difficulty) an AI shot goes for the player
    const DWORD aiAreaLimit    = 0x43097E;   // imm8 2 in cmp [0x95910C], 2: area weapons held while this many are out
    const DWORD callKinStep    = 0x474CD2;   // call 0x4E6630: move a box set's boxes with their parts (once per step)
    const DWORD fnKinStep      = 0x4E6630;   //   (thiscall, box set)
    const DWORD callKinSetup   = 0x474AEA;   // call 0x4E69D0: collision boxes for a trigger's animated model
    const DWORD fnKinSetup     = 0x4E69D0;   //   (thiscall, box set; model, ?, detect-only flag)
    const DWORD callKinCreate  = 0x4E69B2;   // call 0x5F2260: make a box's PhysX actor (thiscall, box; no arguments)
    const DWORD fnKinCreate    = 0x5F2260;   //   (NxBox dimensions = max - min of box +0x54: the part's half-size)
    const DWORD fnSetLinVel    = 0x5F9910;   // vehicle: set linear velocity (thiscall, const float[3])
    const DWORD fnSetAngVel    = 0x5F9970;   // vehicle: set angular velocity (thiscall, const float[3])
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
        { boneFetch,      "\x8B\x4D\x08\x8B\x34\x81\xC1\xE6\x06\x03\xB3\xD0\x01\x00\x00", 15 },   // mov ecx,[ebp+8] / mov esi,[ecx+eax*4] / shl esi,6 / add esi,[ebx+0x1D0]
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
        { 0x5A2420,       "\xB0\x01\xA2\x24\x0B\x96\x00", 7 },     // mov al,1 / mov [0x960B24],al
        { callCamFov,     "\xE8\x8E\x24\xF8\xFF", 5 },               // call 0x53C980
        { scrReadFloat,   "\x51\xE8\xDA\xF8\xFF\xFF\x8B\x89\x14\x02\x00\x00", 12 },   // push ecx / call NextToken / mov ecx,[ecx+0x214]
        { scrReadInt,     "\xE8\xBB\xF8\xFF\xFF\x8B\x44\x24\x04", 9 },               // call NextToken / mov eax,[esp+4]
        { scrNextToken,   "\x80\xB9\x20\x02\x00\x00\x00", 7 },                       // cmp byte [ecx+0x220], 0
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
        { contactFilter,  "\x56\x8B\x74\x24\x08\x57\x8B\x7E\x30", 9 },   // push esi / mov esi,[esp+8] / push edi / mov edi,[esi+0x30]
        { 0x457CF2,       "\x6A\x00\x6A\x01\xE8\xF5\x2F\x06\x00", 9 },   // push 0 / push 1 / call 0x4BACF0
        { 0x4A38EC,       "\x88\x5C\x24\x68\xE8\x7B\x43\xFB\xFF", 9 },   // mov [esp+0x68],bl / call 0x457C70
        { 0x470B0F,       "\xE8\x5C\x71\xFE\xFF", 5 },            // call 0x457C70 (the mortar's)
        { 0x471098,       "\xE8\xD3\x6B\xFE\xFF", 5 },            // call 0x457C70 (the rockets')
        { 0x470B38,       "\xE8\x13\x2E\x03\x00", 5 },            // call 0x4A3950 (x6)
        { 0x4731E2,       "\xE8\x69\x07\x03\x00", 5 },
        { 0x49D352,       "\xE8\xF9\x65\x00\x00", 5 },
        { 0x49EAA0,       "\xE8\xAB\x4E\x00\x00", 5 },
        { 0x4A1128,       "\xE8\x23\x28\x00\x00", 5 },
        { 0x4A1FE3,       "\xE8\x68\x19\x00\x00", 5 },
        { vtSteerSetup,   "\x30\x6D\x6C\x00", 4 },                // -> 0x6C6D30
        { 0x6C6E12,       "\xD9\x05\xC0\x8F\x8C\x00\xD9\x5E\x74", 9 },   // fld [0x8C8FC0] / fstp [esi+0x74]
        { 0x53B03E,       "\x38\x46\x28\x0F\x84\xB5\x00\x00\x00", 9 },   // cmp [esi+0x28],al / je 0x53B0FC
        { 0x603D1E,       "\xD9\x05\x80\x87\x93\x00\x51\xDC\x05\x68\x42\x8A\x00", 13 },   // fld [0x938780] / push ecx / fadd [0x8A4268]
        { 0x4620BC,       "\xE8\x9F\x12\x06\x00", 5 },            // call 0x4C3360
        { 0x4C3360,       "\x8B\x44\x24\x04\x83\xF8\x01\x7C\x05", 9 },   // mov eax,[esp+4] / cmp eax,1 / jl
        { fnTakeHit,      "\x83\xEC\x0C\x53\x8B\xD9\x80\xBB\xD4\x12\x00\x00\x00", 13 },
        { fnStuntStart,   "\x51\x53\x8B\x5C\x24\x0C\x56\x57\x8B\xF1\x33\xFF", 12 },
        { 0x4BAF1F,       "\x80\xBE\x8C\x03\x00\x00\x00", 7 },   // cmp byte [esi+0x38C],0: the roll's push and torque skipped when quiet
        { callKinStep,    "\xE8\x59\x19\x07\x00", 5 },               // call 0x4E6630
        { callGridPlace,  "\xE8\x08\x28\x1C\x00", 5 },               // call 0x63E750
        { callGridReset,  "\xE8\x05\x16\x1C\x00", 5 },               // call 0x63E750
        { vt321Update,    "\x00\x85\x4E\x00", 4 },                   // dd 0x4E8500
        { fn321Update,    "\x53\x55\x56\x8B\xF1\x83\x7E\x30", 8 },   // push ebx/ebp/esi / mov esi,ecx / cmp [esi+0x30],..
        { fnAnimPastMark, "\x83\xEC\x08\x8B\x44\x24\x0C", 7 },   // sub esp,8 / mov eax,[esp+0xC]
        { strIntroEnd,    "intro_end", 10 },
        { callIntroGo,    "\xE8\xCB\x64\xFF\xFF", 5 },               // call 0x479E30
        { fnCarStartBoost, "\x56\x8B\xF1\x68\xA3\x0F\x00\x00", 8 },  // push esi / mov esi,ecx / push 0xFA3 (boost)
        { catchupAhead,   "\x7A\x1C", 2 },                           // jp +0x1C
        { aiFireCooldown, "\xD9\x86\xF4\x0A\x00\x00", 6 },           // fld dword ptr [esi+0xAF4]
        { aiTargetChance, "\xDC\x05\xF0\xEF\x89\x00", 6 },           // fadd qword ptr [0x89EFF0]
        { 0x430978,       "\x83\x3D\x0C\x91\x95\x00\x02", 7 },     // cmp dword ptr [0x95910C], 2
        { fnPlaceCar,     "\x81\xEC\xF8\x00\x00\x00", 6 },           // sub esp, 0xF8
        { fnGameMode,     "\x56\x8B\xF1\x8B\x06\x8B\x50\x18", 8 },   // push esi / mov esi,ecx / mov eax,[esi] / mov edx,[eax+0x18]
        { callBrainTargets, "\xE8\x9D\x7C\xFD\xFF", 5 },             // call 0x648B50
        { 0x456FEA,       "\xD8\x0D\x20\xEA\x92\x00", 6 },           // fmul dword ptr [0x92EA20]
        { sparkStreakLen, "\xD9\x05\xAC\xE1\x89\x00", 6 },           // fld dword ptr [0x89E1AC]
        { callGameStep,   "\xE8\x98\xC7\x18\x00", 5 },              // call 0x59B3F0
        { call2D1,        "\xE8\x5E\x24\x01\x00", 5 },              // call 0x421100
        { call2D2,        "\xE8\xA9\x24\x01\x00", 5 },              // call 0x421150
        { callFlush,      "\xE8\x52\xED\x13\x00", 5 },              // call 0x54DA00
        { shadowTexMake,  "\x8B\x87\xB8\x00\x00\x00\x6A\x00", 8 },   // mov eax,[edi+0xB8] / push 0
        { 0x57D7CB,       "\x6A\x72\x6A\x01\x6A\x01\x6A\x08", 8 },   // push 0x72 / push 1 / push 1 / push 8
        { shadowDepthMake, "\x8B\xBF\xB8\x00\x00\x00\x8B\x07", 8 },   // mov edi,[edi+0xB8] / mov eax,[edi]
        { 0x57D80C,       "\x6A\x4D\x6A\x08\x6A\x08\x57\xFF\xD1", 9 },   // push 0x4D / push 8 / push 8 / push edi / call ecx
        { shadowViewport, "\xBF\x08\x00\x00\x00", 5 },             // mov edi,8
        { shadowSizeParam, "\xD9\x05\xCC\x57\x8A\x00", 6 },        // fld dword ptr [0x8A57CC]
        { shadowDepthK,   "\x00\x00\x80\x42", 4 },                   // 64.0f
        { shadowFadeK,    "\x00\x00\x80\x41", 4 },                   // 16.0f
        { shadowWideK,    "\x00\x00\x00\x43", 4 },                   // 128.0f
        { alphaSort,      "\x83\xF8\x04\x8B\x4E\x10\x8D\xBC\x8B\x28\x36\xD5\x00", 13 },   // cmp eax,4 / mov ecx,[esi+0x10] / lea edi,[ebx+ecx*4+0xD53628]
        { alphaSortEnd,   "\x8B\x0E\x8B\x46\x04\x83\xF8\x04\x8D\xBC\x8B\x28\x41\xEC\x00", 15 },   // mov ecx,[esi] / mov eax,[esi+4] / cmp eax,4 / lea edi,[ebx+ecx*4+0xEC4128]
        { meshSort2,      "\x8B\x4E\x08\x8B\x46\x0C\x83\xF8\x04\x8D\xBC\x8B\xA8\x79\xED\x00", 16 },   // mov ecx,[esi+8] / mov eax,[esi+0xC] / cmp eax,4 / lea edi,[ebx+ecx*4+0xED79A8]
        { meshSortEnd,    "\x83\x7C\x24\x28\x03", 5 },               // cmp dword ptr [esp+0x28],3
        { callBlendMode,  "\xE8\xE7\xAE\xFF\xFF", 5 },               // call 0x5420F0
        { fnBlendMode,    "\x8B\x54\x24\x04\x83\xFA\x07", 7 },     // mov edx,[esp+4] / cmp edx,7
        { fnZWrite,       "\x8B\x81\xB4\x00\x00\x00\x80\xB8\xEC\x08\x01\x00\x00", 13 },   // mov eax,[ecx+0xB4] / cmp byte ptr [eax+0x108EC],0
        { skyFarLd,       "\xD9\x05\x44\xA4\x8A\x00", 6 },           // fld dword ptr [0x8AA444]
        { skySetWorld1,   "\x8B\x90\x24\x02\x00\x00", 6 },           // mov edx,[eax+0x224]
        { skySetWorld2,   "\x8B\x90\x24\x02\x00\x00", 6 },
        { 0x53E905,       "\x8B\xCD\xFF\xD2", 4 },                   // mov ecx,ebp / call edx
        { 0x53EAE9,       "\x8B\xCD\xFF\xD2", 4 },
        { shadowBackOffLd, "\xD9\x05\xF0\x98\x96\x00", 6 },        // fld dword ptr [0x9698F0]
        { shadowFarLd,    "\xD9\x05\x10\x99\x96\x00", 6 },          // fld dword ptr [0x969910]
        { shadowFadeLd,   "\xD9\x05\x24\x99\x96\x00", 6 },          // fld dword ptr [0x969924]
        { 0x53C27F,       "\xD9\x81\xC0\x00\x00\x00\xD8\xC9\xD8\x60\x04\xDE\xE1", 13 },   // fld [ecx+0xC0] / fmul st,st1 / fsub [eax+4] / fsubrp
        { 0x55A0DF,       "\xD9\x81\xC0\x00\x00\x00\xD8\xC9\xD8\x60\x04\xDE\xE1", 13 },
        { 0x53C33E,       "\xD9\x81\xC0\x00\x00\x00\xD8\xC9\xD8\x60\x04\xDE\xE1", 13 },
        { callTrackName,  "\xE8\xFF\xD0\x11\x00", 5 },            // call 0x59D840
        { vtScrollDlgUpd, "\x60\x11\x68\x00", 4 },                // -> 0x681160
        { sndHwVoicesCmp, "\x83\xF8\x10", 3 },
        { sndHwVoicesMin, "\xC7\x86\xC8\x00\x00\x00\x10\x00\x00\x00", 10 },
        { sndDopplerLd,   "\xD9\x46\x24", 3 },
        { sndDopplerInit, "\xD9\xE8", 2 },
        { callSndUpdate,  "\xE8\x80\x6E\x11\x00", 5 },            // call 0x537F70
        { voiceStop[0],   "\x8B\x51\x48\x50\xFF\xD2", 6 },     // mov edx,[ecx+0x48] / push eax / call edx
        { voiceStop[1],   "\x8B\x51\x48\x50\xFF\xD2", 6 },
        { voiceStop[2],   "\x8B\x51\x48\x50\xFF\xD2", 6 },
        { callStopAll[0], "\xE8\x5B\xFF\xFF\xFF", 5 },            // call 0x537060
        { callStopAll[1], "\xE8\x36\xFF\xFF\xFF", 5 },
        { callStopAll[2], "\xE8\x62\xD0\xFF\xFF", 5 },
        { fnLineCollide,  "\x83\xEC\x30\x8B\x44\x24\x38\x8B\x08\x8B\x50\x04", 12 },
        { 0x50A35A,       "\xB9\xF0\xD0\x92\x00\xE8\x4C\x92\xF2\xFF", 10 },   // mov ecx,0x92D0F0 / call 0x4335B0
        { fnVolApply,     "\x56\x8B\xF1\x8B\x06\x50\xE8", 7 },
        { callPoolRelease[0], "\xE8\x9D\x74\x18\x00", 5 },        // call 0x5CF890
        { callPoolRelease[1], "\xE8\x4D\x73\x18\x00", 5 },
        { fnPoolRelease,  "\x56\x8B\xF1\x83\x7E\x04\x00\x74\x20\x8B\x06\x8B\x48\x04", 14 },
        { 0x47B7DA,       "\x8B\x0D\x68\x3D\x95\x00\x83\xC4\x04\xE8\x38\x67\xFB\xFF\x84\xC0\x74\x13", 18 },   // mov ecx,[0x953D68] / .. / call 0x431F20
        { fnJobsDone,     "\x83\xEC\x08\x53\x55\x56\x57\xC6\x44\x24\x13\x01", 12 },
        { fnArcService,   "\x51\x56\x8B\x35\xB4\x12\x96\x00\x85\xF6", 10 },
        { fnLoadMgrTick,  "\x56\x8B\xF1\x80\x7E\x4C\x02\x75\x05", 9 },
        { callOvRead,     "\xE8\x09\xD8\x1C\x00", 5 },            // call 0x708820
        { ioThreadWait,   "\x6A\x0A\x50\xFF\xD6", 5 },            // push 0xA / push eax / call esi (WaitForSingleObject)
        { eaxDefaultOff,  "\xC7\x41\x14\x00\x00\x00\x00", 7 },
        { 0x6105DC,       "\xE8\x3F\x6E\xF2\xFF", 5 },            // call 0x537420
        { 0x6105E8,       "\xE8\x33\x6E\xF2\xFF", 5 },
        { fnEnvChange,    "\x56\x8B\x74\x24\x08\x56\x68\x88\x34\x8B\x00\xE8", 12 },
        { sndVolTable,    "\x00\x80\xBB\xC5\x00\x00\x16\xC5", 8 },   // -6000.0, -2400.0
        { musChunkMov,    "\xC7\x86\xB0\x01\x03\x00\xD8\x72\x00\x00", 10 },
        { musReadCbMov,   "\xC7\x86\xE8\x05\x03\x00\xF0\xE3\x5D\x00", 10 },
        { callMusThread,  "\xFF\x15\x84\x6D\x97\x00", 6 },     // call dword ptr [CreateThread]
        { 0x58E4CB,       "\x8B\x46\x30\x03\x46\x2C\x8B\x0D\x44\x6F\x93\x00", 12 },   // I/O thread: start + position, the archive handle
        { 0x58E290,       "\x53\x8B\x5C\x24\x0C\x56\x8B\xF1\x8B\x46\x34", 11 },     // PCArcFile::Read: +0x34 size
        { callIntroPlay,  "\xE8\x9B\xF4\x0B\x00", 5 },            // call 0x538C20
        { movIntroGo,     "\xC6\x05\x80\x9F\x95\x00\x00", 7 },    // mov byte ptr [0x959F80],0
        { callPauseResume1, "\xE8\x49\xA5\x0B\x00", 5 },          // call 0x538C20
        { callPauseResume2, "\xE8\x2B\x9D\x0B\x00", 5 },          // call 0x538C20
        { callContactCopy1, "\xE8\xCA\x8E\xF6\xFF", 5 },            // call 0x5C41C0
        { callContactCopy2, "\xE8\xBA\x8C\xF6\xFF", 5 },
        { fnSndCopy,      "\x56\x8B\xF1\x8B\x06\x57\x8B\x7C\x24\x0C", 10 },   // push esi / mov esi,ecx / mov eax,[esi] / push edi / mov edi,[esp+0xC]
        { fnSndBind,      "\x53\x55\x56\x57\x8B\x7C\x24\x14\x33\xF6", 10 },   // push ebx/ebp/esi/edi / mov edi,[esp+0x14] / xor esi,esi
        { callCollSparks1, "\xE8\xD7\xF4\xFF\xFF", 5 },             // call 0x441BF0
        { callCollSparks2, "\xE8\x29\xEF\xFF\xFF", 5 },             // call 0x441BF0
        { fnContactVec,   "\x8B\x54\x24\x08\x83\xEC\x0C\x85\xD2", 9 },   // mov edx,[esp+8] / sub esp,0xC / test edx,edx
        { fnSparkSpawn,   "\xE8\xDF\xA9\xE9\xFF\x8B\x4C\x24\x04", 9 },   // call 0x401424 / mov ecx,[esp+4]
        { sparkSpawnEnd,  "\xD9\x83\xD8\x66\xCB\x01\xD9\x59\x38", 9 },   // fld dword ptr [ebx+0x1CB66D8] / fstp dword ptr [ecx+0x38]
        { fnSparkSys,     "\xE9\xF7\xC1\x00\x00", 5 },              // jmp 0x40D620
        { blurFlagLevel,  "\x75\x18\x8B\x4B\x08", 5 },               // jne 0x572896 / mov ecx,[ebx+8]
        { 0x5727E8,       "\x56\x68\x4C\x4E\x8B\x00\xE8\xFD\x68\x01\x00", 11 },   // push esi / push "+%s: %f" / call 0x5890F0
        { blurAmount,     "\x8B\x8B\x34\x02\x00\x00\xD9\x44\x24\x10", 10 },   // mov ecx,[ebx+0x234] / fld dword ptr [esp+0x10]
        { rampageTopSpd,  "\x66\x66\xE6\x3E", 4 },                   // dd 0.45f
        { fnBrainTargets, "\x83\xEC\x14\x53\x55\x8B\xE9\x56", 8 },   // sub esp,0x14 / push ebx/ebp / mov ebp,ecx / push esi
        { 0x4E668E,       "\xE8\xDD\xB9\x10\x00", 5 },               // call 0x5F2070 (x2)
        { 0x4E68FB,       "\xE8\x70\xB7\x10\x00", 5 },
        { fnCarDustBall,  "\x53\x57\x8B\xF9\x8B\x87\xA4\x0D\x00\x00", 10 },   // push ebx / push edi / mov edi,ecx / mov eax,[edi+0xDA4]
        { fnCarReset,     "\x64\xA1\x00\x00\x00\x00\x6A\xFF", 8 },   // mov eax,fs:[0] / push -1
        { fnKinStep,      "\x83\xEC\x68\xD9\x05\x14\x0B\x96\x00", 9 },   // sub esp,0x68 / fld [0x960B14]
        { callKinSetup,   "\xE8\xE1\x1E\x07\x00", 5 },               // call 0x4E69D0
        { fnKinSetup,     "\x56\x8B\xF1\x8B\x4C\x24", 6 },           // push esi / mov esi,ecx / mov ecx,[esp+..]
        { callKinCreate,  "\xE8\xA9\xB8\x10\x00", 5 },               // call 0x5F2260
        { fnSetLinVel,    "\x8B\x44\x24\x04\x56\x57\x8B\xF9", 8 },   // mov eax,[esp+4] / push esi / push edi / mov edi,ecx
        { fnSetAngVel,    "\x8B\x54\x24\x04\x56\x8B\x32", 7 },       // mov edx,[esp+4] / push esi / mov esi,[edx]
        { callIceHit,     "\xE8\x20\x5A\xFD\xFF", 5 },               // call 0x478E40
        { callIceFull,    "\xE8\x74\xFD\xFF\xFF", 5 },               // call 0x478E40
        { callMslTarget,  "\xE8\x22\xFC\xFF\xFF", 5 },               // call 0x473370
        { callShieldOn,   "\xE8\xC0\xA6\x00\x00", 5 },               // call 0x4A7D20
        { callToonCanFire, "\xE8\xD8\x2D\x03\x00", 5 },              // call 0x451370
        { callToonCanFire2, "\xE8\x3E\x41\xFE\xFF", 5 },             // call 0x451370
        { callToonShowWpn[0], "\xE8\xF1\x22\x02\x00", 5 },           // call 0x451380
        { callToonShowWpn[1], "\xE8\xB7\x22\x02\x00", 5 },
        { vtSmokeUpd,     "\x50\xDA\x49\x00", 4 },                   // 0x49DA50
        { vtSmokeFlUpd,   "\x50\xDA\x49\x00", 4 },
        { vtParaLaunch,   "\xD0\x10\x47\x00", 4 },                   // 0x4710D0
        { vtEmpFire,      "\xA0\xEE\x49\x00", 4 },                   // 0x49EEA0
        { vtEmpUpFire,    "\xA0\xEE\x49\x00", 4 },
    };
}
