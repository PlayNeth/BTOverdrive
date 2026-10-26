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
//    log/file, frame timing and a crash log in BTOverdrive.log.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cmath>
#include <mmsystem.h>

#define MOD_NAME "Beat That! Overdrive"
#define MOD_FILE "BTOverdrive"                   // .asi, .ini, logs, tuning file
#define HWBT_VERSION "0.5.0"
#define HWBT_BASE "HWBTFix 0.3.8"

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
    int  interpolateBones = 1;           // with interpolate=1: animated (skinned) models too - the ghoul and co.
    int  fpsLimit = 0;                   // 0 = refresh rate
    int  closeExits = 1;                 // closing the window quits like Alt+F4
    int  raceMusic = 1;                  // each race plays its own song (the PC port always played song 0)
    int  logToFile = 1;
    int  gameLog = 0;                    // diagnostics: the game's own debug messages to BTOverdrive_game.log
    int  tuningLog = 1;                  // every number the game's scripts load, to BTOverdrive_tuning.log
    float speedFov = 12.0f;              // degrees added to the camera's field of view at speed (0 = off)
    float speedFovMin = 11.0f;           // speed (units/s) where it starts (11 ~ 25 mph)
    float speedFovMax = 45.0f;           // speed where it's full (45 ~ 100 mph)
    float speedBlur = 0.35f;             // motion blur at speed: strength at full (0 = off; the game's boost blur tops out at 0.7)
    float speedBlurMin = 30.0f;          //   speed where it starts
    float speedBlurMax = 70.0f;          //   speed where it's full
    float hdrGlare = 0.5f;               // HDR "star" glare streaks, x the game's strength and brightness cap
    float hdrBrightness = 1.0f;          // HDR: the tone-mapped image's brightness x this (g_OverBrightScale; 1 = the level's)
    float hdrBloom = 1.0f;               //   bloom strength x this (g_BloomScale)
    float hdrBloomThreshold = 1.0f;      //   bloom threshold x this (g_brightPassThreshold: higher = less blooms)
    float speedShake = 0.4f;             // camera shake at speed: degrees at full (0 = off)
    float speedShakeMin = 40.0f;         //   speed where it starts
    float speedShakeMax = 70.0f;         //   speed where it's full
    int   hazardHits = 1;                // moving track hazards (the graveyard ghoul's hands) throw cars
    float hazardMinSpeed = 50.0f;        // a part must move at least this fast (units/s) to hit
    float hazardReach = 3.0f;            // a part hits within this many times its size (takes in the long fingers, which have no box)
    float hazardPush = 150.0f;           // the car is thrown this fast (units/s) sideways, away from the part
    float hazardLift = 120.0f;           // plus this much upward (units/s)
    float hazardSpin = 6.0f;             // and a tumble (radians/s)
    float hazardFlyTime = 3.0f;          // a thrown car goes through walls for this long (seconds; 0 = never)
    int   hazardSmoke = 1;               // a burst of smoke on a car the hazard throws
    float hazardFallReset = 25.0f;       // a thrown car this far below where it was hit is put back on the track (0 = never)
    float hazardPushTime = 0.5f;         // the throw keeps pushing for this long (seconds)
    int   hazardGateFix = 1;             // the graveyard gate's panels' collision made 3 units thick
    int   aiLeadSlowdown = 0;            // AI cars ahead of the player are slowed down (the game's rubber-banding)
    float aiWeaponCooldown = 2.5f;       // seconds between any two AI shots (game: 5)
    float aiTargetPlayer = 60.0f;        // % of AI shots aimed at the player, +10 per difficulty (game: 40)
    float aiStartBoost = 50.0f;          // % chance each AI car gets the start boost at GO (game: never)
    int   aiAreaWeaponLimit = 4;         // area weapons the AI holds back while this many are out (game: 2)
    float boostMeterSpeed = 2.4f;        // the start-boost dial runs this many times as fast (1 = original)
    int   rampageSpread = 1;             // Rampage: AI cars start spread over the whole track
    int   rampageFlee = 1;               // Rampage: AI cars run from the player
    float rampageSpeed = 1.0f;           //   their target speeds x this all the time
    int   shadowMapSize = 2048;          // sun shadow map size (game: 8 x 8; 0 = the game's)
    float shadowRange = 1000.0f;         //   how far ahead of the camera sun shadows reach (game: 64)
    float shadowFade = 150.0f;           //   fading out over the last this much (game: 16)
    float skyFar = 20000.0f;             // the sky's far plane (game: 5000)
    float skyStretch = 1.0f;             // the sky stretched upward this many times (1 = as is)
    int   glassSort = 1;                 // see-through draws sorted back to front
    int   glassNoZWrite = 1;             // blended draws don't write depth
    int   shadowFit = 1;                 //   the shadow light sees the whole height of the scene
    float shadowBackOff = 1000.0f;       //   ...backing off this far toward the sun (game: 16)
    float shadowFar = 2000.0f;           //   ...and seeing this deep (game: 64)
    int   perfLog = 0;                   // frame timing in the log every 10 s
    int   menuFixes = 1;                 // menus: the stray line at the top, the track name changing before its card
    int   musicAtGo = 1;                 // race music starts at GO, after the countdown (as on the PS2)
    int   scrapeSound = 1;               // the game's metalscrape sound while grinding walls and cars
    int   sparkStreaks = 1;              // dotted sparks (car hits) drawn as streaks
    float sparkLength = 0.5f;            //   a streak's length: seconds of the spark's travel (game: 0.25)
    float sparkWidth = 4.0f;             //   the switched sparks' width x this
    float sparkVary = 0.6f;              //   each streak spark's length and width x 1 +- up to this
    float sparkFloor = 0.6f;             //   no collision sparks from the road under the car (jumps, ramps): 1 = all
    float sparkRate = 120.0f;            //   at most this many extras a second
    int   sparkCount = 3;                //   extra sparks thrown with each one the game throws
    float sparkSpread = 0.5f;            //   how far apart they scatter (x the spark's speed)
    float sparkUp = 1.0f;                //   how hard they're thrown upward (x the spark's speed)
    float sparkImpactRef = 3800.0f;      //   a hit this strong throws normal-size sparks (in proportion; 0 = all the same)
    float sparkImpactMax = 1.6f;         //   ...and the hardest hits' sparks at most this x normal
    float sparkTwist = 25.0f;            //   each spark turned up to this many degrees off its course
    float sparkHot = 1.0f;               //   how far sparks can burn toward white-hot (0 = game colours)
    float rampageTopSpeed = 1.0f;        // Rampage AI cars' top speed, x the player's (game: 0.45)
    float rampageFleeRange = 60.0f;      //   within this distance (units)
    float rampageFleeSpeed = 1.4f;       //   target speed x this when the player is right on them
    float rampageMinDist = 250.0f;       //   AI cars start at least this far from you, when there's room
    float rampageBehind = 0.4f;          //   a spot behind you counts as this x as far
    float rampageDodge = 0.3f;           //   steer up to this much of the road's width away from the player
    int   gridLayout = 1;                // start in a grid, as in the trailer (0 = the game's single file)
    int   gridColumns = 4;               // cars side by side in a row
    float gridWidth = 7.0f;              // between neighbouring cars in a row (units)
    float gridRowGap = 14.0f;            // between rows (units)
    float gridStagger = 3.5f;            // each car to the right this much further back: the row descends (units)
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
    "; " MOD_FILE ".ini (" MOD_NAME " v" HWBT_VERSION ") - Hot Wheels: Beat That!\n"
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
    "; with interpolate=1, smooth animated models too (the graveyard's ghoul, characters...) (0 = only\n"
    ";   whole objects: they animate at 60 Hz)\n"
    "interpolateBones=1\n"
    "; frame cap with interpolate=1: 0 = monitor refresh rate, or a number (30-1000)\n"
    "fpsLimit=0\n"
    "; closing the window (title bar, window manager, taskbar) quits the game the way Alt+F4 does\n"
    "closeExits=1\n"
    "; each race plays its own music and the results screen gets its music, as on consoles (1),\n"
    ";   or the PC original: always the same song, no results music (0)\n"
    "raceMusic=1\n"
    "logToFile=1\n"
    "; diagnostics: the game's own debug messages (errors, missing files, unfinished PC code) and\n"
    ";   failed sound lookups to BTOverdrive_game.log, and frame-rate statistics to this log\n"
    "gameLog=0\n"
    "; --- tuning ---\n"
    "; list every number the game's scripts load (physics, cameras, effects...) in BTOverdrive_tuning.log;\n"
    ";   changes go in BTOverdrive_tuning.txt (see the top of the log)\n"
    "tuningLog=1\n"
    "; --- camera ---\n"
    "; widen the race camera's field of view with speed: degrees added at full speed (0 = off),\n"
    ";   from speedFovMin to speedFovMax (game units per second; 11 ~ 25 mph, 45 ~ 100 mph)\n"
    "speedFov=12\n"
    "speedFovMin=11\n"
    "speedFovMax=45\n"
    "; motion blur at high speed (the game's own, which it otherwise only shows when a boost widens\n"
    ";   the view; needs post-processing on): strength at full speed, 0-1 (0 = off), building up\n"
    ";   from speedBlurMin to speedBlurMax (units per second)\n"
    "speedBlur=0.35\n"
    "speedBlurMin=30\n"
    "speedBlurMax=70\n"
    "; HDR glare: the star streaks the race tracks put on bright paint and floors, at up to 3.5x\n"
    ";   - what made highlights look so aggressive (x the game's: 1 = as it was, 0.5 = half, 0 = off)\n"
    "hdrGlare=0.5\n"
    "; HDR, as a share of each level region's own settings (1 = as the game has it): the image's\n"
    ";   brightness (g_OverBrightScale - 1.5 on the car select), bloom strength, and the bloom\n"
    ";   threshold (higher = only brighter things glow); the log lists each region's values\n"
    "hdrBrightness=1\n"
    "hdrBloom=1\n"
    "hdrBloomThreshold=1\n"
    "; any HDR setting by name, as an absolute value, e.g. hdr.g_Star=0 (glare streaks off), hdr.g_StarScale=0.3,\n"
    ";   hdr.g_brightPassThreshold=0.9, hdr.g_Exposure=0.6 (the log lists them all per region)\n"
    "; shake the race camera at high speed: how far it turns (degrees) at full shake (0 = off),\n"
    ";   building up from speedShakeMin to speedShakeMax (units per second)\n"
    "speedShake=0.4\n"
    "speedShakeMin=40\n"
    "speedShakeMax=70\n"
    "; --- hazards ---\n"
    "; moving track hazards (the ghoul's hands on the graveyard track) throw cars off the track: a car\n"
    ";   within hazardReach times a moving part's size, while the part moves faster than\n"
    ";   hazardMinSpeed, is thrown hazardPush units/s sideways (away from the part) and hazardLift\n"
    ";   upward, with a tumble of hazardSpin (radians/s); for scale, cars race at about 30-45 units/s.\n"
    ";   Also shrinks the slabs' and hands' collision boxes to half, as they reach well beyond what's\n"
    ";   drawn (invisible walls). 0 = original\n"
    "hazardHits=1\n"
    "hazardMinSpeed=50\n"
    "hazardReach=3\n"
    "hazardPush=150\n"
    "hazardLift=120\n"
    "hazardSpin=6\n"
    "; a thrown car flies through walls (the track's invisible walls too) for this many seconds,\n"
    ";   so it can be thrown off the track (0 = it bounces off them)\n"
    "hazardFlyTime=3\n"
    "; a thrown car that falls this far (units) below where it was thrown is put back on the track,\n"
    ";   the way the game resets a car that's stuck (0 = never)\n"
    "hazardFallReset=25\n"
    "; a burst of smoke on a car the hazard throws (1) or none (0)\n"
    "hazardSmoke=1\n"
    "; the throw keeps pushing for this many seconds (the car's grip takes sideways speed out fast)\n"
    "hazardPushTime=0.5\n"
    "; the graveyard gate's panels: 1 = their collision made 3 units thick, so cars can't slip through\n"
    ";   as they swing shut; 0 = as the game has them\n"
    "hazardGateFix=1\n"
    "; --- starting grid ---\n"
    "; races start in a grid, as in the trailer (1), or in single file (0); gridColumns: cars side by\n"
    ";   side; gridWidth: between them (units)\n"
    "gridLayout=1\n"
    "gridColumns=4\n"
    "gridWidth=7\n"
    "; gridRowGap: between rows; gridStagger: each car to the right this much further back (units)\n"
    "gridRowGap=14\n"
    "gridStagger=3.5\n"
    "; the start-boost dial (hit the green during 3-2-1) runs this many times as fast: higher = harder\n"
    ";   (1 = original; the countdown itself keeps its timing)\n"
    "boostMeterSpeed=2.4\n"
    "; --- AI opponents (game values in brackets) ---\n"
    "; slow down AI cars that get ahead of you, the game's rubber-banding (1), or let them race (0)\n"
    "aiLeadSlowdown=0\n"
    "; seconds between any two AI weapon shots, across all AI cars [5]\n"
    "aiWeaponCooldown=2.5\n"
    "; % of AI shots aimed at you, +10 per difficulty level [40]; the rest go at the leaders\n"
    "aiTargetPlayer=60\n"
    "; the AI holds back area weapons (ice, EMP, airstrike, smoke...) while this many are out [2]\n"
    "aiAreaWeaponLimit=4\n"
    "; % chance each AI car gets the start boost at GO, as if it hit the green [never]\n"
    "aiStartBoost=50\n"
    "; --- rendering ---\n"
    "; sun shadow map size: the PC game renders track and scenery shadows into an 8x8 map, a blurry\n"
    ";   dark patch round your car (0 = the game's); shadowRange: how far ahead they reach [64],\n"
    ";   fading out over the last shadowFade [16]\n"
    "shadowMapSize=2048\n"
    "shadowRange=1000\n"
    "shadowFade=150\n"
    "; the sun's shadow light sees the whole height of the scene, so roofs and wall tops cast shadows\n"
    ";   too (the game's was only 16 units deep toward the sun): back-off and depth (shadowFit=0: the game's)\n"
    "shadowFit=1\n"
    "shadowBackOff=1000\n"
    "shadowFar=2000\n"
    "; the sky: its far plane (game: 5000) and how many times it's stretched upward (1 = as is) - for the\n"
    ";   black hole at the top of the sky the wider view shows\n"
    "skyFar=20000\n"
    "skyStretch=1\n"
    "; see-through things (glass, glass road pieces): drawn back to front, and they don't hide what's\n"
    ";   drawn behind them afterward (they flickered between clear and washed out) (0 = the game's)\n"
    "glassSort=1\n"
    "glassNoZWrite=1\n"
    "; frame timing in BTOverdrive.log every 10 s (where the time goes: waiting, game, 3D, draw+present)\n"
    "perfLog=0\n"
    "; --- menus ---\n"
    "; the 1-pixel line at the top of the menus, and the track select showing the next track's name\n"
    ";   on the old card for a frame (0 = as the game has them)\n"
    "menuFixes=1\n"
    "; --- race music ---\n"
    "; the race music starts at GO, after the countdown and start boost, as on the PS2 (0 = during the countdown)\n"
    "musicAtGo=1\n"
    "; --- scrape sound ---\n"
    "; grinding along walls and other cars plays the game's own metalscrape sound (its surfaces never\n"
    ";   name one, so it was silent) (0 = off)\n"
    "scrapeSound=1\n"
    "; --- sparks ---\n"
    "; draw the dotted sparks (car-on-car hits) as streaks like the other sparks (0 = original);\n"
    ";   sparkLength: how long every streak is, in seconds of the spark's flight [0.25];\n"
    ";   sparkWidth: the switched sparks' width x this\n"
    "sparkStreaks=1\n"
    "sparkLength=0.5\n"
    "sparkWidth=4\n"
    "; each streak spark's length and width vary by up to this (0.6 = 40% to 160%; 0 = all the same),\n"
    ";   and sparkHot: how far some burn toward white-hot (0 = the game's colours, 1 = a few near white)\n"
    "sparkVary=0.6\n"
    "sparkHot=1\n"
    "; harder hits throw bigger, longer, faster and more sparks: a hit of sparkImpactRef strength is\n"
    ";   normal size (the log shows your hits' range; 0 = all the same); sparkTwist: each spark is\n"
    ";   turned up to this many degrees off its course, so they don't all line up\n"
    "sparkImpactRef=3800\n"
    "; the hardest hits' sparks at most this x normal size (and count)\n"
    "sparkImpactMax=1.6\n"
    "sparkTwist=25\n"
    "; collision sparks only from walls and cars, not the road under you (landing jumps, ramps):\n"
    ";   a hit more than this much along your car's up is the road (1 = sparks from everything)\n"
    "sparkFloor=0.6\n"
    "; extra sparks thrown with each car-hit spark (the game throws only 1-2 a hit; 0 = none), at most\n"
    ";   sparkRate a second; how far they scatter and how hard they shoot upward (x the spark's speed)\n"
    "sparkCount=3\n"
    "sparkRate=120\n"
    "sparkSpread=0.5\n"
    "sparkUp=1\n"
    "; --- Rampage ---\n"
    "; AI cars start spread over the whole track instead of on the grid (1)\n"
    "rampageSpread=1\n"
    "; ...at least this far from you when there's room, and a spot behind you counts as\n"
    ";   rampageBehind x as far, so they start ahead of you and you don't just turn round\n"
    "rampageMinDist=250\n"
    "rampageBehind=0.4\n"
    "; AI cars run from you when you're within rampageFleeRange: up to rampageFleeSpeed x their\n"
    ";   speed and rampageDodge of the road's width to the side away from you (0 = original)\n"
    "rampageFlee=1\n"
    "; Rampage AI cars' top speed, as a share of yours: the game gives them 0.45 (under half), so\n"
    ";   they were easy to catch\n"
    "rampageTopSpeed=1\n"
    "; AI target speeds x this in Rampage, all the time (corners too)\n"
    "rampageSpeed=1\n"
    "rampageFleeRange=60\n"
    "rampageFleeSpeed=1.4\n"
    "rampageDodge=0.3\n";

// Settings files from before the rename (HWBTMod) are carried over: if the new
// one isn't there yet, the old one is copied to the new name and used.
static void CarryOver(const char* newName, const char* oldName) {
    char np[MAX_PATH], op[MAX_PATH];
    snprintf(np, sizeof(np), "%s%s", g_asiDir, newName); snprintf(op, sizeof(op), "%s%s", g_asiDir, oldName);
    if (GetFileAttributesA(np) == INVALID_FILE_ATTRIBUTES && GetFileAttributesA(op) != INVALID_FILE_ATTRIBUTES)
        CopyFileA(op, np, TRUE);
}
static void LoadConfig() {
    CarryOver(MOD_FILE ".ini", "HWBTMod.ini");
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%s" MOD_FILE ".ini", g_asiDir);
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
        if (!_strnicmp(key, "hdr.", 4) && key[4] && strlen(key + 4) < 40) {   // hdr.<effect parameter>=value (absolute)
            extern void HdrOverride(const char* name, float v);
            HdrOverride(key + 4, (float)atof(val));
            continue;
        }
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
        else if (!_stricmp(key, "interpolateBones"))   g_cfg.interpolateBones = n;
        else if (!_stricmp(key, "fpsLimit"))           g_cfg.fpsLimit = n;
        else if (!_stricmp(key, "closeExits"))         g_cfg.closeExits = n;
        else if (!_stricmp(key, "raceMusic"))          g_cfg.raceMusic = n;
        else if (!_stricmp(key, "gameLog"))            g_cfg.gameLog = n;
        else if (!_stricmp(key, "logToFile"))          g_cfg.logToFile = n;
        else if (!_stricmp(key, "tuningLog"))          g_cfg.tuningLog = n;
        else if (!_stricmp(key, "speedFov"))           g_cfg.speedFov = (float)atof(val);
        else if (!_stricmp(key, "speedFovMin"))        g_cfg.speedFovMin = (float)atof(val);
        else if (!_stricmp(key, "speedFovMax"))        g_cfg.speedFovMax = (float)atof(val);
        else if (!_stricmp(key, "speedBlur"))          g_cfg.speedBlur = (float)atof(val);
        else if (!_stricmp(key, "speedBlurMin"))       g_cfg.speedBlurMin = (float)atof(val);
        else if (!_stricmp(key, "speedBlurMax"))       g_cfg.speedBlurMax = (float)atof(val);
        else if (!_stricmp(key, "hdrGlare"))           g_cfg.hdrGlare = (float)atof(val);
        else if (!_stricmp(key, "hdrBrightness"))      g_cfg.hdrBrightness = (float)atof(val);
        else if (!_stricmp(key, "hdrBloom"))           g_cfg.hdrBloom = (float)atof(val);
        else if (!_stricmp(key, "hdrBloomThreshold"))  g_cfg.hdrBloomThreshold = (float)atof(val);
        else if (!_stricmp(key, "speedShake"))         g_cfg.speedShake = (float)atof(val);
        else if (!_stricmp(key, "speedShakeMin"))      g_cfg.speedShakeMin = (float)atof(val);
        else if (!_stricmp(key, "speedShakeMax"))      g_cfg.speedShakeMax = (float)atof(val);
        else if (!_stricmp(key, "hazardHits"))         g_cfg.hazardHits = n;
        else if (!_stricmp(key, "hazardMinSpeed"))     g_cfg.hazardMinSpeed = (float)atof(val);
        else if (!_stricmp(key, "hazardReach"))        g_cfg.hazardReach = (float)atof(val);
        else if (!_stricmp(key, "hazardPush"))         g_cfg.hazardPush = (float)atof(val);
        else if (!_stricmp(key, "hazardLift"))         g_cfg.hazardLift = (float)atof(val);
        else if (!_stricmp(key, "hazardSpin"))         g_cfg.hazardSpin = (float)atof(val);
        else if (!_stricmp(key, "hazardFlyTime"))      g_cfg.hazardFlyTime = (float)atof(val);
        else if (!_stricmp(key, "hazardSmoke"))        g_cfg.hazardSmoke = n;
        else if (!_stricmp(key, "hazardPushTime"))     g_cfg.hazardPushTime = (float)atof(val);
        else if (!_stricmp(key, "aiLeadSlowdown"))     g_cfg.aiLeadSlowdown = n;
        else if (!_stricmp(key, "aiWeaponCooldown"))   g_cfg.aiWeaponCooldown = (float)atof(val);
        else if (!_stricmp(key, "aiTargetPlayer"))     g_cfg.aiTargetPlayer = (float)atof(val);
        else if (!_stricmp(key, "aiStartBoost"))       g_cfg.aiStartBoost = (float)atof(val);
        else if (!_stricmp(key, "aiAreaWeaponLimit"))  g_cfg.aiAreaWeaponLimit = n;
        else if (!_stricmp(key, "boostMeterSpeed"))    g_cfg.boostMeterSpeed = (float)atof(val);
        else if (!_stricmp(key, "rampageSpread"))      g_cfg.rampageSpread = n;
        else if (!_stricmp(key, "rampageFlee"))        g_cfg.rampageFlee = n;
        else if (!_stricmp(key, "shadowMapSize"))      g_cfg.shadowMapSize = n;
        else if (!_stricmp(key, "shadowRange"))        g_cfg.shadowRange = (float)atof(val);
        else if (!_stricmp(key, "shadowFade"))         g_cfg.shadowFade = (float)atof(val);
        else if (!_stricmp(key, "skyFar"))             g_cfg.skyFar = (float)atof(val);
        else if (!_stricmp(key, "skyStretch"))         g_cfg.skyStretch = (float)atof(val);
        else if (!_stricmp(key, "glassSort"))          g_cfg.glassSort = n;
        else if (!_stricmp(key, "glassNoZWrite"))      g_cfg.glassNoZWrite = n;
        else if (!_stricmp(key, "shadowFit"))          g_cfg.shadowFit = n;
        else if (!_stricmp(key, "shadowBackOff"))      g_cfg.shadowBackOff = (float)atof(val);
        else if (!_stricmp(key, "shadowFar"))          g_cfg.shadowFar = (float)atof(val);
        else if (!_stricmp(key, "perfLog"))            g_cfg.perfLog = n;
        else if (!_stricmp(key, "menuFixes"))          g_cfg.menuFixes = n;
        else if (!_stricmp(key, "musicAtGo"))          g_cfg.musicAtGo = n;
        else if (!_stricmp(key, "scrapeSound"))        g_cfg.scrapeSound = n;
        else if (!_stricmp(key, "sparkStreaks"))       g_cfg.sparkStreaks = n;
        else if (!_stricmp(key, "sparkLength"))        g_cfg.sparkLength = (float)atof(val);
        else if (!_stricmp(key, "sparkWidth"))         g_cfg.sparkWidth = (float)atof(val);
        else if (!_stricmp(key, "sparkVary"))          g_cfg.sparkVary = (float)atof(val);
        else if (!_stricmp(key, "sparkFloor"))         g_cfg.sparkFloor = (float)atof(val);
        else if (!_stricmp(key, "sparkRate"))          g_cfg.sparkRate = (float)atof(val);
        else if (!_stricmp(key, "sparkCount"))         g_cfg.sparkCount = n;
        else if (!_stricmp(key, "sparkSpread"))        g_cfg.sparkSpread = (float)atof(val);
        else if (!_stricmp(key, "sparkUp"))            g_cfg.sparkUp = (float)atof(val);
        else if (!_stricmp(key, "sparkImpactRef"))     g_cfg.sparkImpactRef = (float)atof(val);
        else if (!_stricmp(key, "sparkImpactMax"))     g_cfg.sparkImpactMax = (float)atof(val);
        else if (!_stricmp(key, "sparkTwist"))         g_cfg.sparkTwist = (float)atof(val);
        else if (!_stricmp(key, "sparkHot"))           g_cfg.sparkHot = (float)atof(val);
        else if (!_stricmp(key, "rampageTopSpeed"))    g_cfg.rampageTopSpeed = (float)atof(val);
        else if (!_stricmp(key, "rampageSpeed"))       g_cfg.rampageSpeed = (float)atof(val);
        else if (!_stricmp(key, "rampageFleeRange"))   g_cfg.rampageFleeRange = (float)atof(val);
        else if (!_stricmp(key, "rampageFleeSpeed"))   g_cfg.rampageFleeSpeed = (float)atof(val);
        else if (!_stricmp(key, "rampageMinDist"))     g_cfg.rampageMinDist = (float)atof(val);
        else if (!_stricmp(key, "rampageBehind"))      g_cfg.rampageBehind = (float)atof(val);
        else if (!_stricmp(key, "rampageDodge"))       g_cfg.rampageDodge = (float)atof(val);
        else if (!_stricmp(key, "gridLayout"))         g_cfg.gridLayout = n;
        else if (!_stricmp(key, "gridColumns"))        g_cfg.gridColumns = n;
        else if (!_stricmp(key, "gridRowGap"))         g_cfg.gridRowGap = (float)atof(val);
        else if (!_stricmp(key, "gridStagger"))        g_cfg.gridStagger = (float)atof(val);
        else if (!_stricmp(key, "gridWidth"))          g_cfg.gridWidth = (float)atof(val);
        else if (!_stricmp(key, "hazardGateFix"))      g_cfg.hazardGateFix = n;
        else if (!_stricmp(key, "hazardFallReset"))    g_cfg.hazardFallReset = (float)atof(val);
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
            Logf("exe check: bytes at 0x%06lX differ - not the analysed HOT.exe, or already patched (HWBTFix\n"
                 "  loaded as well?): all changes off\n",
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
    DWORD g_boneFetchCont = hot::boneFetch + 15;
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
static volatile LONG g_kicksPending = 0;              // hazard hits waiting for their car (see "Hazard hits")
static bool g_hzOn = false;
static void ApplyHazardKick(BYTE* car);
static void NoteHazardCar(BYTE* car);
static void HazardFallCheck(BYTE* car);
extern "C" void __cdecl RegCar(BYTE* car) {          // per car, per step, at the start of its physics sync
    if (!car) return;
    if (g_hzOn) { NoteHazardCar(car); if (g_kicksPending) ApplyHazardKick(car); HazardFallCheck(car); }
    if (!g_interpOn) return;
    if (Track* t = FindTrack(car, 1)) t->seenStep = g_stepsDone + 1;
}
static BYTE* g_applyCam = nullptr;                   // the camera Camera::Apply is setting up (speed FOV)
extern "C" void __cdecl RegCam(BYTE* cam) {          // per camera, per frame
    g_applyCam = cam;
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
    LONG   fPrevStep;     // step of the value before it (fprev)
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
static const float* FollowCar(const float* v, float* o);   // (see "Effects and shadows")
static const float* FilterWorld(MTrack* t, const float* v) {
    if (t->fFrame != g_frameNo) {                  // first time this frame
        t->fFrame = g_frameNo;
        if (t->fStep != g_stepsDone) {             // step(s) since the last value
            // Usually one step; a slow frame can have two or more, and then the
            // value in between was never seen. The older one is still a fine
            // neighbour: blend across the steps (below). Only a long gap (or the
            // first value) starts over.
            LONG gap = g_stepsDone - t->fStep;
            if (t->fn && gap >= 1 && gap <= 4) { memcpy(t->fprev, t->fcur, 64); t->fPrevStep = t->fStep; t->fn = 2; }
            else t->fn = 1;
            memcpy(t->fcur, v, 64);
            t->fStep = g_stepsDone;
        } else if (memcmp(t->fcur, v, 64)) {       // changed without a step
            t->fPerFrameStep = g_stepsDone;
        }
    }
    static float out[8][16]; static int k = 0;
    float* o = out[k = (k + 1) & 7];
    // Animated per frame (a car part like a spoiler, moved every frame on top
    // of the car): left at its raw position it sat a fraction of a step behind
    // the smoothed body, and since that mark wears off after half a second it
    // jumped back and forth. On a race car it follows the car's blend instead.
    if (PerFrame(t)) { const float* f = FollowCar(v, o); return f ? f : v; }
    if (t->fn < 2 || memcmp(t->fcur, v, 64) || !memcmp(t->fprev, t->fcur, 64)) return v;
    // the drawn moment is step (fStep - 1 + alpha); fprev is from fPrevStep
    float span = (float)(t->fStep - t->fPrevStep);
    float f = span > 1.0f ? (span - 1.0f + g_alpha) / span : g_alpha;
    if (!Blend(t->fprev, t->fcur, f, o)) { g_st.mCuts++; return v; }
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
        t->fFrame = t->fStep = t->fPrevStep = LONG_MIN / 2; t->fn = 0; t->fPerFrameStep = LONG_MIN / 2; t->drawnOnly = false;
        int h = MHash(p);
        while (g_mHash[h]) h = (h + 1) & 4095;
        g_mHash[h] = (short)g_mtN;
    }
    t->seenFrame = g_frameNo;
    t->src |= (BYTE)src;
    return src == 1 && !t->written ? FilterWorld(t, p) : p;     // (written: already blended in memory)
}
extern "C" void SetWorldStub(); extern "C" void GetXformStub(); extern "C" void BoneFetchStub();
asm(".text\n"
    ".globl _BoneFetchStub\n_BoneFetchStub:\n"   // replaces the bone fetch at 0x54AF55 (skinned models: the ghoul...)
    "    movl 8(%ebp), %ecx\n"
    "    movl (%ecx,%eax,4), %esi\n"
    "    shll $6, %esi\n"
    "    addl 0x1D0(%ebx), %esi\n"             // esi = the bone's matrix (in the model's bone array)
    "    pushal\n"
    "    pushl $0x54AF55\n"                    // (caller tag, for the log)
    "    pushl $1\n"
    "    pushl %esi\n"
    "    call _RegMat\n"                       // blended like a SetWorld matrix (FilterWorld)
    "    addl $12, %esp\n"
    "    movl %eax, 4(%esp)\n"                 // esi after popal
    "    popal\n"
    "    jmp *_g_boneFetchCont\n"
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
// Frame timing (perfLog=1): where a frame's time goes, logged every 10 s. The
// main loop (0x40EC1A on): the frame cap (waiting), the 60 Hz steps (0x59B3F0
// each: the game's simulation, AI, physics), the 3D pass (0x421160: walks the
// scene and queues the draws), the 2D pass (0x421100, 0x421150: HUD), the
// renderer's flush (0x54DA00: every Direct3D draw call, shadows, the
// post-processing - CPU time spent feeding the driver, plus waits for the GPU)
// and the frame end (0x54DC30: Present). What's left over is "other".
enum { PF_FRAME, PF_WAIT, PF_SIM, PF_3D, PF_2D, PF_DRAW, PF_PRESENT, PF_OTHER, PF_N };
static LONGLONG g_pfQpf = 0, g_pfFrameStart = 0, g_pfCur[PF_N];
static double g_pfSum[PF_N], g_pfMax[PF_N]; static LONG g_pfN = 0, g_pfSlow = 0, g_pfSteps = 0; static DWORD g_pfLogMs = 0;
static LONGLONG Qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static void PerfFrameStart(LONGLONG waitTicks) {                // the limiter: one frame ends, the next starts
    LONGLONG now = Qpc();
    if (!g_pfQpf) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_pfQpf = f.QuadPart; }
    if (g_pfFrameStart) {
        double k = 1000.0 / (double)g_pfQpf, v[PF_N];
        g_pfCur[PF_FRAME] = now - waitTicks - g_pfFrameStart;   // (from this frame's wait start back to the last one's)
        LONGLONG other = g_pfCur[PF_FRAME];
        for (int i = PF_WAIT; i < PF_OTHER; i++) other -= g_pfCur[i];
        g_pfCur[PF_OTHER] = other;
        for (int i = 0; i < PF_N; i++) { v[i] = g_pfCur[i] * k; g_pfSum[i] += v[i]; if (v[i] > g_pfMax[i]) g_pfMax[i] = v[i]; }
        g_pfN++;
        if (v[PF_FRAME] - v[PF_WAIT] > 16.7) g_pfSlow++;        // (work alone over a 60 Hz frame)
    }
    g_pfFrameStart = now - waitTicks;
    memset(g_pfCur, 0, sizeof(g_pfCur));
    g_pfCur[PF_WAIT] = waitTicks;
    DWORD ms = GetTickCount();
    if (!g_pfLogMs) g_pfLogMs = ms;
    if (ms - g_pfLogMs >= 10000 && g_pfN > 0) {
        double n = (double)g_pfN;
        static const char* nm[PF_N] = { "frame", "waiting", "game steps", "3D queue", "2D", "draw (flush)", "present", "other" };
        char line[600]; int l = snprintf(line, sizeof(line), "perf: %.0f fps, %.2f steps/frame; avg/max ms:", 1000.0 / (g_pfSum[PF_FRAME] / n),
                                         (double)g_pfSteps / n);
        for (int i = PF_WAIT; i < PF_N && l < 560; i++)
            l += snprintf(line + l, sizeof(line) - l, " %s %.1f/%.1f%s", nm[i], g_pfSum[i] / n, g_pfMax[i], i + 1 < PF_N ? "," : "");
        Logf("%s; %ld of %ld frames over 16.7 ms of work\n", line, g_pfSlow, g_pfN);
        memset(g_pfSum, 0, sizeof(g_pfSum)); memset(g_pfMax, 0, sizeof(g_pfMax)); g_pfN = g_pfSlow = g_pfSteps = 0; g_pfLogMs = ms;
    }
}
static void __cdecl HkPfStep() {
    LONGLONG t = Qpc(); reinterpret_cast<VoidFn_t>(hot::fnGameStep)(); g_pfCur[PF_SIM] += Qpc() - t; g_pfSteps++;
}
static void __cdecl HkPf2D1() { LONGLONG t = Qpc(); reinterpret_cast<VoidFn_t>(hot::fn2D1)(); g_pfCur[PF_2D] += Qpc() - t; }
static void __cdecl HkPf2D2() { LONGLONG t = Qpc(); reinterpret_cast<VoidFn_t>(hot::fn2D2)(); g_pfCur[PF_2D] += Qpc() - t; }
static void __attribute__((thiscall)) HkPfFlush(void* r) {
    LONGLONG t = Qpc(); reinterpret_cast<Limiter_t>(hot::fnFlush)(r); g_pfCur[PF_DRAW] += Qpc() - t;
}
static void InstallPerf() {
    if (!g_cfg.perfLog || !g_cfg.interpolate) return;           // (the frame start comes from the frame cap)
    if (!PatchCall(hot::callGameStep, reinterpret_cast<void*>(&HkPfStep), 5) ||
        !PatchCall(hot::call2D1, reinterpret_cast<void*>(&HkPf2D1), 5) ||
        !PatchCall(hot::call2D2, reinterpret_cast<void*>(&HkPf2D2), 5) ||
        !PatchCall(hot::callFlush, reinterpret_cast<void*>(&HkPfFlush), 5)) Logf("perf: patch FAILED\n");
    else Logf("perf: frame timing in the log every 10 s\n");
}
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
    LONGLONG pf0 = g_cfg.perfLog ? Qpc() : 0;
    reinterpret_cast<VoidFn_t>(hot::fnRender3D)();
    if (pf0) g_pfCur[PF_3D] += Qpc() - pf0;
}

// End of the frame's drawing (0x54DC30, after the 2D pass and the renderer's
// flush 0x54DA00): put the step state back. The renderer queues draws and
// reads many matrices only when it flushes, so the blend has to stay in until
// then, not just through the 3D pass.
static void CamShakeUndo();
static void __attribute__((thiscall)) HkFrameEnd(void* renderer) {
    LONGLONG pf0 = g_cfg.perfLog ? Qpc() : 0;
    reinterpret_cast<Limiter_t>(hot::fnFrameEnd)(renderer);
    if (pf0) g_pfCur[PF_PRESENT] += Qpc() - pf0;
    CamShakeUndo();                                // (before the check below: a shaken camera isn't "moved by the pass")
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
            {
                float span = (float)(m->fStep - m->fPrevStep);       // (as FilterWorld: across 2+ steps)
                float f = span > 1.0f ? (span - 1.0f + g_alpha) / span : g_alpha;
                return Blend(m->fprev, m->fcur, f, buf + 16) ? buf + 16 : p;
            }
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

// A matrix on a race car (within 4 units of its position) moved by the car's
// step -> blend transform: whatever the part does on its own is kept, the car's
// motion is smoothed. nullptr = no race car there (or no blend this frame).
static const float* FollowCar(const float* v, float* o) {
    Track* best = nullptr; float bestD = 4.0f * 4.0f;
    for (Track& t : g_tr) {
        if (!t.obj || t.kind != 1 || t.n != 2 || t.snapStep != g_stepsDone) continue;
        float d[3] = { v[12] - t.cur[12], v[13] - t.cur[13], v[14] - t.cur[14] };
        float dd = Dot3(d, d);
        if (dd < bestD) { bestD = dd; best = &t; }
    }
    if (!best) return nullptr;
    const float* b = CarBlend(best->obj);
    float inv[16], d[16];
    if (!b || !InvAffine(best->cur, inv)) return nullptr;
    MulAffine(inv, b, d);                                       // the car's step -> blend
    MulAffine(v, d, o);
    return o;
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

// ---------------------------------------------------------------------------
// Speed FOV. Camera::Apply hands the camera's field of view to the renderer
// (0x5BA4ED); it's widened by up to speedFov degrees with the speed of the car
// it follows, between speedFovMin and speedFovMax: the player's car nearest the
// camera (else the nearest AI race car; car-select cars don't count) within 40 units, from its last two step
// positions (physics: exactly once a step; the camera itself may move per frame,
// which made a speed taken from it jump), smoothed over about a third of a second.
// Cameras with no race car near (menus, car select) are left alone; across a
// cut (a jump of over 25 units in a step) the last speed is kept.
// ---------------------------------------------------------------------------
// Each camera keeps its own smoothed speed: the game sets up more than one
// camera a frame, and a shared value was pulled to and fro between the moving
// race camera and the still ones, so the field of view jumped every frame.
// Speed blur. The game's post-processing has a "speed ghosting" blur: every
// frame a small copy of the scene goes into a 3-frame history (0x6A85E0), and
// when the post-process flag g_MotionBlur (+0x31C) is on, the three are
// averaged and Gaussian-blurred (0x6A8F08) and the final pass mixes that in by
// g_MotionBlurAmount. The game works the amount out from the field of view -
// (fov in radians - 1.3) x 2, 0 to 0.7 - so it only showed when a boost widened
// the view past about 75 degrees, and a level's own settings can switch the
// flag off (0x5726C0). So:
//  - the level's g_MotionBlur no longer overrides the flag (0x57287C: always
//    skipped; it starts at 1)
//  - just before the amount goes to the shader (0x575F65), the shader's own
//    g_MotionBlur is set to 1 and the amount raised to the speed blur: up to
//    speedBlur, from speedBlurMin to speedBlurMax (the player's speed, as the
//    speed FOV sees it), whichever is more.
// Not a directional blur: the whole frame trails a little. The car mostly
// stays put on screen, so it's the road and the surroundings that smear.
static float g_blurSpeed = 0.0f; static DWORD g_blurSpeedMs = 0;
static float g_blurMax = 0.0f; static LONG g_blurOn = 0; static bool g_blurLogged = false;
typedef HRESULT (__stdcall *EffSetFloat_t)(void*, const char*, float);
// HDR. Each region of a level has HDR settings ("HDR Region File Settings":
// g_OverBrightScale, g_Bloom, g_BloomScale, g_brightPassThreshold,
// g_brightnessClamp...) that the post-processing (0x5726C0) hands to its
// effect as they are, printing each one through the game's debug output
// (0x5727EE: "+%s: %f"). That print is hooked: the values are kept (and logged
// once each), and every frame the ones with an INI scale other than 1 are set
// again, scaled: hdrBrightness x g_OverBrightScale (the tone-mapped image's
// brightness: 1.5 on the car select), hdrBloom x g_BloomScale, hdrBloomThreshold
// x g_brightPassThreshold (only what's brighter than it blooms). Any setting
// can also be given outright as hdr.<name>=value (applied after the scales, so
// it wins). And hdrGlare x g_StarScale and g_brightnessClampStar: the "star"
// glare streaks the race regions draw from anything above 30% brightness
// (g_brightPassThresholdStar 0.3) at 0.5 strength, up to 3.5x - the overdone
// highlights on paint and floors. At 0.5 they're half as strong, up to 1.75x.
struct HdrParam { const char* name; float base; bool have; float* scale; };
static HdrParam g_hdr[5] = {
    { "g_OverBrightScale", 0, false, &g_cfg.hdrBrightness },
    { "g_BloomScale", 0, false, &g_cfg.hdrBloom },
    { "g_brightPassThreshold", 0, false, &g_cfg.hdrBloomThreshold },
    { "g_StarScale", 0, false, &g_cfg.hdrGlare },               // the glare streaks' strength
    { "g_brightnessClampStar", 0, false, &g_cfg.hdrGlare },     // and how bright they can get
};
struct HdrOv { char name[40]; float v; };
static HdrOv g_hdrOv[32]; static int g_hdrOvN = 0;
void HdrOverride(const char* name, float v) {
    if (g_hdrOvN >= 32) return;
    snprintf(g_hdrOv[g_hdrOvN].name, sizeof(g_hdrOv[0].name), "%s", name); g_hdrOv[g_hdrOvN++].v = v;
}
static bool HdrActive() { if (g_hdrOvN) return true; for (const HdrParam& h : g_hdr) if (*h.scale != 1.0f) return true; return false; }
extern "C" void __cdecl HdrParamNote(const char* fmt, const char* name, double v) {
    reinterpret_cast<void (__cdecl *)(const char*, const char*, double)>(hot::debugPrint)(fmt, name, v);   // (the game log, if on)
    if (!name || IsBadStringPtrA(name, 64)) return;
    static char seen[48][40]; static float seenV[48]; static int nSeen = 0;
    bool fresh = true;
    for (int i = 0; i < nSeen && fresh; i++) if (!strcmp(seen[i], name) && seenV[i] == (float)v) fresh = false;
    if (fresh && nSeen < 48) {
        snprintf(seen[nSeen], sizeof(seen[nSeen]), "%s", name); seenV[nSeen++] = (float)v;
        Logf("hdr: region setting %s = %.3f\n", name, v);
    }
    for (HdrParam& h : g_hdr) if (!strcmp(h.name, name)) { h.base = (float)v; h.have = true; }
}
static void HdrApply(void* eff) {
    if (!eff) return;
    void** vt = *reinterpret_cast<void***>(eff);
    for (const HdrParam& h : g_hdr)
        if (h.have && *h.scale != 1.0f)
            reinterpret_cast<EffSetFloat_t>(vt[0x78 / 4])(eff, h.name, h.base * *h.scale);
    static bool logged = false;
    for (int i = 0; i < g_hdrOvN; i++) {
        HRESULT hr = reinterpret_cast<EffSetFloat_t>(vt[0x78 / 4])(eff, g_hdrOv[i].name, g_hdrOv[i].v);
        if (!logged) Logf("hdr: %s set to %.3f%s\n", g_hdrOv[i].name, g_hdrOv[i].v, hr < 0 ? " - FAILED (no such parameter in the post-processing effect)" : "");
    }
    logged = true;
}
extern "C" void __cdecl SpeedBlur(float* amount, BYTE* pp) {
    float game = *amount, ours = 0.0f;
    if (GetTickCount() - g_blurSpeedMs < 500) {                 // (a race camera on the player's car just now)
        float span = g_cfg.speedBlurMax - g_cfg.speedBlurMin;
        float x = span > 0.0f ? (g_blurSpeed - g_cfg.speedBlurMin) / span : 0.0f;
        if (x < 0.0f) x = 0.0f; else if (x > 1.0f) x = 1.0f;
        ours = g_cfg.speedBlur * x * x * (3.0f - 2.0f * x);     // smoothstep
    }
    BYTE* fx = pp ? *reinterpret_cast<BYTE**>(pp + 0x234) : nullptr;
    void* eff = fx ? *reinterpret_cast<void**>(fx + 0x1C) : nullptr;   // the ID3DXEffect
    HdrApply(eff);
    if (g_cfg.speedBlur <= 0.0f) return;
    if (eff) {
        void** vt = *reinterpret_cast<void***>(eff);
        reinterpret_cast<EffSetFloat_t>(vt[0x78 / 4])(eff, "g_MotionBlur", 1.0f);   // (no such parameter: does nothing)
    }
    if (ours > game) *amount = ours;
    if (ours > 0.0f) {                                          // (for the log: once, ~10 s into the first speed blur)
        if (*amount > g_blurMax) g_blurMax = *amount;
        if (!g_blurLogged && ++g_blurOn == 600) {
            g_blurLogged = true;
            Logf("speed blur: on in a race; strongest so far %.2f (game's own now %.2f)\n", g_blurMax, game);
        }
    }
}
extern "C" void SpeedBlurStub();
asm(".text\n"
    ".globl _SpeedBlurStub\n_SpeedBlurStub:\n"   // call from 0x575F65 (replaces mov ecx,[ebx+0x234]); ebx = post-process, the amount at the caller's [esp+0x10]
    "    pushal\n"
    "    leal 0x34(%esp), %eax\n"                // 0x10 + 4 (return address) + 0x20 (pushal)
    "    pushl %ebx\n"
    "    pushl %eax\n"
    "    call _SpeedBlur\n"
    "    addl $8, %esp\n"
    "    popal\n"
    "    movl 0x234(%ebx), %ecx\n"
    "    ret\n");

// Speed shake. Camera::Apply builds the view from the camera's matrix right
// after it sets the field of view, so the shake goes into the camera's matrix
// here and comes out again at the end of the frame's drawing (HkFrameEnd),
// before the step state goes back: the game never sees it. It's a small turn
// (yaw and pitch, up to speedShake degrees), not a move: moving a camera that
// close to the car slid the car about on screen against a still background,
// which read as jitter. Strength builds from speedShakeMin to speedShakeMax;
// a few sine waves of 1.7-5 Hz that never line up make the rumble.
struct CamShakeSave { BYTE* cam; float orig[16], shaken[16]; };
static CamShakeSave g_shake[4];
static int g_shakeN = 0;
static LONG g_shakeDone = 0, g_shakeOutside = 0, g_shakeFrames = 0; static bool g_shakeLogged = false;
static void CamShake(BYTE* cam, float speed, double t) {
    float span = g_cfg.speedShakeMax - g_cfg.speedShakeMin;
    float x = span > 0.0f ? (speed - g_cfg.speedShakeMin) / span : (speed >= g_cfg.speedShakeMin ? 1.0f : 0.0f);
    if (x <= 0.0f || *reinterpret_cast<volatile BYTE*>(hot::worldFrozen)) return;   // (not while paused)
    if (x > 1.0f) x = 1.0f;
    float a = g_cfg.speedShake * x * x * (3.14159265f / 180.0f);   // radians
    float* m = reinterpret_cast<float*>(cam + 0x10);
    CamShakeSave* e = nullptr;                                  // (the same camera twice in a frame: from its own matrix)
    for (int i = 0; i < g_shakeN; i++) if (g_shake[i].cam == cam) { e = &g_shake[i]; break; }
    if (e) { if (memcmp(m, e->shaken, 64)) return; memcpy(m, e->orig, 64); }
    else { if (g_shakeN >= 4) return; e = &g_shake[g_shakeN++]; e->cam = cam; memcpy(e->orig, m, 64); }
    float yaw   = a * (0.5f * (float)sin(t * 10.7) + 0.3f * (float)sin(t * 18.2 + 1.3) + 0.2f * (float)sin(t * 28.9 + 4.0));
    float pitch = a * (0.5f * (float)sin(t * 12.3 + 0.7) + 0.3f * (float)sin(t * 20.6 + 2.1) + 0.2f * (float)sin(t * 31.4 + 5.2));
    // small-angle turn of the rows (0 right, 1 up, 2 forward): still square to
    // within angle^2 (under 1e-4), and it's put back after the frame anyway
    for (int k = 0; k < 3; k++) {
        float r = m[k], u = m[4 + k], f = m[8 + k];
        m[k] = r - f * yaw; m[4 + k] = u - f * pitch; m[8 + k] = f + r * yaw + u * pitch;
    }
    memcpy(e->shaken, m, 64);
    g_shakeDone++;
}
static void CamShakeUndo() {
    for (int i = 0; i < g_shakeN; i++) {
        float* m = reinterpret_cast<float*>(g_shake[i].cam + 0x10);
        if (!memcmp(m, g_shake[i].shaken, 64)) memcpy(m, g_shake[i].orig, 64);   // (unless something else moved it since)
    }
    g_shakeN = 0;
    if ((g_shakeDone || g_shakeOutside) && !g_shakeLogged && ++g_shakeFrames >= 600) {   // (once, ~10 s into the first shaking)
        g_shakeLogged = true;
        Logf("camera shake: %ld shakes in 600 frames, %ld camera setups outside drawing skipped\n", g_shakeDone, g_shakeOutside);
    }
}
struct CamFov { BYTE* cam; float speed; LONGLONG qpc; float fovPrev, fovCur; LONG fovStep; };
static CamFov g_camFov[8];
static float g_maxCamSpeed = 0.0f;           // for the log (calibration)
extern "C" void __cdecl HkSetFov(float fov) {
    const Track* car = nullptr;                                 // the car this camera follows
    if (g_interpOn && g_applyCam) {
        const float* cp = reinterpret_cast<const float*>(g_applyCam + 0x10) + 12;   // the camera's position
        float best = 40.0f * 40.0f; bool bestPlayer = false;
        for (const Track& t : g_tr) {
            if (!t.obj || t.kind != 1 || t.n != 2 || t.snapStep != g_stepsDone) continue;
            float d[3] = { t.cur[12] - cp[0], t.cur[13] - cp[1], t.cur[14] - cp[2] };
            float dd = Dot3(d, d);
            DWORD vt = reinterpret_cast<DWORD>(t.vt);
            bool player = vt == 0x8A0384;                       // GSPlayerRaceCar_c
            if (!player && vt != 0x8A0144 && vt != 0x8A3FA4) continue;   // GSAIRaceCar_c, GSRaceCar_c only
            if (dd > 40.0f * 40.0f) continue;
            if ((player && !bestPlayer) || (player == bestPlayer && dd < best)) { car = &t; best = dd; bestPlayer = player; }
        }
    }
    CamFov* c = nullptr;
    if (g_applyCam) for (CamFov& e : g_camFov) if (e.cam == g_applyCam) { c = &e; break; }
    bool moving = false;                                        // the camera itself is tracked (it moves)
    if (g_interpOn && g_applyCam)
        for (const Track& t : g_tr) if (t.obj == g_applyCam && t.n == 2) { moving = true; break; }
    if (!car && !c && !moving) {                                // still and no car (menus): as it is
        reinterpret_cast<void (__cdecl *)(float)>(hot::fnSetFov)(fov);
        return;
    }
    if (!c) {                                                   // new camera: take a free slot or the oldest
        c = &g_camFov[0];
        for (CamFov& e : g_camFov) if (!e.cam || e.qpc < c->qpc) c = &e;
        c->cam = g_applyCam; c->speed = 0.0f; c->qpc = 0; c->fovStep = LONG_MIN / 2;
    }
    // The camera's own field of view changes on steps (the game's camera effects
    // set it every step) while its position is interpolated: blend it the same way.
    if (c->fovStep != g_stepsDone) {
        c->fovPrev = c->fovStep == g_stepsDone - 1 ? c->fovCur : fov;
        c->fovCur = fov;
        c->fovStep = g_stepsDone;
    }
    if (fov == c->fovCur && fabsf(c->fovCur - c->fovPrev) < 20.0f)   // (a jump: a cut, not blended)
        fov = c->fovPrev + (c->fovCur - c->fovPrev) * g_alpha;
    float speed = 0.0f;                                         // (no car near: ease back to none)
    if (car) {
        float d[3] = { car->cur[12] - car->prev[12], car->cur[13] - car->prev[13], car->cur[14] - car->prev[14] };
        float step = Len3(d);
        speed = step < kCutDist ? step / *reinterpret_cast<float*>(hot::stepDt) : c->speed;   // (a cut: keep)
    }
    static LONGLONG qpf = 0;
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    if (!qpf) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpf = f.QuadPart; }
    float dt = c->qpc ? (float)(q.QuadPart - c->qpc) / (float)qpf : 0.0f;
    c->qpc = q.QuadPart;
    if (dt > 0.25f) dt = 0.25f;
    // While the world is frozen (pause menu, countdown) the cars don't move or
    // aren't tracked: the speed would ease to 0 and the view narrow under the
    // pause menu. It's held as it was.
    if (!*reinterpret_cast<volatile BYTE*>(hot::worldFrozen))
        c->speed += (speed - c->speed) * (1.0f - expf(-dt / 0.33f));   // ~1/3 s smoothing
    if (c->speed > g_maxCamSpeed) g_maxCamSpeed = c->speed;
    float span = g_cfg.speedFovMax - g_cfg.speedFovMin;
    float x = span > 0.0f ? (c->speed - g_cfg.speedFovMin) / span : 0.0f;
    if (x < 0.0f) x = 0.0f; else if (x > 1.0f) x = 1.0f;
    x = x * x * (3.0f - 2.0f * x);                              // smoothstep
    reinterpret_cast<void (__cdecl *)(float)>(hot::fnSetFov)(fov + g_cfg.speedFov * x);
    if (car && reinterpret_cast<DWORD>(car->vt) == 0x8A0384) { g_blurSpeed = c->speed; g_blurSpeedMs = GetTickCount(); }
    if (car && g_cfg.speedShake > 0.0f) {
        if (g_inRender) CamShake(g_applyCam, c->speed, q.QuadPart / (double)qpf);
        else g_shakeOutside++;
    }
}

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
    LONGLONG pf0 = g_cfg.perfLog ? Qpc() : 0;
    PaceWait();
    if (pf0) PerfFrameStart(Qpc() - pf0);
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
    if (g_cfg.interpolateBones) {
        BYTE jmp15[15] = { 0xE9 }; memset(jmp15 + 5, 0x90, 10);
        rel = reinterpret_cast<DWORD>(&BoneFetchStub) - (hot::boneFetch + 5); memcpy(jmp15 + 1, &rel, 4);
        ok = ok && PatchBytes(hot::boneFetch, jmp15, 15);
    }
    ok = ok && PatchCall(hot::callVehLights, reinterpret_cast<void*>(&HkVehLights), 5);
    for (DWORD at : hot::callCarShadow) ok = ok && PatchCall(at, reinterpret_cast<void*>(&HkCarShadow), 5);
    ok = ok && PatchCall(hot::callCarTrails, reinterpret_cast<void*>(&HkCarTrails), 5);
    const struct { DWORD at; void (*stub)(); } effHooks[] = {
        { hot::effPos, &EffPosStub }, { hot::effPosObj18, &EffPosObj18Stub }, { hot::effPosNode, &EffPosNodeStub },
        { hot::effPosObj10, &EffPosObj10Stub }, { hot::effRow2, &EffRow2Stub }, { hot::effRow1, &EffRow1Stub },
        { hot::effRow0, &EffRow0Stub }, { hot::effRows, &EffRowsStub } };
    for (const auto& h : effHooks) ok = ok && PatchJmp(h.at, reinterpret_cast<DWORD>(h.stub));
    ok = ok && PatchCall(hot::garageLift, reinterpret_cast<void*>(&GarageLiftStub), 6);
    ok = ok && PatchCall(hot::callCamFov, reinterpret_cast<void*>(&HkSetFov), 5);   // (FOV blend + speed FOV)
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
// to BTOverdrive_game.log, each message (by format string) at most 5 times, with a
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
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%s" MOD_FILE "_game.log", g_asiDir);
    if (!(g_glog = fopen(path, "w"))) { Logf("game log: can't create %s\n", path); return; }
    InitializeCriticalSection(&g_glogLock);
    fprintf(g_glog, "HOT.exe debug messages (" MOD_NAME " v" HWBT_VERSION "): time, caller, message\n");
    if (!PatchJmp(hot::debugPrint, reinterpret_cast<DWORD>(&GameDebugPrint))) Logf("game log: patch FAILED\n");
    if (!PatchJmp(hot::soundFind, reinterpret_cast<DWORD>(&HkSoundFind))) Logf("game log: sound audit patch FAILED\n");
}

// ---------------------------------------------------------------------------
// Hazard hits. A track's animated hazards (the vampire's hands on the graveyard
// track...) are trigger events whose model gets collision boxes, one per part,
// that follow the parts every step (set up by 0x4E67F0, moved by 0x4E6630).
// Two game bugs:
//  - The boxes of the slabs and hands reach well beyond what's drawn (invisible
//    walls around the slabs): they're made half the size while the actor is made
//    (0x5F2260). (Not because the game doubles them, as first thought: each box
//    is the size the level file gives it, a CollisionBox's +0x88 being its
//    half-size - the authored boxes are just bigger than the parts. The gate's
//    panels match theirs, so they're left at full size; see "Gate panels".)
//  - The parts that reach furthest (the vampire's long fingers) get no box at all
//    (size 0), and the boxes' speed is next to nothing, so a swipe can't throw a
//    car: the hands only ever blocked or nudged.
// So the mod gives moving hazards their own hit: each step it tracks every box's
// position (its own speed from step to step), and a race car that's within the
// box's reach - hazardReach times the part's size, which takes in the fingers -
// while the part moves faster than hazardMinSpeed is thrown: sideways away from
// the part and up, at most once per swipe, kept pushing for hazardPushTime. The
// throw is applied at the car's physics sync (0x5FB260). A thrown car passes
// through walls for hazardFlyTime, and one that falls off the track is put back.
// ---------------------------------------------------------------------------
static void TuningFlush();
struct HazardKick {
    BYTE* car; float dv[3]; float w[3]; volatile LONG pending; DWORD ms;
    volatile DWORD flying;                      // when it was thrown (0: not flying)
    float m0[16];                               // the car's matrix then: on the track, where it's put back
    bool watch, reset;                          // watching it fall / a put-back is due
    DWORD safe;                                 // when it was put back: no new hit for a while
    float dir[2];                               // the throw's horizontal direction (x, z)
};
static HazardKick g_kicks[16];
struct HazardCar { BYTE* car; DWORD ms; };                      // race cars seen at their physics sync
static HazardCar g_hzCars[16];
struct HazardBox { BYTE* box; float pos[3]; DWORD ms; float v[3]; float maxSpeed; LONG hits; };
static HazardBox g_hzBoxes[64];
static LONG g_hzKicks = 0, g_hzLogged = 0;
typedef void (__attribute__((thiscall)) *SetVec_t)(BYTE*, const float*);
static void ApplyHazardKick(BYTE* car) {
    DWORD now = GetTickCount();
    for (HazardKick& k : g_kicks) {
        if (!k.pending) continue;
        bool mine = k.car == car;
        if (!mine && now - k.ms < 500) continue;                        // (a car that's gone: dropped)
        if (InterlockedCompareExchange(&k.pending, 0, 1) != 1) continue;
        InterlockedDecrement(&g_kicksPending);
        if (!mine) continue;
        const float* v = reinterpret_cast<const float*>(car + 0x260);
        const float* w = reinterpret_cast<const float*>(car + 0x26C);
        float nv[3] = { v[0] + k.dv[0], v[1] + k.dv[1], v[2] + k.dv[2] };
        float nw[3] = { w[0] + k.w[0], w[1] + k.w[1], w[2] + k.w[2] };
        reinterpret_cast<SetVec_t>(hot::fnSetLinVel)(car, nv);
        reinterpret_cast<SetVec_t>(hot::fnSetAngVel)(car, nw);
        bool airborne = k.watch && k.flying && now - k.flying < 8000;  // hit again in the air: keep where it was
        if (!airborne) memcpy(k.m0, car + 0x10, sizeof(k.m0));         //   first hit (on the track) for the put-back
        k.flying = now ? now : 1;
        if (g_cfg.hazardSmoke)                                          // a burst of smoke on the swatted car
            reinterpret_cast<void (__attribute__((thiscall)) *)(BYTE*)>(hot::fnCarDustBall)(car);
        k.watch = g_cfg.hazardFallReset > 0.0f;
        k.reset = false;
    }
}
// A car thrown off the track falls forever (nothing below catches it, and the
// game's reset of a stuck car finds no reset point from down there). So when a
// thrown car falls hazardFallReset below where it was thrown, or is still
// falling fast 3 seconds after the throw, it's put back where it was hit, with
// the game's own put-back (GSResetOnTrackAP_c, 0x433F90: position, stops the
// car, camera...). That runs with the hazards' step (HkKinStep), outside the
// car's physics sync.
static void HazardFallCheck(BYTE* car) {                        // from RegCar
    DWORD now = GetTickCount();
    for (HazardKick& k : g_kicks) {
        if (k.car != car || !k.flying || k.reset) continue;
        float y = reinterpret_cast<const float*>(car + 0x10)[13];
        float vy = reinterpret_cast<const float*>(car + 0x260)[1];
        // The car's own handling takes the sideways speed out within half a second
        // (12 units/s left of 150, in the air too), so the throw keeps pushing for
        // hazardPushTime: its speed along the throw is kept up to hazardPush.
        if (now - k.flying < (DWORD)(g_cfg.hazardPushTime * 1000.0f)) {
            const float* v = reinterpret_cast<const float*>(car + 0x260);
            float along = v[0] * k.dir[0] + v[2] * k.dir[1];
            if (along < g_cfg.hazardPush) {
                float add = g_cfg.hazardPush - along;
                float nv[3] = { v[0] + k.dir[0] * add, v[1], v[2] + k.dir[1] * add };
                reinterpret_cast<SetVec_t>(hot::fnSetLinVel)(car, nv);
            }
        }
        if (!k.watch) continue;
        if (now - k.flying > 8000) { k.watch = false; continue; }      // landed on the track
        float drop = g_cfg.hazardFallReset < 10.0f ? 10.0f : g_cfg.hazardFallReset;   // (never while it's still near the track)
        if ((y < k.m0[13] - drop && vy < -10.0f) || (now - k.flying > 3000 && vy < -30.0f && y < k.m0[13] - 5.0f)) {
            k.reset = true;
            Logf("hazards: thrown car %p fell %.0f units (y %.1f -> %.1f, falling %.0f units/s): back on the track\n",
                 car, k.m0[13] - y, k.m0[13], y, -vy);
        }
    }
}
alignas(16) static BYTE g_resetObj[0x88];                       // a GSResetOnTrackAP_c's fields for 0x433F90
static void HazardResets() {                                    // from HkKinStep
    for (HazardKick& k : g_kicks) {
        if (!k.reset) continue;
        k.reset = k.watch = false;
        k.flying = 0;                                           // walls are walls again
        k.safe = GetTickCount();
        DWORD vt = *reinterpret_cast<DWORD*>(k.car);
        if (vt != 0x8A0384 && vt != 0x8A0144 && vt != 0x8A3FA4) continue;   // (gone)
        memset(g_resetObj, 0, sizeof(g_resetObj));
        memcpy(g_resetObj + 0x10, k.m0, sizeof(k.m0));         // +0x10: matrix (position at +0x40)
        *reinterpret_cast<BYTE**>(g_resetObj + 0x60) = k.car;  // +0x60: the car
        reinterpret_cast<void (__attribute__((thiscall)) *)(BYTE*)>(hot::fnCarReset)(g_resetObj);
    }
}
static void NoteHazardCar(BYTE* car) {                          // from RegCar, once per car per step
    DWORD vt = *reinterpret_cast<DWORD*>(car);
    if (vt != 0x8A0384 && vt != 0x8A0144 && vt != 0x8A3FA4) return;  // race cars only
    DWORD now = GetTickCount();
    HazardCar* slot = nullptr;
    for (HazardCar& c : g_hzCars) {
        if (c.car == car) { c.ms = now; return; }
        if (!slot || now - c.ms > now - slot->ms) slot = &c;            // a free or the oldest slot
    }
    slot->car = car; slot->ms = now;
}
static void QueueKick(BYTE* car, const HazardBox& b, float sp) {
    DWORD now = GetTickCount();
    HazardKick* k = nullptr;
    for (HazardKick& e : g_kicks) if (e.car == car) { k = &e; break; }
    if (k && (k->pending || now - k->ms < 700 || (k->safe && now - k->safe < 2000))) return;   // one hit per swipe; none 2 s after a put-back
    if (!k) {
        for (HazardKick& e : g_kicks) if (!e.pending && !e.reset && (!k || now - e.ms > now - k->ms)) k = &e;   // the oldest free one
        if (!k) return;
    }
    if (k->car != car) { k->safe = 0; k->flying = 0; k->watch = k->reset = false; }   // (another car's slot)
    k->car = car; k->ms = now;
    // The hands move 100-300 units/s at the box centre and often downwards (a slam):
    // their velocity as it is would fire a car into the ground at 400 units/s. And
    // a swipe along the track only sent the car down the track, into the walls'
    // reach again. So the car goes out sideways (its own right or left, whichever
    // the swipe leans to), with a set strength, and up.
    const float* right = reinterpret_cast<const float*>(car + 0x10);   // the car's row 0: its right
    float h[3] = { right[0], 0.0f, right[2] };
    float hl = sqrtf(Dot3(h, h));
    if (hl > 0.01f) for (int i = 0; i < 3; i++) h[i] /= hl;
    else { h[0] = b.v[0]; h[2] = b.v[2]; hl = sqrtf(Dot3(h, h)); if (hl > 0.01f) { h[0] /= hl; h[2] /= hl; } }
    // Away from the part (the vampire's hand comes from his side of the track: thrown
    // towards him, the car hit his body and stayed on the track).
    const float* cp = reinterpret_cast<const float*>(car + 0x10) + 12;
    if (h[0] * (cp[0] - b.pos[0]) + h[2] * (cp[2] - b.pos[2]) < 0.0f) { h[0] = -h[0]; h[2] = -h[2]; }
    k->dir[0] = h[0]; k->dir[1] = h[2];
    k->dv[0] = h[0] * g_cfg.hazardPush;
    k->dv[1] = g_cfg.hazardLift;
    k->dv[2] = h[2] * g_cfg.hazardPush;
    k->w[0] = h[2] * g_cfg.hazardSpin;                                  // tumble about the horizontal axis
    k->w[1] = 0.0f;                                                     //   across the swipe
    k->w[2] = -h[0] * g_cfg.hazardSpin;
    InterlockedIncrement(&g_kicksPending);
    InterlockedExchange(&k->pending, 1);
    if (++g_hzKicks <= 30)
        Logf("hazards: box %p (moving %.1f units/s) hit car %p%s: velocity +(%.1f %.1f %.1f)\n", b.box, sp, car,
             *reinterpret_cast<DWORD*>(car) == 0x8A0384 ? " (you)" : "", k->dv[0], k->dv[1], k->dv[2]);
}
// A part's size (its mesh's, +0x88), or null.
static const float* PartSize(BYTE* box) {
    BYTE* obj = *reinterpret_cast<BYTE**>(box + 0x6C);
    BYTE* x = obj ? *reinterpret_cast<BYTE**>(obj + 0x174) : nullptr;
    BYTE* mesh = x ? *reinterpret_cast<BYTE**>(x + 0x10) : nullptr;
    return mesh ? reinterpret_cast<const float*>(mesh + 0x88) : nullptr;
}
// A gate panel: thin (under 2 units) and upright (third axis vertical).
static bool IsPanel(const float* size, const float* m) {
    return size && size[0] > 0.01f && size[0] < 2.0f && fabsf(m[9]) > 0.95f;
}
// After the game moves a box set's boxes (once per step): their speed, and the hit test.
static void HazardStep(BYTE* set) {                             // (after the game moved the boxes: +0x14 is current)
    BYTE* boxes = *reinterpret_cast<BYTE**>(set + 0xC);
    DWORD n = *reinterpret_cast<DWORD*>(set + 0x10);
    DWORD now = GetTickCount();
    float dt = *reinterpret_cast<const float*>(hot::stepDt);           // 1/60
    for (DWORD i = 0; boxes && i < n; i++) {
        BYTE* box = boxes + i * 0x7C;
        if (!box[8] || !*reinterpret_cast<void**>(box)) continue;       // (as 0x4E6630: inactive or unused)
        const float* m = reinterpret_cast<const float*>(box + 0x14);   // rows: axes; row 3: position
        const float* lo = reinterpret_cast<const float*>(box + 0x54);
        const float* hi = reinterpret_cast<const float*>(box + 0x60);
        if (hi[0] - lo[0] < 0.01f) continue;                            // a part without a size
        if (IsPanel(PartSize(box), m)) continue;                        // the gate: it blocks, it doesn't throw
        HazardBox* b = nullptr; HazardBox* slot = nullptr;
        for (HazardBox& e : g_hzBoxes) {
            if (e.box == box) { b = &e; break; }
            if (!slot || now - e.ms > now - slot->ms) slot = &e;
        }
        if (!b) { b = slot; memset(b, 0, sizeof(*b)); b->box = box; b->ms = now - 1000; }
        bool fresh = now - b->ms < 100;                                 // last step's position is usable
        for (int k = 0; k < 3; k++) {
            b->v[k] = fresh ? (m[12 + k] - b->pos[k]) / dt : 0.0f;
            b->pos[k] = m[12 + k];
        }
        b->ms = now;
        float sp = sqrtf(Dot3(b->v, b->v));
        if (sp > 1000.0f) { b->v[0] = b->v[1] = b->v[2] = 0.0f; continue; }   // a jump (a reset), not a swipe
        if (sp > b->maxSpeed) b->maxSpeed = sp;
        if (sp < g_cfg.hazardMinSpeed) continue;
        for (const HazardCar& c : g_hzCars) {
            if (!c.car || now - c.ms > 200) continue;
            const float* cp = reinterpret_cast<const float*>(c.car + 0x10) + 12;
            float d[3] = { cp[0] - m[12], cp[1] - m[13], cp[2] - m[14] };
            bool in = true;
            for (int a = 0; a < 3 && in; a++)                           // within reach on each of the box's axes
                in = fabsf(Dot3(d, m + 4 * a)) <= hi[a] * g_cfg.hazardReach + 1.5f;   // (+ about half a car)
            if (in) { b->hits++; QueueKick(c.car, *b, sp); }
        }
    }
}
// A thrown car flies through walls (the track's invisible walls keep it on the
// track otherwise) for hazardFlyTime seconds: the collision code asks the contact
// filter (0x489090) about each contact, and contacts of a flying car whose normal
// is level (a wall, not the ground: |y| < 0.6) are deleted. The rest is 0x489090
// as it was.
static LONG g_hzWallsLogged = 0;
typedef void (__cdecl *BoxHit_t)(BYTE* box, BYTE* car);
extern "C" bool __cdecl HkContactFilter(BYTE* c) {
    BYTE* car = *reinterpret_cast<BYTE**>(c + 0x30);
    if (!car || *reinterpret_cast<DWORD*>(c + 0x34)) return false;
    int idA = *reinterpret_cast<int*>(c + 0x28), idB = *reinterpret_cast<int*>(c + 0x2C);
    const float* n = reinterpret_cast<const float*>(c + 0xC);          // the contact normal
    if (g_cfg.hazardFlyTime > 0.0f && fabsf(n[1]) < 0.6f) {
        DWORD now = GetTickCount();
        for (HazardKick& k : g_kicks)
            if (k.car == car && k.flying && now - k.flying < (DWORD)(g_cfg.hazardFlyTime * 1000.0f)) {
                if (++g_hzWallsLogged <= 5)
                    Logf("hazards: thrown car %p through a wall (materials %d, %d)\n", car, idA, idB);
                return true;
            }
    }
    const int* noIds = reinterpret_cast<const int*>(hot::contactNoIds);
    bool noId = false;
    for (int i = 0; i < 2 && !noId; i++) noId = noIds[i] != -1 && (idA == noIds[i] || idB == noIds[i]);
    if (BYTE* box = *reinterpret_cast<BYTE**>(c + 0x38)) {
        if (!noId) {
            if (!box[0x10]) return false;                               // a solid box: a normal contact
            if (BoxHit_t cb = *reinterpret_cast<BoxHit_t*>(box + 4)) cb(box, car);
        }
    } else if (!noId) return false;
    reinterpret_cast<void (__cdecl *)(const char*, ...)>(hot::debugPrint)(
        reinterpret_cast<const char*>(hot::contactDelMsg), idA, idB);
    return true;
}
typedef void (__attribute__((thiscall)) *KinStep_t)(BYTE*);
static void __attribute__((thiscall)) HkKinStep(BYTE* set) {
    reinterpret_cast<KinStep_t>(hot::fnKinStep)(set);
    HazardResets();
    HazardStep(set);
}
// Gate panels (thin, upright parts: the graveyard's swinging gate). Each part's
// collision box (a CollisionBox, obj +0x174 -> +0x10: +0x88 its half-size, +0x24
// its frame) is authored in the level file, centred and at the right size, and
// the game's PhysX box matches it (0x5F21D0/0x5F2260). But a panel is only
// 1.6-2.6 thick and swings shut a unit a step: cars slipped through it. With
// hazardGateFix=1 a panel's box is made 3 units thick.
typedef void (__attribute__((thiscall)) *KinCreate_t)(BYTE*);
static void __attribute__((thiscall)) HkKinCreate(BYTE* box) {  // a box's actor: halved, or a gate panel's made thicker
    // box +0x54: min x, y, z / max x, y, z, = -/+ half the half-size; the actor's
    // NxBox gets max - min as its dimensions (= half-sizes).
    float* c = reinterpret_cast<float*>(box + 0x54);
    float saved[6];
    memcpy(saved, c, sizeof(saved));
    if (IsPanel(PartSize(box), reinterpret_cast<const float*>(box + 0x14))) {   // (a gate panel: never halved)
        if (g_cfg.hazardGateFix && c[3] - c[0] < 1.5f) { c[0] = -0.75f; c[3] = 0.75f; }   // (3 thick: half-size 1.5)
    } else {
        for (int i = 0; i < 6; i++) c[i] *= 0.5f;               // (the slabs' and hands' boxes, see above)
    }
    reinterpret_cast<KinCreate_t>(hot::fnKinCreate)(box);
    memcpy(c, saved, sizeof(saved));
}
typedef void (__attribute__((thiscall)) *KinSetup_t)(BYTE*, void*, void*, DWORD);
static void __attribute__((thiscall)) HkKinSetup(BYTE* set, void* model, void* a2, DWORD flag) {
    reinterpret_cast<KinSetup_t>(hot::fnKinSetup)(set, model, a2, flag);
    TuningFlush();                                              // (a track is loading: its numbers are in)
    BYTE* boxes = *reinterpret_cast<BYTE**>(set + 0xC);
    DWORD n = *reinterpret_cast<DWORD*>(set + 0x10);
    if (++g_hzLogged > 40) return;
    Logf("hazards: animated object %p: %lu part(s)%s\n", model, (unsigned long)n, (flag & 0xFF) ? ", pass-through" : "");
    for (DWORD i = 0; boxes && i < n && i < 12; i++) {
        const BYTE* b = boxes + i * 0x7C;
        const float* p = reinterpret_cast<const float*>(b + 0x44);
        const float* lo = reinterpret_cast<const float*>(b + 0x54);
        const float* hi = reinterpret_cast<const float*>(b + 0x60);
        if (hi[0] - lo[0] < 0.01f) Logf("  box %p: no size (no collision)\n", b);
        else Logf("  box %p at (%.1f %.1f %.1f), size %.1f x %.1f x %.1f\n", b, p[0], p[1], p[2],
                  hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
    }
}
static void HazardReport() {                                    // at quitting: how fast each part moved
    for (const HazardBox& b : g_hzBoxes)
        if (b.box && b.maxSpeed > 0.5f)
            Logf("hazards: box %p: fastest %.1f units/s, %ld hit(s)\n", b.box, b.maxSpeed, b.hits);
}
static void InstallHazards() {
    if (!g_cfg.hazardHits) { Logf("hazards: original (hazardHits=0)\n"); return; }
    if (!InstallCarSync() ||
        !PatchCall(hot::callKinSetup, reinterpret_cast<void*>(&HkKinSetup), 5) ||
        !PatchCall(hot::callKinCreate, reinterpret_cast<void*>(&HkKinCreate), 5) ||
        !PatchCall(hot::callKinStep, reinterpret_cast<void*>(&HkKinStep), 5) ||
        !PatchJmp(hot::contactFilter, reinterpret_cast<DWORD>(&HkContactFilter))) {
        Logf("hazards: patch FAILED\n");
        return;
    }
    g_hzOn = true;
    Logf("hazards: on (min speed %.1f, reach x%.2f, push %.1f, lift %.1f, spin %.1f, fly time %.1f, fall reset %.1f, smoke %d)\n",
         g_cfg.hazardMinSpeed, g_cfg.hazardReach, g_cfg.hazardPush, g_cfg.hazardLift, g_cfg.hazardSpin,
         g_cfg.hazardFlyTime, g_cfg.hazardFallReset, g_cfg.hazardSmoke);
}

// ---------------------------------------------------------------------------
// AI opponents. Their rubber-banding is CatchUp (per car, 0x6681A0; the script's
// Catchup* values are loaded and never used): an AI more than 0/80/160 units
// (by difficulty) ahead of the player has its target speed capped at 26.7 (31.1
// level, 35.6 behind) and its grip and turning cut. aiLeadSlowdown=0 skips that
// case (0x66845D: jp -> jmp), so a leading AI races at the level pace; the
// boost when behind stays.
// Weapons: every AI can get and fire every item, but after any AI shot all AIs
// wait 5 s (gunner+0xAF4 x gunners, counted down by each: 5 s real time); only
// 40/50/60% of shots go for the player (the rest at the leaders); and area
// weapons (ice, EMP, airstrike, smoke, mortar 2, tesla 2) are held while 2 of
// them are out. Each is set from the INI here.
// ---------------------------------------------------------------------------
// Start boost: at GO the game gives each car its start boost if the player hit
// the green (0x479E30): the AI cars' version of it (vtable +0x204) is empty, so
// they never had one. After it, each AI car gets the boost (0x4545B0, the
// player's) with aiStartBoost percent chance.
static DWORD g_rng = 0;
static DWORD Rand32() {                                         // xorshift, seeded from the clock
    if (!g_rng) { LARGE_INTEGER q; QueryPerformanceCounter(&q); g_rng = (DWORD)q.QuadPart | 1; }
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
    return g_rng;
}
typedef void (__attribute__((thiscall)) *ObjFn_t)(BYTE*);
static void __attribute__((thiscall)) HkIntroGo(BYTE* state) {
    reinterpret_cast<ObjFn_t>(hot::fnIntroGo)(state);
    int boosted = 0, ai = 0;
    for (int i = 0; i < 8; i++) {
        BYTE* racer = reinterpret_cast<BYTE* const*>(0x953C90)[i];
        BYTE* car = racer ? *reinterpret_cast<BYTE**>(racer + 0x10) : nullptr;
        if (!car) continue;
        DWORD vt = *reinterpret_cast<DWORD*>(car);
        if (vt != 0x8A0144 && vt != 0x8A3FA4) continue;         // AI cars only
        ai++;
        if (Rand32() % 10000 < (DWORD)(g_cfg.aiStartBoost * 100.0f)) {
            reinterpret_cast<ObjFn_t>(hot::fnCarStartBoost)(car);
            boosted++;
        }
    }
    Logf("AI: start boost for %d of %d AI car(s)\n", boosted, ai);
}
static float  g_aiCooldown = 5.0f;              // per AI gunner: x gunners, counted down by each = seconds
static double g_aiTarget = 40.0;
static void InstallAI() {
    bool ok = true;
    if (!g_cfg.aiLeadSlowdown) { BYTE j = 0xEB; ok = ok && PatchBytes(hot::catchupAhead, &j, 1); }
    if (g_cfg.aiStartBoost > 0.0f) ok = ok && PatchCall(hot::callIntroGo, reinterpret_cast<void*>(&HkIntroGo), 5);
    if (g_cfg.aiWeaponCooldown > 0.0f && g_cfg.aiWeaponCooldown != 5.0f) {
        g_aiCooldown = g_cfg.aiWeaponCooldown;
        BYTE b[6] = { 0xD9, 0x05 };                             // fld dword ptr [g_aiCooldown]
        DWORD a = reinterpret_cast<DWORD>(&g_aiCooldown); memcpy(b + 2, &a, 4);
        ok = ok && PatchBytes(hot::aiFireCooldown, b, 6);
    }
    if (g_cfg.aiTargetPlayer >= 0.0f && g_cfg.aiTargetPlayer != 40.0f) {
        g_aiTarget = g_cfg.aiTargetPlayer;
        BYTE b[6] = { 0xDC, 0x05 };                             // fadd qword ptr [g_aiTarget]
        DWORD a = reinterpret_cast<DWORD>(&g_aiTarget); memcpy(b + 2, &a, 4);
        ok = ok && PatchBytes(hot::aiTargetChance, b, 6);
    }
    if (g_cfg.aiAreaWeaponLimit != 2) {
        BYTE n = (BYTE)(g_cfg.aiAreaWeaponLimit < 1 ? 1 : g_cfg.aiAreaWeaponLimit > 127 ? 127 : g_cfg.aiAreaWeaponLimit);
        ok = ok && PatchBytes(hot::aiAreaLimit, &n, 1);
    }
    Logf("AI: %s; lead slowdown %s, a shot every %.1f s, %.0f%%+ at the player, area weapons up to %d at once, start boost %.0f%%\n",
         ok ? "on" : "patch FAILED", g_cfg.aiLeadSlowdown ? "on" : "off", g_aiCooldown, g_aiTarget, g_cfg.aiAreaWeaponLimit, g_cfg.aiStartBoost);
}

// ---------------------------------------------------------------------------
// Start boost. The 3-2-1 dial (GSSingle321_c) has four Flash animations: the
// text (3-2-1-GO: it times the start), the gauge, the wheel and the fire. After
// its intro the gauge loops; pressing accelerate while the gauge is on frames
// 127-144 (the green light) gives the boost start - one try (0x4E8619). The
// gauge and the wheel are run boostMeterSpeed times as fast: each update they get
// the extra time on top of the game's own step. The text, and so the countdown
// and GO, keep their timing.
// ---------------------------------------------------------------------------
typedef void (__attribute__((thiscall)) *Upd321_t)(BYTE*);
typedef bool (__attribute__((thiscall)) *PastMark_t)(BYTE*, const char*);
static void __attribute__((thiscall)) Hk321Update(BYTE* self) {
    float k = g_cfg.boostMeterSpeed;
    if (k > 1.0f && !self[0x494] && *reinterpret_cast<int*>(self + 0x30) < 5) {   // no press yet, before GO
        float dt = *reinterpret_cast<const float*>(hot::frameDt);
        BYTE* anims[2] = { self + 0x1B0, self + 0x2C4 };        // the gauge, the wheel
        for (BYTE* a : anims)
            if (*reinterpret_cast<int*>(a + 0x18) == 2 &&       // playing, past its intro: looping
                reinterpret_cast<PastMark_t>(hot::fnAnimPastMark)(a, reinterpret_cast<const char*>(hot::strIntroEnd)))
                *reinterpret_cast<float*>(a + 0x14) += (k - 1.0f) * dt * (a[0x23] ? 1.0f : -1.0f);
    }
    reinterpret_cast<Upd321_t>(hot::fn321Update)(self);
}
static void InstallStartBoost() {
    if (g_cfg.boostMeterSpeed <= 1.0f) return;
    if (!PatchPointer(reinterpret_cast<void**>(hot::vt321Update), reinterpret_cast<void*>(&Hk321Update))) {
        Logf("start boost: patch FAILED\n");
        return;
    }
    Logf("start boost: meter x%.2f\n", g_cfg.boostMeterSpeed);
}

// ---------------------------------------------------------------------------
// Starting grid. Each track has one start action point per grid slot (type 1,
// index = slot; position at +0x40, heading = matrix row 2 at +0x30), laid one
// behind the other ~15 apart: the cars start in single file. GSWorldInitState_c
// (and GSWorldResetState_c on a restart) puts each car on its slot's point with
// 0x63E750. The mod puts them in a compact grid instead, as in the trailer:
// from the front of the game's line, facing the way the start points face,
// gridColumns abreast gridWidth apart, rows gridRowGap apart, each car to the
// right gridStagger further back (the rows descend; with gridRowGap = gridColumns
// x gridStagger every car is further back than the one before, as on an F1
// grid across four lanes). The car order is kept.
// ---------------------------------------------------------------------------
static const BYTE* FindStartAP(int idx) {
    const DWORD lists[2] = { hot::apUserList, hot::apList };
    for (DWORD list : lists) {
        BYTE* const* aps = *reinterpret_cast<BYTE* const* const*>(list);
        int n = *reinterpret_cast<const int*>(list + 4);
        for (int i = 0; aps && i < n && i < 4096; i++) {
            const BYTE* ap = aps[i];
            if (ap && *reinterpret_cast<const short*>(ap + 0x50) == 1 && *reinterpret_cast<const short*>(ap + 0x52) == idx) return ap;
        }
    }
    return nullptr;
}
typedef void (__attribute__((thiscall)) *PlaceCar_t)(BYTE*, const float*, const float*, int, float, int);
static int GameMode() {
    BYTE* ev = *reinterpret_cast<BYTE**>(hot::curEvent);
    return ev ? reinterpret_cast<int (__attribute__((thiscall)) *)(BYTE*)>(hot::fnGameMode)(ev) : -1;
}
static bool RampageSpawn(BYTE* car, const float* pos, const float* mat, int a3, float a4, int a5);
static void SparkSetup();
static void __attribute__((thiscall)) HkGridPlace(BYTE* car, const float* pos, const float* mat, int a3, float a4, int a5) {
    SparkSetup();
    int mode = GameMode();
    if (mode == 7 && g_cfg.rampageSpread && RampageSpawn(car, pos, mat, a3, a4, a5)) return;
    if (mode != 6 && mode != 8 && mode != 10) {                 // the grid is for races (race, eliminator, tournament)
        reinterpret_cast<PlaceCar_t>(hot::fnPlaceCar)(car, pos, mat, a3, a4, a5);
        return;
    }
    const BYTE* ap = reinterpret_cast<const BYTE*>(pos) - 0x40;
    int slot = *reinterpret_cast<const short*>(ap + 0x52);
    const BYTE* ap0 = FindStartAP(0);
    int n = 0;                                                  // start points on this track
    while (n < 16 && FindStartAP(n)) n++;
    if (!g_cfg.gridLayout || slot < 0 || slot >= n || n < 2 || !ap0) {
        reinterpret_cast<PlaceCar_t>(hot::fnPlaceCar)(car, pos, mat, a3, a4, a5);
        return;
    }
    // ahead: the way the cars face (the start point's row 2), level; right: across
    const float* h = reinterpret_cast<const float*>(ap0 + 0x30);
    float ah[3] = { h[0], 0.0f, h[2] };
    float al = sqrtf(Dot3(ah, ah));
    if (al < 0.01f) { reinterpret_cast<PlaceCar_t>(hot::fnPlaceCar)(car, pos, mat, a3, a4, a5); return; }
    ah[0] /= al; ah[2] /= al;
    float rt[3] = { ah[2], 0.0f, -ah[0] };
    // the front of the game's line: the start point furthest ahead; the car order
    // (slot 0 first) is kept, front to back
    const BYTE* front = ap0; float best = -1e30f;
    for (int i = 0; i < n; i++) {
        const float* p = reinterpret_cast<const float*>(FindStartAP(i) + 0x40);
        float d = p[0] * ah[0] + p[2] * ah[2];
        if (d > best) { best = d; front = FindStartAP(i); }
    }
    const float* p0 = reinterpret_cast<const float*>(ap0 + 0x40);
    const float* pn = reinterpret_cast<const float*>(FindStartAP(n - 1) + 0x40);
    bool slot0Front = p0[0] * ah[0] + p0[2] * ah[2] >= pn[0] * ah[0] + pn[2] * ah[2];
    int rank = slot0Front ? slot : n - 1 - slot;                // 0 = pole
    int cols = g_cfg.gridColumns < 1 ? 1 : g_cfg.gridColumns > 8 ? 8 : g_cfg.gridColumns;
    int row = rank / cols, col = rank % cols;
    float across = (col - (cols - 1) * 0.5f) * g_cfg.gridWidth;
    float back = row * g_cfg.gridRowGap + col * g_cfg.gridStagger;   // each car further right, further back
    const float* fp = reinterpret_cast<const float*>(front + 0x40);
    float np[3] = { fp[0] + rt[0] * across - ah[0] * back, fp[1], fp[2] + rt[2] * across - ah[2] * back };
    Logf("grid: slot %d -> row %d column %d (%.1f %.1f %.1f)\n", slot, row, col, np[0], np[1], np[2]);
    reinterpret_cast<PlaceCar_t>(hot::fnPlaceCar)(car, np, reinterpret_cast<const float*>(front + 0x10), a3, a4, a5);
}
// ---------------------------------------------------------------------------
// Rampage (game mode 7: hunt down the opponents before the time runs out). The
// game starts it on the race grid and its AI simply races the track, so it was
// a walkover. The mod spreads the AI cars over the whole track at the start and
// makes them run from the player.
// Spawn points: the track's reset points (action points of type 0xB4, placed on
// the road and facing along it) and points every 30 units along the track's
// lines (the ones the AI drives, 0x9680D0). From those, each AI car gets the one
// farthest from the player and from the cars already placed, with spots behind
// the player counted as nearer (rampageBehind) and spots within rampageMinDist
// of them left till last. The player keeps the start.
// Fleeing: the AI brain refills each car's target points (0x648B50; one brain a
// frame, in turn). After it, an AI car with the player within rampageFleeRange
// gets its target speeds raised by rampageFleeSpeed, and its target points moved
// across the road (up to rampageDodge of the road's width) away from the player.
// Turning round isn't possible: the AI's routes only go forward.
// ---------------------------------------------------------------------------
struct SpawnPt { float m[16]; };
static SpawnPt g_spawns[1024];
static int g_spawnN = 0, g_spawnUsed = 0;
static DWORD g_spawnMs = 0;
static void AddSpawn(const float* pos, const float* fwd) {
    if (g_spawnN >= 1024) return;
    float f[3] = { fwd[0], 0.0f, fwd[2] };
    float l = sqrtf(Dot3(f, f));
    if (l < 0.01f) return;
    f[0] /= l; f[2] /= l;
    float* m = g_spawns[g_spawnN++].m;
    memset(m, 0, 64);
    m[0] = f[2]; m[2] = -f[0];                                  // right
    m[5] = 1.0f;                                                // up
    m[8] = f[0]; m[10] = f[2];                                  // forward (row 2: the heading 0x63E750 uses)
    m[12] = pos[0]; m[13] = pos[1] + 0.5f; m[14] = pos[2]; m[15] = 1.0f;
}
static void CollectSpawns() {
    g_spawnN = 0;
    const DWORD lists[2] = { hot::apUserList, hot::apList };
    int nAP = 0, types[8][2] = {}, nt = 0;                      // (type histogram, for the log)
    for (DWORD list : lists) {
        BYTE* const* aps = *reinterpret_cast<BYTE* const* const*>(list);
        int na = *reinterpret_cast<const int*>(list + 4);
        for (int i = 0; aps && i < na && i < 4096; i++) {
            const BYTE* ap = aps[i];
            if (!ap) continue;
            int t = *reinterpret_cast<const short*>(ap + 0x50);
            int k = 0; while (k < nt && types[k][0] != t) k++;
            if (k < nt) types[k][1]++; else if (nt < 8) { types[nt][0] = t; types[nt][1] = 1; nt++; }
            if (t != 0xB4) continue;
            const float* q = reinterpret_cast<const float*>(ap + 0x40);
            bool dup = false;                                   // (an AP in both lists)
            for (int j = 0; j < g_spawnN && !dup; j++) {
                const float* o = g_spawns[j].m + 12;
                dup = fabsf(o[0] - q[0]) < 1.0f && fabsf(o[2] - q[2]) < 1.0f;
            }
            if (!dup) { AddSpawn(q, reinterpret_cast<const float*>(ap + 0x30)); nAP = g_spawnN; }
        }
    }
    // The track's own lines (the AI drives these; loaded with the level, before
    // the grid): a spot every kSpotGap units along each. A line's nodes run one
    // way; which way the cars go comes from the reset points next to it (they
    // face along the track), else from its start junction.
    const float kSpotGap = 30.0f;
    int nLines = *reinterpret_cast<const int*>(hot::lineCount), nUsed = 0, nFlagged = 0, nRev = 0;
    const BYTE* lines = *reinterpret_cast<BYTE* const*>(hot::lineTable);
    BYTE* const* juncs = *reinterpret_cast<BYTE* const* const*>(hot::juncTable);
    int nJuncs = *reinterpret_cast<const int*>(hot::juncCount);
    for (int i = 0; lines && i < nLines && i < 1024; i++) {
        const BYTE* L = lines + i * 0x74;
        if (L[0x4A]) { nFlagged++; continue; }                  // (skipped by the game's own nearest-line search)
        int fmt = *reinterpret_cast<const int*>(L + 0x54);
        int stride = fmt == 1 ? 0x30 : fmt == 2 ? 0x40 : 0x10;
        int nn = *reinterpret_cast<const short*>(L + 0x3E);
        const BYTE* nodes = *reinterpret_cast<BYTE* const*>(L + 0x60);
        if (!nodes || nn < 2 || nn > 100000) continue;
        auto node = [&](int k) { return reinterpret_cast<const float*>(nodes + k * stride); };
        // which way: vote with the reset points within 40 units of a node
        float vote = 0.0f;
        for (int r = 0; r < nAP; r++) {
            const float* rm = g_spawns[r].m;
            for (int k = 0; k + 1 < nn; k++) {
                const float* a = node(k); const float* b = node(k + 1);
                float dx = a[0] - rm[12], dz = a[2] - rm[14];
                if (dx * dx + dz * dz > 40.0f * 40.0f) continue;
                vote += (b[0] - a[0]) * rm[8] + (b[2] - a[2]) * rm[10];
                break;
            }
        }
        bool rev = false;
        if (vote != 0.0f) rev = vote < 0.0f;
        else {                                                  // the start junction: > 0 = entered at node 0
            int j = *reinterpret_cast<const short*>(L + 0x34);
            const BYTE* J = (juncs && j >= 0 && j < nJuncs) ? juncs[j] : nullptr;
            if (J) {
                int nc = *reinterpret_cast<const short*>(J + 0x42);
                for (int c = 0; c < nc && c < 8; c++)
                    if (*reinterpret_cast<const short*>(J + 0x1C + c * 2) == i) {
                        rev = *reinterpret_cast<const short*>(J + 0x2C + c * 2) <= 0; break;
                    }
            }
        }
        nUsed++; if (rev) nRev++;
        float run = 0.0f;                                       // (the first one kSpotGap in: not at the junction)
        for (int k = 0; k + 1 < nn; k++) {
            const float* a = node(k); const float* b = node(k + 1);
            float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            run += sqrtf(Dot3(d, d));
            if (run < kSpotGap) continue;
            run = 0.0f;
            bool dupe = false;                                  // (parallel lanes: one spot)
            for (int j = 0; j < g_spawnN && !dupe; j++) {
                const float* o = g_spawns[j].m + 12;
                dupe = fabsf(o[0] - a[0]) < 10.0f && fabsf(o[2] - a[2]) < 10.0f && fabsf(o[1] - a[1]) < 10.0f;
            }
            if (dupe) continue;
            if (rev) { d[0] = -d[0]; d[2] = -d[2]; }
            AddSpawn(a, d);
        }
    }
    char hist[160] = ""; int hl = 0;
    for (int k = 0; k < nt && hl < 140; k++) hl += snprintf(hist + hl, sizeof(hist) - hl, " %X:%d", types[k][0], types[k][1]);
    Logf("rampage: %d spot(s): %d reset point(s), %d along %d of %d track line(s) (%d flagged, %d reversed; action point types%s)\n",
         g_spawnN, nAP, g_spawnN - nAP, nUsed, nLines, nFlagged, nRev, hist);
}
static bool RampageSpawn(BYTE* car, const float*, const float*, int a3, float a4, int a5) {
    DWORD vt = *reinterpret_cast<DWORD*>(car);
    if (vt != 0x8A0144 && vt != 0x8A3FA4) return false;         // the player keeps the start
    DWORD now = GetTickCount();
    if (now - g_spawnMs > 3000) { CollectSpawns(); g_spawnUsed = 0; }   // a new start: the points afresh
    g_spawnMs = now;
    if (!g_spawnN) return false;
    // Where the player is: the start (they keep it), facing along it
    float ref[3] = { 0, 0, 0 }, fwd[3] = { 0, 0, 0 }; bool haveRef = false;
    if (const BYTE* start = FindStartAP(0)) { memcpy(ref, start + 0x40, 12); memcpy(fwd, start + 0x30, 12); haveRef = true; }
    fwd[1] = 0.0f;
    { float l = sqrtf(Dot3(fwd, fwd)); if (l > 0.01f) { fwd[0] /= l; fwd[2] /= l; } else fwd[0] = fwd[2] = 0.0f; }
    // Each AI car: the spot farthest from the player and from the AI cars placed
    // so far. Spots behind the player count as closer to them (x rampageBehind),
    // and spots nearer the player than rampageMinDist are used only if nothing
    // else is left.
    static float placed[16][3];
    float bestD = -1.0f; int best = -1;
    for (int pass = 0; pass < 2 && best < 0; pass++) {
        for (int i = 0; i < g_spawnN; i++) {
            const float* q = g_spawns[i].m + 12;
            bool taken = false;
            for (int j = 0; j < g_spawnUsed && j < 16 && !taken; j++)
                taken = fabsf(q[0] - placed[j][0]) < 1.0f && fabsf(q[2] - placed[j][2]) < 1.0f;
            if (taken) continue;
            float dmin = 1e30f;
            if (haveRef) {
                float d[3] = { q[0] - ref[0], q[1] - ref[1], q[2] - ref[2] };
                dmin = sqrtf(Dot3(d, d));
                if (d[0] * fwd[0] + d[2] * fwd[2] < 0.0f) dmin *= g_cfg.rampageBehind;
                if (pass == 0 && dmin < g_cfg.rampageMinDist) continue;
            }
            for (int j = 0; j < g_spawnUsed && j < 16; j++) {
                float d[3] = { q[0] - placed[j][0], q[1] - placed[j][1], q[2] - placed[j][2] };
                float dd = sqrtf(Dot3(d, d)); if (dd < dmin) dmin = dd;
            }
            if (dmin > bestD) { bestD = dmin; best = i; }
        }
    }
    if (best < 0) return false;
    const float* m = g_spawns[best].m;
    if (g_spawnUsed < 16) memcpy(placed[g_spawnUsed], m + 12, 12);
    g_spawnUsed++;
    Logf("rampage: AI car %p spawned at (%.1f %.1f %.1f), %.0f from you/the nearest car (of %d spots)\n",
         car, m[12], m[13], m[14], bestD, g_spawnN);
    reinterpret_cast<PlaceCar_t>(hot::fnPlaceCar)(car, m + 12, m, a3, a4, a5);
    return true;
}
typedef void (__attribute__((thiscall)) *Brain_t)(BYTE*);
static int g_rampageMode = -2; static DWORD g_rampageModeMs = 0;
static void __attribute__((thiscall)) HkBrainTargets(BYTE* brain) {
    reinterpret_cast<Brain_t>(hot::fnBrainTargets)(brain);
    DWORD now = GetTickCount();
    if (now - g_rampageModeMs > 1000) { g_rampageMode = GameMode(); g_rampageModeMs = now; }   // (once a second)
    if (g_rampageMode != 7) return;
    BYTE* car = *reinterpret_cast<BYTE**>(brain + 0xC);
    if (!car || *reinterpret_cast<DWORD*>(car) != 0x8A0144) return;    // AI cars only
    BYTE* player = nullptr;                                     // the player's car (GSPlayerRaceCar_c)
    for (int i = 0; i < 8 && !player; i++) {
        BYTE* r = reinterpret_cast<BYTE* const*>(0x953C90)[i];
        BYTE* c = r ? *reinterpret_cast<BYTE**>(r + 0x10) : nullptr;
        if (c && *reinterpret_cast<DWORD*>(c) == 0x8A0384) player = c;
    }
    if (!player) return;
    const float* cp = reinterpret_cast<const float*>(car + 0x10) + 12;
    const float* pp = reinterpret_cast<const float*>(player + 0x10) + 12;
    float rel[3] = { cp[0] - pp[0], 0.0f, cp[2] - pp[2] };     // from the player to the car
    float dist = sqrtf(Dot3(rel, rel));
    int n = *reinterpret_cast<int*>(car + 0xC60);
    BYTE* pts = *reinterpret_cast<BYTE**>(car + 0xC64);
    float* spd = *reinterpret_cast<float**>(car + 0xC6C);
    if (n <= 0 || n > 256 || !pts) return;
    if (spd && g_cfg.rampageSpeed != 1.0f)                      // all the time (1 = as the game drives them)
        for (int i = 0; i < n; i++) spd[i] *= g_cfg.rampageSpeed;
    if (dist > g_cfg.rampageFleeRange || dist < 0.01f) return;
    float close = 1.0f - dist / g_cfg.rampageFleeRange;         // 0 at the edge of range, 1 on top
    for (int i = 0; i < n; i++) {
        float* tp = reinterpret_cast<float*>(pts + i * 0x1C);    // target xyz; +0x10: road width vector (right - left)
        const float* side = tp + 4;
        float w2 = side[0] * side[0] + side[2] * side[2];
        if (w2 > 0.01f && g_cfg.rampageDodge > 0.0f) {
            float away = (rel[0] * side[0] + rel[2] * side[2]) >= 0.0f ? 1.0f : -1.0f;   // the side of the road away from the player
            float k = away * g_cfg.rampageDodge * close;
            tp[0] += side[0] * k; tp[2] += side[2] * k;
        }
        if (spd) spd[i] *= 1.0f + (g_cfg.rampageFleeSpeed - 1.0f) * close;
    }
}
static void InstallSpeedBlur() {
    // (the HDR region settings are always noted, for the log)
    if (!PatchCall(hot::callHdrPrint, reinterpret_cast<void*>(&HdrParamNote), 5)) Logf("hdr: patch FAILED\n");
    bool blur = g_cfg.speedBlur > 0.0f;
    if (blur && !g_cfg.interpolate) { Logf("speed blur: needs interpolate=1 (it uses the speed FOV's car speed)\n"); blur = false; }
    if (!blur && !HdrActive()) return;
    const BYTE jmp = 0xEB;
    if (!PatchCall(hot::blurAmount, reinterpret_cast<void*>(&SpeedBlurStub), 6) ||
        (blur && !PatchBytes(hot::blurFlagLevel, &jmp, 1))) { Logf("speed blur: patch FAILED\n"); return; }
    if (blur) Logf("speed blur: up to %.2f, from %.0f to %.0f units/s\n", g_cfg.speedBlur, g_cfg.speedBlurMin, g_cfg.speedBlurMax);
    if (HdrActive()) Logf("hdr: brightness x%.2f, bloom x%.2f, bloom threshold x%.2f\n", g_cfg.hdrBrightness, g_cfg.hdrBloom, g_cfg.hdrBloomThreshold);
}
static void InstallRampage() {
    if (g_cfg.rampageTopSpeed > 0.0f && g_cfg.rampageTopSpeed != 0.45f) {
        if (!PatchBytes(hot::rampageTopSpd, &g_cfg.rampageTopSpeed, 4)) Logf("rampage: top speed patch FAILED\n");
        else Logf("rampage: AI top speed %.0f%% of yours\n", g_cfg.rampageTopSpeed * 100.0f);
    }
    if (!g_cfg.rampageFlee) return;
    if (!PatchCall(hot::callBrainTargets, reinterpret_cast<void*>(&HkBrainTargets), 5)) { Logf("rampage: patch FAILED\n"); return; }
    Logf("rampage: AI flees within %.0f (speed x%.2f, dodge %.2f)\n", g_cfg.rampageFleeRange, g_cfg.rampageFleeSpeed, g_cfg.rampageDodge);
}

static void InstallGrid() {
    if (!g_cfg.gridLayout && !g_cfg.rampageSpread && !g_cfg.sparkStreaks) { Logf("grid: original (gridLayout=0)\n"); return; }   // (the hook also sets up the sparks)
    if (!PatchCall(hot::callGridPlace, reinterpret_cast<void*>(&HkGridPlace), 5) ||
        !PatchCall(hot::callGridReset, reinterpret_cast<void*>(&HkGridPlace), 5)) { Logf("grid: patch FAILED\n"); return; }
    if (g_cfg.gridLayout) Logf("grid: %d abreast, %.1f apart\n", g_cfg.gridColumns, g_cfg.gridWidth);
    else Logf("grid: original (gridLayout=0)\n");
}

// ---------------------------------------------------------------------------
// Sparks. The game's spark particles (0x563xxx-0x566xxx: 50 types, 650
// particles) are drawn by type flags (0x5668F9): flag 4 draws each spark as one
// quad stretched along its velocity (0x5654D0, length P+0x48 = 0.25 s of
// travel); flag 1 as a chain of up to 8 little squares over just 0.04 s of
// travel (0x564BE0) - with a width of 0.035, the orange "line of dots" of a
// car-on-car hit (type 7, flags 1|2|8). Every type with flag 1 is switched to
// flag 4 here (flag 8, whose history the PC version never sets up, cleared
// too), made sparkWidth x as wide, and every streak is sparkLength seconds of
// travel long instead of 0.25 (0x563CB7 reads our value). Done each time a
// level places its cars, after it has loaded its spark types; a type already
// switched is left alone.
// Variety: as each streak spark (a flag-4 type) is born (0x563D23, the end of
// the spawn 0x563AF0, every field set; ecx = the particle, ebp = its type) its
// length and width are scaled by a random amount in sparkVary, and a share of
// them (sparkHot) burn hotter: yellow to near white, full alpha. Colours are 7
// bits a channel there (P+0x28: A R G B, 0-0x7F).
// More of them: the game throws only 1-2 sparks a hit. Each car-hit spark (a
// type switched above; not the wall-scrape sparks, which come every step while
// scraping) brings sparkCount more of its type with it, sparkRate a second at
// most, through the game's own spawn (0x566A40: type, position, velocity,
// gravity), from the same spot, each sent off in its own direction: the first one's velocity, scattered by sparkSpread
// and thrown upward by sparkUp - the fountain of the intro.
// ---------------------------------------------------------------------------
static float g_sparkLength = 0.25f;
static float Rand01() { return (Rand32() & 0xFFFFFF) / 16777216.0f; }
typedef void (__cdecl *SparkSpawn_t)(int, const float*, const float*, const float*, float);
static int g_sparkDepth = 0;
static DWORD g_sparkSwitched[50];                                // types switched from dots to streaks (car hits): their texture then
                                                                 // (a slot a later level fills with another type no longer matches)
static DWORD g_sparkBudgetMs = 0; static float g_sparkBudget = 0.0f;
// The hit's strength: set while the game throws a collision's sparks (HkCollSparks), 0 otherwise.
static float g_sparkImpact = 0.0f;
// How big this hit's sparks are: impact / sparkImpactRef, 0.5 to sparkImpactMax (1 outside a collision).
// (Hits measured in a race ran about 3300 to 5300, averaging 3800.)
static float SparkScale() {
    if (g_sparkImpact <= 0.0f || g_cfg.sparkImpactRef <= 0.0f) return 1.0f;
    float k = g_sparkImpact / g_cfg.sparkImpactRef;
    float mx = g_cfg.sparkImpactMax > 1.0f ? g_cfg.sparkImpactMax : 1.0f;
    return k < 0.5f ? 0.5f : (k > mx ? mx : k);
}
extern "C" void __cdecl SparkSpawned(BYTE* P, int type, BYTE* sys) {
    if (!P || !sys || type < 0 || type >= 50) return;
    const BYTE* E = sys + 0x1CC3C28 + type * 0x3C;
    if (!(*reinterpret_cast<const DWORD*>(E + 0x28) & 4)) return;   // streak sparks only (not smoke)
    bool carHit = g_sparkSwitched[type] && g_sparkSwitched[type] == *reinterpret_cast<const DWORD*>(E);
    float hit = SparkScale();
    if (g_sparkDepth == 0 && g_cfg.sparkCount > 0 && carHit) {  // (the extras' own births don't bring more)
        DWORD now = GetTickCount();                             // budget: sparkRate extras a second, a quarter second's worth banked
        g_sparkBudget += (now - g_sparkBudgetMs) * 0.001f * g_cfg.sparkRate;
        g_sparkBudgetMs = now;
        if (g_sparkBudget > g_cfg.sparkRate * 0.25f) g_sparkBudget = g_cfg.sparkRate * 0.25f;
    }
    if (g_sparkDepth == 0 && g_cfg.sparkCount > 0 && carHit && g_sparkBudget >= 1.0f) {
        g_sparkDepth = 1;
        float pos[3], v0[3], acc[3];
        memcpy(pos, P + 4, 12); memcpy(v0, P + 0x10, 12); memcpy(acc, P + 0x1C, 12);
        float sp = sqrtf(Dot3(v0, v0));
        if (sp < 8.0f) sp = 8.0f;                               // (a light scrape still throws them)
        int extra = (int)(g_cfg.sparkCount * hit + 0.5f);          // harder hits throw more
        for (int i = 0; i < extra && i < 32 && g_sparkBudget >= 1.0f; i++, g_sparkBudget -= 1.0f) {
            float v[3];
            float k = 0.5f + Rand01();                          // 0.5 - 1.5 x as fast
            for (int a = 0; a < 3; a++) v[a] = v0[a] * k + sp * g_cfg.sparkSpread * (2.0f * Rand01() - 1.0f);
            v[1] += sp * g_cfg.sparkUp * (0.3f + Rand01());
            reinterpret_cast<SparkSpawn_t>(hot::fnSparkSpawn)(type, pos, v, acc, 0.0f);
        }
        g_sparkDepth = 0;
    }
    // Its own shape: a size, and somewhere between a long thin needle and a short
    // fat flake; a stronger hit's sparks are longer, wider and faster.
    float v = g_cfg.sparkVary, len = hit, wid = sqrtf(hit);
    if (v > 0.0f) {
        float size = 1.0f + v * (2.0f * Rand01() - 1.0f);       // e.g. 0.4 - 1.6 for 0.6
        float u = 2.0f * Rand01() - 1.0f;                       // -1 flake ... +1 needle
        len *= size * (1.0f + 0.5f * u);
        wid *= size * (1.0f - 0.4f * u);
    }
    *reinterpret_cast<float*>(P + 0x48) *= len;
    *reinterpret_cast<float*>(P + 0x30) *= wid;
    *reinterpret_cast<float*>(P + 0x34) *= wid;
    // Its own flight: the streak lies along its velocity, so turning that turns
    // the spark - up to sparkTwist degrees off its course - and a heavier or
    // lighter fall (gravity x 0.5-1.5) bends some into arcs; it lives 0.7-1.3 x
    // as long.
    float* vel = reinterpret_cast<float*>(P + 0x10);
    float sp = sqrtf(Dot3(vel, vel));
    if (sp > 1e-3f) {
        float sk = sqrtf(hit);
        if (g_cfg.sparkTwist > 0.0f) {
            float r[3] = { 2.0f * Rand01() - 1.0f, 2.0f * Rand01() - 1.0f, 2.0f * Rand01() - 1.0f };
            float tw = g_cfg.sparkTwist < 80.0f ? g_cfg.sparkTwist : 80.0f;   // (tan: keep well short of 90)
            float t = tanf(Rand01() * tw * (3.14159265f / 180.0f));
            float rd = Dot3(r, vel) / sp;                               // r made square to the course: turns it, never
            for (int a = 0; a < 3; a++) r[a] -= vel[a] / sp * rd;       //   back on itself
            float rl = sqrtf(Dot3(r, r));
            if (rl > 1e-3f) {
                for (int a = 0; a < 3; a++) vel[a] = vel[a] / sp + r[a] / rl * t;
                float nl = sqrtf(Dot3(vel, vel));
                for (int a = 0; a < 3; a++) vel[a] *= sp * sk / nl;
            }
        } else for (int a = 0; a < 3; a++) vel[a] *= sk;
        float* acc = reinterpret_cast<float*>(P + 0x1C);
        float gk = 0.5f + Rand01();
        for (int a = 0; a < 3; a++) acc[a] *= gk;
        *reinterpret_cast<float*>(P) *= 0.7f + 0.6f * Rand01();
    }
    if (g_cfg.sparkHot > 0.0f) {
        float h = Rand01(); h = h * h * g_cfg.sparkHot;         // most a little, a few a lot
        if (h > 1.0f) h = 1.0f;
        DWORD c = *reinterpret_cast<DWORD*>(P + 0x28);
        int r = (c >> 16) & 0x7F, g = (c >> 8) & 0x7F, b = c & 0x7F;
        r += (int)((0x7F - r) * h); g += (int)((0x7F - g) * h); b += (int)((0x70 - b) * h * 0.8f);
        if (b < 0) b = 0;
        *reinterpret_cast<DWORD*>(P + 0x28) = 0x7Fu << 24 | (DWORD)r << 16 | (DWORD)g << 8 | (DWORD)b;
    }
}
extern "C" void SparkSpawnStub();
asm(".text\n"
    ".globl _SparkSpawnStub\n_SparkSpawnStub:\n"   // call from 0x563D23 (replaces fld dword ptr [ebx+0x1CB66D8]); ecx = particle, ebp = type, ebx = system
    "    pushal\n"
    "    pushl %ebx\n"
    "    pushl %ebp\n"
    "    pushl %ecx\n"
    "    call _SparkSpawned\n"
    "    addl $12, %esp\n"
    "    popal\n"
    "    flds 0x1CB66D8(%ebx)\n"
    "    ret\n");
static void SparkSetup() {
    if (!g_cfg.sparkStreaks) return;
    BYTE* sys = reinterpret_cast<BYTE* (__cdecl *)()>(hot::fnSparkSys)();
    if (!sys) return;
    for (int i = 0; i < 50; i++) {
        BYTE* E = sys + 0x1CC3C28 + i * 0x3C;
        if (!*reinterpret_cast<DWORD*>(E)) continue;            // no texture: unused
        DWORD& flags = *reinterpret_cast<DWORD*>(E + 0x28);
        if (!(flags & 1) || (flags & 4)) continue;              // (flag 4 wins the dispatch anyway)
        flags = (flags & ~0x9u) | 4u;
        g_sparkSwitched[i] = *reinterpret_cast<DWORD*>(E);
        *reinterpret_cast<float*>(E + 0x2C) *= g_cfg.sparkWidth;
        *reinterpret_cast<float*>(E + 0x18) *= g_cfg.sparkWidth;
        Logf("sparks: type %d drawn as streaks (flags now %X, width %.3f, colour %08X)\n", i, flags,
             *reinterpret_cast<float*>(E + 0x2C), *reinterpret_cast<DWORD*>(E + 0x34));
    }
}
// Only walls and cars: the game throws collision sparks (0x441BF0, thiscall:
// contact, mover, impact; called from 0x442714 and 0x442CC2) for any hard
// contact - landing a jump or bottoming out on a ramp too. The contact's vector
// for the car (0x620B00: the same one the sparks fly along) is compared with
// the car's up: mostly along it (more than sparkFloor of it) is the road under
// the car, and no sparks are thrown; mostly across it is a wall or a car.
typedef void (__attribute__((thiscall)) *CollSparks_t)(BYTE*, BYTE*, BYTE*, float);
typedef void (__attribute__((thiscall)) *ContactVec_t)(BYTE*, float*, BYTE*, int);
static LONG g_sparkFloorSkipped = 0, g_sparkSideKept = 0, g_sparkImpactN = 0;
static float g_sparkImpactMax = 0.0f, g_sparkImpactMin = 0.0f, g_sparkImpactSum = 0.0f;
static void __attribute__((thiscall)) HkCollSparks(BYTE* self, BYTE* contact, BYTE* mover, float impact) {
    bool skip = false;
    if (contact && mover && g_cfg.sparkFloor < 1.0f) {   // (the road filter)
        float n[3] = { 0, 0, 0 };
        reinterpret_cast<ContactVec_t>(hot::fnContactVec)(contact, n, mover, 1);
        const float* up = reinterpret_cast<const float*>(mover + 0x10) + 4;   // the car's matrix, row 1
        float nl = sqrtf(Dot3(n, n));
        if (nl > 1e-4f) {
            skip = fabsf(Dot3(n, up)) / nl > g_cfg.sparkFloor;
            (skip ? g_sparkFloorSkipped : g_sparkSideKept)++;
            LONG all = g_sparkFloorSkipped + g_sparkSideKept;
            if (all == 50 || all == 500)                        // (for the log)
                Logf("sparks: collision sparks: %ld from walls/cars, %ld from the road skipped\n", g_sparkSideKept, g_sparkFloorSkipped);
        }
    }
    if (skip) return;
    if (impact > 0.0f) {                                        // (for the log: the hits' range, to set sparkImpactRef by)
        if (impact > g_sparkImpactMax) g_sparkImpactMax = impact;
        if (g_sparkImpactMin <= 0.0f || impact < g_sparkImpactMin) g_sparkImpactMin = impact;
        g_sparkImpactSum += impact; g_sparkImpactN++;
        if (g_sparkImpactN == 20 || g_sparkImpactN == 200)
            Logf("sparks: hit strength over %ld hits: weakest %.0f, average %.0f, strongest %.0f (sparkImpactRef %.0f)\n",
                 g_sparkImpactN, g_sparkImpactMin, g_sparkImpactSum / g_sparkImpactN, g_sparkImpactMax, g_cfg.sparkImpactRef);
    }
    g_sparkImpact = impact;
    reinterpret_cast<CollSparks_t>(hot::fnCollSparks)(self, contact, mover, impact);
    g_sparkImpact = 0.0f;
}
static void InstallSparks() {
    if (g_cfg.sparkFloor < 1.0f || (g_cfg.sparkStreaks && g_cfg.sparkImpactRef > 0.0f)) {
        if (!PatchCall(hot::callCollSparks1, reinterpret_cast<void*>(&HkCollSparks), 5) ||
            !PatchCall(hot::callCollSparks2, reinterpret_cast<void*>(&HkCollSparks), 5)) Logf("sparks: road filter patch FAILED\n");
        else Logf("sparks: collision sparks hooked (road filter %.2f, size by hit strength / %.0f)\n", g_cfg.sparkFloor, g_cfg.sparkImpactRef);
    }
    if (!g_cfg.sparkStreaks) return;
    g_sparkLength = g_cfg.sparkLength > 0.0f ? g_cfg.sparkLength : 0.25f;
    BYTE fld[6] = { 0xD9, 0x05 };                               // fld dword ptr [&g_sparkLength]
    DWORD a = reinterpret_cast<DWORD>(&g_sparkLength); memcpy(fld + 2, &a, 4);
    if (!PatchBytes(hot::sparkStreakLen, fld, 6) ||
         ((g_cfg.sparkVary > 0.0f || g_cfg.sparkHot > 0.0f || g_cfg.sparkCount > 0 ||
           g_cfg.sparkImpactRef > 0.0f || g_cfg.sparkTwist > 0.0f) &&
         !PatchCall(hot::sparkSpawnEnd, reinterpret_cast<void*>(&SparkSpawnStub), 6))) { Logf("sparks: patch FAILED\n"); return; }
    Logf("sparks: dotted sparks drawn as streaks, %.2f s of travel long, x%.1f wide; size +-%.0f%%, hot %.2f; "
         "%d more a car-hit spark, %.0f/s at most (spread %.2f, up %.2f)\n", g_sparkLength, g_cfg.sparkWidth, g_cfg.sparkVary * 100.0f,
         g_cfg.sparkHot, g_cfg.sparkCount, g_cfg.sparkRate, g_cfg.sparkSpread, g_cfg.sparkUp);
}

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
static void InstallScrapeSound() {
    if (!g_cfg.scrapeSound) return;
    if (!PatchCall(hot::callContactCopy1, reinterpret_cast<void*>(&HkContactCopy), 5) ||
        !PatchCall(hot::callContactCopy2, reinterpret_cast<void*>(&HkContactCopy), 5)) { Logf("scrape sound: patch FAILED\n"); return; }
    Logf("scrape sound: the game's metalscrape where a surface has no contact sound\n");
}

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
static void InstallMenuFixes() {
    if (!g_cfg.menuFixes) return;
    static const BYTE faddp[2] = { 0xDE, 0xC1 };
    bool ok = true;
    for (DWORD at : hot::ui2DYSub) ok = ok && PatchBytes(at, faddp, 2);
    g_scrollDlgUpd = *reinterpret_cast<ThisVoid_t*>(hot::vtScrollDlgUpd);
    ok = ok && PatchCall(hot::callTrackName, reinterpret_cast<void*>(&HkTrackName), 5) &&
         PatchPointer(reinterpret_cast<void**>(hot::vtScrollDlgUpd), reinterpret_cast<void*>(&HkScrollDlgUpd));
    Logf(ok ? "menus: 2D half-pixel offset fixed, track names change with their cards\n" : "menus: patch FAILED\n");
}

// ---------------------------------------------------------------------------
// Race music at GO. The race's track is opened while the race loads
// (GSWorldInitState_c, 0x47C5CF) and started by the intro (GSBaseIntroState_c,
// 0x4796B0, every mode's intro): its countdown step calls Music play (0x538C20)
// every frame (0x479780), so it plays under the 3-2-1. That call is dropped and
// the track is started at GO instead, where the intro clears its "countdown on"
// byte (0x4797BF: mov byte ptr [0x959F80],0 - done here too). Leaving the pause
// menu plays the music if it isn't paused (0x47E6D2, 0x47EEF0): during the
// countdown it's only opened, so that would start it early - held until GO.
// A restart (GSWorldResetState_c) re-opens the track and runs the intro again.
static void __cdecl MusicIntroFrame() {}                       // (the countdown's per-frame play: nothing)
static void __cdecl MusicAtGo() {
    *reinterpret_cast<volatile BYTE*>(hot::introActive) = 0;   // (what the patched instruction did)
    reinterpret_cast<VoidFn_t>(hot::fnMusicPlay)();
}
static void __cdecl MusicPauseResume() {
    if (*reinterpret_cast<volatile BYTE*>(hot::introActive)) return;   // (still counting down: GO starts it)
    reinterpret_cast<VoidFn_t>(hot::fnMusicPlay)();
}
static void InstallMusicAtGo() {
    if (!g_cfg.musicAtGo) return;
    bool ok = PatchCall(hot::callIntroPlay, reinterpret_cast<void*>(&MusicIntroFrame), 5) &&
              PatchCall(hot::movIntroGo, reinterpret_cast<void*>(&MusicAtGo), 7) &&
              PatchCall(hot::callPauseResume1, reinterpret_cast<void*>(&MusicPauseResume), 5) &&
              PatchCall(hot::callPauseResume2, reinterpret_cast<void*>(&MusicPauseResume), 5);
    Logf(ok ? "race music: starts at GO\n" : "race music: patch FAILED\n");
}

// ---------------------------------------------------------------------------
// Rendering fixes
//  - Sun shadows: the PC build renders the sun's shadow (track, scenery) into
//    an 8 x 8 shadow map (0x57D6F0: an R32F render target and a D24X8 depth
//    surface, both 8 x 8; the caster pass's viewport 8 x 8 at 0x57C28F;
//    g_ShadowMapSize 8.0 at 0x54A70D). Spread over the 64 units in front of the
//    camera it's a blur: a dark patch round the car that comes and goes. The
//    two creates are replaced (shadowMapSize x shadowMapSize), the viewport
//    and g_ShadowMapSize follow. The shadow's reach (0x436xxx, each constant
//    read there only: 64 deep, fading over the last 16; width 128) is
//    shadowRange deep, fading over shadowFade, twice as wide as deep.
//    The shadow "camera" (0x64DBD0 -> 0x64CE70/0x64C570) is an orthographic
//    box fitted round the stretch of track ahead of the camera, looking down the
//    sun. But its depth range is thin: it starts only "fade width" (16) toward
//    the sun from the box centre and ends at "reach" (64) - so whatever is
//    higher up toward the sun (a roof, a wall top) fell outside it and cast no
//    shadow, and the edge of that moved with the camera. With shadowFit=1 the
//    light backs off shadowBackOff units toward the sun (0x64D084) and sees to
//    shadowFar (0x64DDF9); the fade width it hands the shaders (0x64DE17) stays
//    shadowFade. All three read our floats instead of the shared parameter block.
//  - See-through things (glass, the glass road pieces) flickered between clear
//    and washed out as the camera moved. Two causes:
//    - the alpha pass (0x54B7D0) sorts its draws (0x54B8F8-0x54BB17) by pass
//      only, not by distance, and unstably - so their order changed from frame
//      to frame. With glassSort=1 they're sorted back to front by their depth
//      (record +0x70), nearer last; equal ones by pass, then address.
//    - blended materials kept writing depth (0x5470A0 only turns it off for a
//      material flag; the blend switch 0x5420F0 only for MAX), so glass hid
//      whatever was drawn behind it later. With glassNoZWrite=1, after the
//      blend switch (called at 0x547204) sets a blended mode (0 add, 1 sub,
//      2 alpha, 3 mod2x, 6 add2) depth writing is turned off (0x541FE0) for
//      that draw; the next material sets it again as usual.
//    - meshes (the bell jar, the glass road) go through their own lists
//      (renderer+0xEC4128 and +0xED79A8), sorted in 0x54B7D0 only by technique
//      class (instance +0x1FC), unstably; their see-through parts are drawn
//      afterward in reverse list order (0x54B640) - so two glass meshes of the
//      same class swapped places as the visible set changed. With glassSort=1
//      both lists are sorted by class, then by the instance's address (the
//      order the frame's scene walk made them in): the same order every frame.
// ---------------------------------------------------------------------------
static float g_shadowSizeF = 8.0f;
static DWORD g_shadowSize = 8;
typedef HRESULT (__stdcall *CreateTex_t)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**, void*);
typedef HRESULT (__stdcall *CreateDS_t)(void*, UINT, UINT, DWORD, DWORD, DWORD, BOOL, void**, void*);
extern "C" HRESULT __cdecl ShadowCreateTex(BYTE* r) {          // replaces 0x57D7BA-0x57D7E7 (edi = r)
    void* dev = *reinterpret_cast<void**>(r + 0xB8);
    *reinterpret_cast<DWORD*>(r + 0x1CB6658) = 3;
    void** vt = *reinterpret_cast<void***>(dev);
    HRESULT hr = reinterpret_cast<CreateTex_t>(vt[0x5C / 4])(dev, g_shadowSize, g_shadowSize, 1, 1 /* RENDERTARGET */,
        0x72 /* R32F */, 0 /* DEFAULT */, reinterpret_cast<void**>(r + 0xA4), nullptr);
    if (hr < 0 && g_shadowSize != 8) {                          // (too big for this card: the game's size)
        Logf("shadows: %ux%u shadow map not made (%08lX), 8x8 kept\n", g_shadowSize, g_shadowSize, (unsigned long)hr);
        g_shadowSize = 8;
        hr = reinterpret_cast<CreateTex_t>(vt[0x5C / 4])(dev, 8, 8, 1, 1, 0x72, 0, reinterpret_cast<void**>(r + 0xA4), nullptr);
    }
    return hr;
}
extern "C" HRESULT __cdecl ShadowCreateDepth(BYTE* r, void** out) {   // replaces 0x57D7F8-0x57D815 (edi = r, ebx = out)
    void* dev = *reinterpret_cast<void**>(r + 0xB8);
    void** vt = *reinterpret_cast<void***>(dev);
    HRESULT hr = reinterpret_cast<CreateDS_t>(vt[0x74 / 4])(dev, g_shadowSize, g_shadowSize, 0x4D /* D24X8 */, 0, 0, TRUE, out, nullptr);
    static int logged = 0;
    if (logged++ < 2) Logf("shadows: sun shadow map %ux%u %s\n", g_shadowSize, g_shadowSize, hr >= 0 ? "made" : "FAILED");
    // the caster pass's viewport and the shaders' g_ShadowMapSize follow the size made
    DWORD vp = g_shadowSize;
    PatchBytes(hot::shadowViewport + 1, &vp, 4);
    g_shadowSizeF = (float)g_shadowSize;
    return hr;
}
extern "C" void ShadowTexStub(); extern "C" void ShadowDepthStub();
extern "C" { DWORD g_shadowTexCont = hot::shadowTexEnd; DWORD g_shadowDepthCont = hot::shadowDepthEnd; }
asm(".text\n"
    ".globl _ShadowTexStub\n_ShadowTexStub:\n"     // jmp from 0x57D7BA; on to 0x57D7E7 with eax = HRESULT
    "    pushl %edi\n"
    "    call _ShadowCreateTex\n"
    "    addl $4, %esp\n"
    "    jmp *_g_shadowTexCont\n"
    ".globl _ShadowDepthStub\n_ShadowDepthStub:\n"   // jmp from 0x57D7F8; on to 0x57D815 with eax = HRESULT
    "    pushl %ebx\n"
    "    pushl %edi\n"
    "    call _ShadowCreateDepth\n"
    "    addl $8, %esp\n"
    "    movl 0xB8(%edi), %edi\n"                  // (as the original left it)
    "    jmp *_g_shadowDepthCont\n");
static float g_shadowDepth = 64.0f, g_shadowFadeW = 16.0f, g_shadowWide = 128.0f;

static float g_shadowBackOff = 1000.0f, g_shadowFar = 2000.0f, g_shadowFadeOut = 16.0f;
static int __cdecl AlphaRecCmp(const void* a, const void* b) {
    const BYTE* ra = *static_cast<BYTE* const*>(a);
    const BYTE* rb = *static_cast<BYTE* const*>(b);
    int pa = *reinterpret_cast<const int*>(ra), pb = *reinterpret_cast<const int*>(rb);
    if (pa != pb) return pa < pb ? -1 : 1;                      // (pass first, as the game)
    float za = *reinterpret_cast<const float*>(ra + 0x70), zb = *reinterpret_cast<const float*>(rb + 0x70);
    if (za != zb) return za > zb ? -1 : 1;                      // farther (larger) first
    return ra < rb ? -1 : (ra > rb ? 1 : 0);
}
extern "C" void __cdecl AlphaSort(BYTE** list, int count) {
    if (list && count > 1 && count < 100000) qsort(list, count, sizeof(BYTE*), AlphaRecCmp);
}
static int __cdecl MeshInstCmp(const void* a, const void* b) {
    const BYTE* ia = *static_cast<BYTE* const*>(a);
    const BYTE* ib = *static_cast<BYTE* const*>(b);
    int ka = *reinterpret_cast<const int*>(ia + 0x1FC), kb = *reinterpret_cast<const int*>(ib + 0x1FC);
    if (ka != kb) return ka < kb ? -1 : 1;                      // technique class first, as the game (draws go by its ranges)
    return ia < ib ? -1 : (ia > ib ? 1 : 0);
}
extern "C" void __cdecl MeshSort(BYTE** list, int count) {
    if (list && count > 1 && count < 100000) qsort(list, count, sizeof(BYTE*), MeshInstCmp);
}
extern "C" void MeshSortStub1(); extern "C" void MeshSortStub2();
extern "C" { DWORD g_meshSort1Cont = hot::meshSort2; DWORD g_meshSort2Cont = hot::meshSortEnd; }
asm(".text\n"
    ".globl _MeshSortStub1\n_MeshSortStub1:\n"   // jmp from 0x54BB17: esi = group, ebx = renderer; on to 0x54BD77
    "    pushal\n"
    "    movl (%esi), %ecx\n"
    "    leal 0xEC4128(%ebx,%ecx,4), %edx\n"
    "    pushl 4(%esi)\n"
    "    pushl %edx\n"
    "    call _MeshSort\n"
    "    addl $8, %esp\n"
    "    popal\n"
    "    jmp *_g_meshSort1Cont\n"
    ".globl _MeshSortStub2\n_MeshSortStub2:\n"   // jmp from 0x54BD77; on to 0x54BFE7
    "    pushal\n"
    "    movl 8(%esi), %ecx\n"
    "    leal 0xED79A8(%ebx,%ecx,4), %edx\n"
    "    pushl 0xC(%esi)\n"
    "    pushl %edx\n"
    "    call _MeshSort\n"
    "    addl $8, %esp\n"
    "    popal\n"
    "    jmp *_g_meshSort2Cont\n");
extern "C" void AlphaSortStub();
extern "C" { DWORD g_alphaSortCont = hot::alphaSortEnd; }
asm(".text\n"
    ".globl _AlphaSortStub\n_AlphaSortStub:\n"   // jmp from 0x54B8F8: eax = count, esi = segment, ebx = renderer; on to 0x54BB17
    "    pushal\n"
    "    movl 0x10(%esi), %ecx\n"
    "    leal 0xD53628(%ebx,%ecx,4), %edx\n"     // the segment's draws in the alpha list
    "    pushl %eax\n"
    "    pushl %edx\n"
    "    call _AlphaSort\n"
    "    addl $8, %esp\n"
    "    popal\n"
    "    jmp *_g_alphaSortCont\n");
typedef void (__attribute__((thiscall)) *BlendMode_t)(BYTE*, int);
static void __attribute__((thiscall)) HkBlendMode(BYTE* r, int mode) {
    reinterpret_cast<BlendMode_t>(hot::fnBlendMode)(r, mode);
    if (mode == 0 || mode == 1 || mode == 2 || mode == 3 || mode == 6)
        reinterpret_cast<BlendMode_t>(hot::fnZWrite)(r, 0);
}
// Sky. The level's sky (its SKY_ node, drawn first by 0x53E650) is centred on
// the camera, scale 1, with the far plane set to 5000 for it (0x53E7B9) - with
// the wider speed-FOV view a black hole showed at the top of it. skyFar: the
// sky's own far plane (0x53E7BB reads our value; 0x8AA444 itself has 11 other
// users); skyStretch: the sky's world matrix (both instances, 0x53E84F and
// 0x53EA33: they load the SetWorld call) gets its up axis scaled, so its rim
// stays above a wider view.
static float g_skyFar = 5000.0f;
extern "C" { float g_skyStretch = 1.0f; }             // (read by the stub below)
extern "C" void SkyWorldStub();
asm(".text\n"
    ".globl _SkyWorldStub\n_SkyWorldStub:\n"     // called instead of the renderer's SetWorld (vtable +0x224): ecx = renderer, the matrix by value at [esp+4]
    "    flds 0x14(%esp)\n    fmuls _g_skyStretch\n    fstps 0x14(%esp)\n"   // row 1 (up): x y z
    "    flds 0x18(%esp)\n    fmuls _g_skyStretch\n    fstps 0x18(%esp)\n"
    "    flds 0x1C(%esp)\n    fmuls _g_skyStretch\n    fstps 0x1C(%esp)\n"
    "    movl (%ecx), %eax\n"
    "    jmp *0x224(%eax)\n");
static void InstallRendering() {
    if (g_cfg.skyFar > 5000.0f) {
        g_skyFar = g_cfg.skyFar;
        DWORD a = reinterpret_cast<DWORD>(&g_skyFar);
        if (!PatchBytes(hot::skyFarLd + 2, &a, 4)) Logf("sky: far plane patch FAILED\n");
        else Logf("sky: far plane %.0f (game: 5000)\n", g_skyFar);
    }
    if (g_cfg.skyStretch > 1.0f) {
        g_skyStretch = g_cfg.skyStretch > 4.0f ? 4.0f : g_cfg.skyStretch;
        BYTE mv[6] = { 0xBA, 0, 0, 0, 0, 0x90 };              // mov edx, SkyWorldStub / nop
        DWORD a = reinterpret_cast<DWORD>(&SkyWorldStub); memcpy(mv + 1, &a, 4);
        if (!PatchBytes(hot::skySetWorld1, mv, 6) || !PatchBytes(hot::skySetWorld2, mv, 6)) Logf("sky: stretch patch FAILED\n");
        else Logf("sky: stretched %.2fx upward\n", g_skyStretch);
    }
    if (g_cfg.shadowMapSize > 8) {
        g_shadowSize = (DWORD)ClampInt(g_cfg.shadowMapSize, 8, 4096);
        BYTE j1[6] = { 0xE9 }, j2[6] = { 0xE9 };
        DWORD r1 = reinterpret_cast<DWORD>(&ShadowTexStub) - (hot::shadowTexMake + 5); memcpy(j1 + 1, &r1, 4); j1[5] = 0x90;
        DWORD r2 = reinterpret_cast<DWORD>(&ShadowDepthStub) - (hot::shadowDepthMake + 5); memcpy(j2 + 1, &r2, 4); j2[5] = 0x90;
        BYTE fld[6] = { 0xD9, 0x05 }; DWORD fa = reinterpret_cast<DWORD>(&g_shadowSizeF); memcpy(fld + 2, &fa, 4);
        if (!PatchBytes(hot::shadowTexMake, j1, 6) || !PatchBytes(hot::shadowDepthMake, j2, 6) ||
            !PatchBytes(hot::shadowSizeParam, fld, 6)) Logf("shadows: patch FAILED\n");
        else Logf("shadows: sun shadow map %ux%u (game: 8x8)\n", g_shadowSize, g_shadowSize);
    }
    if (g_cfg.shadowRange > 0.0f && g_cfg.shadowRange != 64.0f) {
        g_shadowDepth = g_cfg.shadowRange;
        g_shadowFadeW = g_cfg.shadowFade > 0.0f ? g_cfg.shadowFade : 16.0f;
        if (g_shadowFadeW > g_shadowDepth * 0.5f) g_shadowFadeW = g_shadowDepth * 0.5f;
        g_shadowWide = g_shadowDepth * 2.0f;
        if (!PatchBytes(hot::shadowDepthK, &g_shadowDepth, 4) || !PatchBytes(hot::shadowFadeK, &g_shadowFadeW, 4) ||
            !PatchBytes(hot::shadowWideK, &g_shadowWide, 4)) Logf("shadows: range patch FAILED\n");
        else Logf("shadows: reach %.0f units, fading over the last %.0f (game: 64, 16)\n", g_shadowDepth, g_shadowFadeW);
    }
    if (g_cfg.glassSort) {
        BYTE j[6] = { 0xE9 }; DWORD rel = reinterpret_cast<DWORD>(&AlphaSortStub) - (hot::alphaSort + 5);
        memcpy(j + 1, &rel, 4); j[5] = 0x90;
        BYTE j1[5] = { 0xE9 }, j2[5] = { 0xE9 };
        DWORD r1 = reinterpret_cast<DWORD>(&MeshSortStub1) - (hot::alphaSortEnd + 5); memcpy(j1 + 1, &r1, 4);
        DWORD r2 = reinterpret_cast<DWORD>(&MeshSortStub2) - (hot::meshSort2 + 5); memcpy(j2 + 1, &r2, 4);
        if (!PatchBytes(hot::alphaSort, j, 6) || !PatchBytes(hot::alphaSortEnd, j1, 5) || !PatchBytes(hot::meshSort2, j2, 5))
            Logf("glass: sort patch FAILED\n");
        else Logf("glass: see-through draws sorted back to front\n");
    }
    if (g_cfg.glassNoZWrite) {
        if (!PatchCall(hot::callBlendMode, reinterpret_cast<void*>(&HkBlendMode), 5)) Logf("glass: depth patch FAILED\n");
        else Logf("glass: blended draws don't write depth\n");
    }
    if (g_cfg.shadowFit) {
        g_shadowBackOff = g_cfg.shadowBackOff > 1.0f ? g_cfg.shadowBackOff : 1000.0f;
        g_shadowFar = g_cfg.shadowFar > g_shadowBackOff ? g_cfg.shadowFar : g_shadowBackOff * 2.0f;
        float need = g_shadowBackOff + (g_cfg.shadowRange > 64.0f ? g_cfg.shadowRange : 64.0f);   // (the light must see the whole box)
        if (g_shadowFar < need) g_shadowFar = need;
        g_shadowFadeOut = g_shadowFadeW;                        // (the fade the shadow box got: 16 if its reach is the game's)
        DWORD a1 = reinterpret_cast<DWORD>(&g_shadowBackOff), a2 = reinterpret_cast<DWORD>(&g_shadowFar),
              a3 = reinterpret_cast<DWORD>(&g_shadowFadeOut);
        if (!PatchBytes(hot::shadowBackOffLd + 2, &a1, 4) || !PatchBytes(hot::shadowFarLd + 2, &a2, 4) ||
            !PatchBytes(hot::shadowFadeLd + 2, &a3, 4)) Logf("shadows: depth range patch FAILED\n");
        else Logf("shadows: light backs off %.0f toward the sun, sees %.0f deep (game: 16, 64)\n", g_shadowBackOff, g_shadowFar);
    }
}

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
static FILE* g_tlog = nullptr;
static CRITICAL_SECTION g_tLock;
static bool g_tLockInit = false;
static void TuningFlush() {
    if (!g_tlog || !g_tLockInit) return;
    EnterCriticalSection(&g_tLock); fflush(g_tlog); LeaveCriticalSection(&g_tLock);
}
static bool IsNumberTok(const char* s) {
    if (!s || !*s) return false;
    char* e; strtod(s, &e);
    return e != s && *e == 0;
}
static void LoadTuning() {
    CarryOver(MOD_FILE "_tuning.txt", "HWBTMod_tuning.txt");
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%s" MOD_FILE "_tuning.txt", g_asiDir);
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f) && g_tovN < 512) {
        if (char* c = strpbrk(line, ";#\r\n")) {
            if (*c == '#' && c[1] >= '0' && c[1] <= '9') {          // KEY#1: not a comment
                if (char* c2 = strpbrk(c + 1, ";\r\n")) *c2 = 0;
            } else *c = 0;
        }
        char* eq = strchr(line, '=');
        if (!eq) continue;
        bool scale = eq > line && eq[-1] == '*';
        char* ke = scale ? eq - 1 : eq;
        *ke = 0;
        char* k = line; while (*k == ' ' || *k == '\t') k++;
        char* kend = k + strlen(k); while (kend > k && (kend[-1] == ' ' || kend[-1] == '\t')) *--kend = 0;
        if (!*k) continue;
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
    fclose(f);
    Logf("tuning: %d change(s) from %s\n", g_tovN, path);
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
static void InstallTuning() {
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

// ---------------------------------------------------------------------------
// Crash log: an access violation (or another fatal CPU exception) in HOT.exe's
// or this ASI's code is written to the log - where (with its offset), the
// registers, and the code addresses found on the stack - before the game's own
// handling. Each place is logged once (the game may catch some itself), 8 at most.
// ---------------------------------------------------------------------------
static BYTE* g_modBase; static DWORD g_modSize;
static void CrashWhere(char* out, size_t n, DWORD a) {
    if (a >= 0x401000 && a < 0x900000) snprintf(out, n, "HOT.exe %06lX", a);
    else if (a >= reinterpret_cast<DWORD>(g_modBase) && a < reinterpret_cast<DWORD>(g_modBase) + g_modSize)
        snprintf(out, n, MOD_FILE ".asi +%05lX", a - reinterpret_cast<DWORD>(g_modBase));
    else snprintf(out, n, "%08lX", a);
}
static bool CrashOurs(DWORD a) {                               // in HOT.exe or this ASI
    return (a >= 0x401000 && a < 0x900000) ||
           (a >= reinterpret_cast<DWORD>(g_modBase) && a < reinterpret_cast<DWORD>(g_modBase) + g_modSize);
}
static LONG CALLBACK CrashHandler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW) return EXCEPTION_CONTINUE_SEARCH;
    DWORD eip = ep->ContextRecord->Eip;
    // Only faults in the game's or the mod's own code: IsBadReadPtr and co. (used
    // here and there to probe pointers) fault inside Windows on purpose and
    // catch it themselves - those aren't crashes. (A crash inside a driver or
    // Windows itself isn't logged either.)
    if (!CrashOurs(eip)) return EXCEPTION_CONTINUE_SEARCH;
    static DWORD seen[8]; static LONG nSeen = 0;
    for (LONG i = 0; i < nSeen && i < 8; i++) if (seen[i] == eip) return EXCEPTION_CONTINUE_SEARCH;
    LONG i = InterlockedIncrement(&nSeen) - 1;
    if (i >= 8) return EXCEPTION_CONTINUE_SEARCH;
    seen[i] = eip;
    const CONTEXT* c = ep->ContextRecord;
    char buf[1024]; size_t n = 0; char where[64];
    auto add = [&](const char* fmt, auto... a) {                // (one Logf for the whole record: one line block, one timestamp)
        if (n < sizeof(buf)) { int k = snprintf(buf + n, sizeof(buf) - n, fmt, a...); if (k > 0) n += (size_t)k; }
    };
    CrashWhere(where, sizeof(where), eip);
    add("CRASH: exception %08lX at %s", code, where);
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
        add(" (%s %08lX)", ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
            static_cast<DWORD>(ep->ExceptionRecord->ExceptionInformation[1]));
    add("\n  eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX edi %08lX ebp %08lX esp %08lX\n  stack:",
        c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    const DWORD* sp = reinterpret_cast<const DWORD*>(c->Esp); int found = 0;
    MEMORY_BASIC_INFORMATION mbi;                               // (the stack's committed part: no probing faults)
    DWORD top = VirtualQuery(sp, &mbi, sizeof(mbi)) ? reinterpret_cast<DWORD>(mbi.BaseAddress) + mbi.RegionSize : c->Esp;
    for (int k = 0; k < 256 && found < 12; k++) {
        if (reinterpret_cast<DWORD>(sp + k) + 4 > top) break;
        DWORD v = sp[k];
        if (!CrashOurs(v) || (v >= 0x880000 && v < 0x900000)) continue;   // (code only: not HOT.exe's data)
        CrashWhere(where, sizeof(where), v); add(" [%d] %s", k, where); found++;
    }
    Logf("%s\n", buf);
    return EXCEPTION_CONTINUE_SEARCH;
}
static void InstallCrashLog(HMODULE self) {
    g_modBase = reinterpret_cast<BYTE*>(self);
    const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(g_modBase + reinterpret_cast<const IMAGE_DOS_HEADER*>(g_modBase)->e_lfanew);
    g_modSize = nt->OptionalHeader.SizeOfImage;
    AddVectoredExceptionHandler(1, CrashHandler);
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
            InstallMenuFixes();
            InstallPerf();
            InstallRaceMusic();
            InstallGameLog();
            InstallCloseExits();
            InstallTuning();
            InstallHazards();
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
