# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

BARE!, a bare-metal synthesizer (no OS): boots via Limine on any PC since the Pentium Pro, draws its UI on the
framebuffer, plays through Intel HDA, AC97 or a Sound Blaster 16 (PC-speaker 1-bit fallback), saves projects to a USB
stick via BIOS int 13h or its own USB stack. It was called homebrew until 2.2: the on-disk magics (`HBTAPE01`,
`HBPROJ01`, `HBVER001`, `HBUPD002`, the `HB_A.ELF` slots) and `tools/hb.py` keep the old initials. The "!" stays out of
file names, identifiers and the boot menu (in a shell, `!` is history expansion).
The README is the user manual and the roadmap; keep it in sync when adding pages, keys, or sounds.

## Build / run / test

Toolchain: clang + ld.lld, Limine in `/usr/share/limine`, xorriso, QEMU, Terminus font, and 32-bit glibc for the host
checks (`pacman -S clang lld limine xorriso qemu-desktop terminus-font lib32-glibc`).

```
make                 # build/i386/bare.iso (default ARCH=i386)
make ARCH=x86_64     # 64-bit build in build/x86_64/ (ISO only; its storage is the USB stack)
make img             # build/i386/bare.img — USB image with persistent project partition (the real product)
make upd             # build/i386/bare.upd — A/B update file (BARE.UPD on any FAT drive; HOMEBREW.UPD, 1.0-2.2's name, too)
make dist            # build/dist/bare-RELEASE/: stick image, ISO, BARE.UPD + HOMEBREW.UPD, CHANGES.md, SHA256SUMS
make run / run-img   # boot ISO / disk image in QEMU with GTK display, PipeWire audio, serial log on stdout
make run-p2          # emulated Pentium II, no KVM: proves the i686 build has no SSE/687 leaks
make usb DEV=/dev/sdX   # flash a stick, preserving its project partition (sudo)
make test            # pass/fail checks on the host (test/check.c) against a scratch copy of the .img grown by 64 MiB,
                     # run twice: as a 512 MB PC, then with HOST_MEM=28 as a 32 MB one
make bench           # DSP cost in cycles per frame (test/bench.c)
make shots           # every page at 800x600, 1024x768, 1280x800, 1366x768, 1280x1024, 1920x1080, 3840x2160
                     # → build/host/out/shots/*.png, plus framebuffer KB/s and cycles per frame for each page
make render          # WAVs + level/clip/DC/stereo figures + sequencer timing → build/host/out/wav
make video           # test/video.c: a scripted 70-second performance → build/host/out/showcase.mp4 (ffmpeg);
                     # its script is a list of timed key/pointer events — update it when pages or keys move
                     # (other modes: --fx --ans --xen --inst --shruti --touch --themes --seq, --file IMG, --doom IMG,
                     # --splash N, --card TITLE LINE SECONDS; tools/showcase.py records them all into one long video)
tools/manual.py      # the manual: README/INSTRUMENTS/NOTICE as an A4 PDF (Chromium), CREAM pictures, pixel headings
tools/site.py        # the web page (build/site, for GitHub Pages), NIGHT pictures, the loop and the showcase
make freedoom        # Freedoom 0.13.0 (BSD) → build/freedoom: the `doom` checks, `make render`'s doom-e1m1.wav
make doom-img        # build/i386/bare-doom.img: the stick image with freedoom1.wad in DOOM/ (mkimage.py --doom)
make clean
```

One check group at a time (names in `test/check.c:main`: audio, timing, stretch, keys, project, sampler, mix, tape,
tracker, fx, midi, fm, undo, usb, net, link, stick, update, install, rhythm, layers, pad, touch, colours, fxpage,
ans, xen, inst, doom, splash, ui;
`stick`/`update`/`install`/`doom` need the image argument; `doom` writes Freedoom onto it, or skips most of its checks
without `make freedoom`):

```
make build/host/check && CHECKS=usb,undo build/host/check build/host/check.img      # HOST_LOG=1 for the kernel log
```

Groups run on their own after `app_init`, but some leave state behind: run the whole suite before committing.

**Host harness (`test/`).** `test/host.c` implements `core/platform.h` for a Linux process: a simulated millisecond
clock (`host_now_ms`), scripted keys and pointer (`host_key`, `host_tap`, `host_pointer`), an XRGB memory framebuffer,
and drive 0 backed by an image file. `host_run(ms)` advances time: 1 ms audio ticks and a main-loop pass
(`app_step`) each; `plat_idle` is a millisecond and an audio tick too (Doom's waits spin on it). `host_audio_pump_ms` renders the way the HDA driver does (128-frame chunks kept 512 ahead), which is
what timing measurements need. Core objects are built with the kernel's code-generation flags (i686, no SSE/x87) so
cycle counts carry over. `test/host.mk` can also build the programs against another checkout (`HB_ROOT=… HOST_EXTRA=
-DHB_BASELINE`) for before/after measurements; the `HOST_RENDER`/`HOST_SET_ECHO` macros in `host.h` bridge the
old API. Prefer the harness for anything it can see (sound, timing, layout); it takes seconds.

**QEMU** for the real kernel (drivers, BIOS disk, boot):

```
python3 tools/qemu-test.py --keys "4:1200 wait:300 a wait:150 s shot:strum" [--seconds N] [--img] [--arch x86_64] [--cpu pentium2] [--res 1024x768] [--mem 4G] [--image stick.img]
                           [--uefi] [--usb usb-kbd,usb-hub@2,usb-tablet@2.1] [--usbmidi]
```

Key spec tokens: QEMU key names (`f2`, `spc`, `ret`), `key:MS` hold, `wait:MS`, `shot:NAME` (→ `build/<arch>/NAME.ppm`),
`abs:X,Y` / `down` / `up` for the pointer (0..32767), `mm:DX,DY` / `mb:N` relative mouse. It prints the serial log
(`build/<arch>/serial.log`) at the end and always writes `final.ppm` plus `out.wav` (16-bit stereo 44.1 kHz, RIFF sizes
are zero: skip the 44-byte header). `--img --image copy.img` twice checks save → reboot → autoload. `--uefi` boots
OVMF (with `--img`: the 64-bit kernel and our USB stack); `--usb` adds devices to the xHCI controller (`name@port`,
`2.1` = port 1 of the hub on port 2); `--usbmidi` plugs in a USB MIDI keyboard emulated over usb-redir by
`tools/usbredir_midi.py` once the guest runs (a hot-plug), `umidi:90,45,64` tokens play into it and what the guest
sends lands in `build/<arch>/umidi-out.bin`. Environment: `HB_MACHINE=,i8042=off,vmport=off` appends machine options
(no PS/2 at all), `HB_QEMU_EXTRA` adds arguments, `HB_SHOW_CMD=1` prints the command line. A QEMU left running from an
interrupted test keeps the image locked: the next one dies at start (`Connection reset by peer` on the monitor).
UI changes must be checked at 800x600 (100x37 cells, the supported minimum), 1024x768 (what old BIOSes hand Limine),
1280x800, 1920x1080 (the 12x24 font) and 3840x2160 (the 12x24 font doubled) — `make shots` does these, 1366x768
and 1280x1024.

**Emulated PCs:** `tools/qemu-matrix.py [-j N] [--only TEXT]` boots the kernel on ~80 QEMU machines (BIOS/UEFI, i440fx/
q35/isapc, USB 1.1/2/3, IDE, AHCI, NVMe, virtio, SCSI, SD, CPUs from the 486 to EPYC, HDA/AC97/SB16/none, seven VGA
models, 640x480 to 4K, 32 MB to 8 GB, USB-only input, our USB stack under UEFI with a hub and with the 32-bit kernel,
the BIOS-boot takeover) and checks boot, picture, sound and save → reset → reload (BIOS boots, and UEFI ones where our
USB driver reaches the stick; a machine's `log` lists lines its serial log must have).
Results: PASS, LIMIT (fails as expected), EMU (a traced QEMU bug), FAIL; `build/matrix/summary.txt`, `contact.png` and
a directory per machine (serial log, screen, wav). QEMU's UHCI model drops packets on long BIOS reads (Limine then
fails some boots: the matrix resets and retries) and its OHCI model dies during SeaBIOS bulk writes (saving fails and
says so) — neither is ours. Run it after changing boot, disk, audio drivers or the UI layout.

The release (`BARE_RELEASE` in `core/app.h`, "2.5") is for people and `make dist`; updates compare only the build
number. The build number (`VERSION := date +%y%m%d%H%M`) is baked into `core/app.o` behind the magic `HBVER001`; `app.o`
is relinked whenever any other object changes, and `tools/hb.py` / `tools/mkimage.py` read the number back from the ELF.
The two kernels can carry different numbers (each arch's `app.o` is rebuilt on its own), so an `.upd` (`HBUPD002`) holds
each kernel's number and each running kernel compares its own: an update can't loop. `make VERSION=…` overrides the
number (propagated to the 64-bit sub-make) — delete `build/*/core/app.o` afterwards, or later builds carry the made-up
number until something else changes.

Generated sources (regenerate rather than hand-edit): `core/font.h` from `tools/psf2c.py` (Makefile rule; it draws
the glyphs Terminus lacks — rounded corners, eighth blocks, ●, × — from the font's own line metrics), and
`core/tables.c` + `core/tables.h` from `python3 tools/gen_tables.py core` (sines, the filter's coefficients, GENDY's
six step distributions, a −ln u table for Poisson waits); `core/inst_builtin.c` from `tools/txt2c.py` (Makefile rule:
the files in `instruments/` as C strings — the built-in instruments).

## Architecture

**Layering rule:** `core/` is portable C and never includes anything from `arch/` or `boot/`. It reaches hardware only
through the functions declared in `core/platform.h` (`plat_*`), implemented in `arch/x86/platform.c` (and in
`test/host.c` for the harness). A new machine is a new `arch/<x>/` + `boot/<x>/` pair; `-I` paths select
`arch/x86/$(ARCH)` and `boot/$(ARCH)` so same-named headers (`arch.h`, `cpu.c`, `mem.c`, `isr.S`) are per-bitness.

**Constraints that shape all code:** freestanding, `-ffreestanding -mno-sse -mno-80387` — no libc beyond `core/libc.c`,
no floats anywhere, all DSP is fixed-point (Q12/Q15/Q24). No heap; `plat_alloc` is a bump allocator for never-freed
buffers (tape, the screen's back and front buffers). The i386 build must stay i686-clean (`make run-p2`). 64-bit
divisions are a slow library loop on i386: keep them out of per-sample code (32-bit products and divisions there).

**Boot path:** `boot/i386/multiboot2.S` + `entry.c` (Limine loads it on BIOS and UEFI) parses memory map / framebuffer /
boot device, starts the 1 kHz timer, PS/2, sensors, BIOS real-mode trampoline (`arch/x86/i386/bios.c`, `tramp.S`), int
13h block driver (`blkbios.c`), the USB stack (started without a BIOS, only prepared with one), the sound chip, then
calls `app_init` and `app_run` from `core/app.c`. `boot/x86_64/entry.c` uses the Limine protocol instead and always
starts the USB stack. The image and the i386 ISO carry both kernels: `limine.conf` / `limine.img.conf` list the x86_64
one first with `if_fw_type: UEFI` + `if_arch: x86-64`, so UEFI on a 64-bit CPU boots it (it reaches a framebuffer or
BARs above 4 GiB; the i386 build refuses such a framebuffer and can't reach such an xHCI) and BIOS boots the i386 one.
The Makefile builds the x86_64 kernel for the i386 image/ISO (`KERNEL64`); the updater flips the `HB_A`/`HB_B` and
`H64_A`/`H64_B` path lines. `mkimage.py` without `--kernel64` makes a stick whose UEFI boot runs the i386 kernel (the
matrix tests its USB stack that way, with OVMF told to keep BARs below 4 GiB). `timer_start` (`arch/x86/pit.c`) uses the
8254, else the HPET in legacy-replacement mode on the same IRQ 0 (UEFI-only laptops often gate the 8254), else TSC time
with `timer_poll` run from `plat_ms`/`plat_idle`; PS/2 is also polled from the main loop (`take()` only reads when the
controller has a byte). `arch/x86/earlycon.c` draws the log on the framebuffer until `app_run`, replaying what was
logged before the framebuffer was known. With no usable framebuffer (text mode, or a depth gfx.c can't write) both put a
notice on the text screen and run the instrument on a framebuffer in RAM (`arch/x86/textmode.c`): no picture, but keys
and sound. `blkbios.c` rebuilds the registers and the disk address packet for its one retry — a failed int 13h leaves
the error code in AH (AH=0Ch is "seek", which succeeds without moving data).

**Two execution contexts:**
- *Timer interrupt (1 kHz)* — `arch/x86/platform.c:audio_tick` → the sound driver's `pump` → `audio_render` in
  128-frame chunks (without a sound chip it still renders a millisecond per tick and drives `pcspk_tone` from
  `synth_mono_freq`; with one, 1-bit mode is `audio_set_onebit`: the engine writes that square wave as its output).
  Anything touched from here and from the main loop needs `plat_irq_save` when it spans several fields.
- *Main loop* — `core/app.c:app_step`: key events, `stretch_work` / `tape_work` (heavy non-realtime work),
  `personality_tick` (sensors, LEDs, thermal modulation), and every 16 ms a frame: pointer, the page's `pointer` hook,
  `ui_draw`, `text_flush`, the SCAN wave (`ui_scan`), `gfx_present`.

**Audio (`core/audio.c`)** is the graph, rendered in blocks of at most `SYNTH_BLOCK` (32) frames: sequencer and rhythm
events → voices (`synth_render` into stereo 32-bit buses) → the mixer's channels (below) → stretcher capture/pull → tape
→ stereo ping-pong echo and reverb from the channels' sends → the master inserts (`fx_master`) → master volume (2 dB
steps, −6 dB at boot, laptop volume keys and Shift+-/=) → DC removal → a look-ahead peak limiter at −3 dBFS (the output
runs 32 frames late, so the gain is down before a peak goes out; nothing overshoots) with a tanh knee from −2 dBFS as a
safety net → output and the scope rings (`audio_scope_lr`). Volume, mute and 1-bit live in the stick's tape header
(`struct tape_hdr`, "SET1"): `disk_note_settings` keeps them current in memory every frame and they are written only
with the header writes a project save or load makes — never on their own, since the stick may have been pulled. The PLAY
page's rhythm section (`core/rhythm.c`) is a second event source in that loop: eight one-bar patterns written as strings
(kick, snare, hat, and a bass lane in chord degrees), 4 steps a beat (3 for SWING), at `seq.bpm`, falling in on the
sequencer's step when both play; its auto bass follows `omni`'s chord and `rhythm.mute` silences lanes. The **sample
clock is the master clock**: the sequencer (`seq_run_events` / `seq_next_event` / `seq_advance`) counts frames with a Q16
row length, and `audio_render` splits blocks at its events, so rows, ticks and note-offs land on exact samples.

**Tracker (`core/seq.c`):** `seq_pat[32]` patterns of up to 64 rows × 8 channels of `struct seq_cell` (note, inst =
preset+1, vol = velocity+1, fx, param: all zero is an empty cell), an order list, per-channel default sound and mute.
Each row fires at `row_left_q16`; ticks 1..15 at `tick_at(t)` inside it (`SEQ_TICKS`, the last on the row
boundary). Per channel (`struct chan`): the sounding note and the effect state; pitch effects move `bend` (1/256
semitone) and `update` sends it with `synth_tag_bend` (voices multiply their increment by `bend_q16`; FM operators
too), volume with `synth_tag_velocity`, pan with `synth_tag_pan`. Voices are tagged `0x300 | channel`. Bxx/Dxx set
where the next row comes from. `steps_to_rows` turns the 1.0 step format (ties, gate %) into rows: a gate becomes an
ECx cut on the note's last row. The SEQ page scrolls by pages while following playback (a scrolling grid rewrites every
row on screen each step).

**Voices (`core/synth.c`):** 24 voices plus 4 tails (a stolen voice fades out there over ~3 ms). Each is started with a
16-bit `tag` so groups (a chord, a track) release together, and a pan (`synth_note_on_pan`, balance law: the centre
stays at full level). Per block, `voice_block` advances the envelope (`core/env.h`, shared with FM operators), pitch
envelope / vibrato / thermal detune and the filter coefficient; per sample it runs one oscillator loop per wave type
(`VOICE_LOOP`), the Chamberlin SVF, and ramps the left/right gains. Pulse waves have their DC taken out analytically;
drawn/scanned tables have their mean subtracted. Presets `P_*` in `core/synth.h`: fixed presets, then wave-bank
sources (DRAWN/MORPH/SCAN/ROM from `core/wave.c`), then FM1–4 which are user patches in `core/fm.c`. FM renders a
block per voice through an inner loop specialised per algorithm (`fm_loop` with constant masks); phase modulation
wraps in 32 bits. Operator waves are 8192-entry tables. `fm_voice_start` works out each operator's velocity and key
scaling once per note (`scale[]`, Q15); per block the patch's LFO bends the increments (on top of the tracker's
`bend_q16`), scales the carriers' gains (tremolo) and the modulation depths. The bank is saved as `FMB2`; 1.0's `FMBK`
is read through `struct fm_patch_v1`, the layout of that time.

**Sound chips (`arch/x86/drivers/sound.h`):** `sound_init` tries HDA, then AC97, then a Sound Blaster 16, and the
platform calls the one that came up through `struct sound` (`sound->pump` from the tick, `prefill` before BIOS disk
calls, `latency`, `hold`, `status`, the test tone, and HDA's jacks and inputs where a chip has them). Each keeps a ring
of 16-bit stereo ahead of its DMA. HDA also takes Intel's audio DSPs (Skylake on, class 04:01, a list of IDs in
`intel_dsp`): their BAR 0 is still an HDA controller for the analog codec. AC97 (`ac97.c`, ICH-style bus master: Intel,
nForce, AMD, SiS 7012 with SR/PICB swapped; ICH4+ get their I/O BARs switched on): 32 entries of 128 frames, LVI kept
one behind CIV so the DMA never stops, 48 kHz; line in and mic through the PCM-in box, drained in the pump. SB16
(`sb16.c`): DSP reset at 0x220-0x280, DSP ≥ 4, the 16-bit DMA channel from mixer register 0x81, auto-init over a 32 KiB
ring aligned to its size below 16 MB, 44.1 kHz with the engine's 48 kHz interpolated per frame (Q15 so the products stay
32-bit); no IRQ handler, the pump reads the DMA count and acknowledges the DSP. `qemu-test.py --sound ac97|sb16` boots
with those; QEMU's AC97 model only moves with a clocked audio backend (`--audiodev none,id=snd0` records silence), not
ALSA's null device.

**Mixer and input (`core/mix.h`, the channel loop in `core/audio.c`):** voices render into six buses by tag (`v->bus`:
0x3xx SEQ, 0x4xx RHYTHM, 0x8xx CLOUDS, 0x9xx UPIC, 0xBxxx DOOM (Doom's effects are added into that bus too), the rest
PLAY; `bus_ch[]` maps them to channels; `synth_render` returns a bit per bus that has voices, silent buses cost
nothing). Tags in use: 0x1xx the chord pad, 0x2xx strums, 0x5xx keyboards on pages (WAVE 0x500, GENDY 0x580 / its
drone 0x5FF, the sieve scale 0x5C0), 0x6xx MIDI, 0xA00-0xA7F the instruments, 0xBxxx Doom's music; tests and tools use
0x7xx. `render_block` runs each channel through `mix_block` — fader (Q12, −60…+12 dB, ramped per block), balance pan,
into the heard mix (mute/solo), the capture bus (unless muted) and the mono echo send — then: stretcher capture (the
capture bus: the channels that are not muted, dry), stretcher pull → STRETCH channel, tape records the capture bus (now
with the stretcher) and plays into the TAPE channel, echo from the sends, `SMP_OUT` tap, master. Samples are clamped to
±2^17 before the fader so products stay 32-bit. The input: the platform lists its inputs (`plat_audio_inputs`),
`mix_set_input` picks one, and the driver pushes frames from the audio interrupt (`audio_input_push`) into a 4096-frame
FIFO that the render drains after 256 frames have gathered (dropping the oldest if it falls far behind, counting
`in_xruns` when it runs dry). On HDA (`arch/x86/drivers/hda.c`): `find_inputs` pairs input pins (line in, mic, aux; jack
or built-in) with an ADC reachable through mixers/selectors (`path_to`), `configure_input` sets the path (mixers: that
input unmuted, the rest muted), the pin (IN, 80 %/50 % bias for a jack mic, +20 dB boost) and the ADC (48 kHz stereo,
stream 2); the first input stream descriptor runs only while an input is chosen, and `hda_pump` drains it before
rendering. QEMU: `tools/qemu-test.py --codec hda-micro` or `hda-duplex`; to feed the input a signal, `--audiodev
"alsa,id=snd0,in.dev=hbin,out.dev=hbout,…"` with `ALSA_CONFIG_PATH=/usr/share/alsa/alsa.conf:file.conf` defining
`hbin`/`hbout` as ALSA `file` plugins (raw 48 kHz s16 stereo in and out). The host harness has one input, LINE IN,
playing `host_input_tone`.

**Touch (`core/touch.c`, `core/page_touch.c`):** a crackle-box-like circuit simulated at 96 kHz (two steps a sample,
averaged) in 32-bit fixed point. Nodes are the eight pads: four with capacitors (IN−, C1, IN+, C2; explicit Euler,
each node's pull clamped to 0.9 a step so nothing overshoots), OUT and COMP driven by the op-amp, +9V and 0V fixed. The
op-amp: gain 2…1024 on IN+ − IN−, a soft rail (x − 4x³/27), then one pole at gain-bandwidth / gain; IN+ gets half the
output back (a trigger), so a path from OUT to IN− makes a relaxation oscillator. Fingers are conductances from pads to
the body, a node without capacitance solved each step as the conductance-weighted mean of the touched pads, the mains
(the HUM knob is the body's coupling to it) and ground; so two touched pads are joined through it. A finger's
conductance comes from its pressure (`skin_g16`, about 5 Hz to 1 kHz as a corner with a unit capacitor) times the
share of its fingertip on each pad (`touch_pad_rect`); each contact drops out at random and grows back (CRACKLE),
more often and slower for a light touch. Everything that depends on the fingers and knobs is worked out once a block
(`prepare`); `touch_render` is its own mixer channel (`CH_TOUCH`), and stops rendering 40 blocks after the last finger
lifts and the output is quiet. The page fills `touch.f[]`: slots 0-4 the touchpad's fingers, 5 the mouse, 6-13 a key
(or MIDI note) per pad, whose pressure climbs while held. `make render` writes `touch.wav`, 25 scripted seconds of it.

**Splash (`core/splash.c`, `splash_ans.c`, `splash_gendy.c`, `splash_cmi.c`, `splash_meta.c`):** after `app_init`,
one of four pieces (`struct splash_piece`: start, draw at a time, audio from a frame). `app_init` picks one before the
autoload (`splash_pick`: random, never the last one, which rides in `tape_hdr.splash` — the autoload's header write
carries it, so no extra write; `disk_note_settings` keeps it through a rescan, like the volume) and starts it at its end. Picture and sound follow one clock, the audio frames
`splash_audio` has played (it adds into the mix after the sampler's tap, before the master volume, so volume and mute
hold); `splash_draw` runs instead of `ui_draw` while `splash_showing`, then `ui_redraw_all` gives the screen back.
Any key (swallowed with its release), click, touch or MIDI note calls `splash_skip`: the picture goes at once, the
sound fades over 150 ms; a natural end also fades. The pieces draw with palette entries from `GFX_FREE_COLOR` and
their own reverb (`sp_verb`); their sound costs 200-550 cycles a frame on average (`make bench`). `splash_mode`:
`SPLASH_RANDOM` in the kernel, `SPLASH_OFF` in the host programs (a constructor in `test/host.c`), or a piece
(`build/host/video --splash N out.mp4` records one). The QEMU tools send Esc once the log says `running` and a
`splash:` line is there (`qemu-test.py --splash` lets it play). `arch/x86/earlycon.c` draws the boot log under the
ASCII name (`splash_logo`, figlet's slant, kerned); `ui_boot_message` shows it too.

**FX page (`core/page_fx.c`, `core/perf.c`):** effects played live. `perf_block` runs on everything heard (in
`render_block` after the echo and reverb returns, before `fx_master`) or on the input alone (`perf.source`, on `il/ir`
before the input's fader). A ring of the last seconds (`perf_alloc` from `plan_memory`, a power of two frames, int16
stereo, a peak a 256 frames for the page's strip) is written as it plays and held still while REPEAT loops a slice of
it. REPEAT, REVERSE and TAPE read the ring; GATE, the FILTER (a Chamberlin SVF: low-pass left of the middle, high-pass
right, off between) and CRUSH work on the signal; every effect crossfades in and out (5 ms, the tape 20 ms). DUB and
FREEZE drive the mixer's echo and reverb from `perf_work` in the main loop: `perf_echo_throw` / `perf_rev_throw` raise
every channel's send in `mix_block` (only the input's in input mode), the echo's time/feedback and the reverb's size are
set while held and put back once the tail is gone (5 s / 8 s). Lengths come from `fpb` (frames a beat: the Link tempo
or `seq.bpm`). The page: keys 1-8 hold, Shift latches, Space holds what sounds, 0 all off; the pad's first finger (or
the mouse) plays `perf.sel`, a second one the filter; MIDI notes 36-43 hold, CC 16/17 are X/Y, CC 102-109 hold
(`app_midi`). `fxpage` checks run each effect on a test signal through `perf_block`, then the keys and a DUB throw
through the app; `build/host/video --fx out.mp4` records a scripted performance (`--ans`, `--xen` the ANS and XENAKIS
pages).

**ANS page (`core/ans.c`, `core/page_ans.c`):** `ans.plate` is `ANS_ROWS` (360: five octaves of 72, from C of
`ans.octave`) × `ANS_COLS` (512) bytes, column-major, from `plat_alloc` in `audio_init`. `ans_render` (its mixer
channel `CH_ANS`) blends the two columns around the slit (Q32 position, `step_q32` from `ans_frames_per_pass`: bars of
the Link or sequencer tempo, or seconds), eases each row towards brightness² over ~4 ms, renders only rows sounding
(sine table, alternate rows a little left/right), and scales everything by 1/sqrt(Σ power) so density doesn't change
loudness (~1960 cycles a frame with all 360 sounding). Held rows (`ans_note`: the page's letter keys, MIDI notes) sound
and, while playing, are written under the slit from the audio side (`ans.dirty` columns). The camera
(`plat_camera_*`): `ans_snap` lays the next picture over the plate (max), live mode (`ans_work`) replaces it each new
frame; light above `thresh`, or edges. The page redraws only dirty plate columns and the slit's neighbourhood on a
keyed canvas, about 4 ms of the measured screen speed (`gfx_speed_mbs`) a frame, the rest the next frame from where it
stopped (a live camera dirties the whole plate each picture; a slow framebuffer would hold up the main loop and starve
the camera's USB transfers). Undo kind `U_ANS` (the whole plate); saved as `ANS1` + the plate as project media after the samples.

**Instruments from files (`core/inst.c`, `core/page_inst.c`; format in INSTRUMENTS.md):** `inst_load_all` (boot,
before the autoload; every `disk_rescan`) parses the built-in texts (`inst_builtin`) and then `INSTR/*.TXT` from the
stick's FAT (`fat_list`), up to `INSTS`; a file of the same name replaces a built-in one. `inst_parse` reads `key:
value` lines, `[sound]` `[play]` `[knobs]`, notes a mistake (the first, and a count) and keeps reading; no name, no
instrument. Each instrument's sound is a user preset (`P_INST1 + i`, appended after `P_GD4`; `synth_user_preset`):
such presets keep their own envelope, level and filter even when the wave is GENDY, FM or a sample. The PLAY page
(`page_play.c`) has `showing`: F1 again (`again`) steps through the omnichord and the instruments (`play_show`), the
tab name is a buffer, and every hook hands over to `inst_page_*` when an instrument shows (its key hook takes every
key, so nothing reaches the omnichord). Playing is declarative: the page says which notes are held (`inst_note`, by
key/pad/MIDI id) and where the strip's finger is (`inst_strip`); `inst_block` (the audio loop, after UPIC's) makes
voices: one per held note (tags 0xA00 | slot), one gliding voice for the strip or a mono instrument (0xA40; a stepped
strip strikes a new note on each key, a free one follows smoothed), or an arpeggio on the tempo's steps (0xA50, the
fractions carried so the beat holds). Hold (`inst_latch`) keeps notes after they're let go until a new set starts;
`hold: toggle` makes a key open its note and the next press close it (in `inst_note`; Space is `inst_clear`). Just
tuning (`tuned`, in `inst_note`/`inst_strip`) adds each pitch class's 5-limit offset from the tonic (`just_q8`, 1/256
semitones) to pitches on the semitone grid; free strip pitches stay as they are. Bellows: `inst_pump` (the page: Enter
held, finger 0's travel where the pad is not a strip or pads, the mouse's) adds air, `inst_block` leaks it (~2.7 s)
and every other block gives the held notes' voices a level (`synth_tag_level`, finer than velocity) and a small flat
bend at low air. SHRUTI (instruments/shruti.txt) uses all three. Knob values are saved as `INS1` (by name). The checks use `to_play()` (check.c) to reach the omnichord: F1 on PLAY
steps.

**Doom (`core/doomhost.c`, `core/doomsnd.c`, `third_party/doom`):** typing `iddqd` on any page (app.c's `iddqd`, on
BARE!'s side of `handle_key`, not while a page takes text, nor with Shift or Ctrl held) calls `doom_open`. The engine is
doomgeneric's Chocolate Doom (GPL-2.0-or-later; `third_party/doom/README.md` lists its changed lines, each marked
`BARE!` under `BARE_DOOM`); `third_party/doom/bare/` is its platform layer (`i_bare.c`) and C library (`libc.c`,
headers in `bare/libc/`, functions renamed `doom_*`), and asks BARE! for everything through `core/doomhost.h` (named
so: the engine has a `doom.h` of its own). Doom's objects are built without floating point like the rest, without
`-g` (the kernel slots are 4 MiB) and without `-fdata-sections`; `llvm-objcopy` renames their `.data`/`.bss` to
`doom_data`/`doom_bss`, which the linker scripts place before `_kernel_end`. `doom_reserve` (from `plan_memory`,
before the tape: avail/6 up to 64 MB, where 96 MB are free) takes the pool and a copy of `doom_data`; each fresh
start puts the variables back and zeroes `doom_bss`, so after an `I_Error` (`doom_fatal`: `__builtin_longjmp` to
`doom_open`/`doom_step`) the next `iddqd` starts clean. The pool is shared out at the start: the C library's heap
(2 MB), the WAD (read whole with `fat_seq`, a progress line on `ui_boot_message`; the engine's WAD class maps it, so
lumps are used in place), a song's events (1 MB), the zone (the rest, 6-32 MB). The WAD: DOOM/ then the root, known
names first (`fat_find` reads long names: freedoom1.wad), then any IWAD. Files Doom writes are kept in memory until
the tic ends (`doom_libc_flush`): a save written as temp.dsg and renamed reaches the stick only as DOOMSAV0.DSG. While
it shows, `app_step` runs `app_background` (keys, the background work) and `doom_step` instead of the UI: a tic when
`I_GetTime` moves on (35 a second), each frame scaled to a 4:3 box (as large as the screen, the framebuffer's measured
speed at 25 fps and ~2 MP allow) with Doom's 256 colours as the palette (BARE!'s saved and put back). Its waits
(`I_Sleep`, the screen melt) run `app_background`; a page key pressed then only asks (`leave_asked`). The clock
(`doom_clock_ms`) stops while it is left; coming back opens its menu (`doom_engine_resume`). Keys: the F keys leave for
their page, the volume keys stay BARE!'s, everything else is queued for Doom (a key held from before goes back to
BARE!'s side on release, and a key pressed in Doom is swallowed when let go after leaving). Sound (`core/doomsnd.c`):
effects are the WAD's DMX samples, interpolated, panned as Chocolate Doom does, added into the DOOM bus from
`render_block`; music (MUS, or MIDI files as in Freedoom) becomes MIDI-style events in tracks merged as they play
(`doomsnd_block` per audio block, 64-bit tick position, tempo changes), each note a voice tagged `0xB000 | ch << 7 |
note` on `BUS_DOOM` (CH_DOOM, the last mixer channel; the MIX page shows it once `doom_started`), GM programs mapped to
presets (29/30 GRIND), drums to KICK/SNARE/HAT. The `doom` check group plays it through (start, an error and a fresh
start, a level, a save, leaving and coming back, Quit, loading the save); `build/host/video --doom IMAGE out.mp4`.

**XENAKIS page (`core/page_xen.c`, views in `core/xen.h`: `page_upic.c`, `page_gendy.c`, `page_cloud.c`,
`page_sieve.c`):** one page, four views; the page dispatches keys, pointer, drawing and MIDI to the view showing and
draws the view bar (row 1). `xen_current`/`xen_goto` for tests and scripts (key taps are queued, `xen_goto` is
immediate: run the app between them). On CLOUDS the digits 1-4 and 0 come before the keyboard's upper row, whose
2 3 5 6 7 9 0 are black keys elsewhere.
- GENDY (`core/gendy.c`): a period is a polygon of up to 16 breakpoints; after each period every height takes a
  second-order step (the step walks too, `inertia`) and every segment length a step, from one of six distributions
  (`gendy_dist_q15`), mirrored at the patch's walls. Lengths are shares of the period (edges as shares of 2^32, one
  32-bit division a period, a reciprocal a segment), so the note is in tune unless `drift` walks the period too. Each
  period's mean is taken out and the output scaled by ¾. Voices: `WAVE_GENDY`, presets `P_GD1-4` (appended) with the
  patch's envelope, level and filter; `struct gendy_osc` in each voice. The view runs its own oscillator for the
  picture (30 fps, a keyed canvas).
- Sieves (`core/sieve.c`): `m@r`, `|`, `&`, `-`, parentheses by recursive descent into a postfix program, swapped in
  under `plat_irq_save` (the rhythm section reads it from the audio side); bounded `sieve_next`/`sieve_nearest`;
  `sieve_snap_pitch` reads members in steps of 1/unit semitone. `sieve_changes` counts compiles (the PLAY page's step
  grid keys on it). The rhythm section's pattern `RHYTHM_SIEVE` evaluates S1-S4 at `steps_done` (from the start).
- Clouds (`core/cloud.c`): `cloud_block` before each audio block (in `audio_render`, after the sequencer and rhythm
  events): Poisson waits (`neglog_q8`) or, on a rhythm sieve, a Poisson count on the sieve's sixteenths (the rhythm
  section's while it plays, else its own count at the tempo); `cloud_draw` gives a note's pitch, length, velocity, pan
  and glide (shared with the UPIC writer). At most `CLOUD_NOTES` sound; bends every other block. `cloud_marks` (a ring
  of the last 256) feed the view's sweeping score, which only draws near its head.
- UPIC (`core/upic.c`): `upic.d` holds arcs and points (each arc's points contiguous, in time order; the page is
  2^16 across, pitch in 1/256 semitones); `upic_block` moves the cursor (or takes the hand's `scrub`), starts a voice
  for each arc it enters (tag 0x900 | arc), bends it every other block (a cached segment, one division), lets go
  when it leaves. Edits hold interrupts; deleting (indices shift) and mirroring let every voice go first. The view
  buffers each finger's (and the mouse's) stroke and adds it as an arc when let go; snapping inserts a step before
  each new pitch. The picture redraws bands around the old and new cursor (clipped), whole only on `upic.changes`.

**USB video (`arch/x86/drivers/usbuvc.c`, `core/usbclass.c`'s `uvc_*`):** descriptors (the VideoControl interface
claims the camera; of the streaming interfaces its header lists, the first with YUY2/NV12 frames — a webcam that is two
cameras, picture and infrared, has a VideoControl each; MJPEG, H.264 and greys skipped), `uvc_pick` (closest to 320
wide), the probe/commit (26/34/48 bytes by UVC version), payload assembly (FID/EOF/ERR, double-buffered) and
brightness are host-checked (`usb` group). The driver copies the streaming interface's isochronous alternate settings
at probe (the enumerator's buffer is reused), streams only on `plat_camera_on(true)`: probe, commit, the smallest
setting carrying `dwMaxPayloadTransferSize`, `usb_add_ep` (its `ring_n`: as many transfers as `USB_CAMERA_DMA` holds,
at most `USB_ISO_SLOTS` = 255, one 4 KiB ring, 32 ms at one a microframe) + `xhci_configure`, SET_INTERFACE,
`xhci_iso_start` with the driver's `struct usb_iso` into `USB_CAMERA_DMA` (reserved in `usb_prepare`, taken once);
`poll` takes finished transfers in order and refills. xHCI isochronous (`xhci_iso_*`): slot k is TRB k of the ring, each TRB a
whole TD with SIA/TLBPC/TBC, no retries (CErr 0), high-speed transactions a microframe in Max Burst; completions record
lengths, skipped slots count as empty; the event ring is 1024. Bulk-streaming cameras aren't supported yet.
`tools/usbredir_uvc.py` (`qemu-test.py --camera`) is a high-speed UVC 1.1 YUY2 webcam over usb-redir; the LOG view's
`C` streams it. The host harness has a 160x120 camera (`host_camera_present`).

**Effects (`core/fx.c`):** the reverb is a send like the echo (`rsend`, filled by `mix_block` from each channel's
`reverb`): two diffusing allpasses into four delay lines mixed by a 4×4 Hadamard with a one-pole low-pass in each loop,
left = lines 0+2, right = 1+3; it skips its work once the send and the tail are silent. `fx_master` runs after the echo
and reverb returns, before the `SMP_OUT` tap and the master volume: the voices' Chamberlin SVF on the mix (states
clamped to ±65535), a cubic soft clip after up to ×8 gain, then bit masking and sample-and-hold. The echo's time is
`fx_echo_div_q4` sixteenths of a beat and its feedback `fx.echo_feedback` (37 = the old fixed 0.37).

**MIDI (`core/midi.c`, `arch/x86/midiport.c`):** the platform collects incoming bytes (x86: from the 1 kHz tick, so a
16-byte UART FIFO can't overflow) and takes outgoing ones as fast as the port accepts (`plat_midi_write` returns how
many). `midi_work` (main loop) parses — running status, sysex skipped, realtime bytes anywhere — handles clock/start/
stop itself (tempo from the last 24 clocks), queues channel messages for `app.c:app_midi` (notes on voice tags
0x600 | note playing the page's `strum_sound`, or a page's `midi` hook: the tracker's step entry), and drains an out
ring that the audio interrupt fills: the tracker's `chan_off`/note-ons, `midi_clock_run` counting 24 clocks a beat on
the sample clock, `midi_transport` from `seq_play`. Serial ports run at 38400 (115200/3; a 16550 can't make 31250);
opening COM1 for MIDI switches the log off it (`serial_log`). The port, channel and switches ride in the stick's
settings header (`midi_lo`/`midi_hi`). QEMU: `tools/qemu-test.py --midi` makes COM2 a pair of FIFOs; `midi:90,45,64`
tokens send bytes, and what the guest sent is in `build/<arch>/midi-out.bin`.

**Sampler (`core/sampler.c`):** eight mono slots of equal size carved from one `plat_alloc` pool. `app.c:plan_memory`
shares out RAM once at boot (`plat_alloc_avail`, after the screen buffers): a quarter to the sampler (capped at
64 MB), then the tape; the harness plays a smaller PC with `HOST_MEM=<MiB>` and `make test` runs the checks twice,
the second time as a 32 MB machine. A slot is 16-bit or 8-bit (`int8_t`, held without interpolation on playback)
at the output rate × 1, 2/3, 1/2, 1/3 or 1/6. Recording is a tap in `audio.c:render_block` (`sampler_tap`: SMP_OUT
after the echo, before the master volume; SMP_FREEZE builds the stretcher's pull into its own buffers while
listening) that averages frames down to the slot's rate; the main loop finishes it (`sampler_work`: trim trailing
silence, normalise). `sampler_grab` copies `stretch_phrase` (the region a freeze would take) out of the held
capture ring; `sampler_to_stretch` fills the ring from a slot and `stretch_freeze_region`s it. Every edit brackets
itself with `busy` + `synth_kill_preset`, since voices read the frames from the interrupt. Voices: `WAVE_SAMPLE`,
presets `P_SMP1..8` appended after FM4 (saved projects keep their preset numbers); `smul` turns the modulated phase
increment into frames per output frame with one 32×32→64 multiply per block. Projects save each slot's settings in
an `SMPL` chunk and its frames after the blob, sector-aligned from `MEDIA_SECTOR` in the 1 MiB slot, written and read
straight from the sampler's memory (`project_media`); what doesn't fit is saved without frames.

**Tape (`core/tape.c`):** 8 mono tracks on a 30-minute timeline (`TAPE_SPANS` spans of `TAPE_BLOCK` = 32768 frames).
A track's `map` gives the block under each span, `TAPE_NO_BLOCK` for silence; blocks come from one pool sized by
`plan_memory` (everything the sampler and the reserve leave) and are popped from `free_stack` as recording reaches a
span (in the interrupt: `take_block` under `plat_irq_save`). Freed blocks (erase) go to `dirty` and are cleared in
`tape_work` before they return, so a block taken while recording needs no clearing. Each block keeps a peak per 512
frames (`peaks`, updated by `put` as frames are written, recomputed by `repeak` after edits), which is what
`tape_peak`/`tape_peak_coarse` read to draw lanes and the overview without touching frames. Recording writes each
armed track from the capture bus (MIX) or the post-fader input (IN) at the head minus `plat_audio_latency` (plus
`audio_input_latency` for IN), taken when a take starts, so overdubs line up with what was heard; inside a loop the
write position wraps with it. The TAPE page caches per-column peaks per lane and redraws only the columns the
playhead leaves and reaches (and, for a track being recorded, the columns near the head); column maths is done with
per-frame steps, never a 64-bit division per pixel (a library loop on i386).

**Undo (`core/undo.c`):** before an edit a page calls `undo_one(kind, index, what, now)` (or `undo_begin` / `undo_save`
… / `undo_end` for an action touching several things): the thing's bytes are copied into an arena sized by
`plan_memory` (avail/16, 1–64 MB). Records of an action share a group; `undo_begin` on the same thing within a second
continues the last action (pen strokes, held keys). Undoing a group first records the present state as a redo group,
then restores in reverse (the earliest save of a thing wins). Kinds: a pattern, the song (order list, tempo, channels),
a wave slot, an FM patch, a sample with its frames (`U_SAMPLE`) or just its settings (`U_SMETA`), a tape block
(index = track << 12 | span, with the track's `used`; size 0 = there was no block), the ANS plate, a GENDY patch, a
sieve (its text and unit, compiled again on restore), a cloud's settings (whether it plays is kept), UPIC's page (what
it uses; restored with interrupts held). A tape take is one action:
`tape_take_begin` saves the spans at the head and `tape_work` keeps saving a span ahead from the main loop, so the
audio interrupt never copies. When the arena can't hold an action at all, the action is dropped and logged.

**Stretcher (`core/stretch.c`):** paulstretch with block-floating-point FFTs (each stage scales by 0–2 bits as the data
needs, products stay 32-bit), a half-size complex FFT for the real input, and one inverse FFT that yields left and
right from two independently randomised spectra (`Z = L + iR`). Freezing (`stretch_phrase`) walks back through the 10 s
capture to the last sound and the 0.4 s of quiet before it; the read window loops through that region.

**Screen:** `core/gfx.c` keeps an 8-bit indexed back buffer the size of the screen and a copy of what the framebuffer
shows; `gfx_present` compares the chunks marked dirty (32-pixel pieces of rows, a bitmap per row) and writes only the
pixels that differ, in 32/24/16 bpp. Framebuffer memory is often uncached, so pixels written is what the UI costs —
`gfx_px_written` counts them and `make shots` reports KB/s. The first whole frame after `gfx_init` is timed (`gfx_speed_mbs`, logged as "gfx: a whole frame … MB/s" and shown in the LOG view): it tells an uncached framebuffer from a write-combined one on real machines. The palette is 16 UI colours (`C_*`) plus 16-step ramps
(`ramp(R_GREEN, level)`) from the background to each accent, 144 entries, all set by the colour scheme (`themes[]` in
`gfx.c`, ten of them, `gfx_theme`, `Shift+H`, saved as `tape_hdr.theme`). The `C_*` names are roles, not hues: C_AMBER
is the first accent, C_BLACK the ink for text on an accent's fill (light in PAPER, LCD, CREAM). Entries from
`GFX_FREE_COLOR` (144) up are for pictures (`gfx_color`); a palette change needs `gfx_repaint` (the next present writes
every pixel). `colour_checks` holds each scheme to WCAG contrast minimums for the pairs the UI draws; `make
theme-shots` renders every page in every scheme with contact sheets (`tools/theme_sheets.py`). The pointer is a sprite composited in `gfx_present`.
`cell_font` in `core/app.c` picks the cell font: 12x24 only where it still leaves 128x40 cells (so 1280x1024 gets
8x16 and the full layout), and on 4K-class screens the fonts doubled (`text_font_doubled`) with the pointer, so pages
keep their 1080p proportions; code that sizes things from `text_font()` scales with it.
`core/text.c` is the cell grid on top: pages redraw the whole grid every frame (immediate mode) and `text_flush`
rasterises only changed cells. `text_gfx` hands a rectangle of cells to graphics for the frame (the grid leaves those
pixels alone) — that is how pages mix text and pixels.

**Pages and input:** a page is one file, `core/page_<name>.c`, exporting a `struct page` (`core/ui.h`): name and key,
`key_event` (return true when the page used the key), optional `typing` (text entry: global keys step aside),
`pointer`, `strum_sound` (what the strum plate plays there) and `draw`. `ui_pages[]` in `core/ui.c` is the tab order
(F1…F12, Ctrl+1…9, Ctrl+0, Ctrl+-, Ctrl+=; a page's own key pressed on it calls its optional `again`, which the XENAKIS
page uses to step views). `core/app.c:handle_key`: the F keys (or Ctrl+digit) switch pages and do nothing else; Shift+key is
the function layer (`shift_function`: sounds, 1-bit, echo, freeze, thermal, volume, mute, `⇧?` help — `ui_help`
replaces the page, so closing it redraws like a page switch); Shift+keys it doesn't take reach the page, which can read
`ui_shift` (SEQ: ⇧1-3 demos, checked before its note keys, where 2 3 5… are sharps). A key whose press was taken has
its release swallowed. Then the page, then — on pages with `plays_omni` — `omni_key` for whatever the page didn't use.
Key codes are portable (`core/keys.h`; laptop volume keys are `KEY_VOLUP`/`KEY_VOLDOWN`/`KEY_MUTE`). The ⇧ glyph is
drawn by `tools/psf2c.py` (code page 0xDE). The host harness presses Shift+key with `host_shift_tap`; tests that also
build against the baseline use `HOST_DEMO`/`HOST_FREEZE`/`HOST_KEY_*` from `test/host.h`.
The WAVE page (`core/page_wave.c`) is four sub-pages in CMI style (D 3D stack, 5 harmonics, 6 draw, 8 sampler); it
does not play the omnichord: its bottom letter rows are a chromatic keyboard (`kb[]`), and it owns the touchpad as a
pen — `pointer()` maps the pad onto the whole page area and treats a finger like the mouse button. The sample
overview is cached per column and only the columns playheads leave and reach are redrawn (`ov_column`).

**Drawing a page** (`core/ui.h`): `ui_panel`, `LEGEND`/`FOOTER` (keycap legends that wrap and drop items — use them for
key help, never a raw `text_str`), `ui_label`, `ui_led`, `ui_bar` (a pixel slider). Pixel pictures go on canvases:
`ui_canvas` (cleared every frame), `ui_canvas_keyed` (redrawn only when a hash of what it shows changes — hash
everything the picture depends on, `ui_hash`), and the self-updating `ui_scope` / `ui_stereo`, which keep their pixels
and only replace the columns/lines that changed, at 30 fps. Layout must work from 100x37 cells up.

**Persistence:** `core/project.c` serialises state as tagged chunks: `HBPJ` + version byte, `OMNI`, `SONG` and a `PATN`
per pattern (the tracker), `WAVE`, `STRC`, `FMB2`, `GDY1` (GENDY patches), `SIEV` (the sieves' text and unit), `CLD1`
(the clouds, saved off), `TAP8`, `RHYT`, `MIX2`, `TUCH`, `MFX1`, `SMPL` per sample, `ANS1` (the plate's settings; the
plate itself is media after the samples), `UPC1` (UPIC's length and counts; its arcs and points are media after the
plate, checked by `upic_loaded` from `project_loaded` once read), `TAPB`
(the tape's blocks), `END `. 1.0's `SEQ `, `FMBK` and `TAPE` (4 tracks), and 2.1's `MIXR` (six channels, arrays per
field; `MIX2` is a count and a record per channel, so channels can be added) are still read; `MFX1` keeps the first six
channels' reverb sends, `MIX2` has them all. Mixer channels are only ever appended (`CH_DOOM` is last). Loaders
tolerate missing/short chunks and clamp every value (a slot can hold anything); new fields go in new chunks, so old
projects keep loading. Version 2 changed FM algorithm 3's routing; loading a version 1 project migrates it (op 3 level
0). `core/disk.c` finds the stick's FAT boot partition and the raw `HBTAPE01` partition of 1 MiB project slots
(`HBPROJ01` headers), and implements the A/B kernel update: both kernels into their inactive slots (`HB_A`/`HB_B`,
`H64_A`/`H64_B`, 4 MiB files), CRCs, then the fixed-size `limine.conf` rewritten in place (`slot_index` finds each path
line's letter), reboot; a 1.0 stick's single `HB64.ELF` gets an `H64_B.ELF` made with `fat_create` and its path line
rewritten. At boot `grow_partition` stretches the tape partition's MBR entry to the end of the stick (only when it is
the last partition; never smaller; verified by reading back). A slot holds the header, the blob (≤ `PROJECT_MAX`, 128
KiB) and frames from `MEDIA_SECTOR`; frames past the slot's megabyte go to the slot's extent (`ext_start`,
`ext_sectors`, header version 3) found by `find_extent` among the other slots' extents after the slot area. The media —
samples, then the tape's 64 KiB blocks listed in a `TAPB` chunk — are enumerated by `project_media_get` and moved
straight between memory and the stick (`media_io`, split where the slot area ends). If no extent fits, the project is
saved without the tape (`disk.tape_left_out`). `core/fat.c` is a small FAT32 reader/writer: `fat_create` chains the
first free clusters in every FAT copy, writes an 8.3 entry and keeps FSInfo's free count exact; `fat_seq` moves a file
sequentially with a cursor (runs of adjacent clusters per call, a one-sector FAT cache). `core/song.c` exports the song
(the platform's output held with `plat_audio_hold` while the main loop renders 32 KiB pieces into the file) and imports
PCM WAVs through a streaming decoder and linear resampler. `tools/fat32.py` builds the same layout host-side for
`mkimage.py`; `make test` runs against the image grown by 64 MiB, so the partition growth, extents, exports and updates
are checked. Block I/O is BIOS int 13h on a BIOS boot (`arch/x86/i386/blkbios.c`), and our USB stack's sticks on UEFI
boots and after a takeover (`usb_blk_*`).

**USB (`arch/x86/drivers/xhci.c`, `usb.c`, `usbhub.c`, `usbhid.c`, `usbmsc.c`, `usbmidi.c`; `core/usbclass.c`):** our
own stack for xHCI controllers, polled from the main loop (`usb_service`, called from `plat_key_poll` /
`plat_pointer_poll` / the MIDI read, at most once a millisecond) — no interrupts. It starts at boot where there is no
BIOS (`usb_init`: the x86_64 kernel, the i386 one under UEFI); on a BIOS boot `usb_prepare` only sets memory aside and
`plat_usb_takeover` (`U` in the MIDI view) starts it later, after which the block driver routes to USB. All DMA memory
is one piece taken at boot (`usb_mem` for scratchpads and the stick's 64 KiB-aligned buffer, a pool of 4 KiB blocks via
`usb_dma` for contexts and rings): later, `plan_memory` has given everything to the instrument. `xhci.c`: BIOS
handoff (USBLEGSUP), Intel 7–9 series port routing from EHCI (XUSB2PR), reset, one command ring, one event ring,
64-TRB transfer rings; every transfer is a single TRB (callers keep buffers inside 64 KiB), control transfers are
setup/data/status; `xhci_wait` spins on the event ring (calling `timer_poll`, so polled-timer machines keep their
audio) and cancels on timeout; `xhci_recover` resets a halted endpoint, or stops and drops/adds one that isn't (the
only way to reset its data toggle). `usb.c`: root ports scanned every 100 ms (connect/disconnect; a new connection
settles 100 ms before its reset, as on hub ports, and a device that fails is retried 0.3 s and 1 s later with 100 ms of
reset recovery), enumeration (address, descriptors, product string), each alternate-0 interface offered to the drivers'
`probe` (which claim endpoints with `usb_add_ep`), `SET_CONFIGURATION`, `xhci_configure`, then the drivers' `start`.
Hubs (2.0 and 3.x) set the slot's hub fields, power their ports and watch the status-change endpoint; devices behind
them get a route string and, at full/low speed behind a high-speed hub, its TT. HID: boot protocol for keyboards and
mice, the report descriptor for everything else (absolute X/Y as the vmmouse-style pointer, buttons, pen tip, consumer
volume keys); keys and pointer events go into the PS/2 driver's queues (`ps2_inject_*`). Storage: bulk-only transport,
SCSI READ(10)/WRITE(10) in 64 KiB pieces, sense-driven retries; drive numbers stay put until a rescan. MIDI: class-
compliant USB-MIDI 1.0 on bulk (or interrupt) endpoints; `core/usbclass.c` packs and unpacks the 4-byte packets and
parses HID reports — the host checks run it. QEMU has no USB MIDI device, hence `tools/usbredir_midi.py`.

**Fingers:** a touch `pointer_event` names its finger slot (0..4, kept while that finger stays down), pressure `z` (≥ 30
is down; 60 when the pad can't tell) and contact `size`. Precision touchpads (`core/usbclass.c:touch_report`): each
Finger collection's tip, confidence (no confidence = a palm = lifted), contact ID, X/Y, optional pressure/width/height;
a frame is one report or, in hybrid mode, several (the first carries the contact count); contact IDs map to slots, and
a contact missing from a complete frame is lifted. Synaptics (`core/synaptics.c`, fed by `ps2.c`): W mode packets,
plus advanced gesture mode where the pad has it (ext caps 0x0C bit 0x080000 or an image sensor 0x000800; enabled with
the sliced command 0x03 + Set Rate 0xC8, as Linux does): a w = 2 packet carries the second finger at half resolution,
and the two positions keep their slots by nearness. Both are plain arithmetic, checked on the host (`usb` group). The
core keeps `pad.f[FINGERS]` (`core/ui.h`); finger 0 is also `pad.x/y/z/on` and moves the pointer. PLAY strums per
finger (`to_string`: every arrival plucks, a string lets go when its last finger leaves).

**Install (`core/install.c`, the FILE page's INSTALL view):** copies sectors 1…end of the FAT partition from the boot
drive (MBR boot code, Limine's BIOS stage 2 in the gap, the FAT), then the project partition's used part (header, the
slots, the furthest extent), a piece (≤ 2 MiB, ~25 ms) per `install_work` from `app_step`; then the header's
`part_sectors`, zeros over the last 34 sectors (a stale backup GPT), and last the MBR: the stick's, the 0x7F partition
to the end (≤ 2 TiB), a new disk signature at 440; read back, the FAT mounted. `plat_boot_disk_id` (Limine's
executable-file request, `mbr_disk_id`; 0 on the i386 paths, where a BIOS boot lists the boot drive first) makes
`disk_init` try the disk the machine started from first. Host checks: `test/host.c` backs several drives
(`host_disk_add`), `host_boot_id` plays the loader; `install_checks` installs onto a GPT'd image and boots "from" it.
`qemu-test.py --disk nvme:FILE` (ahci, ide, nvme4k) attaches internal disks, after the stick in the boot order, or
alone (started from them): install with the stick, then boot without it.

**Link (`core/link.c`):** Ableton Link's protocol, written from its source (github.com/Ableton/link: discovery/v1,
link/v1, Sessions, Measurement). Discovery: `_asdp_v`+1, type (1 alive, 2 response, 3 bye-bye), TTL 5, group 0, the
8-byte node id, then payload entries (4-byte key, 4-byte size, big-endian): `tmln` (µs a beat, beat origin in µbeats,
time origin in ghost µs), `sess`, `stst` (a byte, µbeats, ghost µs), `mep4` (our IP, port 20809). Alive every 250 ms
from port 20808 to 224.76.78.75:20808 (at once, ≥ 50 ms apart, after a change); an alive is answered to where it came
from; peers expire after the TTL. Measurement: `_link_v`+1, ping (`__ht`, then `_pgt` echoing the last pong's ghost
time) / pong (`sess`, `__gt`, the ping's payload back); two samples a round trip, 101 of them, the median is the offset
(ghost = host + intercept). Sessions: another session is measured (not again within 30 s) and joined if its clock is
over 0.5 s ahead, or within that and its id sorts lower; within a session a timeline with a higher beat origin wins,
start/stop by the later timestamp; our own session is remeasured every 30 s. Our tempo changes (`seq.bpm` moved
here) make a timeline continuous at now with beat origin > the old; the session's tempo reaches the engine as
`seq_link_q16` (BPM × 65536, fractional; the sequencer and rhythm derive their row/step lengths from it). Starts:
`seq_play`/`rhythm_play` ask `link_start_request`/`link_rhythm_request`: with others, armed for the next bar (4 beats,
the rhythm's pattern length); alone, the grid is moved to now. `link_audio_block` (per block in `audio_render`, with
the host time the block will be heard: `plat_us` + `plat_audio_latency`) fires armed starts on the exact frame
(`seq_start_in`) and holds the phase: the position in the bar against the session's beat, corrected by nudging the
next row/step (`seq_nudge`: at most 6 % of a block near the beat, 25 % when far off); the log prints the error every
2 s. `plat_us`: the 1 kHz tick plus the TSC since it. Tested against Ableton's library (a small peer on it, in a user
network namespace with a tap: `unshare -Urn`, `ip tuntap add tap0 mode tap`, a route for 224.0.0.0/4, QEMU with
`-netdev tap`), and two QEMUs bridged; host checks (`link` group) play a peer byte for byte.

**Network (`core/net.c`; `arch/x86/drivers/netdev.c`, `e1000.c`, `usbnet.c`):** the stack is portable and polled from
`app_step` (`net_work`): ARP (a 16-entry cache; up to 4 frames wait for an answer, asked again each second, dropped
after three), IPv4 (no options but IGMP's Router Alert, no fragments), ICMP echo replies, UDP (`net_listen`,
`net_send`; checksums both ways), IGMPv2 reports for `net_join`ed groups (on join, every minute, on a query), DHCP
(DISCOVER three times over six seconds, REQUEST, renew at half the lease) and, without an answer, link-local:
169.254.x.y from a hash of the MAC, three ARP probes a second apart (a conflict picks the next), two announcements,
then a DHCP try every 30 s. The platform side is whole Ethernet frames (`plat_net_*`): `netdev.c` keeps the ports
drivers add and uses the first with a link. `e1000.c`: legacy descriptors (32 RX, 16 TX, 2 KiB buffers), the MAC from
RAL0/RAH0 (EERD otherwise), MPE for multicast; PCH parts (82577 … I219) get LANPHYPC toggled first (the PHY out of
ultra-low-power, as Linux's `e1000_toggle_lanphypc_pch_lpt`). `usbnet.c`: CDC-ECM and CDC-NCM (NTB16 out with one
datagram, NTBs in parsed; input size set to 2 KiB); the data interface's alternate setting 1 comes from the whole
configuration descriptor (`struct usb_iface.cfg`), and `usb.c` tries a device's other configurations when nothing binds
in the first (RTL8153-style adapters: vendor first, CDC second). A transfer that would end on a whole packet gets a
byte more (no ZLP needed). Host checks (`net` group) run the stack against `host_net_*` (a simulated port): DHCP, ARP,
ping, UDP, multicast, link-local with a conflict. QEMU: `-netdev user` (slirp) has a DHCP server; matrix machines
`net-e1000`, `uefi-net-e1000e`, `uefi-usb-ethernet` (QEMU's usb-net, ECM in its second configuration).

**Internal disks (`arch/x86/drivers/blkdev.c`, `ahci.c`, `nvme.c`, `vmd.c`):** only started without BIOS disk services
(`internal_disks_init` from the UEFI boot paths; a BIOS boot reaches internal disks through int 13h, and two drivers
must never share a controller). Each driver adds `struct blkdev`s; `plat_blk_*` list them after the USB sticks. All
polled, through 64 KiB bounce buffers taken at boot. AHCI: BIOS/OS handoff, AHCI mode, each port stopped and moved to
our command list / FIS area, COMRESET when there's no link, IDENTIFY (LBA48 only, 512-byte logical sectors), READ/WRITE
DMA EXT in slot 0. NVMe: reset, admin queues, IDENTIFY controller (MDTS) and the first namespace (NSID 1, else the
active list), one I/O queue pair, PRP lists over the bounce pages; a 4 KiB namespace is read around the 512-byte view
(partial blocks read first on writes). VMD (client IDs 9A0B, 467F …): configuration space at BAR 0, 1 MiB a bus from
bus 0/128/224 (VMCAP 0x40 / VMCONFIG 0x44), devices in its windows at the firmware's addresses (the "SHDW" vendor
capability gives an offset under a hypervisor); the NVMe behind it goes to `nvme_add` with a config-space accessor
(`struct nvme_fn`). If the firmware left a port without a bus number or a drive without an address, they are given
one. Not testable in QEMU (no VMD model): the log lists every function found behind it. Matrix machines
`uefi-sata-ahci`, `uefi-nvme` and the 32-bit kernel's versions boot the image from those disks and save/reload;
QEMU's `nvme,logical_block_size=4096` checks the 4 KiB path (booted from the ISO: OVMF can't boot a 512-byte-sector
layout from a 4K disk).

**ACPI and I2C touchpads (`arch/x86/acpi.c`, `arch/x86/drivers/i2c.c`, `i2chid.c`):** `acpi_init` finds the tables (the
RSDP from Limine on x86_64 — physical from base revision 3 —, multiboot2's tags 14/15 on i386, else the BIOS areas)
and maps them cached (`phys_map`). Nothing interprets AML: `acpi_i2c_devices` finds `Device` blocks (`0x5B 0x82`, a
PkgLength, a name) and the I2cSerialBus descriptors (`0x8E`) inside the literal buffers resource templates compile to,
then each owner's `_HID`, whether PNP0C50 (HID over I2C) appears in it, and the controller its descriptor names —
resolved to a PCI function by that device's `_ADR`, or a fixed address by a Memory32Fixed (AMD). `i2c.c` drives
Synopsys DesignWare controllers (Intel LPSS functions, class 0C80, their private registers at BAR+0x200: capabilities
0x2FC, resets 0x204), polled, timed for a 216 MHz input clock so slower chipsets just run the bus slower. UEFI firmware
may leave such a controller with an empty BAR (and in D3): `pci_place_bar` gives it an address between the top of DRAM
(Intel's TOLUD, else the memory map) and ECAM, as high as it fits clear of bus 0's BARs and bridge windows, the boot
memory map (`pmm_note`/`pmm_claimed`; reserved ranges ≥ 256 KiB ignored, as Linux does) and every Memory32Fixed in the
tables (`acpi_fixed_mmio`). QEMU's `-device edu` is a handy device to try it on: clear its BAR 0, place it, and its
register 0 reads 0x010000ed. `i2chid.c` (only when PS/2 found no Synaptics pad): the HID descriptor from registers
0x0001/0x0020/0x0000/0x0002 (the real one is in a `_DSM` we can't run), SET_POWER and RESET, the report descriptor
through `hid_parse`, Input Mode = 3 for a precision touchpad (`hid_mode_report`, SET_REPORT feature), then a plain read
of the input report every 8 ms from `plat_pointer_poll` (no interrupt line); touchpad fields become `touch` pointer
events like Synaptics ones. It logs every step and the first reports' bytes — on a real machine a photo of the log view
is all there is to go on. QEMU has no DesignWare I2C and no I2C-HID device; `tools/acpi_touchpad.py` makes an SSDT
declaring an ELAN touchpad the way firmware does, to check the ACPI reading (`HB_QEMU_EXTRA="-acpitable
file=build/touchpad.aml"`).

**Debugging:** `logf` in `core/log.h` writes to the serial port (QEMU `-serial stdio` / `serial.log`; `HOST_LOG=1` for
the harness). The title bar's `dsp NN%` is the share of CPU spent in the audio interrupt (`plat_audio_load`). On
machines without a serial port: `logf` also keeps the last 16 KB in memory (`core/log.c`), and the FILE page's LOG view
(Tab past SONGS and MIDI) shows it under a live line about the sound device (controller, codec, DMA moving, each output
pin's control, EAPD, amp and jack, timer rate against the CMOS clock); there T plays a 440 Hz tone straight into the
sound chip's ring, P turns every HDA output on regardless of the jacks, and W writes a hardware report to the stick's
FAT root (`page_file.c:write_report`: HB-LOG.TXT, then the platform's `plat_report_file`/`plat_report_piece` —
`arch/x86/report.c`: HB-PCI.TXT and HB-ACPI.BIN, the tables back to back); `tools/fatcat.py image NAME` reads such a
file out of an image, and a real stick mounts on the desktop. `bios_call` restores the PIT if the firmware changed it,
and logs that.

## Repo notes

- Work happens on the `revamp` branch (1.0 to 2.5); `master` is the state before the revamp. This history stays on
  this machine. The public repository is github.com/willbearfruits/bare (a checkout in `../bare-public`): `main` gets
  one commit per release, the tree from `git archive` of `revamp`, authored `willbearfruits <willbear.fruits@gmail.com>`
  (Co-Authored-By kept, no Claude-Session lines); `gh-pages` is `build/site` (`tools/site.py`); each release `vX.Y`
  carries `make dist`'s files, `bare-X.Y.img.xz`, the manual and the showcase video, which the README links to.
- `build/` is ignored and fills with test screenshots and images; `media/` (also ignored) keeps the screenshots and
  videos made for the user.
- Flashing the user's stick is theirs to approve: `pkexec python3 tools/hb.py flash build/i386/bare.img /dev/sdX`
  (absolute paths under pkexec; `make usb DEV=…` does the same with sudo). It keeps the project partition, refuses a
  mounted or non-removable disk, and reads back what it wrote. The desktop mounts the stick's FAT partition when it is
  plugged in (`udisksctl unmount -b /dev/sdX1`; a Files window showing it keeps it busy).
- The demo songs are original (the first one was a transcription of "At Doom's Gate" until 2.5; replaced before
  publishing). BARE! is GPL-3.0-or-later (LICENSE); NOTICE.md lists what the images carry (Limine, Terminus, Doom's
  engine) and their licences in LICENSES/, which mkimage.py, the ISO and `make dist` copy along. Never put a WAD in a
  release image or the repository: id's WADs are not free, and Freedoom stays in build/ (fetched by `make freedoom`).
