/* FILE: projects on the stick. The instrument runs from RAM; the stick only matters when saving or loading. */
#include "ui.h"
#include "disk.h"
#include "seq.h"
#include "keys.h"
#include "log.h"
#include "app.h"
#include "midi.h"
#include "song.h"
#include "sampler.h"
#include "tape.h"
#include "audio.h"
#include "install.h"
#include "net.h"
#include "link.h"
#include "platform.h"

extern uint32_t app_version; extern struct fb_info app_fb;

static int cursor, first;
static bool naming, confirm_del;
static char name[24]; static int name_len;
static char msg[64]; static uint64_t msg_ms; static bool msg_bad;
enum { V_PROJECTS, V_SONGS, V_MIDI, V_LOG, V_INSTALL, VIEWS };
static struct fat_entry wavs[64]; static int nwavs = -1, wav_cur, import_slot;   /* nwavs < 0: not listed yet */
static int view; static bool tone_on, all_on, cam_on; static int log_back;   /* TAB: MIDI, then the kernel log and the sound device */
static struct rect list_px;
/* INSTALL: the other drives, what is on each, and ERASE typed before anything is written */
#define TARGETS 8
static int targets[TARGETS], ntargets = -1, target_cur;
static char target_desc[TARGETS][40];
static bool erase_asked, relist; static char erase_typed[8]; static int erase_len;   /* relist: after a copy ends */
static void list_targets(void) {
    ntargets = 0;
    for (int d = 0; d < plat_blk_drives() && ntargets < TARGETS; d++) {
        if (d == disk.boot_drive) continue;
        targets[ntargets] = d; install_describe(d, target_desc[ntargets], sizeof target_desc[0]);
        ntargets++;
    }
    if (target_cur >= ntargets) target_cur = 0;
}

static void say(const char *m, uint64_t now) {
    snfmt(msg, sizeof msg, "%s", m); msg_ms = now;
    size_t n = strlen(m); msg_bad = n >= 6 && memcmp(m + n - 6, "failed", 6) == 0;   /* "save failed" and friends in red */
}
static int slots(void) { return disk.have_tape ? (int)disk.tape.slots : 0; }

static bool typing(void) { return naming || erase_asked; }

static void write_report(uint64_t now);
static bool key(uint8_t code, bool down, uint64_t now) {
    if (!down) return true;
    if (naming) {
        if (code == KEY_ENTER) {
            naming = false;
            if (!name_len) snfmt(name, sizeof name, "%s", seq.title);
            say(disk_save_slot(cursor, name) ? (disk.tape_left_out ? "saved, without the tape: the stick is full" : "saved") : "save failed", now);
        } else if (code == KEY_ESC) naming = false;
        else if (code == KEY_BACKSPACE) { if (name_len) name[--name_len] = 0; }
        else if (code >= ' ' && code < 0x7F && name_len < (int)sizeof name - 1) {
            char c = (char)code; if (c >= 'a' && c <= 'z') c -= 32;
            name[name_len++] = c; name[name_len] = 0;
        }
        return true;
    }
    if (erase_asked) {                                     /* INSTALL: ERASE typed, or not */
        if (code == KEY_ENTER) {
            erase_asked = false;
            if (erase_len != 5 || memcmp(erase_typed, "ERASE", 5)) { say("not installed: ERASE wasn't typed", now); return true; }
            if (!install_start(targets[target_cur])) say(inst.status, now); else relist = true;
        } else if (code == KEY_ESC) { erase_asked = false; say("not installed", now); }
        else if (code == KEY_BACKSPACE) { if (erase_len) erase_typed[--erase_len] = 0; }
        else if (code >= 'a' && code <= 'z' && erase_len < (int)sizeof erase_typed - 1) { erase_typed[erase_len++] = (char)(code - 32); erase_typed[erase_len] = 0; }
        return true;
    }
    if (song.exporting) { if (code == KEY_ESC) song_export_cancel(); return true; }
    if (inst.running) { if (code == KEY_ESC) install_cancel(); return true; }        /* the copy: only Esc */
    if (code == KEY_TAB) { view = (view + 1) % VIEWS; log_back = 0; nwavs = -1; ntargets = -1; return true; }
    if (view == V_INSTALL) {
        if (ntargets < 0) list_targets();
        switch (code) {
        case KEY_UP:   if (target_cur > 0) target_cur--; break;
        case KEY_DOWN: if (target_cur + 1 < ntargets) target_cur++; break;
        case 'r':      list_targets(); break;
        case 'i':
            if (!ntargets) { say("no drive to install on", now); break; }
            if (!disk.have_tape) { say("not running from a BARE! stick", now); break; }
            erase_asked = true; erase_len = 0; erase_typed[0] = 0;
            break;
        case KEY_ENTER: if (inst.done) plat_reboot(); break;
        }
        return true;
    }
    if (view == V_SONGS) {
        if (nwavs < 0) nwavs = song_wavs(wavs, 64);
        switch (code) {
        case 'e': if (song_export_start()) say("exporting", now); else say(song.status, now); break;
        case KEY_UP:   if (wav_cur > 0) wav_cur--; break;
        case KEY_DOWN: if (wav_cur + 1 < nwavs) wav_cur++; break;
        case '[': import_slot = (import_slot + SAMPLE_SLOTS - 1) % SAMPLE_SLOTS; break;
        case ']': import_slot = (import_slot + 1) % SAMPLE_SLOTS; break;
        case 's': case 't':
            if (wav_cur >= nwavs) { say("no WAV files on the stick", now); break; }
            song_import(&wavs[wav_cur], code == 't', code == 't' ? tape.cur : import_slot);
            say(song.status, now);
            break;
        case 'r': nwavs = song_wavs(wavs, 64); break;
        }
        return true;
    }
    if (view == V_MIDI) {
        const char *names[8]; int n = plat_midi_ports(names, 8);
        switch (code) {
        case KEY_LEFT: case KEY_RIGHT: {                   /* off, then each port */
            int p = midi.port + (code == KEY_RIGHT ? 1 : -1);
            if (p < -1) p = n - 1; if (p >= n) p = -1;
            say(midi_open(p) ? (p < 0 ? "MIDI off" : "port open") : "the port did not open", now);
            break; }
        case KEY_UP:   midi.in_channel = (uint8_t)((midi.in_channel + 1) % 17); break;
        case KEY_DOWN: midi.in_channel = (uint8_t)((midi.in_channel + 16) % 17); break;
        case 'c': midi.clock_in = !midi.clock_in; break;
        case 'k': midi.clock_out = !midi.clock_out; break;
        case 'n': midi.notes_out = !midi.notes_out; break;
        case 't': midi.thru = !midi.thru; break;
        case 'l': link_enable(!lnk.on); say(lnk.on ? "Link on" : "Link off", now); break;
        case 'u':                                          /* a BIOS boot: our own USB stack takes over, for USB MIDI */
            if (plat_usb() != 1) { say(plat_usb() == 2 ? "USB is ours already" : "no USB controller here that this can drive", now); break; }
            if (!plat_usb_takeover()) { say("USB could not be taken over", now); break; }
            disk_rescan();
            say(disk.have_tape ? "USB is ours now; the stick found again" : "USB is ours now, but the stick wasn't found again", now);
            break;
        }
        return true;
    }
    if (view == V_LOG) {
        switch (code) {
        case KEY_UP:   log_back++; break;
        case KEY_DOWN: if (log_back > 0) log_back--; break;
        case KEY_PGUP: log_back += 10; break;
        case KEY_PGDN: log_back = log_back > 10 ? log_back - 10 : 0; break;
        case 't':      tone_on = !tone_on; plat_audio_test_tone(tone_on); break;
        case 'p':      all_on = !all_on; plat_audio_all_outputs(all_on); break;
        case 'w':      write_report(now); break;
        case 'c':                                            /* the camera: on (its light too) and off, to see it works */
            if (!plat_camera(0, 0)) { say(plat_usb() == 1 ? "USB is the BIOS's on this boot: U in the MIDI view hands it over" : "no camera found", now); break; }
            if (plat_camera_on(!cam_on)) cam_on = !cam_on; else say("the camera would not start: see the log", now);
            break;
        }
        return true;
    }
    if (confirm_del) {                                     /* D asked: Y within 3 s deletes, any other key keeps it */
        confirm_del = false;
        if (code == 'y' && now - msg_ms < 3000) say(disk_delete_slot(cursor) ? "deleted" : "delete failed", now);
        else say("kept", now);
        return true;
    }
    switch (code) {
    case KEY_UP:   if (cursor > 0) cursor--; return true;
    case KEY_DOWN: if (cursor + 1 < slots()) cursor++; return true;
    case KEY_ENTER: case 'l':
        if (!slots()) { say("no storage", now); return true; }
        say(disk_load_slot(cursor) ? "loaded" : disk.slot_used[cursor] ? "load failed" : "nothing there", now); return true;
    case 's':
        if (!slots()) { say("no storage", now); return true; }
        naming = true; name_len = 0; name[0] = 0;
        if (disk.slot_used[cursor]) { snfmt(name, sizeof name, "%s", disk.slot[cursor].name); name_len = (int)strlen(name); }
        return true;
    case 'd':
        if (!slots() || !disk.slot_used[cursor]) { say("nothing to delete", now); return true; }
        confirm_del = true; say("delete it? Y deletes, any other key keeps it", now);
        return true;
    case 'r': disk_rescan(); say(disk.have_tape ? "stick found" : "no stick", now); cursor = 0; return true;
    }
    return true;                                          /* nothing on this page plays notes */
}

/* the hardware report: this log and the platform's files (a PCI listing, the firmware's ACPI tables) onto the stick,
   so the machine can be looked at on another computer */
static bool put_file(const char *name, uint32_t size, int index) {
    struct fat_file f;
    if (!fat_create(&disk.fat, "", name, size, &f)) return false;
    uint32_t off = 0; const uint8_t *d; uint32_t len;
    for (int k = 0; index >= 0 && plat_report_piece(index, k, &d, &len); k++) {
        if (off + len > size || !fat_write_inplace(&disk.fat, &f, off, d, len)) return false;
        off += len;
    }
    return true;
}
static void write_report(uint64_t now) {
    if (!disk.have_boot_fat) { say("no stick to write the report to", now); return; }
    static char text[20480]; char line[256]; int n = 0, lines = 0;
    while (log_line(lines, line, sizeof line) >= 0) lines++;
    for (int b = lines - 1; b >= 0 && n < (int)sizeof text - 260; b--) {
        int k = log_line(b, line, sizeof line);
        if (k < 0) break;
        memcpy(text + n, line, (size_t)k); n += k; text[n++] = '\r'; text[n++] = '\n';
    }
    struct fat_file f; int files = 0;
    bool ok = fat_create(&disk.fat, "", "HB-LOG.TXT", (uint32_t)n, &f) && fat_write_inplace(&disk.fat, &f, 0, text, (uint32_t)n);
    files += ok;
    for (int i = 0; ok; i++) {
        char name[13]; uint32_t size;
        if (!plat_report_file(i, name, sizeof name, &size)) break;
        ok = put_file(name, size, i);
        files += ok;
    }
    char m[80];
    snfmt(m, sizeof m, ok ? "report written to the stick: %d files, HB-*" : "the report stopped after %d files (the stick's root full?)", files);
    say(m, now);
}

static void pointer(uint64_t now) {
    (void)now;
    if (!ptr.pressed || !ui_in(list_px, ptr.x, ptr.y)) return;
    int row = (ptr.y - list_px.y) / text_font()->height;
    if (first + row < slots()) cursor = first + row;
}

/* The log view: the sound device's state on top (items split at " · " and flowed over lines), the log below it. */
static void draw_log(int x, int y, int w, int h) {
    ui_panel(x, y, w, h, "LOG", C_CYAN);
    char status[512], line[256], item[160];
    plat_audio_status(status, sizeof status);
    if (gfx_speed_mbs) snfmt(status + strlen(status), sizeof status - strlen(status), " · screen %dx%d, %u MB/s", gfx_width(), gfx_height(), gfx_speed_mbs);
    char cname[40];
    if (plat_camera(cname, sizeof cname)) {
        const uint8_t *lp; int cw, chh; uint32_t fr = cam_on ? plat_camera_frame(&lp, &cw, &chh) : 0;
        if (!cam_on) snfmt(status + strlen(status), sizeof status - strlen(status), " · camera %s: off (C)", cname);
        else if (!fr) snfmt(status + strlen(status), sizeof status - strlen(status), " · camera %s: on, no picture yet", cname);
        else snfmt(status + strlen(status), sizeof status - strlen(status), " · camera %s: %dx%d, picture %u", cname, cw, chh, fr);
    }
    int row = y + 1, used = 0; line[0] = 0;
    for (const char *p = status; *p && row < y + 4;) {
        const char *sep = p; while (*sep && !(sep[0] == ' ' && (uint8_t)sep[1] == 0xC2 && (uint8_t)sep[2] == 0xB7)) sep++;
        int len = (int)(sep - p); if (len > (int)sizeof item - 1) len = (int)sizeof item - 1;
        memcpy(item, p, (size_t)len); item[len] = 0;
        int cells = ui_cells(item);
        if (used && used + 3 + cells > w - 4) { text_str_n(x + 2, row++, line, w - 4, C_BRIGHT, C_PANEL); line[0] = 0; used = 0; }
        if (row >= y + 4) break;
        if (used) { snfmt(line + strlen(line), sizeof line - strlen(line), " · "); used += 3; }
        snfmt(line + strlen(line), sizeof line - strlen(line), "%s", item); used += cells;
        p = *sep ? sep + 4 : sep;                                   /* " · " is four bytes */
    }
    if (used && row < y + 4) text_str_n(x + 2, row, line, w - 4, C_BRIGHT, C_PANEL);
    int top = y + 5, n = y + h - 3 - top;
    for (int r = 0; r < n; r++) {
        int back = log_back + (n - 1 - r);
        if (log_line(back, line, sizeof line) < 0) continue;
        text_str_n(x + 2, top + r, line, w - 4, C_TEXT, C_PANEL);
    }
    const char *leg[] = { "TAB", "projects", "↑ ↓", "scroll", "W", "write a hardware report to the stick", "T", tone_on ? "tone off" : "test tone: 440 Hz, not through the synth",
                          "P", all_on ? "outputs follow the jacks" : "every output on, ignore the jacks" };
    ui_legend(x + 2, y + h - 2, w - 4, 1, leg, 5, C_PANEL);
}

/* The MIDI view: the port, what comes in and what goes out, and the last messages that came in. */
static void draw_midi(int x, int y, int w, int h, uint64_t now) {
    ui_panel(x, y, w, h, "MIDI AND SYNC", C_PINK);
    const char *names[8]; int n = plat_midi_ports(names, 8);
    char buf[64];
    text_str(x + 2, y + 2, "PORT", C_DIM, C_PANEL);
    int px = x + 12;
    for (int i = -1; i < n; i++) {
        const char *t = i < 0 ? "OFF" : names[i];
        bool on = midi.port == i;
        snfmt(buf, sizeof buf, " %s ", t);
        text_str(px, y + 2, buf, on ? C_BLACK : C_TEXT, on ? C_PINK : C_BORDER);
        px += ui_cells(buf) + 1;
    }
    if (!n) text_str(px, y + 2, "none yet: plug a USB MIDI device in", C_DIM, C_PANEL);
    int usb = plat_usb();
    if (usb == 1) text_str_n(x + 12, y + 3, "USB is the BIOS's: U takes it over for USB MIDI, until the next boot", w - 14, C_DIM, C_PANEL);
    text_str(x + 2, y + 4, "IN", C_DIM, C_PANEL);
    if (midi.in_channel) snfmt(buf, sizeof buf, "channel %u", midi.in_channel); else snfmt(buf, sizeof buf, "every channel");
    text_str(x + 12, y + 4, buf, C_TEXT, C_PANEL);
    ui_led(x + 12, y + 5, midi.clock_in, C_GREEN, "clock in: its tempo, start and stop run the tracker (C)", C_PANEL);
    text_str(x + 2, y + 7, "OUT", C_DIM, C_PANEL);
    ui_led(x + 12, y + 7, midi.notes_out, C_GREEN, "the tracker's channels 1-8 as MIDI channels 1-8 (N)", C_PANEL);
    ui_led(x + 12, y + 8, midi.clock_out, C_GREEN, "clock, start and stop at the tempo (K)", C_PANEL);
    ui_led(x + 12, y + 9, midi.thru, C_GREEN, "thru: what comes in goes back out (T)", C_PANEL);
    text_str(x + 2, y + 11, "LINK", C_DIM, C_PANEL);
    ui_led(x + 12, y + 11, lnk.on, C_GREEN, "Ableton Link: tempo, beat and start/stop with other programs on the network (L)", C_PANEL);
    char nb[128]; snfmt(nb, sizeof nb, "network: %s%s%s", net.status, lnk.on ? " · link: " : "", lnk.on ? lnk.status : "");
    text_str_n(x + 12, y + 12, nb, w - 14, net.phase == NET_READY ? C_TEXT : C_DIM, C_PANEL);
    snfmt(buf, sizeof buf, "%u messages in · %u bytes out", midi.in_msgs, midi.out_bytes);
    text_str(x + 2, y + 14, buf, C_DIM, C_PANEL);
    if (midi.ext_bpm) { snfmt(buf, sizeof buf, "clock %u BPM", midi.ext_bpm); text_str(x + 2 + ui_cells("0000 messages in · 00000 bytes out  "), y + 14, buf, C_GREEN, C_PANEL); }
    for (int i = 0; i < 8 && y + 16 + i < y + h - 2; i++) text_str_n(x + 4, y + 16 + i, midi.monitor[i], w - 8, i ? C_TEXT : C_BRIGHT, C_PANEL);
    if (msg[0] && now - msg_ms < 3000) text_str(x + w - 2 - ui_cells(msg), y + 2, msg, msg_bad ? C_RED : C_GREEN, C_PANEL);
    if (usb == 1) LEGEND(x + 2, y + h - 2, w - 4, 1, C_PANEL, "← →", "port", "↑ ↓", "in channel", "L", "Link", "C", "clock in", "N", "notes out", "K", "clock out", "T", "thru", "U", "USB", "TAB", "log");
    else LEGEND(x + 2, y + h - 2, w - 4, 1, C_PANEL, "← →", "port", "↑ ↓", "in channel", "L", "Link", "C", "clock in", "N", "notes out", "K", "clock out", "T", "thru", "TAB", "log");
}

/* SONGS: export the song as a WAV onto the stick; the WAVs there, to bring into the sampler or onto the tape */
static void draw_songs(int x, int y, int w, int h, uint64_t now) {
    ui_panel(x, y, w, h, "SONGS", C_GREEN);
    char buf[96];
    uint32_t len = song_length(), r = audio_rate();
    if (song.exporting) {
        snfmt(buf, sizeof buf, "exporting %s  %u%%", song.name, (uint32_t)((uint64_t)song.done * 100 / MAX(1u, song.total)));
        text_str(x + 2, y + 2, buf, C_BRIGHT, C_PANEL);
        ui_bar(x + 2, y + 3, w - 4, (int)(song.done / 64), (int)(song.total / 64), C_GREEN, C_PANEL);
        text_str(x + 2, y + 4, "the speakers are quiet while it renders; ESC stops", C_DIM, C_PANEL);
    } else {
        if (len) snfmt(buf, sizeof buf, "E exports the song (the tracker from the top, and the tape): %u:%02u, 48 kHz stereo", len / r / 60, len / r % 60);
        else snfmt(buf, sizeof buf, "nothing to export yet: the tracker and the tape are empty");
        text_str_n(x + 2, y + 2, buf, w - 4, len ? C_TEXT : C_DIM, C_PANEL);
        if (song.status[0]) text_str_n(x + 2, y + 3, song.status, w - 4, C_GREEN, C_PANEL);
    }
    static uint32_t listed_at = ~0u;
    if (nwavs < 0 || (listed_at != song.changes && !song.exporting)) { nwavs = song_wavs(wavs, 64); listed_at = song.changes; }
    text_str(x + 2, y + 6, "WAV FILES ON THE STICK", C_DIM, C_PANEL);
    if (!nwavs) text_str(x + 2, y + 7, disk.have_boot_fat ? "none yet: export one, or copy WAVs to the stick from a computer" : "no stick found (boot from the stick, or plug it in and press R on the projects)", C_DIM, C_PANEL);
    int list_h = h - 11;
    int first = wav_cur >= list_h ? wav_cur - list_h + 1 : 0;
    for (int i = 0; i < list_h && first + i < nwavs; i++) {
        int k = first + i; bool cur = k == wav_cur;
        uint8_t bg = cur ? C_BORDER : C_PANEL;
        text_fill(x + 1, y + 7 + i, w - 2, 1, ' ', C_TEXT, bg);
        text_str(x + 2, y + 7 + i, wavs[k].name, cur ? C_BRIGHT : C_TEXT, bg);
        snfmt(buf, sizeof buf, "%u KiB", (wavs[k].size + 1023) / 1024);
        text_str(x + 18, y + 7 + i, buf, C_DIM, bg);
    }
    snfmt(buf, sizeof buf, "into sample slot %d (%s) or onto tape track %d at %u:%02u", import_slot + 1, samples[import_slot].name, tape.cur + 1,
          tape.pos / tape_rate() / 60, tape.pos / tape_rate() % 60);
    text_str_n(x + 2, y + h - 3, buf, w - 4, C_DIM, C_PANEL);
    if (msg[0] && now - msg_ms < 4000) text_str_n(x + 2, y + 4, msg, w - 4, msg_bad ? C_RED : C_GREEN, C_PANEL);
    LEGEND(x + 2, y + h - 2, w - 4, 1, C_PANEL, "E", "export", "↑ ↓", "file", "S", "into the sampler", "[ ]", "slot", "T", "onto the tape", "R", "list again", "TAB", "MIDI");
}

static void size_str(char *out, int cap, uint64_t sectors) {
    uint64_t mib = sectors >> 11;
    if (mib >= 10240) snfmt(out, cap, "%lu GiB", mib >> 10); else snfmt(out, cap, "%lu MiB", mib);
}
static void draw_install(int x, int y, int w, int h, uint64_t now) {
    ui_panel(x, y, w, h, "INSTALL · this computer starts into the instrument, without the stick", C_AMBER);
    if (ntargets < 0 || (relist && !inst.running)) { list_targets(); relist = false; }
    char b[160], size[16];
    snfmt(b, sizeof b, "running from: %s", disk.have_tape ? plat_blk_name(disk.boot_drive) : "no BARE! stick");
    text_str_n(x + 2, y + 2, b, w - 4, disk.have_tape ? C_TEXT : C_RED, C_PANEL);
    int ly = y + 4;
    if (!ntargets) text_str(x + 2, ly, "no other drive here", C_DIM, C_PANEL);
    for (int i = 0; i < ntargets && ly < y + h - 7; i++, ly++) {
        bool on = i == target_cur;
        size_str(size, sizeof size, plat_blk_sectors(targets[i]));
        text_fill(x + 1, ly, w - 2, 1, ' ', C_TEXT, on ? C_BORDER : C_PANEL);
        text_put(x + 2, ly, on ? G_DIAMOND : ' ', C_AMBER, on ? C_BORDER : C_PANEL);
        text_str_n(x + 4, ly, plat_blk_name(targets[i]), MAX(w - 38, 10), on ? C_BRIGHT : C_TEXT, on ? C_BORDER : C_PANEL);
        text_str(x + w - 33, ly, size, C_DIM, on ? C_BORDER : C_PANEL);
        text_str_n(x + w - 22, ly, target_desc[i], 20, C_DIM, on ? C_BORDER : C_PANEL);
    }
    int by = y + h - 6;
    if (inst.running) {
        snfmt(b, sizeof b, "%s: %lu of %lu MiB", inst.status, inst.copied >> 11, inst.total >> 11);
        text_str_n(x + 2, by, b, w - 4, C_AMBER, C_PANEL);
        ui_bar(x + 2, by + 1, w - 4, (int)(inst.copied >> 8), (int)(inst.total >> 8), C_AMBER, C_PANEL);
        text_str_n(x + 2, by + 3, "Esc stops (the disk is then only partly written)", w - 4, C_DIM, C_PANEL);
    } else if (erase_asked && ntargets) {
        size_str(size, sizeof size, plat_blk_sectors(targets[target_cur]));
        snfmt(b, sizeof b, "Everything on %s (%s, %s) will be erased.", plat_blk_name(targets[target_cur]), size, target_desc[target_cur]);
        text_str_n(x + 2, by, b, w - 4, C_RED, C_PANEL);
        snfmt(b, sizeof b, "Type ERASE and press Enter; Esc keeps it:  %s", erase_typed);
        text_str_n(x + 2, by + 2, b, w - 4, C_BRIGHT, C_PANEL);
        if ((now / 500) & 1) text_put(x + 2 + ui_cells(b), by + 2, G_FULL, C_BRIGHT, C_PANEL);
    } else if (inst.done || inst.failed) {
        text_str_n(x + 2, by, inst.status, w - 4, inst.done ? C_GREEN : C_RED, C_PANEL);
    } else {
        text_str_n(x + 2, by, "The chosen drive is erased and gets the stick's boot partition and projects. The computer then starts from", w - 4, C_DIM, C_PANEL);
        text_str_n(x + 2, by + 1, "it (choose it in its boot menu if it doesn't). Updates and saving work as on the stick.", w - 4, C_DIM, C_PANEL);
    }
    if (msg[0] && now - msg_ms < 4000) text_str_n(x + 2, y + h - 3, msg, w - 4, msg_bad ? C_RED : C_GREEN, C_PANEL);
    LEGEND(x + 2, y + h - 2, w - 4, 1, C_PANEL, "↑ ↓", "drive", "I", "install on it", "R", "look again", "TAB", "projects");
}

static void draw(uint64_t now) {
    int cols = text_cols(), rows = text_rows();
    int x = 2, y = 2, w = cols - 4, h = rows - 5;
    if (view == V_INSTALL && !song.exporting) { draw_install(x, y, w, h, now); FOOTER("", "Installing erases the chosen drive: another system on it is gone for good."); return; }
    if (view == V_SONGS || song.exporting) { draw_songs(x, y, w, h, now); FOOTER("", "Songs are 48 kHz 16-bit WAVs in the root of the stick's FAT partition, which any computer can read."); return; }
    if (view == V_LOG) { draw_log(x, y, w, h); FOOTER("", "The kernel log, and the sound device as the driver sees it. A photo of this helps find a problem."); return; }
    if (view == V_MIDI) { draw_midi(x, y, w, h, now); FOOTER("", "USB MIDI devices can be plugged in any time (after U on a BIOS boot). Serial ports run at 38400 baud, the PC mode of Roland and Yamaha."); return; }
    ui_panel(x, y, w, h, "PROJECTS", C_AMBER);
    char buf[128];
    text_str_n(x + 2, y + 1, disk.status, w - 4, disk.have_tape ? C_TEXT : C_RED, C_PANEL);
    snfmt(buf, sizeof buf, "BARE! %s · build %u · %ux%u · %dx%d cells", BARE_RELEASE, app_version, app_fb.width, app_fb.height, cols, rows);
    if (w > ui_cells(disk.status) + ui_cells(buf) + 8) text_str(x + w - 2 - ui_cells(buf), y + 1, buf, C_DIM, C_PANEL);
    int n = slots(), list_h = h - 7;
    if (cursor >= first + list_h) first = cursor - list_h + 1;
    if (cursor < first) first = cursor;
    int dx = x + 6 + 34, sx = dx + 18, bx = sx + 12;
    text_str(x + 2, y + 3, "#", C_DIM, C_PANEL); text_str(x + 6, y + 3, "NAME", C_DIM, C_PANEL);
    if (bx + 16 < x + w) { text_str(dx, y + 3, "SAVED", C_DIM, C_PANEL); text_str(sx, y + 3, "SIZE", C_DIM, C_PANEL); }
    list_px = text_rect(x + 1, y + 4, w - 2, list_h);
    for (int i = 0; i < list_h && first + i < n; i++) {
        int s = first + i, ry = y + 4 + i;
        bool cur = s == cursor, used = disk.slot_used[s];
        uint8_t bg = cur ? C_BORDER : C_PANEL;
        text_fill(x + 1, ry, w - 2, 1, ' ', C_TEXT, bg);
        snfmt(buf, sizeof buf, "%2d", s + 1);
        text_str(x + 2, ry, buf, cur ? C_AMBER : C_DIM, bg);
        if (!used) { text_str(x + 6, ry, "·  empty", C_BORDER, bg); continue; }
        text_str_n(x + 6, ry, disk.slot[s].name, 32, cur ? C_BRIGHT : C_TEXT, bg);
        if (bx + 16 < x + w) {
            char date[20] = ""; if (disk.slot[s].version >= 2) disk_date_str(disk.slot[s].saved, date, sizeof date);
            text_str(dx, ry, date, C_DIM, bg);
            snfmt(buf, sizeof buf, "%u KiB", (disk.slot[s].len + 1023) / 1024);
            text_str(sx, ry, buf, C_DIM, bg);
            if (disk.tape.last_slot == (uint32_t)s + 1) ui_led(bx, ry, true, C_GREEN, "loads at boot", bg);
        }
    }
    int by = y + h - 2;
    if (naming) {
        text_str(x + 2, by, "save as", C_DIM, C_PANEL);
        text_fill(x + 10, by, 26, 1, ' ', C_BRIGHT, C_BORDER);
        snfmt(buf, sizeof buf, "%s_", name); text_str(x + 11, by, buf, C_BRIGHT, C_BORDER);
        LEGEND(x + 40, by, w - 42, 1, C_PANEL, "ENTER", "save", "ESC", "cancel");
    } else {
        LEGEND(x + 2, by, w - 4, 1, C_PANEL, "↑ ↓", "select", "ENTER", "load", "S", "save here", "D Y", "delete", "R", "rescan drives", "TAB", "songs, MIDI, log");
        if (msg[0] && now - msg_ms < 3000) text_str(x + w - 2 - ui_cells(msg), by - 1, msg, msg_bad ? C_RED : C_GREEN, C_PANEL);
    }
    FOOTER("", "The instrument runs from RAM: the stick can be pulled while you play.", "R", "rescan to save again", "", "BARE.UPD on any FAT drive updates at boot.");
}

const struct page page_file = { "FILE", "F7", KEY_F7, false, key, typing, pointer, 0, draw };
