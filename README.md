# BARE!

A synthesizer that is the whole computer. It starts from a USB stick straight into the instrument, with no operating
system underneath, on PCs from the late 1990s to now.

![BARE!'s PLAY page: an omnichord after Suzuki's OM-108](https://willbearfruits.github.io/bare/img/play.png)

**[Download 1.0 beta](https://github.com/willbearfruits/bare/releases/tag/v1.0-beta)** ·
[the manual (PDF)](https://github.com/willbearfruits/bare/releases/download/v1.0-beta/bare-manual-1.0-beta.pdf) ·
[its page](https://willbearfruits.github.io/bare/)

## What it is

A page on each F key, and they all play at once: a song on the tracker, a chord held on PLAY, the tape rolling. It runs from memory, so once it has started the stick can come out; your projects are saved on it.

| Key | Page | |
|---|---|---|
| `F1` | **PLAY** | an omnichord after Suzuki's OM-108: chord buttons on three rows of keys, a 13-string strumplate on the touchpad, a rhythm section that follows you |
| `F2` | **SEQ** | a tracker: patterns of eight channels, effect columns, every note on the exact sample |
| `F3` | **WAVE** | a Fairlight-style page: waves drawn with the touchpad, harmonics, a sampler |
| `F4` | **STRETCH** | freezes the phrase you just played and stretches it, up to a thousand times slower |
| `F5` | **OPERATOR** | four-operator FM: eight algorithms, envelopes, an LFO |
| `F6` | **TAPE** | an eight-track tape: record, overdub, loop, bounce |
| `F7` | **FILE** | projects on the stick, songs out as WAV, MIDI and Ableton Link, the keys, the log |
| `F8` | **MIX** | a strip for every source, echo and reverb, a filter, drive and crusher on the master |
| `F9` | **TOUCH** | a crackle box after Michel Waisvisz's Kraakdoos: the bare pads of a small circuit, closed by your fingers |
| `F10` | **FX** | effects to play live over a song: repeat, reverse, tape stop, gate, dub throws |
| `F11` | **LINEAGE** | homages to play, each with a short history: Xenakis, Murzin's ANS, Steve Reich, Wendy Carlos, Éliane Radigue, Merzbow |
| `F12` | **XENAKIS** | in LINEAGE, on a key of its own: Metastaseis's glissandi, stochastic clouds, sieves, UPIC's drawn arcs and GENDY |

Instruments are text files on the stick; five come with it. MIDI goes in and out over serial ports and USB, and the
tempo can follow Ableton Link. Type `iddqd` and it runs Doom.

## Highlights

- **An OM-108 on the keyboard**: PLAY is laid out after Suzuki's omnichord, its three rows of chord buttons and their
  combinations on the letter rows, a 13-string strumplate on the touchpad, ten voices, ten rhythms, AUTO, HOLD and
  SYNC START, keyboard mode with drums, MIDI out on its channels.
- **A lineage to play** (`F11`): Xenakis (Metastaseis's glissandi as ruled surfaces, clouds, sieves, UPIC, GENDY),
  Murzin's ANS and Coil, Reich's phasing, Wendy Carlos's scales, Éliane Radigue's drones and Merzbow's noise, each
  with a short history; TOUCH has one too, of Michel Waisvisz's Crackle Box.
- **Keys follow the chord** (`Shift+K`): hold a chord, and what you play on the other pages lands on it.
- **Your F keys**: each opens a page, a view, an instrument or nothing.
- **E1M1 as breakcore** (`Shift+4` on SEQ): Doom's first-level music, read from the WAD on your stick and arranged in
  the tracker.

How it got here: [CHANGES.md](CHANGES.md).

## Try it

You need a USB stick of 256 MB or more (everything on it is replaced) and a PC with a Pentium Pro or later.

1. Download the image: [`bare-1.0-beta.img.zip`](https://github.com/willbearfruits/bare/releases/download/v1.0-beta/bare-1.0-beta.img.zip)
   (7 MB), or [`bare-1.0-beta.img.xz`](https://github.com/willbearfruits/bare/releases/download/v1.0-beta/bare-1.0-beta.img.xz) (3 MB).
2. Write it to the stick with [balenaEtcher](https://etcher.balena.io/), [Rufus](https://rufus.ie/) or
   [Raspberry Pi Imager](https://www.raspberrypi.com/software/) (*Use custom*); they take the `.zip` as it is. On
   Linux, one line downloads and writes it (`lsblk` tells you which `/dev/sdX` is the stick; whatever you name there
   is overwritten):
   ```
   curl -L https://github.com/willbearfruits/bare/releases/download/v1.0-beta/bare-1.0-beta.img.xz | xz -dc | sudo dd of=/dev/sdX bs=4M conv=fsync
   ```
3. Turn Secure Boot off, plug the stick in and start the computer from it: the boot menu is usually `F12`, `F9`, `F8`
   or `Esc` at power-on.
4. When the splash plays, press any key. Hold `6` and run a finger along the keys from `Z` to `/`: a C major chord,
   strummed. `Shift+?` shows every key.

A BARE! stick updates itself: put a newer release's `BARE.UPD` in its root and boot. Its projects stay.

It fits on a floppy too: [`bare-1.0-beta-floppy.img`](https://github.com/willbearfruits/bare/releases/download/v1.0-beta/bare-1.0-beta-floppy.img)
is a 3.5" 1.44 MB diskette for a BIOS PC with a floppy drive. It plays but can't save; [MANUAL.md](MANUAL.md#trying-it)
says how to write it.

1.0 is the first release, and a beta. It has been played on a ThinkPad X250 and an ASUS VivoBook (Intel 12th gen),
BIOS and UEFI, and it boots on 90 emulated PCs, from a Pentium II with 32 MB to 4K screens:
[which machines](MANUAL.md#which-machines).

## Read more

- [MANUAL.md](MANUAL.md): every page and every key; [as a PDF](https://github.com/willbearfruits/bare/releases/download/v1.0-beta/bare-manual-1.0-beta.pdf)
  to print.
- [INSTRUMENTS.md](INSTRUMENTS.md): write your own instruments.
- [BUILDING.md](BUILDING.md): build it, test it without hardware, find your way in the code.
- [CHANGES.md](CHANGES.md): what each version changed, the development ones too.

## Licence

BARE! is free software: GPL-3.0-or-later ([LICENSE](LICENSE)). It starts with the Limine bootloader, draws with the
Terminus font and runs Doom on Chocolate Doom's engine; [NOTICE.md](NOTICE.md) has their licences. It comes with no
game data: Doom plays the WAD you put on the stick, your own or [Freedoom](https://freedoom.github.io/)'s.
