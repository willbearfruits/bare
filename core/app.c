#include "app.h"
#include "harmony.h"
#include "ui.h"
#include "touch.h"
#include "install.h"
#include "net.h"
#include "link.h"
#include "gfx.h"
#include "omni.h"
#include "carlos.h"
#include "seq.h"
#include "wave.h"
#include "stretch.h"
#include "disk.h"
#include "fm.h"
#include "tape.h"
#include "synth.h"
#include "audio.h"
#include "rhythm.h"
#include "sampler.h"
#include "midi.h"
#include "song.h"
#include "undo.h"
#include "keys.h"
#include "log.h"
#include "splash.h"
#include "perf.h"
#include "ans.h"
#include "gendy.h"
#include "sieve.h"
#include "upic.h"
#include "inst.h"
#include "doomhost.h"
#include "fkeys.h"
#include "xen.h"
#include "lineage.h"

#ifndef BUILD_VERSION
#define BUILD_VERSION 0
#endif
/* The build number lives in the image behind a magic so tools/hb.py and mkimage.py can read it from the ELF
   (an update file must carry exactly the number the kernel inside it reports, or it would reinstall itself forever). */
struct build_tag { char magic[8]; uint32_t version; };
static volatile const struct build_tag app_build_tag = { { 'H', 'B', 'V', 'E', 'R', '0', '0', '1' }, BUILD_VERSION };
uint32_t app_version;

bool app_thermal;                 /* F12: the machine's temperature plays the filter, and idling heats the CPU */
int  app_temp = -1, app_fan = -1;
struct fb_info app_fb;

static int page = PAGE_PLAY, cur_key;                         /* the page showing; the key whose tab is lit */
int app_page(void) { return page; }
int app_key(void) { return cur_key; }
static void arrive(int k, uint64_t now);                       /* key k opens what it opens (below) */
static uint8_t splash_shown;                                  /* the splash this boot played + 1: for the stick, so the next boot picks another */
static bool ctrl;

/* The cell font: 12x24 where it still leaves the full layouts' 128x40 cells, 8x16 below — a 1280x1024 monitor gets
   160x64 small cells rather than 106x42 big ones. On 4K and other very large screens the same fonts are doubled (the
   pointer too), so the pages keep the proportions they have at 1080p. */
static const struct font *cell_font(uint32_t w, uint32_t h) {
    if (w >= 3072 && h >= 1920) return text_font_doubled(&font_t12x24);
    if (w >= 2048 && h >= 1600) return text_font_doubled(&font_t8x16);
    if (w >= 1536 && h >= 960) return &font_t12x24;
    return &font_t8x16;
}

/* Memory is shared out once, at boot: what the screen buffers leave goes to the sampler and the tape, keeping a
   little for later. The smallest machines it runs on (32 MB) get seconds of each; more RAM scales them up to caps.
   Where there is plenty, a piece is kept for Doom first. */
static void plan_memory(uint32_t rate) {
    uint32_t avail = plat_alloc_avail(), keep = 2u << 20;
    avail = avail > keep ? avail - keep : 0;
    perf_alloc(MIN(avail / 24, (1u << 20) + 4096));           /* the FX page's ring: up to 5.5 s */
    avail = plat_alloc_avail(); avail = avail > keep ? avail - keep : 0;
    doom_reserve(avail);
    avail = plat_alloc_avail(); avail = avail > keep ? avail - keep : 0;
    logf("memory: %u MiB for sound", avail >> 20);
    uint32_t smp = MIN(avail / 4, 64u << 20), und = CLAMP(avail / 16, 1u << 20, 64u << 20);
    sampler_init(rate, smp);
    undo_init(und);
    tape_init(rate, avail > smp + und ? avail - smp - und : 0);
}

void app_init(const struct fb_info *fb, uint32_t sample_rate) {
    app_fb = *fb;
    app_version = app_build_tag.version;
    gfx_init(fb);
    text_init(cell_font(fb->width, fb->height));
    gfx_pointer_scale(text_font()->height >= 32 ? 2 : 1);
    logf("screen %ux%u, %dx%d cells", fb->width, fb->height, text_cols(), text_rows());
    audio_init(sample_rate);
    midi_init(sample_rate);
    seq_init_rate(sample_rate);
    omni_init();
    carlos_init();
    net_init();
    link_init();
    seq_init();
    wave_init();
    stretch_init(sample_rate);
    fm_init();
    gendy_init();
    sieve_init();
    plan_memory(sample_rate);
    logf("app: BARE! %s, build %u", BARE_RELEASE BARE_STAGE, app_version);
    /* storage: find the stick, apply updates, bring back the last project */
    ui_boot_message("looking for storage", "");
    disk_init();
    char msg[96] = "";
    if (disk_check_update(app_version, msg, sizeof msg)) {
        ui_boot_message(msg, "restarting in 3 seconds");
        uint64_t t = plat_ms(); while (plat_ms() - t < 3000) plat_idle();
        plat_reboot();
    } else if (msg[0]) {
        logf("app: %s", msg);
        ui_boot_message(msg, "continuing with the current build");
        uint64_t t = plat_ms(); while (plat_ms() - t < 2500) plat_idle();
    }
    /* the splash: a different piece each boot where the stick remembers the last one — the header write the
       autoload makes carries it */
    int splash = -1;
    if (splash_mode != SPLASH_OFF) {
        splash = splash_mode >= 0 ? splash_mode % SPLASHES : splash_pick(disk.have_tape && disk.tape.splash ? disk.tape.splash - 1 : -1);
        disk.tape.splash = splash_shown = (uint8_t)(splash + 1);
    }
    inst_load_all();                                          /* the instruments, before the project that may tune them */
    fkeys_load();                                             /* which key opens what: KEYS.TXT, which may name them */
    if (disk_autoload()) logf("app: autoloaded last project");
    uint8_t vol, flags, theme; uint16_t mw;
    if (disk_settings(&vol, &flags, &mw, &theme)) {
        audio_set_volume_index(vol); audio_set_mute(flags & SET_MUTED); plat_audio_set_onebit(flags & SET_ONEBIT);
        if (theme && theme < GFX_THEMES) gfx_theme(theme);
        if (flags & SET_LINK) link_enable(true);                          /* it waits for the network itself */
        logf("app: settings from the stick: %d dB%s%s%s, colours %s", audio_volume_db(), flags & SET_MUTED ? ", muted" : "",
             flags & SET_ONEBIT ? ", 1-bit" : "", flags & SET_LINK ? ", Link" : "", gfx_theme_name(gfx_theme_now()));
        /* MIDI: the port (if this machine has it), the channel, and the four switches */
        midi.in_channel = (uint8_t)MIN(mw >> 4 & 31, 16);
        midi.clock_in = mw >> 9 & 1; midi.clock_out = mw >> 10 & 1; midi.notes_out = mw >> 11 & 1; midi.thru = mw >> 12 & 1; midi.omni_out = mw >> 13 & 1;
        if ((mw & 15) && midi_open((mw & 15) - 1)) logf("app: MIDI port %d from the stick", (mw & 15) - 1);
    }
    for (int k = 0; k < FKEYS; k++) if (fkeys[k].kind != FK_OFF) { arrive(k, 0); break; }   /* F1's, or the first key's */
    if (splash >= 0) splash_start(splash);
}

static void toggle_thermal(void) {
    app_thermal = !app_thermal;
    plat_set_burn(app_thermal);
    if (!app_thermal) synth_set_mod(0, 0);
}

static void panic(void) { seq_play(false); rhythm_play(false); tape.playing = false; tape.recording = false; omni_panic(); touch_lift_all(); inst_select(inst_selected()); }

/* Global keys first (page keys, F9–F12, Esc, Ctrl combos for laptops without a free F row), then the page, then
   whatever the page didn't use goes to the chord buttons and strum plate — on pages that play them. */
/* Shift+key: the functions that work on every page. False leaves the key to the page (SEQ: ⇧1-3 demos). */
static bool shift_function(uint8_t code, uint64_t now) {
    switch (code) {
    case KEY_UP:    omni_sound(false, +1, now); return true;
    case KEY_DOWN:  omni_sound(false, -1, now); return true;
    case KEY_RIGHT: omni_sound(true, +1, now); return true;
    case KEY_LEFT:  omni_sound(true, -1, now); return true;
    case 'b': plat_audio_toggle_onebit(); return true;
    case 'e': audio_set_echo(!audio_echo()); return true;
    case 'f': stretch_freeze(!stretch.frozen); return true;
    case 't': toggle_thermal(); return true;
    case 'm': audio_toggle_mute(); return true;
    case '-': audio_volume_step(-1); return true;
    case '=': audio_volume_step(+1); return true;
    case '/': ui_help = !ui_help; return true;
    case 'h': {                                                       /* the colour scheme: ten, round and round */
        gfx_theme(gfx_theme_now() + 1);
        char m[48]; snfmt(m, sizeof m, "colours: %s (%d of %d)", gfx_theme_name(gfx_theme_now()), gfx_theme_now() + 1, GFX_THEMES);
        ui_notice(m, now); return true; }
    case 'z': case 'y': { char m[48]; if (code == 'z') undo_undo(m, sizeof m); else undo_redo(m, sizeof m); ui_notice(m, now); return true; }
    case 'k':                                                         /* keys follow the chord, or not */
        harmony_on = !harmony_on;
        ui_notice(harmony_on ? "keys follow the chord" : "keys play as they are", now); return true;
    }
    return false;
}

/* The F keys open what core/fkeys.h's layout says. Inside a page: PLAY's instrument (+1, 0 the omnichord) and
   LINEAGE's view (XENAKIS keeps its own view as it was left); a key opening the whole page comes back to where it was
   left, the first time to the page's start. */
static int inside(int pg) { return pg == PAGE_PLAY ? play_showing() : pg == PAGE_LINEAGE ? lineage_current() : -1; }
static int start_of(int pg) { return pg == PAGE_PLAY ? 0 : pg == PAGE_LINEAGE ? LV_ANS : -1; }   /* as the pages start */
static void arrive(int k, uint64_t now) {
    struct fkey *f = &fkeys[k], *was = &fkeys[cur_key];
    int i = f->kind == FK_INST ? fkey_inst(f) : 0;
    if (i < 0) { char m[48]; snfmt(m, sizeof m, "%s: no %s on this stick", fkey_names[k], f->inst); ui_notice(m, now); return; }
    if (was->kind == FK_PAGE && was->page == page) was->left_at = (int8_t)inside(page);
    int to = fkey_page(f), at = f->kind == FK_INST ? i + 1 : f->kind == FK_VIEW ? f->view : f->left_at >= 0 ? f->left_at : start_of(to);
    if (to == PAGE_PLAY) play_show(at); else if (to == PAGE_LINEAGE) lineage_goto(at);
    if (f->kind == FK_VIEW && f->sub) xen_goto(f->sub - 1);            /* one of XENAKIS's own views */
    page = to; cur_key = k;
}
/* Pressed again: a key opening a view that no longer shows (another was picked on the bar) goes back to it; one
   opening XENAKIS or one of its views steps XENAKIS's views; any other steps its page (PLAY's instruments, LINEAGE's
   homages). */
void app_goto_key(int k, uint64_t now) {
    if (k < 0 || k >= FKEYS || fkeys[k].kind == FK_OFF) return;
    const struct fkey *f = &fkeys[k];
    if (k == cur_key && fkey_page(f) == page) {
        if (f->kind == FK_VIEW && lineage_current() != f->view) arrive(k, now);
        else if (f->kind == FK_VIEW && f->view == LV_XEN) xen_again();
        else if (ui_pages[page]->again) ui_pages[page]->again(now);
        return;
    }
    arrive(k, now);
}
static int fkey_of(uint8_t code) {                            /* F1 … F12, or Ctrl+1 … 9, 0, -, = : 0 … 11 */
    if (code >= KEY_F1 && code <= KEY_F12) return code - KEY_F1;
    if (!ctrl) return -1;
    return code >= '1' && code <= '9' ? code - '1' : code == '0' ? 9 : code == '-' ? 10 : code == '=' ? 11 : -1;
}

/* Keys: the F keys (or Ctrl+1…9, 0, -, =) open what they open and do nothing else; Shift+key is the function layer;
   the rest goes to the page, and what the page leaves plays chords and strums on pages that play them. */
static bool consumed[256];                                /* a key whose press was a function: its release is too */
static void bare_key(struct key_event ev, uint64_t now) {
    if (splash_showing()) { if (ev.down) { splash_skip(); consumed[ev.code] = true; } return; }   /* any key ends it */
    if (song.exporting) { page = PAGE_FILE; ui_pages[page]->key_event(ev.code, ev.down, now); return; }   /* only ESC, on the export */
    const struct page *pg = ui_pages[page];
    if (ev.code == KEY_LCTRL || ev.code == KEY_RCTRL) { ctrl = ev.down; return; }
    if (ev.code == KEY_LSHIFT || ev.code == KEY_RSHIFT) { ui_shift = ev.down; return; }
    if (pg->typing && pg->typing()) { pg->key_event(ev.code, ev.down, now); return; }
    if (!ev.down && consumed[ev.code]) { consumed[ev.code] = false; return; }
    if (ev.down) {
        int k = fkey_of(ev.code);                             /* a key that opens nothing does nothing */
        if (k >= 0) { app_goto_key(k, now); consumed[ev.code] = true; return; }
        if (ui_shift && shift_function(ev.code, now)) { consumed[ev.code] = true; return; }
        switch (ev.code) {
        case KEY_VOLUP:   audio_volume_step(+1); return;
        case KEY_VOLDOWN: audio_volume_step(-1); return;
        case KEY_MUTE:    audio_toggle_mute(); return;
        case KEY_ESC:     if (ui_help) ui_help = false; else panic(); return;
        }
    }
    if (ctrl && ev.down && (ev.code == 'z' || ev.code == 'y')) {      /* Ctrl+Z, Ctrl+Y: as everywhere else */
        char m[48]; if (ev.code == 'z') undo_undo(m, sizeof m); else undo_redo(m, sizeof m); ui_notice(m, now);
        consumed[ev.code] = true; return;
    }
    if (ctrl) return;
    int sound = pg->strum_sound ? pg->strum_sound() : -1;
    omni.strum_override = sound >= 0 ? (uint8_t)sound : 0xFF;
    if (pg->key_event && pg->key_event(ev.code, ev.down, now)) return;
    if (pg->plays_omni) omni_key(ev.code, ev.down, now);
}

/* Typing iddqd, on any page, starts Doom (or goes back to it): the letters still do what they do on the page. While
   Doom shows, it has every key but the F keys that open something (they leave it for that) and the laptop's volume
   keys; a key held from before it started is let go on BARE!'s side. */
static uint32_t bare_held[8];                             /* keys pressed on BARE!'s side and not let go yet */
static bool iddqd(struct key_event ev, uint64_t now) {
    static const char word[] = "iddqd";
    static int got; static uint64_t last;
    if (!ev.down) return false;
    if (now - last > 3000) got = 0;
    last = now;
    got = ev.code == (uint8_t)word[got] ? got + 1 : ev.code == 'i' ? 1 : 0;
    if (got < 5) return false;
    got = 0;
    return true;
}
static void handle_key(struct key_event ev, uint64_t now) {
    uint32_t mask = 1u << (ev.code & 31), *hb = &bare_held[ev.code >> 5];
    if (doom_showing() && !(!ev.down && (*hb & mask))) {
        int k = ev.code >= KEY_F1 && ev.code <= KEY_F12 ? ev.code - KEY_F1 : -1;
        if (k >= 0 && fkeys[k].kind != FK_OFF) { if (ev.down) { doom_leave(); arrive(k, now); consumed[ev.code] = true; } return; }
        switch (ev.code) {
        case KEY_VOLUP:   if (ev.down) audio_volume_step(+1); return;
        case KEY_VOLDOWN: if (ev.down) audio_volume_step(-1); return;
        case KEY_MUTE:    if (ev.down) audio_toggle_mute(); return;
        }
        doom_key(ev.code, ev.down);
        consumed[ev.code] = ev.down;                          /* let go after leaving Doom: not BARE!'s */
        return;
    }
    const struct page *pg = ui_pages[page];
    bool counts = !splash_showing() && !song.exporting && !(pg->typing && pg->typing()) && !ctrl && !ui_shift;
    if (ev.down) *hb |= mask; else *hb &= ~mask;
    bare_key(ev, now);
    if (counts && iddqd(ev, now)) doom_open(now);
}

/* MIDI coming in: a note plays the page's sound (or the page takes it: the tracker writes it in), velocity and all;
   the sustain pedal holds what is let go under it; pitch bend ±2 semitones; the mod wheel opens the filter */
#define TAG_MIDI 0x600
static uint32_t midi_down[4], midi_held[4];                /* notes a key holds; notes the pedal holds */
static bool pedal; static int32_t bend;
static uint8_t played[128];                                /* the note each incoming one sounds (keys follow the chord) */
static inline bool bit(const uint32_t *m, int n) { return m[n >> 5] >> (n & 31) & 1; }
static inline void set_bit(uint32_t *m, int n, bool on) { if (on) m[n >> 5] |= 1u << (n & 31); else m[n >> 5] &= ~(1u << (n & 31)); }
static void app_midi(struct midi_msg m, uint64_t now) {
    const struct page *pg = ui_pages[page];
    uint8_t type = m.status & 0xF0, n = m.d1 & 127;
    if (type == 0x90 && m.d2) {
        splash_skip();                                         /* a note ends the splash, and plays */
        if (pg->midi && pg->midi(n, m.d2, now)) return;
        int sound = pg->strum_sound ? pg->strum_sound() : omni.strum_preset;
        played[n] = (uint8_t)harmony_note(n);
        synth_note_on(played[n], m.d2, (uint8_t)sound, (uint16_t)(TAG_MIDI | n));
        if (bend) synth_tag_bend((uint16_t)(TAG_MIDI | n), bend);
        set_bit(midi_down, n, true); set_bit(midi_held, n, false);
    } else if (type == 0x80 || type == 0x90) {
        if (pg->midi && pg->midi(n, 0, now)) return;
        set_bit(midi_down, n, false);
        if (pedal) set_bit(midi_held, n, true); else synth_note_off_tag((uint16_t)(TAG_MIDI | n));
    } else if (type == 0xB0 && m.d1 == 64) {
        pedal = m.d2 >= 64;
        if (!pedal) for (int k = 0; k < 128; k++) if (bit(midi_held, k)) { synth_note_off_tag((uint16_t)(TAG_MIDI | k)); set_bit(midi_held, k, false); }
    } else if (type == 0xB0 && (m.d1 == 1 || m.d1 == 74)) {
        if (!app_thermal) synth_set_mod(m.d2 * 40 / 127, 0);
    } else if (type == 0xB0 && (m.d1 == 16 || m.d1 == 17)) {             /* the FX pad from a controller: X, Y */
        if (m.d1 == 16) perf.x[perf.sel] = (int16_t)(m.d2 * 1000 / 127); else perf.y[perf.sel] = (int16_t)(m.d2 * 1000 / 127);
    } else if (type == 0xB0 && m.d1 >= 102 && m.d1 <= 109) {              /* its effects, held while the CC is 64 or more */
        perf.on[m.d1 - 102] = m.d2 >= 64 || perf.latched[m.d1 - 102];
    } else if (type == 0xB0 && (m.d1 == 120 || m.d1 == 123)) {
        for (int k = 0; k < 128; k++) synth_note_off_tag((uint16_t)(TAG_MIDI | k));
        memset(midi_down, 0, sizeof midi_down); memset(midi_held, 0, sizeof midi_held);
    } else if (type == 0xE0) {
        bend = (((int32_t)m.d2 << 7 | m.d1) - 8192) / 16;         /* ±2 semitones, in 1/256 semitone */
        for (int k = 0; k < 128; k++) if (bit(midi_down, k) || bit(midi_held, k)) synth_tag_bend((uint16_t)(TAG_MIDI | k), bend);
    } else if (type == 0xC0) {
        omni.strum_preset = (uint8_t)(m.d1 % P_COUNT);
    }
}

static void personality_tick(uint64_t now) {
    static uint64_t last_sense, last_beat, last_jack; static int beat_led;
    if (now - last_jack >= 250) { last_jack = now; plat_audio_poll(); }
    if (now - last_sense >= 1000) {
        last_sense = now;
        app_temp = plat_cpu_temp(); app_fan = plat_fan_rpm();
        if (app_thermal && app_temp >= 0) {
            int t = app_temp - 45;                        /* cool = dark, hot = open and detuned */
            synth_set_mod(t * 2, t > 0 ? t * 6 : 0);
        }
    }
    /* lid logo / power LED blink on the beat while the sequencer runs */
    if (seq.playing && seq.pos >= 0) {
        int beat = seq.pos / (seq.lpb ? seq.lpb : 4);
        if (beat != beat_led) { beat_led = beat; plat_led(10, 1); plat_led(0, 1); last_beat = now; }
        else if (now - last_beat > 60) { plat_led(10, 0); }
    }
}

/* Input and background work: the first part of each pass of the main loop, and what Doom's waits run. */
void app_background(uint64_t now) {
    struct key_event ev;
    while (plat_key_poll(&ev)) handle_key(ev, now);
    omni_work(now);
    stretch_work();
    tape_work();
    sampler_work();
    song_work();
    install_work();
    net_work(now);
    link_work(now);
    perf_work(now);
    fkeys_work(now);
    ans_work(now);
    upic_work(now);
    midi_work(now);
    struct midi_msg mm;
    while (midi_next(&mm)) if (!song.exporting) app_midi(mm, now);   /* notes would end up in the export */
    personality_tick(now);
    uint16_t mw = (uint16_t)((midi.port + 1) & 15) | (uint16_t)(midi.in_channel << 4) | (uint16_t)(midi.clock_in << 9) |
                  (uint16_t)(midi.clock_out << 10) | (uint16_t)(midi.notes_out << 11) | (uint16_t)(midi.thru << 12) | (uint16_t)(midi.omni_out << 13);
    disk_note_settings((uint8_t)audio_volume_index(),                   /* rides along with the next header write */
                       (uint8_t)((audio_muted() ? SET_MUTED : 0) | (plat_audio_onebit_chosen() ? SET_ONEBIT : 0) | (lnk.on ? SET_LINK : 0)), mw,
                       (uint8_t)gfx_theme_now(), splash_shown);
}

/* One pass of the main loop: input, background work, and a new frame every 16 ms (or Doom's, while it shows). */
void app_step(uint64_t now) {
    static uint64_t last;
    app_background(now);
    if (doom_showing()) { doom_step(now); return; }
    if (now - last < 16) return;
    last = now;
    ui_pointer_update(now, ui_pages[page]->owns_pad && !ui_help);
    static bool splash_had_screen;
    if (splash_showing()) {                                    /* the boot's splash has the screen; a click or touch ends it */
        if (ptr.pressed || pad.touched) splash_skip();
        splash_draw();
        if (splash_showing()) { splash_had_screen = true; gfx_present(); return; }
    }
    if (splash_had_screen) { splash_had_screen = false; ui_redraw_all(); }   /* ended: by itself, a key, a click or a note */
    bool tabs = ui_tabs_pointer(now);                          /* a click or drag on the title bar's tabs */
    if (fkey_page(&fkeys[cur_key]) != page) { int k = fkeys_for_page(page); if (k >= 0) cur_key = k; }   /* keys moved, an export */
    const struct page *pg = ui_pages[page];
    int sound = pg->strum_sound ? pg->strum_sound() : -1;
    omni.strum_override = sound >= 0 ? (uint8_t)sound : 0xFF;
    if (pg->pointer && !tabs) pg->pointer(now);
    ui_draw(now, page);
    text_flush();
    ui_scan(wave_scan_tab);                                   /* the screen under the pointer is the SCAN wave */
    bool shown = ptr.shape == PTR_CROSS || now - ptr.moved_ms < 4000;
    gfx_pointer(ptr.x, ptr.y, shown ? ptr.shape : PTR_HIDDEN);
    gfx_present();
}

void app_run(void) {
    for (;;) {
        uint64_t now = plat_ms();
        app_step(now);
        if (plat_ms() == now) plat_idle();
    }
}
