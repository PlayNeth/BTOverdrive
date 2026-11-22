#include "common.h"
#include "config.h"
#include "camera.h"
#include "items.h"

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

Config g_cfg;
char g_asiDir[MAX_PATH] = {};     // trailing backslash
char g_gameDir[MAX_PATH] = {};    // HOT.exe's folder (Bin), trailing backslash
char g_rootDir[MAX_PATH] = {};    // the game's root (parent of Bin, with Data, videos...), trailing backslash
bool g_knownExe = false;

void InitDirs() {
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
    "; a thrown car that's still in the air this far (units) below where it was thrown, and below every\n"
    ";   road driven this race (or still falling 5 s after the throw), is put back on the track, the way\n"
    ";   the game resets a car that's stuck; one that lands lower down the track is left alone (0 = never)\n"
    "hazardFallReset=25\n"
    "; a burst of smoke on a car the hazard throws (1) or none (0)\n"
    "hazardSmoke=1\n"
    "; the throw keeps pushing for this many seconds (the car's grip takes sideways speed out fast)\n"
    "hazardPushTime=0.5\n"
    "; the graveyard gate's panels: 1 = their collision made 3 units thick, so cars can't slip through\n"
    ";   as they swing shut; 0 = as the game has them\n"
    "hazardGateFix=1\n"
    "; --- handling ---\n"
    "; how fast the front wheels turn when you steer, x the game's: the game turns them at 0.96 rad/s,\n"
    ";   over a second from straight to full lock - the heavy, slow turn-in. The car's mass and inertia\n"
    ";   (BTOverdrive_tuning.txt) hardly change that. 4 = full lock in about 0.3 s; 1 = the game's\n"
    "steerRate=4\n"
    "; --- intro camera ---\n"
    "; the race intro's camera sweeps past the cars on the grid; with the cars side by side it can pass\n"
    ";   through the scenery beside them. 1 = it's pulled in toward the nearest car when the scenery is\n"
    ";   in the way (0 = as the game)\n"
    "introCamClip=1\n"
    "; --- falling off ---\n"
    "; on every track, a race car (yours or the AI's) that falls off into the void is put back on the\n"
    ";   track where it left it, with the game's own put-back - the game's never found it a place from\n"
    ";   down there. Off = it falls forever (0)\n"
    "fallReset=1\n"
    "; an AI car that's been driving for 6 seconds without getting anywhere - lost, or ramming a shortcut's\n"
    ";   barrier over and over - is put back on the track at the nearest reset point (0 = left to it)\n"
    "aiUnstuck=1\n"
    "; the AI slows for a big loop and drops off its wall or ceiling, again and again: an AI car on a\n"
    ";   loop's wall or ceiling (tilted over 60 degrees, on the road) keeps at least this speed (units/s;\n"
    ";   cars race at about 50-75) - 0 = as the game\n"
    "aiLoopSpeed=50\n"
    "; --- items ---\n"
    "; what a crate gives, by race position (as in Mario Kart: the leader gets defence, the back of the\n"
    ";   pack the big weapons): 1st, the front (top third), the middle, the back (the last two). Each\n"
    ";   line: item:weight, ... - the chance is the weight's share of its line. Items: rocket, mortar,\n"
    ";   boost, icegun, mine, emp, parachute, airstrike, smoke, tesla, shield. Blue crates (upgrades)\n"
    ";   are the game's. 0 = the game's own table\n"
    "itemOdds=1\n"
    "itemsFirst=smoke:30, shield:30, mine:25, rocket:15\n"
    "itemsFront=rocket:25, mine:15, shield:15, mortar:12, parachute:10, icegun:10, boost:8, tesla:5\n"
    "itemsMid=rocket:18, mortar:15, parachute:15, icegun:12, boost:12, tesla:10, airstrike:8, mine:5, smoke:5\n"
    "itemsBack=tesla:22, airstrike:20, boost:18, parachute:12, mortar:10, icegun:10.5, emp:7.5\n"
    "; a blue crate's super EMP: only the last two places can get it, and only this share of the times\n"
    ";   the game's roll gives it (the rest are rolled again) - 1 = as often as the game, 0 = never\n"
    "blueEmp=0.5\n"
    "; a track's shield power-up (Spooky Sprint's, before the ghoul's ramp...) shields the car for a while\n"
    ";   and the game won't let it fire its item until it wears off. 1 = it can fire (0 = as the game)\n"
    "shieldPowerFire=1\n"
    "; --- engine sound ---\n"
    "; an engine's pitch follows its wheels: a wheel off the road for a moment (a bump, a crest) spins up\n"
    ";   and the engine screams for a few hundredths of a second. A sound held on (pitch set again within\n"
    ";   0.1 s) rises at most this much per 1/60 s (0.06 = 6%: a real rev-up still goes through in a few\n"
    ";   tenths, a blip of a step or two is cut); drops are as the game. 0 = as the game\n"
    "soundPitchRise=0.06\n"
    "; the ice gun: a frozen car isn't frozen again on top (each shot restarted it), a car that's just\n"
    ";   broken out is free for a second, its steering always comes back with the ice, and a frozen AI\n"
    ";   car can't steer either (it drove on as if nothing happened) - 0 = as the game\n"
    "iceFix=1\n"
    "; while a car's smoke is pouring out, homing rockets and parachutes can't lock on to it (0 = they can)\n"
    "smokeHide=1\n"
    "; the plain parachute homes in on its target like the homing rocket (0 = it flies straight)\n"
    "parachuteHoming=1\n"
    "; how long the shield lasts (seconds; 0 = the game's 5); the mega shield megaShieldSecs (the game's 13)\n"
    "shieldSecs=7.5\n"
    "megaShieldSecs=7.5\n"
    "; the shield's size, times the game's; the mega shield megaShieldSize (1 = the game's)\n"
    "shieldSize=1.25\n"
    "megaShieldSize=1.4\n"
    "; a car that touches a shield gets a shock: slowed down like a parachute's for shieldSlow seconds -\n"
    ";   and a mega shield's also can't steer for shieldLock seconds (0 = the game's shield)\n"
    "shieldZap=1\n"
    "shieldSlow=2\n"
    "shieldLock=2\n"
    "; how close (centre to centre, units) a car has to come to count as touching the shield (cars are\n"
    ";   about 2 wide, 4-5 long)\n"
    "shieldTouch=5.5\n"
    "; the EMP also slows every other car down (as the tesla ray does) for this many seconds (0 = no)\n"
    "empSlow=6\n"
    "; no crate gives the EMP in the first this many seconds of a race (0 = from the start)\n"
    "empAfter=30\n"
    "; --- weapon hits ---\n"
    "; weapon hits throw the car instead of the game's roll on the spot. A mine, airstrike bomb or\n"
    ";   parachute bomb throws it weaponPush units/s away from the blast and weaponLift up, tumbling\n"
    ";   over at weaponTumble and spinning round at weaponSpin (radians/s); a rocket or mortar shell\n"
    ";   throws it weaponForward units/s along (a rocket: the way it flew; a shell: the car's own way)\n"
    ";   and weaponForwardLift up, flipping end over end at weaponForwardTumble - a flip and a crash\n"
    ";   landing, not a free boost. A car caught in an explosion's reach without a direct hit\n"
    ";   gets 40-100% of it by how close it was (the game: a nudge). A direct hit still takes control\n"
    ";   away while it lasts, as in the game; a car thrown off the track is put back (hazardFallReset).\n"
    ";   For scale, the ghoul throws 150 out and 120 up. Needs hazardHits=1. 0 = the game's roll\n"
    "weaponImpact=1\n"
    "weaponPush=40\n"
    "weaponLift=55\n"
    "weaponForward=35\n"
    "weaponForwardLift=45\n"
    "weaponForwardTumble=6\n"
    "; a mortar shell throws this many times as hard as a rocket\n"
    "weaponMortar=1.4\n"
    "; a car thrown by a weapon passes through the track's invisible walls for weaponFlyTime seconds,\n"
    ";   as a hazard's throw does - not the real walls. weaponWallMats: the invisible walls' collision\n"
    ";   material ids (comma-separated; the log names the walls thrown cars meet)\n"
    "weaponFlyTime=2\n"
    "weaponWallMats=12\n"
    "weaponTumble=4\n"
    "weaponSpin=3\n"
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
    "boostMeterSpeed=2.8\n"
    "; pressing while the needle is at the red end gets you slowed down for this many seconds at GO (0 = no)\n"
    ";   startRedFrom/To: the red end, in the gauge's frames (it loops over 110-186; the green is 127-144)\n"
    "startRedSlow=3\n"
    "startRedFrom=165\n"
    "startRedTo=183\n"
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
    "; how far below a car's middle its shadow reaches (units): the game's 4.4 reached through the road\n"
    ";   onto a road below (a car above you on a spiral cast its shadow on your road). 0 = the game's\n"
    "carShadowDepth=1.9\n"
    "; see-through things (glass, glass road pieces): drawn back to front, and they don't hide what's\n"
    ";   drawn behind them afterward (they flickered between clear and washed out) (0 = the game's)\n"
    "glassSort=1\n"
    "glassNoZWrite=1\n"
    "; frame timing in BTOverdrive.log every 10 s (where the time goes: waiting, game, 3D, draw+present)\n"
    "perfLog=0\n"
    "; --- loading ---\n"
    "; faster race loading: the drivers' models load one stage a game step, ~3 s a race - 1 = as many\n"
    ";   stages as fit in each frame while the race loader waits for them; file reads polled every 1 ms,\n"
    ";   not 10 (0 = the game's)\n"
    "fastLoad=1\n"
    "; --- menus ---\n"
    "; the 1-pixel line at the top of the menus, and the track select showing the next track's name\n"
    ";   on the old card for a frame (0 = as the game has them)\n"
    "menuFixes=1\n"
    "; --- race music ---\n"
    "; the race music starts at GO, after the countdown and start boost, as on the PS2 (0 = during the countdown)\n"
    "musicAtGo=1\n"
    "; the music sometimes repeated a bit of itself like a scratched disc: its stream ran dry waiting\n"
    ";   behind the game's other file reads. 1 = more buffer, its own reads, a higher-priority thread (0 = the game's)\n"
    "musicStream=1\n"
    "; every song starts over when it ends: the menus' songs (main menu, garage...) were played once, then\n"
    ";   silence for good (0 = as the game)\n"
    "musicLoop=1\n"
    "; --- sound effects ---\n"
    "; EAX reverb: each track's zones set a reverb (9 presets) as you drive through them, when the sound\n"
    ";   device has EAX - a wrapper such as DSOAL (see dsound-com-redirect.reg); Windows' own has none.\n"
    ";   The PC game finds EAX and then switches it off: its \"EAX on\" setting is never set (0 = as the game)\n"
    "soundEax=1\n"
    "; with EAX: a sound with a wall of the track between it and you is muffled by soundOcclusionDb\n"
    ";   (EAX occlusion: mostly the highs) - the game asks for every sound's EAX controls and never uses them\n"
    "soundOcclusion=1\n"
    "soundOcclusionDb=15\n"
    "; a sound effect that's stopped or cut off to make room fades out over about 50 ms instead of\n"
    ";   stopping dead with a click (0 = the game's)\n"
    "soundFade=1\n"
    "; the music, effects and speech volumes you confirm in the sound menu are kept (BTOverdrive_volume.txt) and\n"
    ";   applied as soon as the sound starts: the game's are 50% until a profile is loaded (0 = the game's)\n"
    "soundKeepVolume=1\n"
    "; up to 64 sound effects at once: on Windows Vista and later the game only got 16, cutting sounds off\n"
    ";   mid-play and restarting engine and skid loops (0 = the game's)\n"
    "soundVoices=1\n"
    "; Doppler (the pitch of a passing car): 2 = from how each sound actually moves; 1 = the game's, which\n"
    ";   treats every sound as standing still, so opponents warble with your own speed; 0 = off.\n"
    ";   soundDopplerScale: how strong with 2 (1 = real-world)\n"
    "soundDoppler=2\n"
    "soundDopplerScale=1\n"
    "; sound effects this many dB quieter, so a pile of them doesn't clip (0 = the game's level)\n"
    "soundHeadroom=3\n"
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
    "; collision sparks move with the car: the game threw them backward (-0.2 x the car's velocity), so\n"
    ";   at speed they all streamed straight back off the screen. sparkInherit: x the car's velocity they\n"
    ";   keep (0 = the game's way); on top, a burst off the contact - sparkBurst x the car's sliding\n"
    ";   speed along the wall or car (at least sparkBurstMin units/s), back along the surface, sparkOut\n"
    ";   out from the car and sparkLift up; each streak's length shows that burst, as the camera sees\n"
    ";   it. sparkLife: their life x this\n"
    "sparkInherit=1\n"
    "sparkBurst=0.35\n"
    "sparkBurstMin=6\n"
    "sparkOut=0.6\n"
    "sparkLift=0.4\n"
    "sparkLife=0.6\n"
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
void CarryOver(const char* newName, const char* oldName) {
    char np[MAX_PATH], op[MAX_PATH];
    snprintf(np, sizeof(np), "%s%s", g_asiDir, newName); snprintf(op, sizeof(op), "%s%s", g_asiDir, oldName);
    if (GetFileAttributesA(np) == INVALID_FILE_ATTRIBUTES && GetFileAttributesA(op) != INVALID_FILE_ATTRIBUTES)
        CopyFileA(op, np, TRUE);
}
void LoadConfig() {
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
        if (!_strnicmp(key, "items", 5)) {                          // itemsFirst/Front/Mid/Back = name:weight, ...
            extern bool ItemTableParse(const char* group, const char* v);
            if (ItemTableParse(key + 5, val)) continue;
        }
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
        else if (!_stricmp(key, "weaponImpact"))       g_cfg.weaponImpact = n;
        else if (!_stricmp(key, "weaponPush"))         g_cfg.weaponPush = (float)atof(val);
        else if (!_stricmp(key, "weaponLift"))         g_cfg.weaponLift = (float)atof(val);
        else if (!_stricmp(key, "weaponForward"))      g_cfg.weaponForward = (float)atof(val);
        else if (!_stricmp(key, "weaponForwardLift"))  g_cfg.weaponForwardLift = (float)atof(val);
        else if (!_stricmp(key, "weaponForwardTumble")) g_cfg.weaponForwardTumble = (float)atof(val);
        else if (!_stricmp(key, "weaponMortar"))       g_cfg.weaponMortar = (float)atof(val);
        else if (!_stricmp(key, "weaponFlyTime"))      g_cfg.weaponFlyTime = (float)atof(val);
        else if (!_stricmp(key, "weaponWallMats")) {
            g_cfg.weaponWallMatCount = 0;
            for (const char* q = val; *q && g_cfg.weaponWallMatCount < 8; ) {
                while (*q == ' ' || *q == ',') q++;
                if (!*q) break;
                char* e; long v = strtol(q, &e, 10);
                if (e == q) break;
                g_cfg.weaponWallMats[g_cfg.weaponWallMatCount++] = (int)v;
                q = e;
            }
        }
        else if (!_stricmp(key, "weaponTumble"))       g_cfg.weaponTumble = (float)atof(val);
        else if (!_stricmp(key, "weaponSpin"))         g_cfg.weaponSpin = (float)atof(val);
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
        else if (!_stricmp(key, "startRedSlow"))       g_cfg.startRedSlow = (float)atof(val);
        else if (!_stricmp(key, "startRedFrom"))       g_cfg.startRedFrom = n;
        else if (!_stricmp(key, "startRedTo"))         g_cfg.startRedTo = n;
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
        else if (!_stricmp(key, "soundOcclusion"))     g_cfg.soundOcclusion = n;
        else if (!_stricmp(key, "soundOcclusionDb"))   { float v = (float)atof(val); g_cfg.soundOcclusionDb = v < 0.0f ? 0.0f : v > 100.0f ? 100.0f : v; }
        else if (!_stricmp(key, "soundKeepVolume"))    g_cfg.soundKeepVolume = n;
        else if (!_stricmp(key, "soundFade"))          g_cfg.soundFade = n;
        else if (!_stricmp(key, "fastLoad"))           g_cfg.fastLoad = n;
        else if (!_stricmp(key, "soundEax"))           g_cfg.soundEax = n;
        else if (!_stricmp(key, "soundVoices"))        g_cfg.soundVoices = n;
        else if (!_stricmp(key, "soundDoppler"))       g_cfg.soundDoppler = n;
        else if (!_stricmp(key, "soundDopplerScale"))  g_cfg.soundDopplerScale = (float)atof(val);
        else if (!_stricmp(key, "soundHeadroom"))      g_cfg.soundHeadroom = (float)atof(val);
        else if (!_stricmp(key, "musicStream"))        g_cfg.musicStream = n;
        else if (!_stricmp(key, "musicLoop"))          g_cfg.musicLoop = n;
        else if (!_stricmp(key, "introCamClip"))       g_cfg.introCamClip = n;
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
        else if (!_stricmp(key, "sparkInherit"))       g_cfg.sparkInherit = (float)atof(val);
        else if (!_stricmp(key, "sparkBurst"))         g_cfg.sparkBurst = (float)atof(val);
        else if (!_stricmp(key, "sparkBurstMin"))      g_cfg.sparkBurstMin = (float)atof(val);
        else if (!_stricmp(key, "sparkOut"))           g_cfg.sparkOut = (float)atof(val);
        else if (!_stricmp(key, "sparkLift"))          g_cfg.sparkLift = (float)atof(val);
        else if (!_stricmp(key, "sparkLife"))          { float v = (float)atof(val); g_cfg.sparkLife = v < 0.05f ? 0.05f : v > 3.0f ? 3.0f : v; }
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
        else if (!_stricmp(key, "fallReset"))          g_cfg.fallReset = n;
        else if (!_stricmp(key, "aiUnstuck"))          g_cfg.aiUnstuck = n;
        else if (!_stricmp(key, "itemOdds"))           g_cfg.itemOdds = n;
        else if (!_stricmp(key, "aiLoopSpeed"))        g_cfg.aiLoopSpeed = (float)atof(val);
        else if (!_stricmp(key, "soundPitchRise"))     g_cfg.soundPitchRise = (float)atof(val);
        else if (!_stricmp(key, "shieldPowerFire"))    g_cfg.shieldPowerFire = n;
        else if (!_stricmp(key, "blueEmp"))            g_cfg.blueEmp = (float)atof(val);
        else if (!_stricmp(key, "iceFix"))             g_cfg.iceFix = n;
        else if (!_stricmp(key, "smokeHide"))          g_cfg.smokeHide = n;
        else if (!_stricmp(key, "parachuteHoming"))    g_cfg.parachuteHoming = n;
        else if (!_stricmp(key, "shieldSecs"))         g_cfg.shieldSecs = (float)atof(val);
        else if (!_stricmp(key, "megaShieldSecs"))     g_cfg.megaShieldSecs = (float)atof(val);
        else if (!_stricmp(key, "shieldSize"))         g_cfg.shieldSize = (float)atof(val);
        else if (!_stricmp(key, "megaShieldSize"))     g_cfg.megaShieldSize = (float)atof(val);
        else if (!_stricmp(key, "shieldZap"))          g_cfg.shieldZap = n;
        else if (!_stricmp(key, "shieldSlow"))         g_cfg.shieldSlow = (float)atof(val);
        else if (!_stricmp(key, "shieldLock"))         g_cfg.shieldLock = (float)atof(val);
        else if (!_stricmp(key, "shieldTouch"))        g_cfg.shieldTouch = (float)atof(val);
        else if (!_stricmp(key, "empSlow"))            g_cfg.empSlow = (float)atof(val);
        else if (!_stricmp(key, "empAfter"))           g_cfg.empAfter = (float)atof(val);
        else if (!_stricmp(key, "carShadowDepth"))     g_cfg.carShadowDepth = (float)atof(val);
        else if (!_stricmp(key, "steerRate"))          { float v = (float)atof(val); g_cfg.steerRate = v < 0.1f ? 0.1f : v > 10.0f ? 10.0f : v; }
    }
    fclose(f);
}
