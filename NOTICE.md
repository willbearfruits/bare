# Notices

BARE! is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See [LICENSE](LICENSE).

Copyright (C) 2026 willbearfruits.

## What the images carry besides BARE!

- **Limine**, the bootloader (its BIOS and UEFI files). Copyright (C) 2019-2026 Mintsuki and contributors. BSD
  2-Clause License: [LICENSES/Limine-BSD-2-Clause.txt](LICENSES/Limine-BSD-2-Clause.txt).
- **Terminus Font**, the text on screen (its 8x16 and 12x24 bitmaps, turned into C by `tools/psf2c.py`, with a few
  glyphs it lacks drawn in the same style). Copyright (C) 2020 Dimitar Toshkov Zhekov. SIL Open Font License 1.1:
  [LICENSES/Terminus-Font-OFL-1.1.txt](LICENSES/Terminus-Font-OFL-1.1.txt).
- **GRUB** 2.14, on the floppy image only: its boot sector and a core image made with its `grub-mkimage` from the
  modules of Arch Linux's grub 2:2.14-1 package (Limine, on the stick and the ISO, can't read a floppy drive).
  Copyright (C) Free Software Foundation, Inc. GPL-3.0-or-later ([LICENSE](LICENSE)). Its source:
  https://ftp.gnu.org/gnu/grub/grub-2.14.tar.xz, and how Arch built it:
  https://gitlab.archlinux.org/archlinux/packaging/packages/grub (tag 2-2.14-1).

## Doom's engine

- **Doom** (id Software, 1993), whose source id released under the GPL in 1999, as **Chocolate Doom** (Simon Howard
  and contributors) and **doomgeneric** (ozkl) carry it: in `third_party/doom`, built into the kernels. Copyright (C)
  1993-1996 id Software, Inc.; (C) 2005-2014 Simon Howard; (C) 1993-2008 Raven Software; and the others named in each
  file. GPL-2.0-or-later ([LICENSES/Doom-GPL-2.0.txt](LICENSES/Doom-GPL-2.0.txt)); its
  `sha1.c` (from GnuPG, (C) 1998-2001 Free Software Foundation) is GPL-3.0-or-later. No game data comes with BARE!.
  DOOM is a trademark of id Software; BARE! is not made or endorsed by id Software.

## Written from others' work

- **Ableton Link**: the protocol was implemented by reading Ableton's own source (github.com/Ableton/link, GPL-2.0-or-
  later) and checked against it. Ableton and Link are trademarks of Ableton AG; BARE! is not made or endorsed by
  Ableton.

## Named after

Pages and sounds are named after the instruments, pieces and musicians that inspired them: Evgeny Murzin's ANS
synthesizer and Coil's album ANS; Iannis Xenakis's Metastaseis, the Philips Pavilion, UPIC, GENDY3 and S.709; the
Fairlight CMI; the Suzuki Omnichord (its OM-108 and OM-84); Ableton's Operator; and, on the LINEAGE page, Steve Reich,
Wendy Carlos, Éliane Radigue and Merzbow (Masami Akita), whose techniques its views play and whose work its short
histories describe; the TOUCH page is after Michel Waisvisz's Crackle Box (the Kraakdoos, made at STEIM), and tells
of it the same way. No music of theirs is quoted: the patterns and sounds are BARE!'s own. The names describe; none of
the people, makers or companies named is involved with BARE! or endorses it.
