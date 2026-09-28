# Building BARE!

What it takes to build it, to test it without hardware, and where things are in the code. How to play it is in
[MANUAL.md](MANUAL.md).

## Toolchain

clang + ld.lld (freestanding, integer-only: `-mno-sse`, so DSP is fixed-point), llvm-strip for the floppy, Limine
(`/usr/share/limine`), xorriso, QEMU, Terminus font (`terminus-font`, SIL OFL) embedded via
`tools/psf2c.py`. On Arch: `pacman -S clang lld limine xorriso qemu-desktop terminus-font`.
The host checks need 32-bit glibc (`lib32-glibc`).

## Building

```
make img        # build/i386/bare.img — bootable USB image with a persistent project partition (the normal way)
make usb DEV=/dev/sdX   # flash it to a stick (keeps the projects already on the stick), boot a laptop from it
make run-img    # boot the image as a USB stick in QEMU with sound through PipeWire
make upd        # build/i386/bare.upd — drop it on any FAT drive to update a machine at boot

make            # build/i386/bare.iso — same instrument, no persistence (CD/ISO, also UEFI-only machines)
make floppy    # build/i386/bare-floppy.img — a 1.44 MB diskette for BIOS PCs, booted by GRUB (no persistence)
make grub      # fetch GRUB 2.14 (Arch's package, checked, unpacked into build/grub): make floppy runs it
make run        # boot the ISO in QEMU
make run-p2     # boot as an emulated Pentium II (no KVM) to prove the i686 build
make ARCH=x86_64  # 64-bit build in build/x86_64/ (ISO only for now)

make test       # checks on the host: audio levels, timing, stretcher, projects and updates on a stick image, every page
make bench      # DSP cost in CPU cycles
make shots      # every page at 800x600 … 1920x1080 as PNGs in build/host/out/shots
make render     # the demo songs and presets as WAVs in build/host/out/wav
make video      # a 70-second showcase with sound, played by a script: build/host/out/showcase.mp4 (needs ffmpeg)
make theme-shots   # every page in every colour scheme, with contact sheets: build/host/out/themes
make freedoom   # fetch Freedoom (free game data for Doom) into build/freedoom: the Doom checks use it
make doom-img   # build/i386/bare-doom.img: the stick image with Freedoom in its DOOM folder
build/host/video --splash N out.mp4   # one of the boot's splash pieces (0-3) as a video with sound
tools/showcase.py out.mp4             # every part of BARE! in one long video, with title cards (needs make doom-img)
```

The 32-bit build is the main one: it runs on every PC since the Pentium Pro, and on a BIOS boot it reads and writes
the stick through the BIOS, so the stick works wherever the BIOS can boot from it. The stick and the ISO also carry
the 64-bit build, which Limine starts instead on UEFI machines with a 64-bit CPU: recent laptops put the framebuffer
above 4 GiB, out of the 32-bit build's reach. A UEFI boot has no BIOS; there the instrument's own USB stack reaches
the stick, and USB keyboards, mice and MIDI (see Which machines).

## Testing without hardware

`make test` runs the instrument as a Linux program with a simulated clock, keyboard, pointer and a copy of the
stick image, and checks what comes out: no clipping and no DC with chords and strums, the sequencer's beats on
the exact sample, the stretcher's level, projects saving and loading on the stick, old projects migrating,
every page drawn. It takes about ten seconds.

For the real kernel, QEMU:

```
python3 tools/qemu-test.py --keys "6:1200 wait:300 z wait:150 x shot:strum"
```
boots headless, holds `6` (C major) for 1.2 s, strums, screenshots to `build/i386/strum.ppm`, writes
audio to `build/i386/out.wav` and the serial log to `build/i386/serial.log`. `HDA_DEBUG=2` traces the HDA
controller from QEMU's side. The wav's RIFF sizes are left at zero (QEMU never finalises it): skip the 44-byte
header and read 16-bit stereo 44.1 kHz. The title bar shows `dsp NN%`: the share of CPU time the audio interrupt takes.
`--res 1024x768` boots with that panel size (the BIOS of an older laptop usually offers 1024x768 whatever the panel is);
every page is laid out for 100x37 cells (800x600) and up, and at 4K the fonts are drawn at twice the size.
`--img --uefi` boots the stick image under UEFI (our USB stack then runs); `--usb usb-kbd,usb-tablet` adds USB devices
(`usb-hub@2,usb-kbd@2.1` puts them behind a hub); `--usbmidi` plugs in a USB MIDI keyboard emulated by
`tools/usbredir_midi.py` (QEMU has none of its own), played with `umidi:90,45,64` tokens.

`tools/qemu-matrix.py` boots the kernel on 90 emulated PCs (BIOS and UEFI, three chipsets, USB 1.1/2/3, IDE,
SATA, NVMe, SCSI and SD storage, CPUs from the Pentium II to current ones, several sound and graphics cards, 640x480
to 4K, 32 MB to 8 GB, our USB stack with a stick, a hub and USB-only input) and checks that each one boots, draws,
plays, and saves and reloads a project where the firmware or our USB driver can. Screenshots and a summary go to
`build/matrix/`. The emulator's own bugs are marked EMU: its OHCI controller stops during writes (saving through it
fails, and says so), its UHCI one drops data on long reads (booting from it sometimes takes a reset), and its VGA
shears 1366-wide modes.

## Layout

```
core/       portable C, no platform code — the audio graph (audio.c), voices (synth.c, fm.c), omnichord, sequencer,
            wave bank, sampler, stretcher, mixer, 8-track tape; the pixel layer (gfx.c) and the text grid on it (text.c); one file per
            page (page_*.c) and the parts they share (ui.c); FAT32, project store, updater, app loop
arch/x86/   hardware shared by both builds: PIT, PS/2 keyboard+mouse, serial and MPU-401 MIDI, PCI, Intel HDA, AC97,
            Sound Blaster 16, PC speaker, the USB stack (xHCI, hubs, keyboards and mice, sticks, MIDI), PIC, CMOS clock,
            sensors (CPU thermal MSR, SMBIOS, ThinkPad EC)
arch/x86/i386   also: real-mode trampoline + BIOS int 13h block driver (saving on a BIOS boot)
arch/x86/i386, arch/x86/x86_64   CPU tables, interrupt stubs, memory mapping per bitness
boot/i386   Multiboot2 header + entry (Limine loads it on BIOS and UEFI); boot/x86_64 Limine-protocol entry
third_party/doom   Doom's engine (Chocolate Doom by way of doomgeneric, GPL-2.0-or-later); bare/ is BARE!'s side of it
test/       the host harness: core/ built as Linux programs (checks, benchmarks, screenshots, renders, videos)
tools/      psf2c.py (font → C, draws the glyphs the console font lacks), gen_tables.py (lookup tables → C),
            mkimage.py + fat32.py (build the stick image, no mtools needed), hb.py (flash sticks keeping projects,
            make update files), qemu-test.py (headless QEMU test rig)
```

`core/` never includes anything from `arch/`; it talks to hardware through `core/platform.h`.
A new platform (Raspberry Pi, PowerPC Mac) is a new `arch/` + `boot/` directory.
