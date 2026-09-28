# Changes

## 1.0 beta — 2026-09-28

The first release. A synthesizer that is the whole computer, started from a USB stick (or a CD, or a floppy) with no
operating system underneath:

- Twelve F keys: PLAY, an omnichord after Suzuki's OM-108 with its rhythm section, and instruments made from text files
  (five come with it); SEQ, a tracker; WAVE, drawn waves and a sampler, after the Fairlight; STRETCH, which freezes
  what you just played; OPERATOR, four-operator FM; TAPE, eight tracks; FILE, projects on the stick, songs, MIDI, the
  keys and the log; MIX, with echo, reverb and master effects; TOUCH, a crackle box after Michel Waisvisz's; FX,
  effects to play live; LINEAGE, homages to play with their histories: XENAKIS (METASTASEIS, CLOUDS, SIEVES, UPIC,
  GENDY; F12 opens it), ANS and Coil, REICH, CARLOS, RADIGUE, MERZBOW.
- Keys follow the chord (`Shift+K`), ten colour schemes (`Shift+H`), undo, MIDI in and out over serial ports and USB,
  Ableton Link, songs out as WAV and in from WAVs, the F keys set up as you like, Doom (`iddqd`) with the WAD on the
  stick and its first level's music as breakcore in the tracker (SEQ `Shift+4`).
- PCs since the Pentium Pro, from 32 MB, BIOS or UEFI, a 32-bit and a 64-bit kernel; Intel HDA, AC'97 and the Sound
  Blaster 16; PS/2, USB (its own xHCI stack) and I2C touchpads; updates from a file on any FAT drive.
- It is a beta: played on a ThinkPad X250 and an ASUS VivoBook, booted on 90 emulated PCs; AC'97, the Sound Blaster
  16, SATA and NVMe disks, the installer, Ethernet and the floppy have not been tried on real hardware yet.

## Before 1.0: the development versions

BARE! grew through versions numbered 1.0 to 2.6 (called homebrew until 2.2), made and played on its author's own
machines; 2.5 was on GitHub for a while as a preview. The code and the documents still name them where they matter
(an old project's chunks, an old stick's files): every project and stick from them loads and updates. Each in a line,
then each in detail, as written at the time.

- **2.6** (a beta): PLAY becomes the Suzuki OM-108 · keys follow the chord (`Shift+K`) · LINEAGE (`F11`): XENAKIS,
  ANS, REICH, CARLOS, RADIGUE, MERZBOW · XENAKIS opens on METASTASEIS · TOUCH tells of Michel Waisvisz's Crackle Box ·
  the F keys are yours · SEQ `Shift+4`: E1M1 as breakcore · a floppy.
- **2.5** (a beta): XENAKIS (`F12`): GENDY, sieves, clouds and UPIC · instruments from text files, SHRUTI among them ·
  Doom · free software, GPL-3.0-or-later.
- **2.4**: ANS (`F11`), after Murzin's synthesizer · USB webcams, laid on its plate or played live.
- **2.3**: the name BARE! (1.0 to 2.2 were called homebrew) · ten colour schemes · a splash at boot · FX (`F10`).
- **2.2**: every finger on the touchpad · TOUCH (`F9`) · installing onto the computer's own disk · Ethernet and
  Ableton Link.
- **2.1**: precision touchpads on I2C, and fixes for UEFI laptops.
- **2.0**: the Omnichord, the Fairlight, the tracker, the 8-track tape, a mixer and effects, mic and line in, MIDI,
  undo, our own USB stack, AC97 and the Sound Blaster 16 — sized to the machine, down to a Pentium II with 32 MB.
- **1.0**: the first release: i386 and x86-64 kernels via Limine, the framebuffer UI from 800x600 to 4K, PS/2 keyboard
  and mouse, Intel HDA and the PC speaker, projects and A/B updates on the stick, the first versions of the pages.

### 2.6 — 2026-09-28

- The F keys can be set up as you like. Each one opens a page, a view of XENAKIS (UPIC, GENDY, CLOUDS, SIEVES), an
  instrument, or nothing. Set them on the FILE page's new KEYS view (`Tab` to it: `←→` what a key opens, `Space` to
  move it, `Del` to empty it, `R` `Y` the defaults), or drag a tab along the top onto another key. The stick keeps
  them in KEYS.TXT, a line for each key that differs from the default, which a computer can edit too; a new stick
  has one with only comments that explain it. Each key remembers where it was: with PLAY on `F1` and SHRUTI on `F2`,
  `F1` still comes back to the omnichord. FILE always stays on a key. A key that opens nothing has no tab, and in
  Doom it is Doom's own key again (`F2` save, `F3` load, `F6` quicksave).
- A click on a tab opens it.
- FILE's views are now projects, SONGS, MIDI, KEYS, LOG, INSTALL: INSTALL is five `Tab`s away.
- PLAY is laid out after the Suzuki OM-108 and its owner's manual. The three letter rows are the MAJOR, MINOR and 7th
  button rows (`1` … `=`, `Q` … `]`, `A` … `'` and `Enter` or `\`), roots Db Ab Eb Bb F C G D A E B F#; buttons pressed
  together make maj7, m7, dim, aug, sus4 and add9, each played as three notes. The strumplate has 13 strings (`Z` …
  `/` the first ten), the chord's tones folded from F# to F four times and the root on top. Ten voices, each a main
  and a sub sound (omni 1, omni 2, harp, celeste, A. piano, guitar, FM piano, organ, vibes, banjo), with MAIN, SUB and
  SUSTAIN knobs; CHORD AUTO, CHORD HOLD, SYNC START and INSTANT OFF; ten rhythms in the OM-108's two sets (ROCK 2, SLOW
  ROCK, COUNTRY, HIP HOP and FUNK are new) beside BARE!'s own, a new one starting at the next bar, and the old kit as
  CLASSIC; transpose and master tune; KEYBOARD mode (`Caps Lock`) with the MINOR and 7th rows as a keyboard, drums on
  the MAJOR row and the strings. `O` in FILE's MIDI view sends it on the OM-108's channels (strings 1, chord 2, bass 3,
  sub 4, drums 10). 2.5's projects load with their chord and pattern. There are 32 voices now (24 before).
- Keys follow the chord (`Shift+K`): a note played on the other pages moves to the nearest tone of the omnichord's
  chord. Off, every page is as it was. Saved with the project.
- LINEAGE (`F11`, where ANS was): homages to play, each with a short history — ANS, and four new ones:
  - REICH: two to four players loop one pattern while a process moves them apart and back (PHASE, SHIFT, DRIFT, and
    LOOP for a sampler slot), on the exact sample.
  - CARLOS: a Moog-style voice in equal temperament or Wendy Carlos's alpha, beta and gamma scales; the omnichord's
    chord compared in cents.
  - RADIGUE: a drone of eight partials tuned tenths of a cent apart, beating, breathing, gliding over minutes.
  - MERZBOW: noise from junk — scraping fingers, struck metal, feedback, the program's own bytes — under a loudness
    cap.
  They play on a new mixer channel, LINEAGE; the mixer's strips scroll where they don't all fit. ANS gains a drone
  sketch after Coil (`Insert`) and passes of two and four minutes.
- XENAKIS opens on a new view, METASTASEIS: families of string glissandi strung between two guide lines, straight or
  crossed, evenly spaced or in the Modulor's proportions, the ruled surface turning in 3D; `Enter` writes them onto
  UPIC's page. The views are in the order of their music, and METASTASEIS and UPIC have a short history.
- XENAKIS moves inside LINEAGE, the first of its homages by date, its five views on a second bar under LINEAGE's.
  `F12` still opens it (now as LINEAGE's XENAKIS view; `F12` again steps its views), `F11` opens LINEAGE on ANS as
  before, and a KEYS.TXT naming XENAKIS or its views still means them.
- TOUCH has one too, under the board: Michel Waisvisz, who wanted electronic music played with the body, and his
  Crackle Box (the Kraakdoos, made at STEIM in Amsterdam), which the page is after.
- A floppy: a 3.5" 1.44 MB diskette that boots BIOS PCs with a floppy drive (GRUB starts it:
  Limine can't read floppies; `make floppy`). It plays like the stick, but can't save.
- New sounds for all of that, appended (projects keep their numbers): the omnichord's voices and drums, MOOG, JUNK
  METAL, MARIMBA.
- The font has É é è Î ü ö – — ¢ for the histories.
- SEQ `⇧4`: Doom's E1M1 (or Doom II's MAP01) read from the WAD on the stick and arranged as breakcore in the tracker —
  172 BPM, the guitars and bass over chopped breaks, rolls, stutters, gates and junk metal, an intro, a breakdown and a
  tape stop. The notes come from the WAD when asked; nothing of Doom's music is in BARE!.

### 2.5 — 2026-09-26

- BARE! is free software: GPL-3.0-or-later (LICENSE). NOTICE lists what the stick and ISO carry (the Limine
  bootloader, the Terminus font) and their licences; the stick has them as LICENSE.TXT and NOTICE.TXT.
- The first demo song is BARE METAL, an original riff: it was a transcription of a copyrighted piece. The saw sound
  it plays is called GRIND now (DOOM before; projects keep their sounds).
- XENAKIS (`F12`, or `Ctrl+=`): a page for Iannis Xenakis's ways of making music, in four views (`F12` again steps
  through them).
- GENDY, his dynamic stochastic synthesis, as a sound: a wave whose breakpoints take random steps every period between
  mirrors. Four patches (GENDY3, S.709, BREATH, STORM) to change and rename on the GENDY view, played anywhere as the
  sounds GENDY 1-4: the strum plate, the tracker, MIDI. Saved with the project.
- Sieves: scales and rhythms from residue classes, written as formulas (`3@0|4@0`; `|` or, `&` and, `-` not). Four of
  them on the SIEVES view, each drawn as a grid of bars. The rhythm section has a ninth pattern, SIEVE: S1 on the kick,
  S2 the snare, S3 the hat, S4 the bass, counted from the start, so a sieve longer than a bar keeps turning across the
  bars. The letter rows play a sieve as a scale, in semitones or finer. Saved with the project.
- Stochastic clouds (the CLOUDS view): masses of notes set by probabilities, after Pithoprakta and Achorripsis — so
  many a second at random moments, spread over a band, each note sliding at its own speed. Four clouds with their own
  sounds; a pitch sieve puts one on a scale, a rhythm sieve on the sieve's sixteenths. Played with the keys 1-4, the
  touchpad (a cloud a finger), the letter rows or MIDI (a cloud around the note held). The picture is the score being
  written. The mixer has new channels for them and for UPIC.
- Instruments from text files: a file in the stick's `INSTR` folder says what an instrument sounds like (a wave, an
  envelope, a filter, from what BARE! has: the basic waves, drawn waves, GENDY, FM, samples) and how it's played (the
  letter rows, chromatic or in a scale — sieves too —, the touchpad as a strip, a grid of pads, an arpeggio on the
  tempo, hold, glide). On PLAY, `F1` again steps to each; its sound plays anywhere by its name. Four come with it:
  STYLO, THEREMIN, GRIDPADS, SIEVHARP. INSTRUMENTS.md has the format. Knobs turned are saved with the project.
- The camera on a BIOS (legacy) boot: `Enter` or `\` twice on the ANS page hands USB to BARE! (the BIOS keeps it
  otherwise), then the camera is found. The LOG view says so too.
- Under UEFI the log says what size the screen reports (its EDID), or that the firmware gave none: a machine whose
  picture is stretched shows why.
- UPIC: draw arcs of pitch over time — every finger on the touchpad at once, the mouse, straight lines — each played
  by the sound chosen when it was drawn, gliding as the line rises and falls. The cursor crosses the page in bars or
  seconds, or the hand holds it (SCRUB). Snapped to semitones or a sieve, a sloping line becomes a run. Transpose,
  backwards (retrograde), upside down (inversion); a cloud can be written onto the page (Enter on CLOUDS). Saved with
  the project.
- Doom: typing `iddqd` on any page runs Doom (1993) full screen, with the WAD on the stick (in a `DOOM` folder or the
  root: DOOM.WAD, DOOM2.WAD, the shareware DOOM1.WAD, Freedoom's). The F keys go back to BARE! and it waits; `iddqd`
  returns to it, paused at its menu. Its sounds and music are on a new mixer channel, DOOM (shown once Doom has run);
  the music is played by BARE!'s voices. Saved games go on the stick. The engine is Chocolate Doom's, by way of
  doomgeneric (GPL-2.0-or-later, in `third_party/doom`); no game data comes with BARE!.
- FILE: `D` then `Y` deletes a project (it was `D` twice, which typing `iddqd` there would have done).
- SHRUTI, a shruti box, comes with the instruments: its keys open and close reeds tuned in just intonation from Sa,
  and a finger moving to and fro on the touchpad pumps its bellows (`Enter` held too): the drone swells with the air
  and fades as it leaks out. Instrument files have three new settings for it: `tuning: just` (5-limit ratios from a
  tonic: the fifth exactly 3:2), `hold: toggle` (a key opens its note, the next press closes it) and `bellows: yes`.

### 2.4 — 2026-09-26

- ANS (`F11`): a plate of 360 tones, 72 to the octave, played by a moving slit, after Murzin's ANS synthesizer. Draw
  on it with every finger or the mouse, lay a camera picture over it, or let the live camera be the plate. Keys and
  MIDI notes write into it as it plays. Saved with the project, its own mixer channel.
- USB webcams (UVC, uncompressed YUY2/NV12): isochronous transfers in the USB driver. The LOG view's `C` turns the
  camera on and shows the pictures arriving (its light is on only while something wants pictures). A webcam that is
  two cameras (the picture and an infrared one for face login) gives the picture.

### 2.3 — 2026-09-26

- The name is BARE! now (1.0 to 2.2 were "homebrew"). Update files are `BARE.UPD`; `HOMEBREW.UPD` is still read.
  The stick shows up as BARE on other computers.
- Ten colour schemes, `Shift+H`: NIGHT, PHOSPHOR, AMBER, PAPER, BLUEPRINT, LCD, STAGE, CREAM, ACID, SUNSET. The stick
  keeps the choice. Every scheme is checked for readable text.
- A splash after the boot, one of four, a different one each time: ANS, GENDY, CMI and METASTASEIS (see Starting up
  in the README). Picture and sound, a few seconds; any key ends it.
- The boot log is drawn under the name in ASCII.
- FX (`F10`): effects to play live, on everything or on the input alone — a DJ filter, beat repeat, reverse, tape stop,
  gate, crush, a dub echo throw and a reverb freeze. Keys hold or latch them, the touchpad plays them (a second finger:
  the filter), MIDI notes and CCs too. Lengths follow the tempo.
- The log says how fast the screen takes a whole frame (MB/s): on real laptops it tells whether the screen is written
  uncached.

### 2.2 — 2026-09-26

- Touchpads follow every finger: up to five on precision touchpads (I2C and USB), two on Synaptics pads (their
  advanced gesture mode, as on the ThinkPad X250). On PLAY each finger strums, so two fingers play two strings at once.
- USB precision touchpads are switched out of mouse mode too.
- TOUCH (`F9`): a small circuit played with the fingers, like a crackle box — eight pads, an op-amp, two capacitors;
  your body joins the pads you touch. Touchpad (every finger, and pressure where the pad reports it), mouse, keys and
  MIDI. It has its own channel on the mixer.
- Projects save the mixer in a new chunk with a record per channel (older projects' mixer settings still load).
- Install (`F7`, `Tab` four times): the instrument onto the computer's own disk, with its projects; the drive is
  erased only after ERASE is typed. The disk the computer started from keeps the projects.
- Fixed: a project whose samples didn't end on a 512-byte boundary lost its slot at the next boot (its header was
  overwritten by the samples' last bytes). Since 2.0.
- Ableton Link (`L` in the MIDI view): tempo, beat phase and start/stop with Live, apps and other copies of this on
  the network; the tracker and the rhythm section follow, starting on the next bar. Checked against Ableton's own
  library.
- Ethernet: Intel PRO/1000 chips (e1000/e1000e, and the I217/I218/I219 of laptops like the X250) and USB Ethernet
  adapters that speak CDC-ECM or CDC-NCM. An address from DHCP, or a link-local one (169.254.x.y) on a cable between
  two computers; it answers ping.
- Internal disks on UEFI boots: SATA (AHCI), NVMe (512-byte and 4 KiB blocks) and NVMe behind Intel VMD (the RST
  setting of laptops from 2020 on). They are listed after the USB sticks, and used when they carry an installed
  homebrew.

### 2.1 — 2026-09-26

- Precision touchpads on I2C (most laptops since about 2015) strum the strings and draw with the pen, like the Synaptics
  PS/2 ones. They are found through the firmware's ACPI tables. Tested on an ASUS VivoBook.
- The 64-bit kernel no longer stops with exception 14 on Intel USB controllers (UEFI laptops such as the ASUS VivoBook
  did not boot 2.0).
- I2C controllers that the firmware leaves without an address (the ASUS VivoBook's) are given one.
- USB: a device just plugged in gets 100 ms to settle before it is reset, and one that doesn't answer is tried twice
  more.

### 2.0 — 2026-09-25

Keys and pages
- The F keys only switch pages. Functions moved to Shift (`Shift+?` lists them); SEQ demos are `Shift+1…3`.
- Volume, mute and 1-bit are kept on the stick with the next project save or load.
- Synaptics touchpads work in absolute mode: on PLAY a finger strums the strings, on WAVE it is a pen.

PLAY
- An omnichord drawn in lines: chord buttons, sonic strings, display, and a rhythm section with eight patterns and
  an auto bass that follows the chord.

WAVE
- Green on black, in four sub-pages like the Fairlight's: D (the slots in 3D), 5 (harmonics), 6 (drawing), 8 (sampler).
- The bottom letter rows are a piano keyboard on this page.
- A sampler: eight slots sized from RAM, 16 or 8 bit at 48 to 8 kHz, record the output or the stretcher, grab the
  last phrase, markers and loops, trim, normalize, reverse, and freeze a sample in the stretcher. Samples are the
  SAMPLE 1–8 sounds everywhere and are saved with the project.

SEQ
- A tracker: 32 patterns of up to 64 rows × 8 channels, an order list, note / instrument / volume / effect columns, and
  the classic effects (arpeggio, slides, glide, vibrato, pan, volume slide, jump, break, cut, delay, retrigger, tempo)
  on 16 ticks a row, sample-exact. Any sound is an instrument, the samples too. 1.0 songs load into pattern 0.

OPERATOR
- Per operator: velocity sensitivity and key scaling. Per patch: an LFO (sine, triangle, saw, square, random) on
  pitch, level and the modulators. 1.0 patches load as they sounded.

Undo
- Ctrl+Z and Ctrl+Y on every page that edits: tracker, waves, FM, samples, tape takes and edits, imports.

Songs and projects
- Export: the song (tracker and tape) rendered faster than real time into a WAV on the stick's FAT partition.
- Import: a WAV (8/16/24 bit, mono or stereo, any rate) into a sample slot or onto a tape track.
- Projects save the tape with them. The project partition grows to the end of the stick at the first boot; big
  projects keep their tape and samples there.

MIDI
- In and out over USB, a serial port (38400 baud, the PC mode of Roland and Yamaha gear) or an MPU-401: notes with
  velocity, sustain, pitch bend, mod wheel, program change; clock in (tempo, start, stop) and out; the tracker's
  channels out as MIDI channels 1-8; a monitor of what comes in. On the FILE page, behind Tab.

TAPE
- 8 tracks as waveform lanes under a ruler, zoom and an overview of the whole song; per track level, pan, EQ, mute,
  solo, and MIX or IN as what it records. Regions: loop, copy, paste, erase. Up to 30 minutes.
- Memory goes to the tracks as they are recorded (blocks from one pool): minutes on a 32 MB PC, an hour or more with
  more RAM.
- Overdubs land where you heard the tape: the output's delay is taken off the recording.
- 1.0 projects load their tape settings into the first four tracks.

MIX and input
- A mixer page (F8): PLAY, SEQ, RHYTHM, INPUT, STRETCH and TAPE strips with meters, faders, pan, echo and reverb sends,
  mute and solo, and the master with the limiter's gain reduction. Saved with the project.
- Effects: a reverb the channels send to; the echo's time (1/16 to dotted 1/4) and repeats; on the master a resonant
  filter, a drive and a crusher (bits and rate).
- Line in and microphones on Intel HDA, heard only with MON on; the sampler and IN tape tracks record it either way.

USB
- Our own USB stack for USB 3 (xHCI) controllers: sticks, keyboards, mice, tablets and touchscreens, hubs and USB
  MIDI, plugged in and out while it runs. It runs on UEFI boots, so UEFI-only laptops save projects now.
- On a BIOS boot the BIOS keeps USB; `U` in the MIDI view hands it to ours (for USB MIDI) until the next boot.
- Updates carry both kernels, so UEFI machines update too. A 1.0 stick gets its second 64-bit slot on its first update.

Sound
- AC97 sound (PCs from about 1999 to 2006: Intel ICH, nForce, AMD, SiS 7012), with line in and microphone.
- Sound Blaster 16 (ISA), at 44.1 kHz.
- Newer Intel laptops whose audio appears as the DSP (Skylake on) play through its HDA side: speakers and headphones.
- The limiter looks 32 frames ahead, so peaks are caught before they go out.
- The DC filter no longer leaves up to 31 steps of offset behind after loud passages.

### 1.0 — 2026-09-25

Sound
- New engine: voices render in blocks, in stereo. About 3× less CPU per voice, 5× for FM.
- The sequencer runs on the audio clock, so steps land on the exact sample.
- Stereo echo, DC removal, and a limiter at the end instead of clipping.
- Master volume, starting at −6 dB. The laptop's volume keys work, or `Ctrl+−` / `Ctrl+=`.
- The stretcher is rewritten: stereo, 5.7× less CPU, and freezing picks the last phrase you played.
- 1-bit mode now plays through the sound chip, so it works on laptops too.
- FM algorithm TWO PAIR now routes operator 3. Old projects keep their old sound.

Screen
- Every page is redrawn, with pixel graphics under the text. Only pixels that changed are sent to the screen, 3 to 4.6× less than before.
- Pages lay out from 800x600 to 4K; on 4K screens the text and pointer are drawn at double size.
- The pointer plays: chord buttons, strumming, drawing waves, sliders.

Machines
- Boots UEFI-only laptops: the stick has a 64-bit kernel for UEFI and the 32-bit one for BIOS, which can save projects.
- Keeps time on machines whose firmware turns off the old PC timer, using the HPET or the CPU's cycle counter.
- Sound works on the ThinkPad X250. Its sound chip was reading stale memory.
- A machine without a graphics mode shows a notice and plays without a picture.
- Until the instrument starts, the boot log is shown on screen. On the FILE page, `Tab` shows the log and the state of the sound device.

Fixes
- A failed BIOS disk call could make a save look successful when nothing had been written.
- Laptop volume keys used to play notes.

Tools
- `make test`, `bench`, `shots`, `render` and `video` run the instrument as a Linux program, in seconds.
- `tools/qemu-matrix.py` boots it on 74 emulated PCs and checks boot, picture, sound and saving.

Projects saved with earlier builds load in 1.0.
