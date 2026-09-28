# Making an instrument

An instrument is a small text file. Put it in the `INSTR` folder on the stick (it's on the stick's first partition,
the one any computer opens), start BARE!, and it is there: on the PLAY page, press `F1` again to step from the
Omnichord to each instrument in turn, or give it an F key of its own (FILE, `Tab` to KEYS, or a line in the stick's
KEYS.TXT: `F9 MYHARP`). The file says what it sounds like and how it's played. Nothing in it can crash
BARE!: a mistake is shown on the instrument's page and in the log (F7, Tab to LOG), and the rest of the file still
counts.

Its sound also becomes a sound like any other: the strum plate, the tracker, clouds and UPIC can all use it by its
name.

## An example

```
# STYLO: a pocket organ played with a stylus on a metal strip.

name: STYLO
by: your name
about: a stylus on a metal strip: the touchpad is the strip

[sound]
wave: pulse
width: 30
attack: 1
release: 15
level: 65

[play]
strip: A2 E4
steps: semitones
keys: chromatic A2
mono: yes

[knobs]
knobs: width cutoff vibrato release level
```

`#` starts a comment. Settings are `name: value` (or `name = value`), one a line, in any order within their section.
Anything you leave out has a sensible default.

## Before the sections

| setting | what it is |
|---|---|
| `name` | up to 8 letters: the page's tab and the sound's name. Required. |
| `by` | who made it |
| `about` | one line saying what it is |

A file on the stick with the same name as a built-in instrument replaces it.

## [sound]

| setting | values | what it does |
|---|---|---|
| `wave` | `pulse` `square` `saw` `tri` `sine` `noise` | the basic waves |
| | `drawn` `morph` `scan` | the WAVE page's drawn waves |
| | `gendy1` … `gendy4` | GENDY's patches (XENAKIS, in LINEAGE) |
| | `fm1` … `fm4` | the OPERATOR page's FM patches |
| | `sample1` … `sample8` | the sampler's slots |
| `width` | 5 – 95 | pulse width, % (for `pulse`) |
| `detune` | 0 – 100 | a second oscillator, a little off: thicker |
| `attack` `decay` `release` | 1 – 8000 | milliseconds |
| `sustain` | 0 – 100 | % |
| `cutoff` | 0 – 127 | the filter; 127 is open |
| `resonance` | 0 – 100 | |
| `envelope` | 0 – 100 | how far the envelope opens the filter |
| `vibrato` | 0 – 100 | |
| `level` | 0 – 100 | |

## [play]

| setting | values | what it does |
|---|---|---|
| `keys` | `chromatic NOTE`, `scale NOTE`, `off` | the letter rows (Z to / and Q to P) as a keyboard from NOTE: every semitone (the upper row an octave up), or up the scale, key by key (the upper row going on from the lower) |
| `strip` | `LOW HIGH` (two notes) | the touchpad as a strip: across it is the pitch, pressing harder is louder |
| `steps` | `semitones`, `scale`, `free` | on the strip: a note a key, the scale's notes, or no steps at all |
| `pads` | `COLUMNSxROWS NOTE` (up to 8x8) | a grid of pads, up the scale from NOTE; the touchpad stands for the grid |
| `scale` | `chromatic` `major` `minor` `dorian` `phrygian` `lydian` `mixolydian` `pentatonic` `minorpenta` `blues` `wholetone` `harmonic`, or `S1` … `S4` | the scale; S1–S4 are XENAKIS's sieves (in LINEAGE) |
| `mono` | `yes` / `no` | one note at a time (the newest) |
| `glide` | 0 – 2000 | milliseconds to slide from note to note |
| `arp` | `off` `up` `down` `updown` `random` | an arpeggio over the notes held, on the tempo |
| `rate` | 1 – 8 | arpeggio notes a beat |
| `octaves` | 1 – 4 | how many octaves the arpeggio spans |
| `hold` | `yes` / `no` / `toggle` | notes stay when let go, until new ones are played (Space turns it on and off); `toggle`: a key opens its note and the next press closes it, one by one (Space closes them all) |
| `tuning` | `equal`, `just`, `just NOTE` | equal temperament, or just intonation from a tonic (the keys' first note, or NOTE): each note is the 5-limit ratio of its interval (the fifth 3:2, the fourth 4:3, the major third 5:4 …) |
| `bellows` | `yes` / `no` | the notes sound only with air in the bellows: a finger moving to and fro on the touchpad (where it is not a strip or pads), the mouse moving, or `Enter` held pumps it, and it leaks out over a few seconds. Louder with more air, a few cents flat when it runs low |

Notes are written `C3`, `F#2`, `Bb4` (middle C is C4), or as MIDI numbers.

## [knobs]

`knobs:` lists up to 8 settings to turn on the page (↑ ↓ to choose, ← → to turn): any of `wave` `width` `detune`
`attack` `decay` `sustain` `release` `cutoff` `resonance` `envelope` `vibrato` `level` `glide` `rate` `octaves`.
What you turn is saved with the project.

## Sharing

An instrument is one text file, so it can go anywhere text goes: a message, a forum post, a repository. The examples
that come with BARE! are in `instruments/` in the source; they're a good place to start copying from.
