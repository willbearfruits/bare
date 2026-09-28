# BARE! — the manual

A synthesizer that is the whole computer. It starts from a USB stick straight into the instrument, with no operating
system underneath, on PCs from the late 1990s to now.

![The PLAY page](https://willbearfruits.github.io/bare/img/play.png)

It has an omnichord laid out after Suzuki's OM-108, with its rhythm section, a tracker, a Fairlight-style wave page
with a sampler, 4-operator FM, a freezer that stretches what you just played, an 8-track tape, a mixer with effects, a
crackle box played with your fingers, effects to play live, a LINEAGE of homages you can play — five of Xenakis's
ways of making music (Metastaseis's glissandi, clouds, sieves, UPIC and GENDY), Murzin's ANS, Steve Reich's phasing,
Wendy Carlos's scales, Éliane Radigue's drones, Merzbow's noise — each with a short history, instruments you write as
text files, MIDI and Ableton Link. And Doom.

- The manual, to read or print: [bare-manual-1.0-beta.pdf](https://github.com/willbearfruits/bare/releases/download/v1.0-beta/bare-manual-1.0-beta.pdf)
- Its page: [willbearfruits.github.io/bare](https://willbearfruits.github.io/bare/)

## Trying it

1.0 is the first release, and a beta. It has been played on a ThinkPad X250 and an ASUS VivoBook (Intel 12th gen),
and it boots on 90 emulated PCs. Not tried on real hardware yet: AC'97, the Sound Blaster 16, SATA and NVMe disks,
the installer and Ethernet. It needs a Pentium Pro or later.

1. Download `bare-1.0-beta.img.zip` (or `.img.xz`) from the [1.0 beta release](https://github.com/willbearfruits/bare/releases/tag/v1.0-beta).
2. Write it to a USB stick of 256 MB or more with balenaEtcher, Rufus or Raspberry Pi Imager, which take the `.zip`
   as it is, or on Linux with `xz -dc bare-1.0-beta.img.xz | sudo dd of=/dev/sdX bs=4M conv=fsync`. Everything on
   the stick is replaced.
3. Turn Secure Boot off, plug the stick in and start the computer from it: the boot menu is usually `F12`, `F9`, `F8`
   or `Esc` at power-on.
4. When the splash plays, press any key. Hold `6` and run a finger along the keys from `Z` to `/`: a C major chord,
   strummed.

It runs from memory, so once it has started the stick can come out; projects are saved on the stick. The installer
(on the FILE page) erases the disk you choose.

It also fits on a floppy: `bare-1.0-beta-floppy.img` is a 3.5" 1.44 MB diskette for a PC with a BIOS and a floppy
drive (internal, or a USB drive the BIOS boots from). Write it with `dd` (`sudo dd if=bare-1.0-beta-floppy.img
of=/dev/fd0 bs=18k`, or the USB drive's `/dev/sdX`) or RawWrite for Windows, put the floppy first in the boot order,
and wait about a minute while the drive reads it. It plays like the stick but can't save (projects live in the
stick's own partition), and it has been booted in emulators only so far. GRUB starts it: Limine can't read a floppy
drive.

Free software: GPL-3.0-or-later ([LICENSE](LICENSE)); [NOTICE.md](NOTICE.md) lists what it includes from others.

## Starting up

While the machine starts, the name and the boot log are on screen (a machine that stops during boot shows where).
Then comes a short splash, a different one each time the stick can remember the last: ANS (the name scratched into a
glass plate and played by light, after Murzin's photoelectronic synthesizer), GENDY (Xenakis's dynamic stochastic
synthesis: waveforms that random-walk, then settle into a chord), CMI (for the Fairlight: a green terminal, 8-bit
orchestra stabs made here, a 3D waveform) and METASTASEIS (Xenakis's 46 string glissandi drawn on graph paper). Any
key, click or MIDI note ends it; the sound follows the saved volume and mute.

## Keys

The F keys open pages and do nothing else. As it comes: `F1` play · `F2` tracker · `F3` waves and sampler · `F4`
stretch · `F5` operator (FM) · `F6` tape · `F7` projects · `F8` mixer · `F9` touch · `F10` effects · `F11` lineage
(Xenakis, ANS, Reich, Carlos, Radigue, Merzbow) · `F12` Xenakis, straight to its place in the lineage. On keyboards
without an F row, `Ctrl+1…9`, `Ctrl+0`, `Ctrl+-` and `Ctrl+=` do the same. A click on a tab at the top does too. (On
ThinkPads `Fn+Esc` locks the F keys.)

You can change which key opens what. On the FILE page, `Tab` to KEYS: `↑↓` picks a key, `←→` sets what it opens (a
page, a view of LINEAGE — XENAKIS, METASTASEIS, UPIC, REICH, RADIGUE … — or an instrument, so SHRUTI can have a key
of its own), `Del`
empties it, `Space` picks it up to move it with `↑↓`, and `R` then `Y` goes back to the keys as they came. Or drag a
tab along the top onto another one to swap them, or onto an empty place (they show while you drag). A key that opens
nothing has no tab; in Doom it is Doom's own key again. FILE always stays on a key. Each key remembers where it was:
with PLAY on `F1` and SHRUTI on `F2`, `F1` still brings back the omnichord. The layout is kept on the stick in
KEYS.TXT, one line for each key that differs from the default (`F2 SHRUTI`, `F6 off`), which you can also edit on a
computer.

Shift is the function layer, on every page: `Shift+↑↓` chord sound · `Shift+←→` strum sound · `Shift+B` 1-bit ·
`Shift+E` echo · `Shift+F` freeze (stretch) · `Shift+T` thermal · `Shift+-` / `Shift+=` volume · `Shift+M` mute ·
`Shift+H` colours · `Shift+K` keys follow the chord · `Shift+?` all of these on screen. `Esc` stops everything and
releases all notes.

Keys follow the chord (`Shift+K`): the omnichord's idea, offered to the rest — hold a chord, and whatever you play
lands on it. While it is on (the title bar shows `♪` and the chord), a note from the letter rows, a pad, a stepped
strip or MIDI moves to the nearest tone of the omnichord's chord (the last one its buttons made; ties go up) and keeps
its octave — on WAVE's keyboard, the instruments, GENDY, CLOUDS (their notes too), UPIC's SNAP CHORD, METASTASEIS's
string ends, ANS's keys, REICH's notes, RADIGUE's base and MERZBOW's junk. Off, every page is as it was: chromatic
keys, the instruments' own scales, the sieves. The tracker's note entry, the sieves' scale keys, keyboard mode and
CARLOS's scales are left alone either way. It is saved with the project.

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

**PLAY page** — an omnichord laid out after Suzuki's OM-108 and its owner's manual, drawn in lines: the chord
buttons in three rows, the strumplate and its INSTANT OFF plate, a display with the chord and its notes, the voice and
rhythm buttons, the real-time switches and the knobs.

| Keys | |
|---|---|
| `1` … `=` · `Q` … `]` · `A` … `'` and `Enter` (or `\`) | the MAJOR, MINOR and 7th rows, twelve roots each: Db Ab Eb Bb F C G D A E B F#, the circle of fifths as on the OM-108. A chord plays while its button is held |
| two or three together | MAJOR + 7th = maj7, MINOR + 7th = m7, MAJOR + MINOR = dim, all three = aug; MAJOR with the 7th button to its left = sus4, with the MINOR to its left = add9. Every chord is three notes, as on the instrument |
| `Z` … `/` | the first ten of the 13 strings (the touchpad and the mouse reach all 13): the chord's root, third and fifth in each octave from F# to F, four times, and the root on top |
| touchpad | fingers across it strum the strings, left low to right high, several at once (I2C precision touchpads: up to five; Synaptics PS/2 pads: two) |
| `Backspace` | INSTANT OFF: the chord, the strings and the accompaniment stop at once (with SYNC START, the rhythm too) |
| `` ` `` | CHORD HOLD: the chord and its accompaniment go on after the buttons are let go |
| `Tab` | CHORD AUTO: the chord and a bass play in the pattern's rhythm (MANUAL: the chord simply sounds) |
| `Space` · `Shift+Space` | rhythm start / stop · SYNC START: the rhythm starts with the next chord |
| `← →` | rhythm pattern, changing at the next bar. SET 1: ROCK 1, ROCK 2, SLOW ROCK, COUNTRY, SWING; SET 2: DISCO, HIP HOP, FUNK, BOSSANOVA, WALTZ; BARE!'s: POP, 16 BEAT, REGGAE and SIEVE (XENAKIS's sieves) |
| `Home` `End` | voice: omni 1, omni 2, harp, celeste, A. piano, guitar, FM piano, organ, vibes, banjo — each a main sound on the strings and a sub sound — and CUSTOM (the sounds `Shift+↑↓` and `Shift+←→` pick, the samples and your instruments among them) |
| `↑ ↓` | the strings' octave |
| `PgUp` `PgDn` · `Shift+PgUp` `PgDn` | tempo (shared with the tracker) · transpose, ±6 semitones |
| `Shift+Home` `End` | master tune, ±6 Hz around A 440 |
| `Caps Lock` | KEYBOARD mode, below |
| `Delete` | CLASSIC: the rhythm section with the drum kit BARE! had before |

KEYBOARD mode, as on the OM-108: the 7th row is a keyboard's white keys (`A` … `'` and `Enter`, C to G), the MINOR
row its black keys (`W` `E` `T` `Y` `U` `O` `P` `]`), and the MAJOR row is T, P, ↓, ↑ and eight drums: `3` `4` move
the keyboard an octave, `1` (T) held with them transposes and `2` (P) held with them tunes; `5` … `=` are bass drum,
snare, high tom, floor tom, closed and open hi-hat, crash and hand claps. The strings become drums (`Z` … `M`: BD SD HT
LT CH OH CC) and INSTANT OFF claps. omni 1 plays one note at a time, the last one held.

With the pointer: press a chord button to play it (`Shift`-click holds several, for the combinations), drag across
the strings, press INSTANT OFF, click the switches, the VOICE and PATTERN buttons (SET pages them) and START; drag a
knob up or down: MAIN and SUB (the two sounds' levels), SUSTAIN (how long the strings ring), CHORD, RHYTHM and TEMPO.
`O` in FILE's MIDI view sends it all as the OM-108 does: strings on channel 1, chord 2, bass 3, sub 4, drums 10. The
chord, voice, levels, switches, transpose, tune and pattern are saved with the project.

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
| `Shift+4` | Doom's E1M1 as breakcore: the song is read from the Doom WAD on the stick (its DOOM folder; Doom II's MAP01 if there is no E1M1), laid onto the grid — the guitars on three channels, the bass on a fourth — and arranged at 172 BPM over chopped breakbeats, snare rolls, stutters and junk metal, with an intro break, a breakdown and a tape stop. The notes come from your WAD; BARE! carries none of Doom's music |

Effects, 16 ticks to a row: `0xy` arpeggio (+x, +y semitones), `1xx`/`2xx` slide up/down, `3xx` glide to the note,
`4xy` vibrato, `8xx` pan (80 centre), `Axy` volume up x or down y, `Bxx` jump to order entry xx, `Cxx` volume,
`Dxx` next pattern from row xx, `ECx` cut after x ticks, `EDx` delay x ticks, `E9x` retrigger every x ticks, `Fxx`
tempo (20–FF) or rows a beat (01–1F). `00` repeats an effect's last value. On wide screens the list of sounds and
effects sits beside the pattern. Songs from the first development versions load into pattern 0, their ties held and
their gates turned into cuts.
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
- Out: `N` the tracker's channels 1–8 as MIDI channels 1–8, `K` clock, start and stop at the tempo, `T` thru, `O` the
  omnichord as the OM-108 sends it (strings on channel 1, chord 2, bass 3, sub 4, drums 10). With `K` on, the rhythm
  section's start and stop go out too while the tracker is stopped.
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
the next boot. `Tab` steps through SONGS, MIDI, KEYS (which F key opens what, above) and the log: the kernel log and
the state of the sound device (a photo
of it helps with a problem on a machine without a serial port); there `T` plays a test tone, `P` turns every output
on, ignoring the jacks, and `W` writes a hardware report to the stick: HB-LOG.TXT (the whole log), HB-PCI.TXT (every
PCI device) and HB-ACPI.BIN (the firmware's ACPI tables), for looking at a machine from another computer.

**Install** (`F7`, `Tab` five times) — puts the instrument on the computer's own disk, so it starts without the stick.
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
INPUT, STRETCH, TAPE, TOUCH, ANS, UPIC (METASTASEIS too), CLOUDS and LINEAGE (REICH, CARLOS, RADIGUE, MERZBOW; and DOOM,
once Doom has run), each with a meter, fader (−60 to +12 dB), pan, echo and reverb sends, mute and solo; then the
master with the volume and how much the limiter is taking off; and the effects along the bottom. Where the strips
don't all fit (800x600), they scroll to keep the chosen one in view.

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
(turn HUM up). Under the board, a short history: Michel Waisvisz and his Crackle Box, the Kraakdoos he made at STEIM
in Amsterdam, a box of bare contacts whose player's body becomes part of the circuit. TOUCH is after it: not a copy
of his circuit, the same idea.

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

**LINEAGE page** (`F11`, or `Ctrl+-`) — homages you can play, in the order of the music they come from: XENAKIS
(1954, five views of its own: below), ANS (1957), REICH (1965), CARLOS (1968), RADIGUE (1969) and MERZBOW (1979).
It opens on ANS; `F11` again goes to the next, and a click on the bar under the title picks one. Under each, a short
history: who, how it worked, and what to listen to. They sound through the mixer's LINEAGE channel (ANS and
XENAKIS's through their own), and all of it is saved with the project. None of the people named here is involved
with BARE!.

- **ANS** — after Evgeny Murzin's ANS synthesizer (1957), which played light through scratches in a glass plate. The
  plate here is 360 pure tones, 72 to the octave (a sixth of a semitone apart), over five octaves up the page, and time
  across it; a slit crosses it in a number of bars of the tempo (or of seconds, up to four minutes) and plays every tone
  it lights, as loud as the plate is bright there. Draw on it: every finger on the touchpad scratches (harder is
  brighter where the pad can tell), the mouse draws, its right button erases; `Tab`: pen, line (press, drag, let go: a
  straight glissando), eraser. `Space` plays, `Home` goes back to the start, `Backspace` twice clears the plate
  (`Ctrl+Z` brings it back). `Insert` scratches a drone in the manner of Coil's ANS, recorded on the machine in Moscow:
  dark bands low on the plate that swell and drift a few microtones, a haze above them, now and then a scratch high
  up. The camera (a USB webcam, or the laptop's own): `Enter` lays its next picture over the plate — its light above
  the BLACK level, or its edges (PICTURE) — and `\` lets the live camera be the plate while it plays: whatever the
  camera sees, you hear. On a BIOS (legacy) boot the BIOS keeps USB: pressing either key twice hands USB to BARE! until
  the next boot (as `U` in the MIDI view does), then the camera is found. The letter rows are a keyboard of its tones
  (`Z`–`/`, `Q`–`P`, two octaves from KEYS; `PgUp PgDn` move it), and so are MIDI notes: held while the plate plays,
  they write their row under the slit. `↑ ↓` picks a knob, `← →` turns it: LENGTH (1–32 bars, or 2 s to 4 minutes),
  RANGE (where the five octaves start: C1 to C4), PICTURE, BLACK, INK (how bright the pen draws), BRUSH, LEVEL, KEYS.
- **REICH** — after Steve Reich's phasing: two to four players loop the same pattern, and a process moves them apart
  and back together, on the exact sample. `Tab` picks the process: PHASE (after Piano Phase: a player holds, then
  plays a little faster until it is a step ahead — the third player two — and locks there, holds, moves again, back
  in unison after as many moves as the pattern has steps), SHIFT (after Clapping Music: it jumps a step ahead after
  some repeats), DRIFT (after It's Gonna Rain: each player a little faster than the one before, never locking) and LOOP
  (the same with a sampler slot as the loop: record a voice on WAVE's sampler and it drifts against itself). The rings
  are the players, turned by how far each is ahead. `Space` plays; the letter rows (and MIDI) write notes at the
  cursor, `Backspace` a rest, `[ ]` move the cursor, `- =` the number of steps (2–16), `Enter` makes a pattern from
  the omnichord's chord. `↑ ↓ ← →`: PROCESS, PLAYERS, STEPS, A BEAT (steps to a beat), TEMPO, HOLD (repeats before a
  move), MOVE (repeats a move takes) or DRIFT (how much faster, 0.1–5 %), SOUND (MARIMBA, PIANO, VIBES, CELESTE, HARP,
  GUITAR, CLAP, CLAVES) or the SLOT, LEVEL. `PgUp PgDn` the keys' octave.
- **CARLOS** — after Wendy Carlos: a Moog-style voice (a saw, square or triangle through a resonant low-pass with its
  contour, and glide), in equal temperament or her ALPHA (78.0 cents a step), BETA (63.8) and GAMMA (35.1) scales,
  which don't repeat at the octave but land close to pure thirds and fifths. The letter rows play 33 consecutive steps
  of the scale from C of the octave; the touchpad is a ribbon over two octaves (steps or free). The ruler shows equal
  temperament above, the scale below, and the pure intervals between. `Tab` scale, `Space` one voice gliding (as the
  Moog) or chords, `Enter` plays the omnichord's chord in the scale and then in equal temperament and says how far
  each interval is from pure. `↑ ↓ ← →`: SCALE, OCTAVE, VOICES, GLIDE, WAVE, CUTOFF, RES, CONTOUR, ATTACK, DECAY,
  SUSTAIN, RELEASE, LEVEL, RIBBON. `PgUp PgDn` octave.
- **RADIGUE** — after Éliane Radigue: a drone of eight partials of one tone, each a harmonic of the base (1–16) tuned
  up to 20 cents off it in tenths of a cent: two on the same harmonic beat at the difference of their frequencies,
  shown in Hz in the table. Each breathes in and out over its own period (30 s to 10 minutes); the base glides to a new
  note over the SWEEP's time, up to half an hour; `Space` fades the whole in and out. The picture is a slow score of
  the last minutes, each partial a line as bright as it sounds. The letter rows (and MIDI) send the base to a note,
  `1`–`8` pick a partial, a finger on the touchpad detunes it (across) and sets its level (up and down). `↑ ↓ ← →`:
  BASE, SWEEP, FADE, BREATH (how far they breathe out), LEVEL, and the partial's HARMONIC, DETUNE (`Shift` for tenths),
  LEVEL and BREATHES. `PgUp PgDn` the keys' octave, `Home` starts over.
- **MERZBOW** — after Merzbow: noise from junk. Fingers on the touchpad (or the mouse on the picture) scrape metal —
  where, how hard and how fast set the band, the level and the grit; the letter rows strike 33 pieces of junk (an
  inharmonic clang each); `Space` held feeds the output back into itself until it howls (`Shift+Space` keeps it);
  `Enter` plays BARE!'s own program code as 8-bit sound. Over all of it DRIVE, CRUSH (BITS) and CHOP; FEEDBACK, GRAIN
  (the scrape's resonance), BYTES (how fast the code is read) and LEVEL are the other knobs (`↑ ↓ ← →`), `Backspace`
  stops everything. However hard it is driven, a cap holds its loudness near −12 dB: noise to play, not to hurt.

**XENAKIS** (in LINEAGE; `F12`, or `Ctrl+=`, goes straight to it) — five of Iannis Xenakis's ways of making music, a
view each on a second bar under LINEAGE's, in the order of the music: METASTASEIS (1954), CLOUDS (1956), SIEVES
(1966), UPIC (1977) and GENDY (1991). `F12` again goes to the next view; a click on its bar picks one. METASTASEIS
and UPIC come with a short history. Clouds and UPIC (with METASTASEIS) have their own channels on the MIX page, and
everything here is saved with the project.

- **METASTASEIS** — after the piece (1953–54) whose 46 string players each slide in a glissando of their own: families
  of strings strung between two guide lines on graph paper. Straight lines crossed draw a curve that no single line
  draws — a ruled surface, the geometry of the Philips Pavilion he designed for Le Corbusier's studio (Expo 58); it
  turns beside the score in 3D. Drag a line with the mouse for guide A, `Tab`, another for guide B; two fingers on the
  touchpad hold a guide's two ends and move it live. Four families (`1`–`4`), each up to 46 strings, evenly spaced or
  in the golden section's proportions (MODULOR, after Le Corbusier), straight or CROSSED, sounded by a SECTION (violins
  I and II, violas, cellos, basses, or the whole orchestra as the score splits them). `Space` plays: a cursor crosses
  the page and every string it crosses sounds. `Home` start, `PgUp PgDn` transpose, `C` crossed, `M` modulor, `O` the
  family on or off, `↑ ↓ ← →` the knobs (FAMILY, ON, STRINGS, CROSSED, MODULOR, SECTION, LEVEL, LENGTH), `Enter`
  writes the family onto UPIC's page, `Backspace` twice brings back the opening.
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

**Tested so far.** On a real laptop (an ASUS VivoBook): the USB stack (sticks, MIDI, the built-in webcam) and the I2C
touchpad driver. On emulated machines only: AC97, the Sound Blaster 16, the SATA and NVMe drivers and the installer,
Ethernet, two fingers on a Synaptics pad; Intel VMD (not emulated) not at all. Link was checked against Ableton's own
library.

Reference hardware: ThinkPad X250 (i8042 keyboard, Synaptics PS/2 trackpad, Realtek HDA codec, AHCI).

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
A power cut mid-update leaves the old slots bootable. Sticks from the development versions before the name changed
(numbered 1.0 to 2.2, as homebrew) look for `HOMEBREW.UPD`: copy `BARE.UPD` under that name. `make usb` re-flashes
the boot partition but keeps the tape partition, so projects survive.

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
