/* The homages' history panels (see lessons.h). "After": these pages play with the ideas; none of the people named is
   involved. Facts checked against published sources (September 2026); keep them to what is documented. */
#include "lessons.h"

const struct lesson lesson_ans = {
    "after Evgeny Murzin's ANS (1957) · and Coil",
    "The engineer Evgeny Murzin built the ANS in Moscow from 1937 to 1957 and named it after the composer Alexander "
    "Nikolayevich Scriabin. The composer scratches a glass plate coated in black mastic; light shines through the "
    "scratches onto tones printed on five glass discs, 720 of them, 1/72 of an octave apart, and photocells turn it "
    "into sound as the plate moves. Eduard Artemyev scored Tarkovsky's Solaris with it; Schnittke, Denisov and "
    "Gubaidulina wrote for it. Coil recorded their drone album ANS on the machine in Moscow (2003; a box set, 2004).",
    "Here the plate holds 360 of those tones, five octaves of 72. Draw with the mouse, every finger on the touchpad, "
    "or the camera; Space sends the slit across it.",
    "Artemyev: Solaris (1972) · Coil: ANS (2003, 2004)",
};

const struct lesson lesson_meta = {
    "after Iannis Xenakis, Metastaseis (1953–54)",
    "Xenakis worked as an engineer in Le Corbusier's studio when he wrote Metastaseis for 61 players: 46 strings, each "
    "with a part of its own, start in unison and slide apart. He drew their glissandi as straight lines on graph "
    "paper; together the lines make curved surfaces, hyperbolic paraboloids, and the same drawings became the Philips "
    "Pavilion at Expo 58. Meta-stasis: after standstill, a mass of sound transforming. Its durations follow Le "
    "Corbusier's Modulor proportions. First played at Donaueschingen, 16 October 1955, under Hans Rosbaud.",
    "Draw two guide lines. Strings are strung between them, each a straight glissando; crossed, they bend into a "
    "curve that no single string draws.",
    "Metastaseis (1954) · Pithoprakta (1956) · Concret PH (1958) · Mycenae-Alpha (1978)",
};

const struct lesson lesson_upic = {
    "after Xenakis's UPIC (1977)",
    "Xenakis wanted a machine that plays what a composer draws. At CEMAMu, his centre near Paris, the UPIC did: lines "
    "of pitch against time drawn on a large tablet became sound, a graphic score heard at once. Mycenae-Alpha (1978) "
    "was drawn on it, arc by arc. Its name says it was meant for teaching as much as composing: Unité Polyagogique "
    "Informatique du CEMAMu.",
    "Draw arcs: each is a voice that slides in pitch as the cursor passes. A line upwards is a glissando, a click a "
    "short note.",
    "Mycenae-Alpha (1978) · Taurhiphanie (1987) · Voyage absolu des Unari vers Andromède (1989)",
};

const struct lesson lesson_reich = {
    "after Steve Reich (b. 1936)",
    "Two tape loops of the same words drifted out of step, and the phrase broke into echoes, canons and new rhythms: "
    "It's Gonna Rain (1965). Steve Reich called it phasing and wrote it for players. In Piano Phase (1967) one pianist "
    "speeds up very slightly until the two are a note apart, and again; in Clapping Music (1972) one clapper moves an "
    "eighth note every 12 bars until both are back in unison, 144 bars later. \"I want to be able to hear the process "
    "happening throughout the sounding music.\" (Music as a Gradual Process, 1968)",
    "Players loop the same pattern: PHASE pulls one ahead a step at a time, SHIFT jumps it, DRIFT lets it wander. "
    "Enter builds the pattern from the chord you hold.",
    "It's Gonna Rain (1965) · Come Out (1966) · Piano Phase (1967) · Clapping Music (1972) · Music for 18 Musicians (1976)",
};

const struct lesson lesson_carlos = {
    "after Wendy Carlos (b. 1939)",
    "Trained in music and physics, Wendy Carlos helped Robert Moog shape his synthesizer: a touch-sensitive keyboard, "
    "portamento, a fixed filter bank. With producer Rachel Elkind she made Switched-On Bach (1968) on a Moog that "
    "played one note at a time, building every part on eight-track tape, line by line; it won three Grammys. Then A "
    "Clockwork Orange with an early vocoder, Sonic Seasonings (1972), Tron (1982). For Beauty in the Beast (1986) she "
    "devised scales that never come back to the octave: alpha, beta and gamma.",
    "The keys play equal temperament or her alpha (78.0 cents a step), beta (63.8) or gamma (35.1), steps chosen so "
    "thirds and fifths come out nearly pure. The sound: a saw through a resonant low-pass, with glide.",
    "Switched-On Bach (1968) · Sonic Seasonings (1972) · Tron (1982) · Beauty in the Beast (1986)",
};

const struct lesson lesson_radigue = {
    "after Éliane Radigue (1932–2026)",
    "Éliane Radigue learned tape with Pierre Schaeffer and Pierre Henry in Paris from 1955, then made music from "
    "feedback and tape loops. From Adnos I (1974) she worked for some 25 years with one ARP 2500 synthesizer: long "
    "tones tuned close together so that they beat, partials surfacing and fading over many minutes, changes too slow "
    "to notice until they have happened. Tibetan Buddhism shaped the Trilogie de la Mort. From 2001 she wrote for "
    "acoustic players, passing the pieces on by ear: Naldjorlak, and the OCCAM series.",
    "Eight partials of one tone, each tuned a hair apart: two close partials beat at their difference in Hz. Sweeps "
    "take minutes. Let it go.",
    "Adnos I–III (1974–80) · Trilogie de la Mort (1988–93) · L'Île re-sonante (2000) · OCCAM Ocean (from 2011)",
};

const struct lesson lesson_merzbow = {
    "after Merzbow (Masami Akita, b. 1956)",
    "Masami Akita began Merzbow in Tokyo in 1979, naming it after Kurt Schwitters' Merzbau, a house filled with found "
    "objects. He rubbed microphones on scrap, struck springs and metal cases strung with wire, fed back tapes and "
    "pedals, and turned it all up past distortion: noise as music, junk as instrument. Computers came in 1999. Pulse "
    "Demon (1996) is a landmark of harsh noise; Merzbox (2000) gathered 50 CDs. Vegan since about 2002, he gives much "
    "of his work to animals.",
    "Scrape with your fingers, strike the letter keys (junk metal); Space feeds the output back into itself, Enter "
    "plays this program's own bytes. A limiter keeps it from hurting.",
    "Venereology (1994) · Pulse Demon (1996) · 1930 (1998) · Merzbox (2000)",
};

const struct lesson lesson_touch = {
    "after Michel Waisvisz (1949–2008) and his Crackle Box",
    "Michel Waisvisz wanted electronic music played with the body. In the late 1960s he and Geert Hamelberg built the "
    "first crackle circuits: unstable oscillators on bare boards, played by fingers on the copper, so that the "
    "player's skin became part of the circuit. At STEIM, the Studio for Electro-Instrumental Music in Amsterdam, which "
    "he joined in 1973 and led from 1981 until his death, they became the Crackle Synth and the Kraakdoos (1974): a "
    "small box with a battery, a speaker and six metal contacts, wired to one of the first op-amps, the 709. No two "
    "people sound alike on it, nor one person on two days. In 1984 came The Hands: wooden frames on both hands whose "
    "sensors turned gestures into MIDI.",
    "Not a copy of his circuit but the same idea: eight bare pads around an op-amp, and your fingers are the wires. "
    "Two pads at once close the circuit through you; press harder and the skin lets more through: the pitch climbs.",
    "Lumps, with Steve Lacy (1974) · Crackle (1978) · The Hands, on stage (from 1984)",
};
