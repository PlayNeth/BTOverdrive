// What the other parts use from config.cpp.
#pragma once
#include "common.h"
#include "items.h"

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
    int   weaponImpact = 1;              // a weapon hit (rockets, mines, mortar, airstrike) throws the car instead of the canned roll
    float weaponPush = 40.0f;            //   a mine or bomb: away from the hit (units/s)
    float weaponLift = 55.0f;            //   and up (units/s)
    float weaponForward = 35.0f;         //   a rocket or mortar: thrown along (units/s)
    float weaponForwardLift = 45.0f;     //   and up (units/s)
    float weaponForwardTumble = 6.0f;    //   flipping end over end (radians/s)
    float weaponMortar = 1.4f;           //   a mortar shell: that x this
    float weaponFlyTime = 2.0f;          // a thrown car passes through the invisible walls (weaponWallMats) for this long (s)
    int   weaponWallMats[8] = { 12 };    //   their collision material ids
    int   weaponWallMatCount = 1;
    float weaponTumble = 4.0f;           //   tumbling over away from the hit (radians/s)
    float weaponSpin = 3.0f;             //   and spinning round, either way (radians/s)
    float hazardPush = 150.0f;           // the car is thrown this fast (units/s) sideways, away from the part
    float hazardLift = 120.0f;           // plus this much upward (units/s)
    float hazardSpin = 6.0f;             // and a tumble (radians/s)
    float hazardFlyTime = 3.0f;          // a thrown car goes through walls for this long (seconds; 0 = never)
    int   hazardSmoke = 1;               // a burst of smoke on a car the hazard throws
    float steerRate = 4.0f;              // how fast the front wheels turn when you steer: x the game's (0.96 rad/s - over a second to full lock)
    float carShadowDepth = 1.9f;         // how far below a car's middle its shadow reaches (game: 4.4 - through to a road below)
    float soundPitchRise = 0.06f;        // a sound held on (an engine) rises in pitch at most this much per 1/60 s (0 = as the game)
    int   shieldPowerFire = 1;           // items can be fired while a track's shield power-up is on (the game: not)
    float blueEmp = 0.5f;                // a blue crate's super EMP: only for the last two, and kept this often (else rolled again)
    int   itemOdds = 1;                  // what a crate gives by race position (itemsFirst/Front/Mid/Back); 0 = the game's table
    int   aiUnstuck = 1;
    float aiLoopSpeed = 50.0f;           // an AI car on a loop's wall or ceiling keeps at least this speed (units/s; 0 = off)
    int   smokeHide = 1;                 // while a car's smoke is pouring out, guided rockets and parachutes can't lock on to it
    int   parachuteHoming = 1;           // the plain parachute homes in like the homing rocket
    float shieldSecs = 7.5f;             // the shield lasts this long (s; 0 = the game's 5); the mega shield megaShieldSecs (the game's 13)
    float megaShieldSecs = 7.5f;
    float shieldSize = 1.25f;            // the shield is this many times the game's size; the mega shield megaShieldSize
    float megaShieldSize = 1.4f;
    int   shieldZap = 1;                 // a car touching a shield is slowed (the parachute's slow) for shieldSlow s; a mega shield's also loses its steering
    float shieldSlow = 2.0f;
    float shieldTouch = 5.5f;            //   a touch: the cars' centres closer than this (and inside the shield)
    float shieldLock = 2.0f;             //   for this long
    float empSlow = 6.0f;                // the EMP also slows every other car (as the tesla ray does) for this long (s; 0 = no)
    float empAfter = 30.0f;              // no crate gives the EMP in the race's first this many seconds (0 = from the start)
    int   iceFix = 1;                    // the ice gun: no freezing a frozen car again, a moment free after breaking out, the AI frozen too
    int   fallReset = 1;                 // any race car that falls off the track into the void is put back where it left it
    float hazardFallReset = 25.0f;       // a thrown car still in the air this far below where it was hit (and below every road driven) is put back on the track (0 = never)
    float hazardPushTime = 0.5f;         // the throw keeps pushing for this long (seconds)
    int   hazardGateFix = 1;             // the graveyard gate's panels' collision made 3 units thick
    int   aiLeadSlowdown = 0;            // AI cars ahead of the player are slowed down (the game's rubber-banding)
    float aiWeaponCooldown = 2.5f;       // seconds between any two AI shots (game: 5)
    float aiTargetPlayer = 60.0f;        // % of AI shots aimed at the player, +10 per difficulty (game: 40)
    float aiStartBoost = 50.0f;          // % chance each AI car gets the start boost at GO (game: never)
    int   aiAreaWeaponLimit = 4;         // area weapons the AI holds back while this many are out (game: 2)
    float boostMeterSpeed = 2.8f;        // the start-boost dial runs this many times as fast (1 = original)
    float startRedSlow = 3.0f;           // a press on the dial's red end: slowed for this long at GO (s; 0 = no)
    int   startRedFrom = 165;            //   the red end: the gauge's frames (it loops 110-186; the green 127-144, the red around 174)
    int   startRedTo = 183;
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
    int   soundOcclusion = 1;            // with EAX: sounds behind the track's walls are muffled
    float soundOcclusionDb = 15.0f;      //   by this much (EAX occlusion: mostly the highs)
    int   soundKeepVolume = 1;           // the volumes set in the sound menu apply from the start (the game: 50% until a profile loads)
    int   soundFade = 1;                 // a sound cut off (to make room, or stopped) fades out over ~50 ms instead of clicking
    int   fastLoad = 1;                  // the game's file reads: polled every 1 ms instead of 10 (faster loading)
    int   soundEax = 1;                  // EAX reverb by trigger zone (tunnels, caves...) when the sound device has EAX (DSOAL...)
    int   soundVoices = 1;               // sound effects: up to 64 at once (Windows Vista+ gave the game 16)
    int   soundDoppler = 2;              // Doppler: 2 = from each sound's real movement, 1 = the game's (sources standing still), 0 = off
    float soundDopplerScale = 1.0f;      //   with 2: how strong (1 = real-world)
    float soundHeadroom = 3.0f;          // sound effects this many dB quieter, against clipping when many overlap
    int   introCamClip = 1;              // the race intro's camera kept out of the scenery
    int   musicLoop = 1;                 // every song starts over at its end (the menus' didn't: silence once it was over)
    int   musicStream = 1;               // music streaming fixes (the "scratched disc" repeats)
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
    float sparkInherit = 1.0f;           // collision sparks move with the car (x its velocity; the game: -0.2 x, backward)
    float sparkBurst = 0.35f;            //   and burst off the contact at this x the car's sliding speed along it
    float sparkBurstMin = 6.0f;          //   (at least this, units/s)
    float sparkOut = 0.6f;               //   that burst: backward along the surface, this much out from the car,
    float sparkLift = 0.4f;              //   this much up
    float sparkLife = 0.6f;              //   their life x this (short, quick)
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
extern Config g_cfg;
extern char g_asiDir[MAX_PATH];
extern char g_gameDir[MAX_PATH];
extern char g_rootDir[MAX_PATH];
extern bool g_knownExe;
void InitDirs();
void CarryOver(const char* newName, const char* oldName);
void LoadConfig();
