# Doom's engine

The engine of id Software's Doom (1993, released under the GPL in 1999), as Chocolate Doom and then doomgeneric
(github.com/ozkl/doomgeneric, commit dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284) carry it. GPL-2.0-or-later
(LICENSE-GPL-2.0.txt); `sha1.c`, from GnuPG, is GPL-3.0-or-later. BARE! is GPL-3.0-or-later, so it takes them as they
are. Copyright id Software, Simon Howard, Raven Software and the others named in each file.

No game data is here. Doom plays the WAD it finds on the stick: the player's own, or Freedoom's (`make freedoom`).

## What is BARE!'s

- `bare/i_bare.c` is the platform layer Chocolate Doom's SDL files were (`i_system`, `i_timer`, `i_video`, `i_input`,
  `i_sound`, the joystick, ENDOOM): the clock, the screen, the keys, the sound and the WAD all go to BARE! through
  `core/doomhost.h`, and the engine is started once and then run a tic at a time from BARE!'s main loop. Those SDL
  files, and `i_cdmus.c` and `i_scale.c`, are left out.
- `bare/libc.c` and the headers in `bare/libc/` are the C library the engine uses: memory, printing (into BARE!'s log),
  strings, and files on the stick. The functions are renamed `doom_*`.

## Changed lines

Each is marked `BARE!` and compiled only with `BARE_DOOM`:

- `i_video.h`, `v_video.c`, `m_config.c`, `m_config.h`, `g_game.c`: floating point (the mouse acceleration, a float
  type of setting, the timed demo's frame rate) done in whole numbers or left out. BARE! is built without it.
- `m_controls.c`: the keys of 1993 (`Ctrl` fires, `Space` opens, `,` `.` strafe) instead of doomgeneric's own codes,
  so `Space` still types a space in a save game's name.
- `m_config.c`: saves go straight into Doom's folder on the stick, not into `.savegame/`.
- `w_file.c`: WAD files are opened by `bare/`'s class, which reads the whole file into memory; the engine then uses the
  lumps in place.
