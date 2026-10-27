# Beat That! Overdrive changelog

Beat That! Overdrive (formerly HWBTMod) is a gameplay and rendering mod for **Hot Wheels: Beat That!** on PC (`HOT.exe`, SHA-1 `0c6c388cd9a8326bfea4da56049ad58a270d4830`). It's forked from HWBTFix 0.3.8 and includes all of its fixes: portable install, high frame rate with interpolation, music, closing the window and so on. On top of that it changes the gameplay and fixes the PC version's rendering.

For the vanilla game, use HWBTFix instead. Install only one of the two: whichever loads second finds the game already patched and stays off (its log says so).

The plugin checks the exe's bytes at every place it patches. If anything differs, it leaves the game alone.

---

## 0.5.3: weapons that hit hard, items by position, polish

**Weapon hits**
- **Real impact instead of the canned barrel roll:** a hit throws the car, and every throw varies a little.
  - Mines, airstrike bombs and parachute bombs throw it away from the blast and up, tumbling over and spinning.
  - Rockets and mortar shells throw it forward and up, flipping end over end. That's a crash landing, not a free boost. A mortar shell hits 1.4× as hard as a rocket.
  - A car caught in a blast without a direct hit is thrown too, at 40–100% strength depending on how close it was. In the game it only got a nudge.
- **Invisible walls:** a thrown car passes through the track's invisible walls for 2 s, as the ghoul's throws do, but not through real walls.
- **Back on the track:** a car thrown off the track is put back.
- `weaponImpact=0` gives back the game's roll. Needs `hazardHits=1`.

**Items**
- **Items by race position, Mario Kart style:** the leader gets defensive items and the back of the pack gets the big weapons. The odds are in four INI lines (`itemsFirst`, `itemsFront`, `itemsMid`, `itemsBack`) of `item:weight` pairs.
- **EMP:**
  - Only the last two places can get it, at low odds, from normal or blue crates. A blue crate keeps the game's odds for everything else.
  - It now also slows every other car down for 3 s, like a Tesla Ray hit.
- **Shields:**
  - Last 1.5× as long; the Mega Shield lasts 1.75×.
  - A car that touches a shield gets a shock: the Tesla Ray's sparks, and slowed to parachute speed for 2 s. A Mega Shield's shock also takes its steering away for 2 s.
- **Parachute:** homes in on its target like the Homing Rocket. An AI's parachute is aimed at a car ahead, as the AI's homing rockets are.
- **Smoke:** while a car's smoke is pouring out, homing rockets and parachutes lose their lock on it.
- **Ice gun fixes:**
  - Each shot of the upgraded ice gun started a new freeze on top of the last. A frozen car is no longer refrozen.
  - A car that has just broken out is free for a second.
  - The steering always comes back with the ice.
  - A frozen AI car can no longer steer; it drove on as if nothing had happened.
- **Spooky Sprint:** a stretch of road before the ghoul's ramp is marked, in the track's data, with the state of the track's shield power-up. That state doesn't allow firing, so nothing could be fired there. Items now fire there, and under the shield power-up too (`shieldPowerFire`).

**Track and AI**
- **Falling off:** on every track, a car that falls off into the void is put back where it left the track. The game's own reset never found it a place from down there.
- **Stuck AI:** an AI car that drives for 6 s without getting anywhere is put back at the nearest reset point. That covers an AI that's lost its route, or keeps ramming a shortcut's unbroken barrier.
- **AI on loops:** the AI slowed down for big loops, fell off the wall or ceiling, and tried again over and over. On a loop it now keeps at least 50 units/s, and one that keeps falling off at the same loop is put back past it.
- **Narrow starts:** the grid fits the room beside the start line. It's moved into the middle, and gets fewer columns, or narrower ones, where there's no room for four.

**Handling and physics**
- **Quicker steering:** the game turned the front wheels at 0.96 rad/s, over a second from straight to full lock, which gave the slow, heavy turn-in. They now turn 2.5× as fast (`steerRate`).
- **Smoother loops and wavy roads:** stiffer suspension damping and slightly softer springs keep the wheels on the road instead of skipping off it.
- **Built-in tuning:** the tuning file's numbers are now built into the mod: a lighter car, a closer and livelier camera, more tyre smoke and the suspension changes. With no `BTOverdrive_tuning.txt`, the mod uses them and writes the file for you to edit. Delete the file to get the built-in numbers back. **Upgrading:** delete the `BTOverdrive_tuning.txt` from an earlier version, which doesn't have the suspension lines.

**Graphics and sound**
- **Car shadows:** a car's shadow reached 4.4 units below it, through the road onto any road underneath. On the spiral, cars above you cast shadows on your road. It now reaches 1.9 (`carShadowDepth`).
- **Sparks:** collision sparks move with the car and burst off the contact, back along the wall and out from the car, and they're short-lived. The game shot them backward at a fifth of the car's speed, so at speed they all streamed straight off the screen.
- **Menu music loops:** the main menu's and garage's songs played once and then went silent for good. Every song now starts over at its end.
- **Engine blips:** a wheel off the road for a moment, over a bump or a crest, revved the engine for a few hundredths of a second, giving a high-pitched blip. A held sound's pitch now rises at most 6% per 1/60 s. Drops in pitch are left as the game sets them (`soundPitchRise`).
- **Doppler:** the listener's speed is measured the same way as each sound's, so your own engine has no Doppler shift of its own.
- **Intro camera:** the race intro's camera is pulled in toward the car when the scenery is in the way.

**New INI keys:** see the table.

## 0.5.2: EAX, sound polish, faster race loading

**EAX (with a wrapper such as DSOAL)**
- **Reverb by zone:** the game has its own reverb for tunnels, caves and halls, set as you drive through each track's zones, with 9 presets tuned for it. The PC game checks the sound device for EAX and then switches it off again, because its "EAX on" setting is never set. It's now on (`soundEax`). Windows' own DirectSound has no EAX, so this needs a wrapper such as DSOAL (see `dsound-com-redirect.reg`, a separate download).
- Some zones ask for presets 9–13, which don't exist, so the game read past its table. Those now get the default preset.
- **Occlusion:** a sound with a wall of the track between it and you is muffled, mostly in the highs, easing in and out. The game fetched every sound's EAX controls and never used them. It uses the game's own line test against the track, a few sounds per step (`soundOcclusion`, `soundOcclusionDb`).

**Sound effects**
- **Fade-outs instead of clicks:** a sound stopped or cut off to make room for another now fades out over about 50 ms instead of stopping dead (`soundFade`).
- **Your volumes from the start:** the game played everything at 50% until a profile was loaded. The volumes you confirm in the sound menu are now kept in `BTOverdrive_volume.txt` and applied as soon as the sound starts (`soundKeepVolume`).

**Music**
- A new track no longer skips about a sixth of a second at its start. Its refill signals could still be set from the previous track.
- A decoding error no longer feeds the stream garbage or loops forever. The track ends, or plays silence while the error lasts.

**Loading**
- **Race loads about 3 s faster:** half of every race load was spent waiting for the drivers' and gunners' models, which the game loads one stage per game step, one model at a time. While it waits, those stages now run as many times as fit in each frame, and the loading screen keeps drawing at 60 fps. File reads are also checked every 1 ms instead of every 10 (`fastLoad`).
- Menu loads are left as the game has them: the same treatment there hung the car select's loading screen.

**Quitting**
- **No more crash on exit:** the PhysX memory pool's destructor freed memory that was already gone as the game quit (an access violation at `0x5CF89B`). Once the game is exiting, it no longer does.

**New INI keys:** see the table.

## 0.5.1: sound

**Music**
- **No more "scratched disc":** the music sometimes repeated a bit of itself over and over, in menus and races, in the vanilla PC game too.
  - It plays from a looping buffer of only half a second, which its thread must refill within a third of a second of each slice playing.
  - Its file reads queued behind every other read in the game, on a single file thread that checks for requests every 10 ms. Behind another read, it missed its turn and the buffer replayed its last half second until the read came through.
  - Fixes: a 1.1-second buffer, so each slice has three quarters of a second to be refilled; the music reads the archive itself through its own handle; and its thread runs at above-normal priority. Any read that still takes over 100 ms is logged (`musicStream`).

**Sound effects**
- **64 voices:** on Windows Vista and later the game got only 16 sound effects at once, where cards of its time gave it 64. Busy moments stopped sounds dead and restarted engine and skid loops from the beginning (`soundVoices`).
- **Real Doppler:** the game pitch-shifted every opponent and world sound by your speed alone, as if each one stood still, so they warbled as you sped up and slowed down. Each 3D sound's speed is now measured from how it actually moves, so a car keeping pace with you doesn't shift and one passing you does. It works the same under Windows' DirectSound or a wrapper such as DSOAL. `soundDoppler=1` gives the game's version back, `0` turns Doppler off, and `soundDopplerScale` sets its strength.
- **Headroom:** sound effects are 3 dB quieter, so a pile of them doesn't clip in the software mix (`soundHeadroom`).
- **New INI keys:** see the table.

## 0.5.0: Beat That! Overdrive

**New name**
- HWBTMod is now **Beat That! Overdrive**: `BTOverdrive.asi`, `BTOverdrive.ini`, `BTOverdrive.log`, `BTOverdrive_tuning.txt`.
- On first start, an existing `HWBTMod.ini` and `HWBTMod_tuning.txt` are copied to the new names, so your settings carry over. Delete the old `HWBTMod.asi`.

**Shadows**
- **Sharp sun shadows:** the PC game renders the sun's shadows of the track and scenery into an 8×8 pixel map, which came out as a blurry dark patch round your car. It's now 2048×2048.
- **No more shadows popping in:** they reached only 64 units ahead of the camera. Now 1000, fading over the last 150.
- **Roofs and wall tops cast shadows too:** the shadow light only saw 16 units toward the sun, so anything higher was cut off.

**Glass and see-through pieces**
- **No more flicker:** see-through things (glass, the glass road pieces) flickered between clear and washed out. The game sorted them back to front in a way that let equal ones swap places from frame to frame, and they hid whatever was drawn behind them afterward. They're now sorted steadily and no longer block what's behind them.

**HDR**
- **Gentler highlights:** the race tracks draw star-shaped glare streaks on anything above 30% brightness, at up to 3.5× brightness, which made paint and floors look harsh. The glare is now half as strong and caps at 1.75× (`hdrGlare`).
- **Your own HDR settings:** `hdrBrightness`, `hdrBloom` and `hdrBloomThreshold` scale each track region's own values. `hdr.<name>=value` sets any HDR setting outright, e.g. `hdr.g_Exposure=0.6`. The log lists every region's values.

**Sky**
- **No black hole at the top of the sky:** with the wider view the speed FOV gives, the sky's far plane cut it off (worst on Spooky Sprint). It now reaches 20000 instead of 5000 (`skyFar`, `skyStretch`).

**Sparks**
- **Size by impact:** harder hits throw bigger, longer, faster sparks, and more of them, up to 1.6× normal size.
- **Each spark is turned up to 25° off its course,** so they don't all line up.

**Sound and music**
- **Scraping sound:** grinding along walls and other cars now plays the game's own `metalscrape` sound. The PC surfaces never name one, so scraping was silent.
- **Race music starts at GO,** after the countdown and start boost, as on the PS2. The PC game started it with the 3-2-1.

**Smoother motion**
- **Animated models are interpolated too:** at high frame rates the graveyard ghoul and other animated (skinned) models still moved in 60 Hz steps. Their bones are now blended between steps like everything else (`interpolateBones`).

**The graveyard gate**
- **It blocks properly now.** The note in 0.4.0 was wrong: the game doesn't double the hazards' collision boxes. Each box is exactly as the track file gives it, and the gate panels' boxes match the panels. The real bug came from the mod's own halving, plus a guess that the panels hang from their hinges, which put their boxes beside the panels. The gate now keeps its own boxes, made 3 units thick so a car can't slip through a closing panel.
- The ghoul's slabs and hands still get boxes half the authored size, because theirs reach well beyond what's drawn.

**Menus**
- **No stray line at the top:** all the game's 2D was drawn half a pixel too low, because Direct3D's half-pixel offset was applied with the wrong sign vertically. That left the top row of pixels showing the menu background through the black band. The HUD moves up one pixel with it.
- **Track select:** for one frame, the next track's name showed on the old card before the cards slid. The name now changes with its card.

**Start boost**
- **Harder:** the start-boost dial now runs 2.4× as fast (was 1.6). An existing INI keeps its own `boostMeterSpeed`, so set it to `2.4` there.

**Diagnostics**
- **Frame timing:** with `perfLog=1`, `BTOverdrive.log` shows every 10 s where each frame's time goes. Off by default.
- **Crash log:** if the game crashes in its own or the mod's code, `BTOverdrive.log` records where, the registers and the call chain.

**Code review before release**
- With `sparkVary`, `sparkHot` and `sparkCount` all at 0, the impact sizing and twist were silently off. They now work on their own.
- `sparkTwist` is capped at 80°, and each spark is now turned sideways from its course, never back on itself.
- The shadow shaders' fade now matches the shadow box's: with a short `shadowRange`, the fade is clamped to half of it.
- The track-name delay counts only the track select's own updates, so another menu updating can't cut it short.
- The crash log ignores the mod's own pointer checks, which Windows handles by design, and writes each crash as one block.
- Stale comments and INI notes fixed, and dead code removed.

**Looked into, not changed**
- **Car damage:** the PS2's damage model is in the PC code. Cars do take damage, but its visuals don't show: denting was emptied out and the renderer never sends it to the GPU, and cracked glass doesn't show either. The damage art appears to be missing from the PC files.
- **New INI keys:** see the table.

## 0.4.1: Rampage, sparks, speed blur and camera shake

**Rampage**
- **AI cars spread over the whole track:** before, they started on the race grid around you. They now start along the track, as far from you and from each other as possible, mostly ahead of you, so you don't just turn round and catch one.
  - The spots come from the track's own driving lines, with a spot every 30 units, plus its reset points.
  - Each car faces the way the track runs.
- **They're as fast as you:** the game builds Rampage AI cars with 45% of your car's top speed, which is why the mode was so easy. They now get all of it.
- **They run from you:** within 60 units, an AI car speeds up and steers to the side of the road away from you. The AI's routes only go forward, so they can't turn round.
- The F1 grid is for races, eliminator and tournaments only.

**Sparks**
- **Streaks, not dots:** car-on-car sparks were drawn in the game's "chain of dots" mode over just 0.04 s of flight, so they came out as a line of orange dots. They're now drawn as streaks, like the game's other sparks, and wider.
- **Longer:** every streak shows 0.5 s of the spark's flight instead of 0.25.
- **Varied and hot:** each spark gets its own length and width (40–160%). Some burn yellow to near white, and all are fully opaque.
- **A fountain on car hits:** the game throws only 1–2 sparks a hit. Each car-hit spark now brings 3 more that scatter and shoot upward, at most 120 a second.
- **Only walls and cars:** the game threw collision sparks for any hard hit, landing a jump included. Hits from the road under the car no longer throw any.

**Camera**
- **Speed blur:** the game has a motion blur that blends in the last 3 frames, but it only showed when a boost widened the view, and some tracks switched it off. It now builds with your speed (from 30 to 70 units/s), on every track. The game's boost blur still applies when it's stronger. It needs post-processing on.
- **Speed shake:** above 40 units/s the race camera rumbles slightly (a small turn of up to 0.4°, slow waves of 1.7–5 Hz), building to full at 70. There's no shake while paused.

**Other**
- **New INI keys:** see the table.

## 0.4.0: the ghoul, the gate, the grid and tougher AI

**The ghoul (graveyard track)**
- **He swats cars off the track,** as in the trailer. When one of his hands sweeps through a car (you or an AI), the car is thrown sideways away from him and up, with a tumble and a burst of smoke.
- **Why it never happened:** the game gives each moving part of a track hazard a collision box, but:
  - his long fingers have none;
  - the boxes were twice the size of their parts (see below);
  - the game measures their speed as almost nothing, so a hand could only block or nudge a car.
  The mod tracks each part's real speed itself and gives the hands a reach that takes in the fingers.
- **Off the track and back:** a thrown car goes through walls for a moment, so it can actually leave the track. One that falls into the void is put back on the track where it was hit, with the game's own put-back.
- The car's grip used to cancel most of a sideways throw within half a second, so the throw keeps pushing for half a second.

**Hazard collision (a game bug)**
- **Twice too big:** every track hazard's collision box was twice the size of its part in every direction, because the game handed the physics engine the full size where it expects half. That caused the invisible walls around them. They're now the right size.
- **The graveyard gate:** its panels hang from hinges, but their boxes were centred on the hinges, so they floated above the track. Cars drove through the gate, and at the old double size there were invisible walls around it.
  - Each panel's box is now moved onto the panel, made thicker so a fast swing can't skip past a car, and extended down to the track.
  - The gate blocks; it doesn't throw.
  - **Known issue:** the hinge area still has no collision.

**Starting grid**
- The cars start in a grid as in the trailer: four lanes, every car a little further back than the one before (like an F1 grid), instead of in single file. The car order is kept, and restarts use the same grid.

**Start boost**
- The dial you time the boost start on runs 1.6 times as fast, so the green is harder to hit. The 3-2-1-GO timing is unchanged.

**AI opponents**
- **No slowdown when ahead:** AI cars that got ahead of you were capped at about 75% of their speed and lost grip (the game's rubber-banding). That's off; AI cars behind you still get their catch-up.
  - Note: the `Catchup…` values in the game's files are loaded and never used.
- **More weapon fire:**
  - After any AI shot, all AI cars waited 5 seconds; now 2.5.
  - More of their shots are aimed at you: 60/70/80% by difficulty, up from 40/50/60%.
  - They hold back area weapons (ice, EMP, airstrike, smokescreen…) only once 4 are already out, up from 2.
- **Start boost for the AI:** at GO each AI car gets the start boost with a 50% chance. The game never gave them one, because their version of it does nothing.
- The AI could already use every item, boost included.

**Other**
- **Pause menu:** the speed field of view no longer drops back while the game is paused.
- **Tuning log:** it's now written out as it goes. Before, the end of it could be lost when the game was closed.
- **Code review:** before release:
  - the throw's push no longer depends on the fall check being on;
  - a reused slot can't put back the wrong car;
  - stale comments and INI notes were fixed.
- **New INI keys:** see the table.

## 0.3.1: smoother on slower machines

- **Back-and-forth stutter on cars:** fixed. The car body, wheels and parts are smoothed from positions sampled once per frame. A frame with two or more game ticks, common below the frame cap, made those samples skip a tick, and the history was thrown away. That part was then drawn at its raw tick position for a frame and snapped back. Now the older sample is kept and the motion is blended across both ticks. On a laptop at 51–60 fps, parts drawn unsmoothed dropped from up to 2.4 per frame to almost none.
- **Camera field of view:** it's now interpolated between ticks too. The game's camera effects change it every tick, so with the stronger camera tuning it visibly jumped while the camera's position moved smoothly. A sudden large change (a camera cut) still switches at once.
- **Car parts animated on top of the car** (for example a wing): when one changes every frame, it now follows the car's smoothing instead of its raw position.
- The speed for the speed FOV now comes from the followed race car (yours if it's near the camera), not from the camera's own movement.

## 0.3.0: speed field of view

- **Speed FOV:** the race camera's field of view widens with speed, by up to `speedFov` degrees (default 12). It starts at `speedFovMin` (11 units/s, about 25 mph) and is full at `speedFovMax` (45, about 100 mph), easing in smoothly.
- Each camera keeps its own smoothed speed. The game sets up more than one camera a frame, and the first test build shared one value between them, so the race camera's field of view jumped every frame (cars appeared to stutter, most visibly at 60 fps).
- Cameras that never move (menus) are left exactly as the game sets them.
- At quitting, `HWBTMod.log` notes the fastest camera speed seen, for calibrating the range.
- **New INI keys:** `speedFov`, `speedFovMin`, `speedFovMax`.

## 0.2.0: tuning

- **Tuning log:** with `tuningLog=1` (default), every number the game's scripts load goes into `HWBTMod_tuning.log`: car physics, cameras, surfaces and effects, about 27,000 values. Each has its keyword, value, and the game code that reads it (`@` address).
- **Tuning file:** `HWBTMod_tuning.txt` next to the `.asi` changes those numbers as the game loads them:
  - `KEY = 2.5` sets every `KEY`, `KEY *= 1.3` scales it.
  - `KEY#1 *= 1.3` targets the second number after `KEY`.
  - `@5F9FCF:MASS *= 0.8` only changes it where that game code reads it, since the same keyword can mean different things (a car's `MASS` and a wheel's `MASS`).
  - The log marks every value the file changed.
- **Included `HWBTMod_tuning.txt`:** a lighter car (inertia × 0.4, body mass × 0.65), a closer, wider camera that reacts more to acceleration, sways and leans more, and tyre smoke on every surface that starts earlier in turns.
- **New INI key:** `tuningLog`.

## 0.1.0: fork of HWBTFix 0.3.8

- Same as HWBTFix 0.3.8. No gameplay changes yet.
- Its own file names, so it can sit next to an HWBTFix install without sharing settings or logs: `HWBTMod.asi`, `HWBTMod.ini`, `HWBTMod.log`, `HWBTMod_game.log`.

---

## INI keys

HWBTFix 0.3.8's keys, plus the mod's own:

| Key | Default | Meaning |
|---|---|---|
| `width` / `height` | `0` | Resolution; `0` = desktop resolution. |
| `modelDetail` | `2` | 0–2. |
| `drawDistance` | `2` | 0–2. |
| `shadowDetail` | `3` | 0–3. |
| `particles` | `1` | 0–1. |
| `postProcessEffects` | `1` | 0–1. |
| `saveFolder` | `save` | Folder for saves and `Settings.cfg`, relative to the game's root or a full path; empty = `Documents\Hot Wheels`. |
| `portableRegistry` | `1` | Answer the game's registry key from the INI (`0` = Windows registry). |
| `interpolate` | `1` | Above 60 fps with interpolation (`0` = original 60 fps). |
| `interpolateBones` | `1` | With interpolation, animated models (the ghoul…) are smoothed too. |
| `fpsLimit` | `0` | Frame cap with interpolation; `0` = refresh rate, otherwise 30–1000. |
| `closeExits` | `1` | Closing the window quits the game cleanly. |
| `raceMusic` | `1` | Per-race and results music (`0` = PC original). |
| `gameLog` | `0` | Diagnostics: the game's debug messages and failed sound lookups to `BTOverdrive_game.log`, frame-rate statistics to `BTOverdrive.log`. |
| `logToFile` | `1` | Write `BTOverdrive.log`. |
| `tuningLog` | `1` | Log every number the game's scripts load to `BTOverdrive_tuning.log`. |
| `speedFov` | `12` | Degrees added to the race camera's field of view at speed; `0` = off. |
| `speedFovMin` / `speedFovMax` | `11` / `45` | Speed range (units/s) over which it eases in. |
| `speedBlur` / `speedBlurMin` / `speedBlurMax` | `0.35` / `30` / `70` | Motion blur at speed: strength (0–1, `0` = off), speed range (units/s). Needs post-processing. |
| `hdrGlare` | `0.5` | HDR glare streaks × the game's strength and brightness cap (`1` = as it was, `0` = off). |
| `hdrBrightness` / `hdrBloom` / `hdrBloomThreshold` | `1` / `1` / `1` | × each region's own image brightness, bloom strength and bloom threshold. |
| `hdr.<name>` | | Any HDR setting outright, e.g. `hdr.g_Exposure=0.6`. |
| `speedShake` / `speedShakeMin` / `speedShakeMax` | `0.4` / `40` / `70` | Camera shake at speed: degrees (`0` = off), speed range (units/s). |
| `hazardHits` | `1` | Moving hazards (the ghoul's hands) throw cars; the slabs' and hands' collision boxes at half size. |
| `hazardMinSpeed` / `hazardReach` | `50` / `3` | How fast a part must move to hit (units/s); its reach, in times its size. |
| `hazardPush` / `hazardLift` / `hazardSpin` | `150` / `120` / `6` | The throw: sideways and up (units/s), tumble (radians/s). |
| `hazardPushTime` / `hazardFlyTime` | `0.5` / `3` | How long the throw keeps pushing; how long a thrown car goes through walls (s). |
| `hazardFallReset` | `25` | A thrown car this far below where it was hit is put back on the track (`0` = never). |
| `hazardSmoke` / `hazardGateFix` | `1` / `1` | Smoke on a thrown car; the gate panels' collision 3 units thick. |
| `gridLayout` / `gridColumns` | `1` / `4` | Grid start (`0` = single file); cars side by side. |
| `gridWidth` / `gridRowGap` / `gridStagger` | `7` / `14` / `3.5` | Lane spacing, row spacing, how far each car is behind the one before (units). |
| `boostMeterSpeed` | `2.4` | Start-boost dial speed (`1` = original). |
| `aiLeadSlowdown` | `0` | `1` = the game's slowdown of AI cars ahead of you. |
| `aiWeaponCooldown` / `aiTargetPlayer` / `aiAreaWeaponLimit` | `2.5` / `60` / `4` | Seconds between AI shots [5]; % aimed at you [40]; area weapons out before the AI holds back [2]. |
| `aiStartBoost` | `50` | % chance each AI car gets the start boost at GO (`0` = never, as in the game). |
| `sparkStreaks` | `1` | Car-hit sparks drawn as streaks instead of dots (`0` = original). |
| `sparkLength` / `sparkWidth` | `0.5` / `4` | Streak length in seconds of flight [0.25]; the switched sparks' width ×. |
| `sparkVary` / `sparkHot` | `0.6` / `1` | Random length/width spread (±); how far sparks can burn toward white (`0` = game colours). |
| `sparkFloor` | `0.6` | No collision sparks from the road under the car (jumps, ramps); `1` = sparks from everything. |
| `sparkCount` / `sparkRate` | `3` / `120` | Extra sparks with each car-hit spark; at most this many a second. |
| `sparkSpread` / `sparkUp` | `0.5` / `1` | How far the extras scatter and how hard they shoot upward (× the spark's speed). |
| `rampageSpread` / `rampageMinDist` / `rampageBehind` | `1` / `250` / `0.4` | Rampage: AI cars start spread over the track, at least this far from you when there's room; a spot behind you counts as this × as far. |
| `rampageTopSpeed` / `rampageSpeed` | `1` / `1` | Rampage AI top speed × yours [0.45]; their target speeds ×. |
| `rampageFlee` / `rampageFleeRange` / `rampageFleeSpeed` / `rampageDodge` | `1` / `60` / `1.4` / `0.3` | Rampage AI runs from you: within this distance, up to this × speed, steering up to this share of the road away. |

| `shadowMapSize` | `2048` | Sun shadow map size (`0` = the game's 8×8). |
| `shadowRange` / `shadowFade` | `1000` / `150` | How far ahead sun shadows reach, fading over the last part [64 / 16]. |
| `shadowFit` / `shadowBackOff` / `shadowFar` | `1` / `1000` / `2000` | The shadow light sees the scene's whole height: back-off toward the sun and depth (`0` = the game's). |
| `skyFar` / `skyStretch` | `20000` / `1` | The sky's far plane [5000]; how many times it's stretched upward. |
| `glassSort` / `glassNoZWrite` | `1` / `1` | See-through things sorted steadily back to front; they don't hide what's drawn behind them. |
| `perfLog` | `0` | Frame timing in the log every 10 s. |
| `menuFixes` | `1` | The 2D half-pixel fix (no line at the top of the menus) and the track name changing with its card. |
| `musicAtGo` | `1` | Race music starts at GO (`0` = during the countdown). |
| `musicStream` | `1` | Music streaming fixes: 1.1 s buffer, its own archive reads, above-normal priority. |
| `soundVoices` | `1` | Up to 64 sound effects at once (Windows Vista and later gave the game 16). |
| `soundDoppler` / `soundDopplerScale` | `2` / `1` | Doppler: `2` = from each sound's movement, `1` = the game's, `0` = off; strength. |
| `soundHeadroom` | `3` | Sound effects this many dB quieter, against clipping. |
| `soundEax` | `1` | EAX reverb by trigger zone when the sound device has EAX (DSOAL…); `0` = off, as the PC game had it. |
| `soundOcclusion` / `soundOcclusionDb` | `1` / `15` | With EAX: sounds behind a wall of the track are muffled by this many dB (0–100). |
| `soundFade` | `1` | Stopped or cut-off sound effects fade out over ~50 ms instead of clicking. |
| `soundKeepVolume` | `1` | The sound menu's volumes are kept (`BTOverdrive_volume.txt`) and applied from the start. |
| `fastLoad` | `1` | Faster race loading: the drivers' models load as fast as frames allow; file reads polled every 1 ms [10]. |
| `scrapeSound` | `1` | The game's metalscrape sound when grinding walls and cars. |
| `sparkImpactRef` / `sparkImpactMax` / `sparkTwist` | `3800` / `1.6` / `25` | Hit strength for normal-size sparks (`0` = all the same); the biggest hits' size ×; degrees each spark turns off its course. |
| `weaponImpact` | `1` | A weapon hit throws the car instead of the game's barrel roll (needs `hazardHits=1`). |
| `weaponPush` / `weaponLift` / `weaponTumble` | `40` / `55` / `4` | Mines and bombs: away from the blast and up (units/s), tumble (radians/s). |
| `weaponForward` / `weaponForwardLift` / `weaponForwardTumble` | `35` / `45` / `6` | Rockets and mortar shells: forward and up (units/s), end-over-end flip (radians/s). |
| `weaponMortar` / `weaponSpin` | `1.4` / `3` | A mortar shell × a rocket; spin round (radians/s). |
| `weaponFlyTime` / `weaponWallMats` | `2` / `12` | A thrown car passes through the invisible walls (these collision material ids) for this long (s). |
| `itemOdds` | `1` | Items by race position (`0` = the game's table). |
| `itemsFirst` / `itemsFront` / `itemsMid` / `itemsBack` | see the INI | `item:weight` pairs for 1st, the front third, the middle and the last two. Items: rocket, mortar, boost, icegun, mine, emp, parachute, airstrike, smoke, tesla, shield. |
| `blueEmp` | `0.5` | A blue crate's EMP: only for the last two, kept this share of the time (else rolled again). |
| `empSlow` | `3` | The EMP slows every other car for this long (s; `0` = no). |
| `shieldTime` / `megaShieldTime` | `1.5` / `1.75` | Shield duration × the game's. |
| `shieldZap` / `shieldSlow` / `shieldLock` / `shieldTouch` | `1` / `2` / `2` / `5.5` | Touching a shield: slowed for this long (s); a Mega Shield's also can't steer this long (s); how close counts as touching (units, centre to centre). |
| `parachuteHoming` / `smokeHide` | `1` / `1` | The plain parachute homes in; smoke hides its car from homing weapons. |
| `iceFix` | `1` | Ice gun fixes: no refreezing, a second free after breaking out, frozen AI can't steer. |
| `shieldPowerFire` | `1` | Items fire under a track's shield power-up (and on Spooky Sprint's marked road). |
| `fallReset` | `1` | A car that falls off the track is put back. |
| `aiUnstuck` | `1` | An AI car stuck for 6 s is put back at the nearest reset point. |
| `aiLoopSpeed` | `50` | An AI car on a loop's wall or ceiling keeps at least this speed (units/s; `0` = off). |
| `steerRate` | `2.5` | How fast the front wheels turn × the game's. |
| `carShadowDepth` | `1.9` | How far below a car its shadow reaches (units) [4.4]. |
| `sparkInherit` / `sparkBurst` / `sparkBurstMin` | `1` / `0.35` / `6` | Collision sparks keep × the car's velocity; burst off the contact at × the sliding speed, at least this (units/s). |
| `sparkOut` / `sparkLift` / `sparkLife` | `0.6` / `0.4` / `0.6` | The burst: out from the car, up; the sparks' life ×. |
| `musicLoop` | `1` | Every song starts over at its end. |
| `soundPitchRise` | `0.06` | A held sound's pitch rises at most this much per 1/60 s (`0` = as the game). |
| `introCamClip` | `1` | The race intro's camera is kept out of the scenery. |

## Build

```
i686-w64-mingw32-g++ -std=c++17 -O2 -shared -s -static-libgcc -static-libstdc++ \
  -o BTOverdrive.asi BTOverdrive.cpp -lwinmm
```

The plugin is one file, loaded by an ASI loader from the game's `Bin` folder. It reads `BTOverdrive.ini` and writes `BTOverdrive.log` next to the `.asi`.
