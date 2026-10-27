# Beat That! Overdrive

A gameplay and rendering mod for **Hot Wheels: Beat That!** on PC. It's a single ASI plugin (`BTOverdrive.asi`) for the retail `HOT.exe` (SHA-1 `0c6c388cd9a8326bfea4da56049ad58a270d4830`).

It's forked from HWBTFix 0.3.8 and keeps all of its fixes: portable install, high frame rate with interpolation, per-race music, closing the window. On top of those it adds:

- **Weapons and items:**
  - weapon hits that throw cars;
  - items by race position;
  - reworked ice gun, smoke, parachute, shields and EMP.
- **Track hazards:** the graveyard ghoul throws cars.
- **AI and resets:**
  - tougher AI;
  - falling cars and stuck AI cars put back on the track;
  - a fitted starting grid.
- **Handling and camera:** quicker steering, a speed field of view, speed blur and camera shake.
- **Rendering:**
  - sharp sun shadows;
  - car shadows that stay on their road;
  - see-through objects drawn without flicker;
  - gentler HDR glare;
  - an uncut sky;
  - streak sparks that move with the car.
- **Sound:**
  - real Doppler;
  - EAX reverb and occlusion (with a wrapper such as DSOAL);
  - 64 voices;
  - fade-outs;
  - music streaming fixes;
  - looping menu music;
  - smoothed engine pitch.
- **Rampage mode**, faster race loading, and a tuning file for the game's script numbers.

See [CHANGELOG.md](CHANGELOG.md) for every change and the full list of INI keys.

## Install

1. Put an ASI loader in the game's `Bin` folder, for example Ultimate ASI Loader as `dinput8.dll`.
2. Copy `BTOverdrive.asi` into `Bin`.
3. Start the game.
   - On first start, the mod writes `BTOverdrive.ini`, which lists every option with notes.
   - It also writes `BTOverdrive_tuning.txt` with the built-in tuning.
   - Its log goes to `BTOverdrive.log`.

Don't install HWBTFix alongside it. Whichever of the two loads second finds the game already patched and stays off.

The mod checks the exe's bytes at every place it patches. If anything differs, it leaves the game alone.

## Build

MinGW-w64, 32-bit:

```
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -shared -s -static-libgcc -static-libstdc++ \
  -o BTOverdrive.asi BTOverdrive.cpp -lwinmm
```

The output imports only KERNEL32, msvcrt, USER32 and WINMM.

## Credits

Based on HWBTFix. The game is © Activision / Mattel; this repository contains no game files.

## License

GPL-3.0. See [LICENSE](LICENSE).
