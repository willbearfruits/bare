# BARE!

A synthesizer that is the whole computer. It starts from a USB stick straight into the instrument, with no operating
system underneath, on PCs from the late 1990s to now.

![The PLAY page](https://willbearfruits.github.io/bare/img/play.png)

It has an omnichord with a rhythm section, a tracker, a Fairlight-style wave page with a sampler, 4-operator FM, a
freezer that stretches what you just played, an 8-track tape, a mixer with effects, a crackle box played with your
fingers, effects to play live, Murzin's ANS, four of Xenakis's ways of making music (UPIC, GENDY, sieves and clouds),
instruments you write as text files, MIDI and Ableton Link. And Doom.

- The manual, to read or print: [bare-manual-2.5.pdf](https://github.com/willbearfruits/bare/releases/download/v2.5/bare-manual-2.5.pdf)
- All of it in a video, six and a half minutes: [bare-showcase.mp4](https://github.com/willbearfruits/bare/releases/download/v2.5/bare-showcase.mp4)
- Its page: [willbearfruits.github.io/bare](https://willbearfruits.github.io/bare/)

## Trying it

2.5 is a beta. It has been played on a ThinkPad X250 and an ASUS VivoBook (Intel 12th gen), and it boots on 89
emulated PCs. Not tried on real hardware yet: AC'97, the Sound Blaster 16, SATA and NVMe disks, the installer and
Ethernet. It needs a Pentium Pro or later.

1. Download `bare-2.5.img.xz` from the [2.5 release](https://github.com/willbearfruits/bare/releases/tag/v2.5).
2. Write it to a USB stick of 256 MB or more with balenaEtcher or Raspberry Pi Imager, or on Linux with
   `xz -dc bare-2.5.img.xz | sudo dd of=/dev/sdX bs=4M conv=fsync`. Everything on the stick is replaced.
3. Turn Secure Boot off, plug the stick in and start the computer from it: the boot menu is usually `F12`, `F9`, `F8`
   or `Esc` at power-on.
4. When the splash plays, press any key. Hold `4` and run a finger along the keys from `A` to `'`: a C major chord,
   strummed.

It runs from memory, so once it has started the stick can come out; projects are saved on the stick. The installer
(on the FILE page) erases the disk you choose.

Free software: GPL-3.0-or-later ([LICENSE](LICENSE)); [NOTICE.md](NOTICE.md) lists what it includes from others.

## Starting up

While the machine starts, the name and the boot log are on screen (a machine that stops during boot shows where).
Then comes a short splash, a different one each time the stick can remember the last: ANS (the name scratched into a
glass plate and played by light, after Murzin's photoelectronic synthesizer), GENDY (Xenakis's dynamic stochastic
synthesis: waveforms that random-walk, then settle into a chord), CMI (for the Fairlight: a green terminal, 8-bit
orchestra stabs made here, a 3D waveform) and METASTASEIS (Xenakis's 46 string glissandi drawn on graph paper). Any
key, click or MIDI note ends it; the sound follows the saved volume and mute.

## Keys

The F keys switch pages and do nothing else: `F1` play · `F2` tracker · `F3` waves and sampler · `F4` stretch ·
`F5` operator (FM) · `F6` tape · `F7` projects · `F8` mixer · `F9` touch · `F10` effects · `F11` ANS · `F12` Xenakis.
On keyboards without an F row, `Ctrl+1…9`, `Ctrl+0`, `Ctrl+-` and `Ctrl+=` do the same.
(On ThinkPads `Fn+Esc` locks the F keys.)

Shift is the function layer, on every page: `Shift+↑↓` chord sound · `Shift+←→` strum sound · `Shift+B` 1-bit ·
`Shift+E` echo · `Shift+F` freeze (stretch) · `Shift+T` thermal · `Shift+-` / `Shift+=` volume · `Shift+M` mute ·
`Shift+H` colours · `Shift+?` all of these on screen. `Esc` stops everything and releases all notes.

Colours: `Shift+H` goes through ten colour schemes: NIGHT (the first one), PHOSPHOR (green on black), AMBER, PAPER
(ink on paper), BLUEPRINT, LCD (a handheld's olive greens), STAGE (black, white and yellow, for projectors and
sunlight), CREAM (an Omnichord's cream and brown), ACID and SUNSET. The stick keeps the choice, with volume and mute.
The SCAN wave reads the screen, so it sounds a little different in each.

`Ctrl+Z` undoes and `Ctrl+Y` redoes (or `Shift+Z`, `Shift+Y`): tracker edits and the order list, wave drawing and
harmonics, FM changes, sample edits, tape takes, erases, pastes and imports. A pen stroke or a run of the same key is
one step. How far back it goes depends on the memory: 1.5 MB of history on a 32 MB PC, up to 64 MB; a tape take too
long for it can't be undone, and the title bar says so.

Volume: the laptop's volume and mute keys work too. It starts at −6 dB (the title bar shows it): built-in speakers
distort when the sound chip is driven at full level. Volume, mute and 1-bit are kept on the stick with the next
project you save or load.

The key strip at the bottom of each page shows its main keys. A page's own keys never also play a chord or a
strum; the keys it doesn't use still do.

**PLAY page** — an omnichord, drawn in lines and laid out like the keyboard: chord buttons on top, the sonic
strings below, a display with the chord, its notes, the two sounds and the scope, and the rhythm section.

| Keys | |
|---|---|
| `1` … `=` | chord root (Eb Bb F C G D A E B F# Db Ab — circle of fifths, like the Omnichord) — plays while held |
| `Q` `W` `E` | major / minor / seventh |
| `Z` … `/` then `A` … `'` | the 21 sonic strings, low to high (the chord's tones over three octaves) |
| touchpad | fingers across it strum the strings, left low to right high, several at once (I2C precision touchpads: up to five; Synaptics PS/2 pads: two) |
| `Space` | hold the chord after releasing the button |
| `↑` `↓` | strings octave |
| `Shift+↑↓` / `Shift+←→` | chord sound / strings sound (ORGAN, PLUCK, SAW BASS, FAT SAW, SQ LEAD, CHIP, BELL, GRIND, KICK, SNARE, HAT, the wave and FM sounds, and the samples that hold something) |
| `R` | rhythm start / stop |
| `[` `]` | rhythm pattern: ROCK, POP, DISCO, 16 BEAT, SWING, WALTZ, BOSSA, REGGAE, and SIEVE (the XENAKIS page's sieves) |
| `PgUp` `PgDn` | tempo (shared with the sequencer, which the rhythm falls in step with) |
| `Tab` | auto bass: a bass that follows the chord you hold, in the pattern's rhythm |

With the pointer: press a chord button to play it; drag across the strings to strum them; click a pattern or the
switches; drag a knob up or down (chord, strings and rhythm levels). The rhythm, its settings and the levels are
saved with the project.


**Your own instruments** — PLAY's `F1` again steps from the Omnichord to instruments made from text files, and the tab
takes the instrument's name. Five come with it: STYLO (the touchpad as a Stylophone's strip), THEREMIN (pitch without
steps), GRIDPADS (a grid of pads with an arpeggio), SIEVHARP (a harp tuned to a sieve) and SHRUTI (a shruti box, the
drone box of Indian music: the keys open and close its reeds, tuned in just intonation from Sa, and a finger moving to
and fro on the touchpad pumps its bellows; `Enter` held pumps too). Anyone can write more: put the file in the stick's
`INSTR` folder; [INSTRUMENTS.md](INSTRUMENTS.md) has the format. On an instrument's page, `↑ ↓ ← →` turn its knobs,
`Space` holds what is played, the letter rows and MIDI play it; its sound is also one of the sounds everywhere else,
by its name.
**SEQ page** — a tracker. Rows run down the page and each of the 8 channels is a column: note, instrument (any sound,
the samples included), volume and effect. A channel plays one note at a time and holds it until its next note or a
note-off. A pattern is 16 to 64 rows; the song is the order list at the top. It plays on while you use the other
pages, and every row and effect lands on the exact sample.

| Keys | |
|---|---|
| `Space` / `Shift+Space` | play the song from the order entry / loop the pattern; stop |
| `Z`…`/` and `Q`…`P` | notes, two octaves of piano keys (in the note column), with the current sound |
| `` ` `` | note-off |
| `0`–`9` `A`–`F` | hex, in the instrument, volume and effect columns |
| arrows, `Tab`, `PgUp` `PgDn`, `Home` `End` | move (or click a cell) |
| `Del` / `Ins` | clear the field / push the channel down a row |
| `[ ]` / `Shift+[ ]` | octave / the sound new notes get |
| `- =` | tempo; `Shift+P` rows a beat |
| `Shift+PgUp` `PgDn` | the pattern on screen; `Shift+L` its length |
| `\` | the order list: `← →` entry, `↑ ↓` its pattern, `Ins` repeat, `Del` remove, `Enter` back |
| `Shift+C` `Shift+V` | copy / paste a pattern |
| `Enter` | mute the channel |
| `Shift+1` `2` `3` | load demo: BARE METAL · HARBOR · EMPTY (and play it) |

Effects, 16 ticks to a row: `0xy` arpeggio (+x, +y semitones), `1xx`/`2xx` slide up/down, `3xx` glide to the note,
`4xy` vibrato, `8xx` pan (80 centre), `Axy` volume up x or down y, `Bxx` jump to order entry xx, `Cxx` volume,
`Dxx` next pattern from row xx, `ECx` cut after x ticks, `EDx` delay x ticks, `E9x` retrigger every x ticks, `Fxx`
tempo (20–FF) or rows a beat (01–1F). `00` repeats an effect's last value. On wide screens the list of sounds and
effects sits beside the pattern. Songs from 1.0 load into pattern 0, their ties held and their gates turned into cuts.
Both demo songs are original.

**WAVE page** — a Fairlight-style page, green on black, in four sub-pages: `Tab` / `Shift+Tab` or a click on the
tabs. The bottom two letter rows are a piano keyboard for the current sound (`Z` C, `S` C#, `X` D … `/` E; `[ ]`
octave; `Space` plays it at its own pitch). Here the touchpad is a pen: the pad stands for the screen, and a finger
draws, drags and picks where it lands. A mouse does the same with the button held.

| Page | |
|---|---|
| D WAVEFORMS | the eight wave slots stacked in 3D, front to back: the path MORPH plays through. `1`–`8` or a click picks a slot; `←→` turn, `↑↓` depth |
| 5 HARMONICS | 32 harmonic bars build the slot's wave: draw them, or `←→` pick one and `↑↓` set it |
| 6 DRAW | draw one cycle of the slot's wave; `←→` cursor, `↑↓` nudge 4 samples |
| 8 SAMPLE | the sampler, below |

On the wave pages: `R` random, `T` smooth, `Y` normalize, `U` invert, `I` clear, `O`/`P` copy/paste. `'` chooses what
the keyboard plays: DRAWN (the slot), MORPH (from the slot to slot 8 over 1.5 s), SCAN (the screen under the pointer
is the wavetable) or ROM (this machine's BIOS, flash, kernel or low memory as a wavetable: `\` source, `- =` and
`PgUp PgDn` move through it; every machine sounds different).

**Sampler** (WAVE page 8) — eight sample slots, sized from the machine's memory: 8 s each at full quality on a 32 MB
PC, 87 s with 512 MB or more.

- `R` arms a recording: it starts when the sound does and stops at `R` or when the slot is full. `Q` picks what it
  records: OUT (everything you hear), FREEZE (the stretcher alone) or IN (the line in or microphone; it turns the
  first input on if none is).
- `A` grabs the phrase you just played from the stretcher's last 10 seconds. Nothing needs arming first.
- `E` 8 or 16 bit and `W` the rate (48, 32, 24, 16, 8 kHz) convert the slot and set the format of the next
  recording. 8-bit frames play without smoothing, as on the old samplers; that is where their grit comes from.
- `↑↓` picks a marker (start, loop, end), `←→` and `PgUp PgDn` move it, or drag it. `K` loop on/off. Under the
  sample is a close-up of the frames at the marker, or of the loop's splice.
- `-` `=` root note, `'` one-shot, `T` trim to the markers, `Y` normalize, `U` reverse, `I` clear, `O`/`P`
  copy/paste; level, attack and release are the sliders.
- `F` hands the sample to the stretcher, which freezes on it (`F4` to shape it).

Elsewhere the slots are the sounds SAMPLE 1–8: chord and strum sounds, tracker instruments. They are saved with the
project, frames included.

**OPERATOR page** (`F5`) — 4-operator FM, Ableton-Operator style. 4 patches (`Home`/`End`), 8 algorithms
(`PgUp`/`PgDn`: serial, Y, two-pair, fan-in, 2+2, 3+1, 1-to-3, additive — drawn as a diagram), op-4 feedback
(`-`/`=`). Per operator: wave (sine/tri/saw/square), ratio 0.5–16 + fine, level, a full ADSR — the carrier
envelopes are the amp envelope, so notes release properly — velocity (how much softer a soft note makes it; on a
modulator, darker) and key scaling (quieter up the keyboard, down to −12 dB four octaves above C3). Per patch, an LFO:
rate 0.1–20 Hz, wave (sine, triangle, saw, square, random), and how far it moves the pitch (up to a semitone), the level
(tremolo) and the modulators (the timbre); each note has its own. `Tab` picks the operator, `↑↓` the parameter, `←→` / `[ ]`
adjust; or press on a parameter and drag across its bar. `Enter` renames the patch. Patches save with the project
and are the FM 1–4 sounds everywhere (chord/strum sounds on PLAY, track sounds in the sequencer).

Strum-plate keys release sustaining sounds on key-up; one-shot sounds (PLUCK, BELL, drums) ring out.

**STRETCH page** — the last 10 s of everything you played is always being captured. `Space` (or `Shift+F` from
anywhere) freezes it: the stretcher finds the last phrase you played (back to the last 0.4 s of quiet) and
paulstretches it, looping through it, in stereo. `←→` slower/faster ×1…×1024, `↑↓` window, `[ ]` scrub, `- =`
mix, `Enter` stay on one spot (infinite). Chords and strums still play on top.

**Songs** (`F7`, then `Tab`) — `E` renders the song into a WAV on the stick's FAT partition, the part any computer
reads: the tracker from its first order entry and the tape from its start, 48 kHz 16-bit stereo, as SONG0001.WAV and
on, faster than real time (the speakers are quiet meanwhile; `Esc` stops). The WAVs in the stick's root are listed:
`S` brings the selected one into a sample slot (`[ ]` picks it), `T` onto the tape's selected track at the head.
Imports take PCM WAVs, 8, 16 or 24 bit, mono or stereo, any rate.

**MIDI and sync** (`F7`, then `Tab` twice) — a USB MIDI keyboard, controller or interface, a module on a serial port, or an
MPU-401 (a sound card's game port). USB MIDI devices can be plugged in any time while our USB stack runs: always on a
UEFI boot; on a BIOS boot `U` here takes USB over from the BIOS until the next boot (the stick then goes through our
driver too). Serial ports run at 38400 baud, the "PC" setting of Roland and Yamaha modules and keyboards. `← →` picks
the port, `↑ ↓` the channel it listens to.

- In: notes play the page's sound with their velocity (on SEQ, stopped, they go in at the cursor), the sustain pedal
  holds, pitch bend is ±2 semitones, the mod wheel opens the filter, program change picks the strings' sound. `C`:
  an incoming clock sets the tempo, and start and stop run the tracker.
- Out: `N` the tracker's channels 1–8 as MIDI channels 1–8, `K` clock, start and stop at the tempo, `T` thru.
- The last messages in are listed, so you can see what a keyboard sends. The settings stay on the stick.
- `L` turns on Ableton Link: tempo, beat and start/stop shared with other programs on the same network (Live, most
  music apps on phones and computers, and another computer running this). The tracker and the rhythm section play in
  time with them; a start comes in on the next bar. The tempo keys change everyone's tempo.

**Network.** A cable between two computers running this, or into a router, is enough: the address comes from the
router (DHCP), or without one each picks its own (169.254.x.y). The line under LINK shows it; the instrument answers
ping. It works with the Ethernet ports of Intel's chips (e1000 family, which includes most ThinkPads), and with USB
Ethernet adapters that follow the standard (CDC-ECM or CDC-NCM: most USB-C ones made for Macs and iPads). Wi-Fi does
not work.

**FILE page** (`F7`) — projects on the stick: `↑↓` select (or click), `Enter` load, `S` save (type a name,
`Enter`), `D` then `Y` deletes. Saves are dated from the machine's clock. The last saved/loaded project comes back at
the next boot. `Tab` steps through SONGS, MIDI and the log: the kernel log and the state of the sound device (a photo
of it helps with a problem on a machine without a serial port); there `T` plays a test tone, `P` turns every output
on, ignoring the jacks, and `W` writes a hardware report to the stick: HB-LOG.TXT (the whole log), HB-PCI.TXT (every
PCI device) and HB-ACPI.BIN (the firmware's ACPI tables), for looking at a machine from another computer.

**Install** (`F7`, `Tab` four times) — puts the instrument on the computer's own disk, so it starts without the stick.
The list shows the other drives with what is on them now ("GPT, 3 partitions" is usually another system). `↑↓` picks
one, `I` asks, and only after you type `ERASE` and press `Enter` is the drive erased and written: the stick's boot
partition, then its projects. Then take the stick out and restart; if the computer still starts its old system, choose
the disk in its boot menu. Updates (BARE.UPD) and saving work as on the stick, and with the stick plugged in too,
the disk the computer started from keeps the projects. On a BIOS boot any disk the BIOS sees can take it; on a UEFI
boot, SATA and NVMe disks (also behind Intel VMD).

**TAPE page** (`F6`) — an 8-track recorder. The tracks are lanes of waveform under a ruler, as in a DAW, and it still
has a cassette's character: reels, wow and flutter, hiss, varispeed, saturation, bounce.

- `↑ ↓` (or click a lane) picks a track, `Q` arms it, `R` records on the armed tracks (again to punch out), `Space`
  plays and stops. Recording replaces what was on the track. `⇧Q` switches a track between MIX (what you hear: chords,
  strums, sequencer, stretcher, the input when MON is on) and IN (the line in or microphone alone, heard or not).
- What you play while the tape rolls lands where you heard the tape, not later: the sound card's delay is taken off.
- Per track: `[ ]` level, `⇧[ ]` pan, `⇧, ⇧.` low shelf and `⇧; ⇧'` high shelf (±12 dB), `W` mute, `⇧W` solo, `E` twice
  erases it.
- Moving: `← →` ±2 s, `PgUp PgDn` ±15 s, `Home`/`End`, a click in a lane or on the overview under the lanes; `- =`
  zoom (5 s to 20 minutes across).
- A region: drag across the ruler, or `⇧I` and `⇧O` at the head. `` ` `` loops it (without a region, the loop is the
  whole song). `⇧C` copies the region of the track, `⇧V` pastes it at the head on the selected track, `⇧D` erases the
  region on the track. Record over a loop with BOUNCE on and it becomes a loop pedal with overdubs.
- `T` BOUNCE (the recording takes the other tracks' playback too), `Y` hiss, `U` wow and flutter, `I`/`O` varispeed (½×
  to 2×, playback only; `P` back to normal).

The sound lives in RAM, in blocks taken as tracks are recorded, so the memory goes to whichever tracks you use: about 3
minutes of one track's sound in all on a 32 MB PC, over an hour with 512 MB. The timeline is 30 minutes long. The TAPE
channel on the MIX page is the tape's output. Saving a project saves the tape and the samples with it; if the stick is
too full for the tape, the project is saved without it and says so.

**MIX page** (`F8`) — a strip per source: PLAY (chords, strums, the WAVE keyboard, the instruments), SEQ, RHYTHM,
INPUT, STRETCH, TAPE, TOUCH, ANS, UPIC and CLOUDS (and DOOM, once Doom has run), each with a meter, fader (−60 to
+12 dB), pan, echo and reverb sends, mute and solo; then the master with the volume and how much the limiter is taking
off; and the effects along the bottom.

- `← →` channel, `↑ ↓` fader (`PgUp PgDn` 6 dB, `Home` 0 dB, `End` off), `Tab` chooses pan, echo send or reverb
  send and `[ ]` turns it, `Enter` mute, `Backspace` solo; or drag the faders, pans and sends and click M and S.
- Past the master, `← →` reaches the effects: REVERB (size, damping, level), ECHO (time as a note length at the tempo,
  repeats), and on the master FILTER (low, band or high pass, cutoff, resonance), DRIVE and CRUSH (bits, and the rate
  the held frames leave). `↑ ↓` picks a setting, `[ ]` changes it, `Enter` turns the unit on or off.
- The chord buttons and strum keys still play here. The mixer and the effects are saved with the project.

**TOUCH page** (`F9`) — a small circuit you play with your fingers, like a crackle box. Eight bare pads lead into it:
an op-amp's output (OUT) and inputs (IN−, IN+), a capacitor behind each input (C1, C2), its compensation point (COMP),
the supply (+9V) and ground (0V). A finger on a pad joins it to your body; two pads touched at once are joined through
your body, and the circuit starts to sound. Pressing harder lowers the skin's resistance and raises the pitch; a light
touch crackles; one finger on the gap between two pads joins them directly; a finger alone picks up the mains hum
(turn HUM up).

- Touchpad: every finger is a finger on the board (the pad stands for the board), with its pressure where the pad
  reports one. Mouse: click and hold on the board (it presses harder the longer you hold; the right button presses
  hard at once).
- `A S D F` / `Z X C V`: a finger on each pad of the top and bottom rows; a key held longer presses harder, `Shift`
  presses hard at once. `Space` holds the key fingers down after the keys go up (press a key again to lift it).
- MIDI notes put a finger on pad (note mod 8), as firm as the velocity.
- `↑ ↓` picks a knob, `← →` turns it (or drag its bar): RANGE (the capacitors: the pitch range), GAIN (the op-amp's:
  low is soft and round, high is square), CRACKLE (how often a light contact breaks), HUM, SKIN (dry to moist), TONE
  and LEVEL. `Tab` switches the hum between 50 and 60 Hz. The knobs are saved with the project; the sound has its own
  channel on the MIX page.

**FX page** (`F10`, or `Ctrl+0`) — effects to play live, on everything you hear or only on the input (a guitar or a
microphone through the line in: pick it on the MIX page, then `Tab` here). Start a song or the rhythm, then:

- `1`–`8` hold an effect while the key is down; `Shift+1`–`8` latch one on or off; `Space` latches whatever is sounding
  (again: lets all latches go); `0` turns everything off.
- The pad is the effect you picked last (`↑ ↓`, or click it in the list): the touchpad or the mouse on the pad plays it
  and sets its two settings, left-right and up-down (`← →` and `PgUp PgDn` do the same from the keys). A second finger
  plays the filter at the same time.
- FILTER: left of the middle a low-pass closing down, right of it a high-pass opening up, nothing in the middle; up is
  resonance. REPEAT: loops the last slice (1/2 to 1/32 of a beat, left to right: slide right while holding it and the
  stutter speeds up); up fades it. REVERSE: plays backwards from the moment you press (1/4 to 2 beats); up is how much.
  TAPE STOP: slows down to a stop (in 0.1 to 2 s); up adds wobble. GATE: chops in time (1/4 to 1/32 of a beat); up is
  how deep. CRUSH: fewer bits to the right, a lower rate upward. DUB: throws everything into the echo while held, the
  echoes ring on after (left-right the echo's time, up the repeats). FREEZE: the same into a reverb as long as a room
  can be (dark to bright, up is its level).
- Lengths follow the tempo (the song's, or the Link session's). The ring along the top holds the last seconds the
  effects read from: the write head moves along it and stops while a repeat holds it.
- MIDI: notes 36–43 (the first row of a drum pad) hold effects 1–8; CC 16 and 17 are the pad's two settings; CC 102–109
  hold effects 1–8 while at 64 or more.

**ANS page** (`F11`, or `Ctrl+-`) — after Evgeny Murzin's ANS synthesizer (1957), which played light through scratches
in a glass plate. The plate here is 360 pure tones, 72 to the octave (a sixth of a semitone apart), over five octaves
up the page, and time across it; a slit crosses it in a number of bars of the tempo (or of seconds) and plays every
tone it lights, as loud as the plate is bright there.

- Draw on it: every finger on the touchpad scratches (harder is brighter where the pad can tell), the mouse draws, its
  right button erases. `Tab`: pen, line (press, drag, let go: a straight glissando), eraser.
- `Space` plays, `Home` goes back to the start. `Backspace` twice clears the plate (`Ctrl+Z` brings it back).
- The camera (a USB webcam, or the laptop's own): `Enter` lays its next picture over the plate — the picture's light
  above the BLACK level, or its edges (PICTURE: light / edges) — and switches the camera off again; `\` lets the live
  camera be the plate while it plays: whatever the camera sees, you hear. On a BIOS (legacy) boot the BIOS keeps USB:
  pressing either key twice hands USB to BARE! until the next boot (as `U` in the MIDI view does), then the camera is
  found.
- The letter rows are a keyboard of its tones (`Z`–`/`, `Q`–`P`, two octaves from KEYS; `PgUp PgDn` move it), and so
  are MIDI notes: held while the plate plays, they write their row under the slit.
- `↑ ↓` picks a knob, `← →` turns it: LENGTH (1–32 bars, or 2–60 s), RANGE (where the five octaves start: C1 to C4),
  PICTURE, BLACK, INK (how bright the pen draws), BRUSH, LEVEL, KEYS. The plate is saved with the project; the sound
  has its own channel on the MIX page.

**XENAKIS page** (`F12`, or `Ctrl+=`) — four of Iannis Xenakis's ways of making music, a view each: UPIC, GENDY,
CLOUDS, SIEVES. `F12` again goes to the next view; a click on the bar under the title picks one. Clouds and UPIC have
their own channels on the MIX page, and everything here is saved with the project.

- **GENDY** — his dynamic stochastic synthesis (GENDY3, 1991): a wave whose corners take random steps every period
  and bounce off mirrors. Four patches, played anywhere as the sounds GENDY 1–4 (strum plate, tracker, MIDI). The
  picture is the wave walking, the last periods behind it. The letter rows play it (`PgUp PgDn` octave), `Space`
  holds a drone, the touchpad (or the mouse on the picture) sets the step sizes: across for the heights, up for the
  lengths. `↑ ↓ ← →`: POINTS; the heights' DIST (LINEAR, CAUCHY, LOGIST, HYPCOS, ARCSIN, EXPON), STEP, MIRROR and
  INERTIA (the steps walk too: drift instead of jitter); the lengths' DIST, STEP and MIRROR; DRIFT (the pitch walks
  as well); envelope, filter, level. `Home End` patch, `Enter` renames it.
- **CLOUDS** — masses of notes set by probabilities, after Pithoprakta and Achorripsis: so many a second at random
  moments, spread over a band, each note gliding at its own speed. Four clouds, each with its own sound. `1`–`4`
  switch them, `0` stops all, `Tab` picks one for the knobs (sound, density, band, shape, length and its spread,
  glide, level, dynamics, width, a pitch sieve, a rhythm sieve). Each finger on the touchpad plays a cloud while it
  stays down (up and down moves the band, across the density); the mouse does it for the chosen one, and the letter
  rows and MIDI play it around the note held. `Enter` writes the chosen cloud onto UPIC's page. The picture is the
  score as it is written.
- **SIEVES** — scales and rhythms from residue classes: `3@1` is every number that leaves 1 when divided by 3 (1, 4,
  7 …); `|` or, `&` and, `-` not, and parentheses. Four sieves, drawn as grids of sixteenths, a row a bar. The rhythm
  section's SIEVE pattern (on PLAY, or `Space` here) plays S1 on the kick, S2 the snare, S3 the hat and S4 the bass,
  counted from the start, so a sieve longer than a bar keeps turning across bars. `Enter` writes a formula
  (`Shift+2` @, `Shift+\` |, `Shift+7` &; `.` and `/` work too), `Tab` puts in an example, `← →` sets the step
  read as pitch (a semitone or smaller), and the letter rows and MIDI play the chosen sieve as a scale.
- **UPIC** — after the drawing machine Xenakis built in Paris (1977; Mycenae-Alpha): draw arcs of pitch over time, and
  a cursor plays them, each with the sound chosen when it was drawn. Every finger on the touchpad draws an arc of its
  own, the mouse draws too, a click is a short note. `Tab`: PEN, LINE, ERASE (or the right button), SCRUB (your hand
  holds the cursor). `Space` plays, `Home` goes to the start. The knobs: the page's LENGTH (bars or seconds), and the
  SOUND, LEVEL and SNAP (free, semitones, a sieve) of what you draw next. `PgUp PgDn` transpose the page, `R` turns it
  backwards, `I` upside down, `Backspace` twice clears it.

**Input** — the line in or a microphone, on the MIX page's INPUT strip: `\` steps through what the sound chip has
(LINE IN, MIC, BUILT-IN MIC … then off), `` ` `` sums it to mono. It is heard only with MON on (`Enter` on the strip),
since a laptop's own microphone next to its speakers howls; use headphones to listen while you play. What is heard
goes where everything else does (the stretcher, the tape's MIX tracks). Unheard, it can still be recorded: by the
sampler (source IN) and by a tape track set to IN. A microphone plugged into a jack gets its bias voltage and 20 dB of
boost where the chip has them.

**Thermal mode** (`Shift+T`) — the CPU's temperature sensor drives filter cutoff and detune, and while it's on
the idle loop spins instead of halting, so the machine warms up as you play. On ThinkPads the EC also gives
fan RPM and the power/lid LEDs blink on the sequencer beat.

**Echo** (`Shift+E`) is a stereo ping-pong: repeats alternate left and right, a dotted eighth apart at the sequencer's
tempo, and get darker as they go. The output ends in a limiter, so loud chords and strums get quieter instead of
clipping. The meter at the right of the title bar is the output, left and right.

**Doom** — type `iddqd` on any page and BARE! runs Doom (1993) full screen, with the WAD it finds on the stick. Put one
in a `DOOM` folder on the stick's first partition, or in its root: your own `DOOM.WAD` or `DOOM2.WAD`, the shareware
`DOOM1.WAD`, or Freedoom's `freedoom1.wad` or `freedoom2.wad` (free, from freedoom.github.io). That partition has room
for one WAD of up to about 40 MB. The keys are the 1993 ones: arrows, `Ctrl` fires, `Space` opens, `Alt` strafes,
`Shift` runs, `Esc` is its menu. The F keys go back to BARE! and Doom waits; `iddqd` again returns to it, paused at its
menu (typed inside Doom, it is still the cheat). Its sounds and its music are on the mixer's DOOM channel; the music is
played by BARE!'s voices (GRIND on the guitars). Saved games go in the same folder. It needs memory to spare: about
the WAD's size and 10 MB more, which a 256 MB machine has.

## Which machines

Sound is a fixed-point engine (PolyBLEP pulse and saw, drawn waves, 4-operator FM, noise, samples) with envelopes and
per-voice filtering, in stereo; the output is Intel HDA (any PC or Intel Mac since 2005), AC'97 (the PCs before that)
or a Sound Blaster 16, and `Shift+B` switches to 1-bit: the PC speaker's square wave, one note at a time, played
through the sound chip (or the real PC speaker on a machine without one).

The instrument runs entirely from RAM: once booted, the stick can be pulled and you keep playing. Saving needs
it back in (`R` rescans on the FILE page; some BIOSes only re-detect USB after a reboot).

| Machine | Boot | Sound | Keyboard | Save to stick |
|---|---|---|---|---|
| any PC 1997–2012, BIOS | yes | HDA (2005+), AC97 (1999–2006), Sound Blaster 16 | yes | yes |
| 2012–2020 laptop with CSM/legacy boot | yes | yes | yes | yes |
| UEFI-only (no CSM), e.g. a recent VivoBook | yes (64-bit build) | HDA yes | yes, USB too | yes, through our USB driver |
| Secure Boot enabled | no — disable it | | | |

Generic setup: **Secure Boot off**; legacy/CSM boot where there is a choice (the BIOS's USB drivers reach more
controllers than ours). Machines whose firmware switches off the old PC timer (common on UEFI-only laptops) get their
1 kHz tick from the HPET, or from the CPU's cycle counter with the main loop running the audio. Until the instrument
starts, the boot log is drawn on the screen, so a machine that stops during boot shows where.

**Touchpads.** Two kinds strum the strings and draw with the pen: Synaptics pads on PS/2 (ThinkPads, and most laptops
until about 2015) and precision touchpads on I2C (most laptops since). An I2C touchpad is found through the firmware's
ACPI tables, switched from its mouse mode to reporting fingers, and asked for a report every 8 ms. The I2C part has
been tried on one laptop so far (an ASUS VivoBook, Intel 12th gen): if the pad does nothing on another, the log (`F7`,
`Tab` three times) says how far it got, and `W` there writes it to the stick.

**USB.** On a UEFI boot the instrument drives the USB 3 (xHCI) controllers itself — every PC since about 2012 has
one; on Intel 7 to 9 series chipsets it also moves the USB 2 ports over from the older EHCI controller. Sticks,
keyboards, mice, tablets and touchscreens (as a pointer), hubs and USB MIDI work, and can be plugged in and out while
it plays (`R` on the FILE page finds a stick plugged in later). On a BIOS boot the BIOS keeps USB, and its keyboard
emulation and disk services go on working; `U` in the MIDI view hands USB to our driver until the next boot. There is
no driver for the older controllers (UHCI, OHCI, EHCI): on a UEFI-only machine whose stick sits on one of those, the
instrument runs without storage.

## Stick layout and updates

`bare.img` = MBR + a 64 MiB FAT32 boot partition (Limine, `BOOT/HB_A.ELF` and `HB_B.ELF` as two 4 MiB kernel slots,
`BOOT/H64_A.ELF` and `H64_B.ELF` for the 64-bit kernel UEFI boots, `EFI/BOOT/*.EFI`, the instruments in `INSTR`,
`LICENSE.TXT` and `NOTICE.TXT`, and the songs you export) + a 128 MiB raw "tape" partition (type 0x7F, magic
`HBTAPE01`) holding 64 project slots of 1 MiB. On a bigger stick the tape partition is grown to the
end of the stick at the first boot (when it is the last partition); a project whose tape and samples don't fit its
slot keeps them in a region of that space.

Updating: put `BARE.UPD` (from `make upd`; it carries both kernels) in the root of *any* FAT drive the instrument
can see — the stick itself, another stick — and boot. If its build number is newer, each kernel is written into its
inactive slot, the CRCs are verified, `limine.conf` is switched to those slots in place, and the machine reboots.
A power cut mid-update leaves the old slots bootable. A 1.0 stick, which has one 64-bit kernel, gets its second slot
on its first update. Sticks from before the name changed (1.0 to 2.2) look for `HOMEBREW.UPD`: the release
has the same file under both names. `make usb` re-flashes the boot partition but keeps the tape partition, so projects survive.

## Building

```
make img        # build/i386/bare.img — bootable USB image with a persistent project partition (the normal way)
make usb DEV=/dev/sdX   # flash it to a stick (keeps the projects already on the stick), boot a laptop from it
make run-img    # boot the image as a USB stick in QEMU with sound through PipeWire
make upd        # build/i386/bare.upd — drop it on any FAT drive to update a machine at boot

make            # build/i386/bare.iso — same instrument, no persistence (CD/ISO, also UEFI-only machines)
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

## Toolchain

clang + ld.lld (freestanding, integer-only: `-mno-sse`, so DSP is fixed-point), Limine
(`/usr/share/limine`), xorriso, QEMU, Terminus font (`terminus-font`, SIL OFL) embedded via
`tools/psf2c.py`. On Arch: `pacman -S clang lld limine xorriso qemu-desktop terminus-font`.
The host checks need 32-bit glibc (`lib32-glibc`).

## Testing without hardware

`make test` runs the instrument as a Linux program with a simulated clock, keyboard, pointer and a copy of the
stick image, and checks what comes out: no clipping and no DC with chords and strums, the sequencer's beats on
the exact sample, the stretcher's level, projects saving and loading on the stick, old projects migrating,
every page drawn. It takes about ten seconds.

For the real kernel, QEMU:

```
python3 tools/qemu-test.py --keys "4:1200 wait:300 a wait:150 s shot:strum"
```
boots headless, holds `4` (C major) for 1.2 s, strums, screenshots to `build/i386/strum.ppm`, writes
audio to `build/i386/out.wav` and the serial log to `build/i386/serial.log`. `HDA_DEBUG=2` traces the HDA
controller from QEMU's side. The wav's RIFF sizes are left at zero (QEMU never finalises it): skip the 44-byte
header and read 16-bit stereo 44.1 kHz. The title bar shows `dsp NN%`: the share of CPU time the audio interrupt takes.
`--res 1024x768` boots with that panel size (the BIOS of an older laptop usually offers 1024x768 whatever the panel is);
every page is laid out for 100x37 cells (800x600) and up, and at 4K the fonts are drawn at twice the size.
`--img --uefi` boots the stick image under UEFI (our USB stack then runs); `--usb usb-kbd,usb-tablet` adds USB devices
(`usb-hub@2,usb-kbd@2.1` puts them behind a hub); `--usbmidi` plugs in a USB MIDI keyboard emulated by
`tools/usbredir_midi.py` (QEMU has none of its own), played with `umidi:90,45,64` tokens.

`tools/qemu-matrix.py` boots the kernel on 89 emulated PCs (BIOS and UEFI, three chipsets, USB 1.1/2/3, IDE,
SATA, NVMe, SCSI and SD storage, CPUs from the Pentium II to current ones, several sound and graphics cards, 640x480
to 4K, 32 MB to 8 GB, our USB stack with a stick, a hub and USB-only input) and checks that each one boots, draws,
plays, and saves and reloads a project where the firmware or our USB driver can. Screenshots and a summary go to
`build/matrix/`. The emulator's own bugs are marked EMU: its OHCI controller stops during writes (saving through it
fails, and says so), its UHCI one drops data on long reads (booting from it sometimes takes a reset), and its VGA
shears 1366-wide modes.

## Status

2.5 (September 2026, a beta): "What 2.5 added". 2.4 (the same month): "What 2.4 added". 2.3 (the same month): "What 2.3 added" (1.0 to 2.2 were called homebrew). 2.2
(the same month): "What 2.2 added". 2.1 (the same month): precision touchpads on I2C, and fixes for UEFI laptops. 2.0
(the same month): everything in "What 2.0 added". 1.0 (the same month) was the first release: i386 and x86-64 kernels
via Limine, the framebuffer UI from 800x600 to 4K, PS/2 keyboard and mouse, Intel HDA and the PC speaker, projects and
A/B updates on the stick, and the first versions of the pages. See CHANGES.md.

The USB stack (sticks, MIDI, the built-in webcam) and the I2C touchpad driver work on a real laptop (an ASUS
VivoBook). Tested on emulated machines only so far: AC97, the Sound Blaster 16, the SATA and NVMe drivers and the
installer, Ethernet, two fingers on a Synaptics pad; Intel VMD (not emulated) not at all. Link was checked against
Ableton's own library.

## Roadmap

Next:
- Hardware controllers made for BARE!: boxes of keys, pads, knobs and encoders laid out for its pages — an
  omnichord's chord buttons and strum plate, a tracker's step keys, the Etch A Sketch's two knobs — built on RP2040 or
  ESP32-S3 boards. They talk USB MIDI, which BARE! already understands.
- Musical Etch A Sketch: a page that works like the toy. Two knobs draw one unbroken line, one across and one up and
  down, and then the drawing plays. It needs two encoders: one of the controllers above, or any MIDI controller with
  two endless knobs (the arrow keys stand in on a bare laptop). Still to decide: how the line becomes sound (retraced
  in the order and at the speed it was drawn, or read left to right) and what shaking it clear is.

After that:
- Machines: an EHCI driver, a text-mode UI for machines without a framebuffer, Realtek Ethernet (most desktops), and
  ports: Raspberry Pi, PowerPC Mac, coreboot payload.

## What 2.5 added

1. XENAKIS (`F12`): GENDY, sieves, clouds and UPIC, after Iannis Xenakis.
2. Instruments from text files, and five that come with it: STYLO, THEREMIN, GRIDPADS, SIEVHARP and SHRUTI, a shruti
   box whose bellows are the touchpad.
3. Doom: type `iddqd` on any page.
4. Free software: GPL-3.0-or-later.

## What 2.4 added

1. ANS (`F11`): a plate of 360 tones played by a moving slit, after Murzin's ANS synthesizer.
2. USB webcams: a camera picture laid on the ANS plate, or the live camera as the score.

## What 2.3 added

1. The name: BARE!.
2. Ten colour schemes (`Shift+H`).
3. A splash after the boot, one of four with picture and sound: ANS, GENDY, CMI, METASTASEIS.
4. FX (`F10`): effects to play live, on everything or on the input alone.

## What 2.2 added

1. Every finger on the touchpad: up to five on precision touchpads, two on Synaptics ones; two-finger strums.
2. TOUCH (`F9`): a small circuit played with the fingers, like a crackle box.
3. Install: the instrument onto the computer's own disk (SATA, NVMe, NVMe behind Intel VMD), projects along.
4. Ethernet (Intel's chips, USB adapters) and Ableton Link: tempo, beat and start/stop with other programs.

## What 2.0 added

Old machines keep working: there is a minimum machine (the Pentium II with 32 MB the emulator tests use), and
everything that needs memory — tape length and tracks, sample time, effect buffers — sizes itself from the RAM
that is there.

1. Keys: F keys for pages only, Shift for functions, a key help screen; look-ahead limiter; volume, mute and
   1-bit kept on the stick.
2. Trackpad: Synaptics PS/2 in absolute mode, as the strum plate and as a pen.
3. PLAY becomes an Omnichord drawn in lines, adapted to the screen, keyboard and trackpad, with its rhythm
   section and auto-bass.
4. WAVE becomes a Fairlight: more of the CMI's pages and look, drawing with the pen, and a sampler (8-bit and
   full), fed by the stretcher too.
5. Mic and line-in capture (HDA and AC97): levels, monitoring, into the sampler, the stretcher and the tape.
6. TAPE with 8 tracks and their waveforms: record track by track, build whole songs.
7. SEQ becomes a tracker: patterns, order list, samples as instruments, effect columns.
8. A mixer and effects.
9. MIDI in and out: serial, MPU-401 and USB.
10. OPERATOR: LFO, velocity, key scaling.
11. Songs in and out: WAV export and import on FAT drives; projects that hold tape tracks and samples, using the
    whole stick.
12. Undo.
13. Our own USB stack (xHCI): sticks, keyboards, mice and tablets, hubs, USB MIDI; saving and updates under UEFI.
14. Sound drivers: AC97 (Intel ICH and compatibles, with line in and mic), Sound Blaster 16, and newer Intel
    laptops whose audio shows up as the DSP (its HDA side drives the speakers and headphones).


Reference hardware: ThinkPad X250 (i8042 keyboard, Synaptics PS/2 trackpad, Realtek HDA codec, AHCI).
