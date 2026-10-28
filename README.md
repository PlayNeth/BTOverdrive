# Beat That! Overdrive

A gameplay and graphics overhaul for **Hot Wheels: Beat That!** on PC.

Beat That! Overdrive makes races harder hitting and more chaotic. It also fixes many of the PC version's bugs, and lets the game run smoothly at high frame rates in any resolution. It's a single file, `BTOverdrive.asi`, and it doesn't touch the game's files on disk.

---

## Contents

- [Requirements](#requirements)
- [Installing](#installing)
- [Uninstalling](#uninstalling)
- [What it changes](#what-it-changes)
- [Settings](#settings)
- [Files the mod creates](#files-the-mod-creates)
- [Troubleshooting](#troubleshooting)
- [Building from source](#building-from-source)
- [Credits](#credits)

---

## Requirements

- **Hot Wheels: Beat That!** for PC, with the retail `HOT.exe` (SHA-1 `0c6c388cd9a8326bfea4da56049ad58a270d4830`).
- An **ASI loader**, such as [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader).
- **Optional:** [DSOAL](https://github.com/kcat/dsoal) or [reAL](https://github.com/PlayNeth/reAL), for the EAX reverb and muffled sounds behind walls. Windows' own sound system has no EAX; everything else works without it.
  - **On Windows,** the game creates its sound through COM, so a `dsound.dll` placed in `Bin` isn't loaded on its own. Set the path in reAL's [`dsound-com-redirect.reg`](https://github.com/PlayNeth/reAL/blob/main/tools/dsound-com-redirect.reg) to that `dsound.dll` and run it; `dsound-com-redirect-undo.reg` removes it. On Wine or Proton, install the DLL into the prefix instead (reAL's `install.sh` does this).

It runs on Windows, and on Linux through Wine or Proton.

## Installing

1. Open the game's **`Bin`** folder, the one with `HOT.exe` in it.
2. Copy the ASI loader's DLL there as **`dinput8.dll`**.
3. Copy **`BTOverdrive.asi`** into the same folder.
4. Start the game.

On the first start the mod creates `BTOverdrive.ini`, with every setting explained, and `BTOverdrive_tuning.txt`. To check it's working, look in `BTOverdrive.log`: the first line names the mod's version, and near the top it should say `exe check: HOT.exe matches`.

> **Don't install HWBTFix as well.** Beat That! Overdrive already includes all of HWBTFix's fixes. If both are present, whichever loads second sees the game already patched and turns itself off.

## Uninstalling

Delete `BTOverdrive.asi`, and the ASI loader's `dinput8.dll` if nothing else uses it. You can also delete the `BTOverdrive_*` files the mod created.

Your saves stay in the `save` folder in the game's main folder. Keep them, or move them back to `Documents\Hot Wheels` if you return to the plain game.

---

## What it changes

Almost everything can be switched off or adjusted in `BTOverdrive.ini`.

### Weapons and items

- **Weapon hits have real impact:**
  - Mines, airstrike bombs and parachute bombs throw a car up and away, tumbling.
  - Rockets and mortar shells send it flipping forward.
  - Cars caught in a blast's edge are thrown too, more weakly.
  - The game's canned barrel roll on the spot is gone.
- **Items by race position**, as in Mario Kart: the leader gets defensive items, and the back of the pack gets the big weapons. The odds are set in the INI.
- **EMP:** only 7th and 8th place can get it, at low odds. It now also slows every other car down for 3 seconds.
- **Shields:**
  - The Shield lasts longer, and the Mega Shield longer still.
  - A car that touches a shield gets a shock that slows it down.
  - A Mega Shield's shock also takes away the other car's steering for a moment.
- **Parachute:** homes in on its target like the Homing Rocket.
- **Smoke:** homing weapons lose track of a car while its smoke is pouring out.
- **Ice gun fixes:**
  - Frozen cars can't be refrozen on top.
  - Steering always comes back when the ice goes.
  - AI cars can't steer while frozen; they used to drive on as if nothing had happened.
- **Spooky Sprint:** a stretch of road before the ghoul's ramp wouldn't let anyone fire. That's fixed.

### Tracks, hazards and AI

- **The graveyard ghoul swats cars off the track,** as in the trailer. The hazards' collision boxes were twice their real size, and that's fixed. The graveyard gate now actually blocks.
- **Falling off the track:** on every track, a car that falls into the void is put back where it left the track.
- **Tougher AI:**
  - It doesn't slow down when it's ahead of you.
  - It fires more often and targets you more.
  - It gets start boosts.
- **Stuck AI:** an AI car that gets lost, or keeps ramming an unbroken shortcut barrier, is put back on the track after 6 seconds.
- **AI on loops:** the AI keeps enough speed for big loops instead of falling off them over and over.
- **Starting grid:** races start from a grid, not single file. The grid fits itself to narrow start lines.
- **Rampage:**
  - AI cars spread out over the whole track.
  - They're as fast as you; the game gave them 45% of your top speed.
  - They run away from you.

### Driving and camera

- **Quicker, lighter steering:** the front wheels turn 2.5 times as fast.
- **Built-in tuning:**
  - a lighter car;
  - a closer, livelier camera;
  - more tyre smoke;
  - suspension that keeps the wheels on the road over loops and wavy roads.
- **Speed effects:** the field of view widens at speed, with an optional speed blur and a slight camera shake.
- **A harder start boost:** the timing dial runs faster.

### Graphics

- **Any resolution and a high frame rate:** cars, cameras and animated models are interpolated, so motion stays smooth above the game's 60 fps.
- **Sharp sun shadows:** they were drawn into an 8×8 pixel map. They now reach far ahead, roofs included, with no pop-in.
- **Car shadows stay on their own road:** they no longer show through onto roads below, as on the spiral.
- **Glass and see-through track pieces** no longer flicker.
- **Gentler HDR glare,** with your own HDR settings in the INI.
- **No black hole at the top of the sky.**
- **Sparks:** collision sparks are bright streaks that move with the car and burst off the impact, bigger on harder hits. The road under the car doesn't make sparks.

### Sound and music

- **Real Doppler:** passing cars change pitch by how they actually move.
- **64 sound effects at once:** modern Windows gave the game only 16, which cut sounds off.
- **Smoother sound:**
  - fade-outs instead of clicks;
  - less clipping;
  - no engine squeal when a wheel leaves the road for a moment.
- **With [DSOAL](https://github.com/kcat/dsoal) or [reAL](https://github.com/PlayNeth/reAL)** (EAX):
  - each track's zones get the game's own reverb for tunnels, caves and halls;
  - sounds behind a wall are muffled.
- **Music:**
  - no more "scratched disc" stutters;
  - every race plays its own song, and the results music plays;
  - menu songs loop instead of going silent;
  - race music starts at GO.
- **The scrape sound** plays when you grind walls and cars.
- **Your volume settings** apply from the moment the game starts.

### Convenience and fixes

- **Portable:**
  - settings live in `BTOverdrive.ini`, not the Windows registry;
  - saves go to a `save` folder next to `Bin`;
  - `DisplayConfig.exe` is no longer needed.
- **Faster race loading,** by about 3 seconds.
- **Quitting:** closing the window quits cleanly, with no crash on exit.
- **Menus:** no line along the top edge, and the track name changes with its card in the track select.

---

## Settings

**`BTOverdrive.ini`** is in the `Bin` folder. Every setting has a note above it saying what it does, and `0` usually means "as the original game". Some common ones:

| Setting | What it does |
|---|---|
| `width` / `height` | Resolution; `0` uses your desktop's. |
| `fpsLimit` | Frame rate cap; `0` uses your monitor's refresh rate. |
| `speedFov`, `speedBlur`, `speedShake` | The speed effects; `0` turns one off. |
| `steerRate` | How quickly the wheels turn; `1` is the original. |
| `weaponImpact` | `0` brings back the original barrel roll on a hit. |
| `itemOdds` and `itemsFirst` / `itemsFront` / `itemsMid` / `itemsBack` | Which items each race position gets. |
| `aiTargetPlayer`, `aiWeaponCooldown` | How aggressive the AI is. |
| `soundDoppler`, `soundEax`, `soundOcclusion` | The sound features. |

[CHANGELOG.md](CHANGELOG.md) has the full table of settings with their defaults.

**`BTOverdrive_tuning.txt`** changes the numbers in the game's own data as it loads them: car physics, cameras, effects. Lines starting with `;` are off. Delete the file to go back to the mod's built-in tuning. `BTOverdrive_tuning.log` lists every number the game loads, if you want to experiment.

## Files the mod creates

All in the `Bin` folder:

| File | |
|---|---|
| `BTOverdrive.ini` | Settings. |
| `BTOverdrive_tuning.txt` | Tuning of the game's data. |
| `BTOverdrive.log` | What the mod did; send it with any bug report. |
| `BTOverdrive_tuning.log` | Every number the game's data files load. |
| `BTOverdrive_volume.txt` | Your sound volumes. |
| `BTOverdrive_game.log` | The game's own debug messages, only with `gameLog=1`. |

Saves and `Settings.cfg` go to the `save` folder in the game's main folder. Existing saves are copied there from `Documents\Hot Wheels` on the first start.

## Troubleshooting

- **Nothing changed, and there's no `BTOverdrive.log`:** the ASI loader isn't loading. Check that `dinput8.dll` and `BTOverdrive.asi` are both in `Bin`, next to `HOT.exe`.
- **The log says `exe check: bytes at ... differ`:** either your `HOT.exe` isn't the version the mod was made for, or HWBTFix is also installed and patched the game first. Remove HWBTFix. If that's not the cause, check your `HOT.exe` against the SHA-1 above. Either way, the mod leaves the game alone to be safe.
- **No reverb or muffling:** these need an EAX-capable sound driver such as [DSOAL](https://github.com/kcat/dsoal) or [reAL](https://github.com/PlayNeth/reAL). The log says whether EAX is on.
- **Something feels off:** most features can be switched off in the INI one at a time. Please report bugs with your `BTOverdrive.log`.

## Building from source

The source is a single file, `BTOverdrive.cpp`, built with MinGW-w64, 32-bit:

```
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -shared -s -static-libgcc -static-libstdc++ \
  -o BTOverdrive.asi BTOverdrive.cpp -lwinmm
```

## Credits

- Built on **HWBTFix**, the vanilla fixes for the PC version.
- Hot Wheels: Beat That! is © Activision and Mattel. This mod contains no game files.
