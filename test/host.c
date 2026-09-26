/* core/platform.h for a Linux process: simulated clock, scripted input, memory framebuffer, disk image file. */
#define _GNU_SOURCE
#include "host.h"
#include "keys.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#if __has_include("audio.h") && !defined(HB_BASELINE)
#include "audio.h"
#include <math.h>
static void clock_ms(void) { }
static bool held;
void plat_audio_hold(bool on) { held = on; }
static void render(int16_t *buf, int frames) { if (held) memset(buf, 0, (size_t)frames * 4); else audio_render(buf, (uint32_t)frames); }
/* a MIDI port: bytes queued by the test come in, bytes going out are kept for it to look at */
static uint8_t midi_q[4096]; static unsigned midi_qw, midi_qr; static bool midi_is_open;
uint8_t host_midi_out[65536]; int host_midi_out_len;
void host_midi_in(const uint8_t *b, int n) { for (int i = 0; i < n; i++) midi_q[midi_qw++ % sizeof midi_q] = b[i]; }
int  plat_midi_ports(const char **names, int max) { if (max < 1) return 0; names[0] = "HOST MIDI"; return 1; }
bool plat_midi_open(int i) { if (i > 0) return false; midi_is_open = i == 0; midi_qr = midi_qw; return true; }
int  plat_midi_read(uint8_t *buf, int max) { int n = 0; while (midi_is_open && n < max && midi_qr != midi_qw) buf[n++] = midi_q[midi_qr++ % sizeof midi_q]; return n; }
int  plat_midi_write(const uint8_t *buf, int n) { for (int i = 0; i < n && host_midi_out_len < (int)sizeof host_midi_out; i++) host_midi_out[host_midi_out_len++] = buf[i]; return n; }
int  plat_usb(void) { return 0; }
bool plat_report_file(int i, char *n, int c, uint32_t *s) { (void)i; (void)n; (void)c; (void)s; return false; }
bool plat_report_piece(int i, int p, const uint8_t **d, uint32_t *l) { (void)i; (void)p; (void)d; (void)l; return false; }
bool plat_usb_takeover(void) { return false; }
/* a line input: a tone (host_input_tone) or silence, delivered a millisecond at a time like a driver would */
static int in_active = -1, in_amp; static double in_hz, in_phase;
void host_input_tone(double hz, int amp) { in_hz = hz; in_amp = amp; }
int  plat_audio_inputs(const char **names, int max) { if (max < 1) return 0; names[0] = "LINE IN"; return 1; }
bool plat_audio_input(int i) { if (i > 0) return false; in_active = i; return true; }
bool plat_audio_input_jack(int i) { return i == 0; }
static void input_ms(void) {
    if (in_active < 0) return;
    int16_t f[48 * 2];
    for (int k = 0; k < 48; k++) { f[2 * k] = f[2 * k + 1] = (int16_t)(in_amp * sin(in_phase)); in_phase += 2 * M_PI * in_hz / 48000; }
    audio_input_push(f, 48);
}
#else
static void input_ms(void) { }
#include "seq.h"
#include "synth.h"
static void clock_ms(void) { seq_tick(host_now_ms); }             /* the old sequencer ran from the 1 kHz timer */
static void render(int16_t *buf, int frames) { synth_render(buf, (uint32_t)frames); }
#endif

uint64_t host_now_ms;
bool host_log;

/* ---- input ---- */
static struct key_event kq[1024]; static unsigned kq_head, kq_tail;
static struct pointer_event pq[1024]; static unsigned pq_head, pq_tail;
void host_key(uint8_t code, bool down) { kq[kq_head++ & 1023] = (struct key_event){ code, down }; }
void host_tap(uint8_t code) { host_key(code, true); host_key(code, false); }
void host_shift_tap(uint8_t code) { host_key(KEY_LSHIFT, true); host_tap(code); host_key(KEY_LSHIFT, false); }
void host_pointer(int ax, int ay, int buttons) { pq[pq_head++ & 1023] = (struct pointer_event){ .ax = (uint16_t)ax, .ay = (uint16_t)ay, .abs = 1, .buttons = (uint8_t)buttons }; }
void host_finger(int finger, int tx, int ty, int z, int size) {
    pq[pq_head++ & 1023] = (struct pointer_event){ .touch = 1, .z = (uint8_t)z, .tx = (uint16_t)tx, .ty = (uint16_t)ty, .finger = (uint8_t)finger, .size = (uint8_t)size };
}
void host_touch(int tx, int ty, int z) { host_finger(0, tx, ty, z, 0); }
bool plat_key_poll(struct key_event *ev) { if (kq_tail == kq_head) return false; *ev = kq[kq_tail++ & 1023]; return true; }
bool plat_pointer_poll(struct pointer_event *ev) { if (pq_tail == pq_head) return false; *ev = pq[pq_tail++ & 1023]; return true; }

const char *plat_blk_name(int drive) { return drive ? "a second image" : "the host's image"; }
uint32_t plat_boot_disk_id(void) { return host_boot_id; }

/* ---- a network port: frames the checks hand in, frames the core sent, kept for them ---- */
static bool hnet_on, hnet_link; static uint8_t hq_in[64][1536], hq_out[64][1536]; static int hq_in_len[64], hq_out_len[64];
static unsigned hin_h, hin_t, hout_h, hout_t;
void host_net(bool present, bool link) { hnet_on = present; hnet_link = link; }
void host_net_inject(const void *f, int n) { memcpy(hq_in[hin_h & 63], f, (size_t)n); hq_in_len[hin_h++ & 63] = n; }
int host_net_sent(void *f, int cap) {
    if (hout_t == hout_h) return 0;
    int n = hq_out_len[hout_t & 63]; if (n > cap) n = cap;
    memcpy(f, hq_out[hout_t++ & 63], (size_t)n);
    return n;
}
bool plat_net_present(uint8_t mac[6]) { static const uint8_t m[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 }; if (hnet_on) memcpy(mac, m, 6); return hnet_on; }
bool plat_net_link(void) { return hnet_on && hnet_link; }
bool plat_net_send(const void *f, int n) { if (!hnet_link) return false; memcpy(hq_out[hout_h & 63], f, (size_t)n); hq_out_len[hout_h++ & 63] = n; return true; }
int plat_net_recv(void *f, int cap) {
    if (hin_t == hin_h) return 0;
    int n = hq_in_len[hin_t & 63]; if (n > cap) n = cap;
    memcpy(f, hq_in[hin_t++ & 63], (size_t)n);
    return n;
}
void plat_net_multicast(bool all) { (void)all; }

/* ---- misc platform ---- */
uint64_t plat_ms(void) { return host_now_ms; }
uint64_t plat_us(void) { return host_now_ms * 1000; }
void plat_rom_read(int source, uint32_t offset, uint8_t *dst, int n) {
    for (int i = 0; i < n; i++) { uint32_t x = (offset + (uint32_t)i) * 2654435761u ^ (uint32_t)source * 40503u; x ^= x >> 13; dst[i] = (uint8_t)(x * 0x5bd1e995u >> 24); }
}
void plat_putc(char c) { if (host_log) fputc(c, stderr); }
void plat_idle(void) { host_now_ms++; host_audio_ms(); }   /* until the next timer tick: a millisecond (Doom's waits) */
uint32_t plat_irq_save(void) { return 0; }
void plat_irq_restore(uint32_t st) { (void)st; }
void plat_reboot(void) { fprintf(stderr, "host: reboot requested\n"); exit(3); }
int plat_cpu_temp(void) { return 52; }
int plat_fan_rpm(void) { return -1; }
const char *plat_machine(void) { return "host"; }
void plat_led(int led, int mode) { (void)led; (void)mode; }
void plat_set_burn(bool burn) { (void)burn; }
bool plat_rtc(struct rtc_time *t) { *t = (struct rtc_time){ 2026, 9, 25, 12, 0, 0 }; return true; }
/* the machine's RAM for plat_alloc: HOST_MEM=<MiB> (default 512) plays a smaller PC */
static uint64_t host_mem_left = 0xFFFFFFFFFFFFull;
static void host_mem_init(void) {
    if (host_mem_left != 0xFFFFFFFFFFFFull) return;
    const char *m = getenv("HOST_MEM");
    host_mem_left = (uint64_t)(m ? atoi(m) : 512) << 20;
}
void *plat_alloc(uint32_t bytes) {
    host_mem_init();
    if (bytes > host_mem_left) return 0;
    host_mem_left -= bytes;
    return calloc(1, bytes);
}
uint32_t plat_alloc_avail(void) { host_mem_init(); return host_mem_left > 0xFFFFF000u ? 0xFFFFF000u : (uint32_t)host_mem_left; }
void plat_audio_poll(void) { }
const char *plat_audio_name(void) { return "HDA 48 kHz · speakers"; }
bool plat_audio_onebit(void) { return false; }
void plat_audio_toggle_onebit(void) { }
void plat_audio_set_onebit(bool on) { (void)on; }
bool plat_audio_onebit_chosen(void) { return false; }
int plat_audio_load(void) { return 3; }
uint32_t host_latency;                         /* 0: what renders is heard at once (host_audio_ms renders in lockstep) */
uint32_t plat_audio_latency(void) { return host_latency; }
void plat_audio_status(char *out, int cap) { snprintf(out, (size_t)cap, "host: no device"); }
void plat_audio_test_tone(bool on) { (void)on; }
void plat_audio_all_outputs(bool on) { (void)on; }

/* ---- block devices: image files, drive 0 and whatever is added after it ---- */
static int disk_fd[4] = { -1, -1, -1, -1 }, ndisk; static uint64_t disk_secs[4];
uint32_t host_boot_id;
static bool attach(int d, const char *path) {
    disk_fd[d] = open(path, O_RDWR);
    if (disk_fd[d] < 0) return false;
    struct stat st; fstat(disk_fd[d], &st); disk_secs[d] = (uint64_t)st.st_size / 512;
    if (d >= ndisk) ndisk = d + 1;
    return true;
}
bool host_disk_open(const char *path) { return attach(0, path); }
int host_disk_add(const char *path) { return ndisk < 4 && attach(ndisk, path) ? ndisk - 1 : -1; }
void host_disk_remove(int d) { if (d == ndisk - 1 && d > 0) { close(disk_fd[d]); disk_fd[d] = -1; ndisk--; } }
int plat_blk_drives(void) { return ndisk; }
void plat_blk_rescan(void) { }
uint64_t plat_blk_sectors(int d) { return d >= 0 && d < ndisk ? disk_secs[d] : 0; }
bool plat_blk_read(int d, uint64_t lba, uint32_t n, void *dst) {
    return d >= 0 && d < ndisk && pread(disk_fd[d], dst, (size_t)n * 512, (off_t)(lba * 512)) == (ssize_t)n * 512;
}
bool plat_blk_write(int d, uint64_t lba, uint32_t n, const void *src) {
    return d >= 0 && d < ndisk && pwrite(disk_fd[d], src, (size_t)n * 512, (off_t)(lba * 512)) == (ssize_t)n * 512;
}

/* ---- framebuffer ---- */
struct fb_info host_fb(int w, int h) {
    struct fb_info fb = { calloc((size_t)w * h, 4), (uint32_t)w, (uint32_t)h, (uint32_t)w * 4, 32, 16, 8, 8, 8, 0, 8 };
    return fb;
}
bool host_write_ppm(const char *path, const struct fb_info *fb) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%u %u\n255\n", fb->width, fb->height);
    for (uint32_t y = 0; y < fb->height; y++) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)fb->addr + (size_t)y * fb->pitch);
        for (uint32_t x = 0; x < fb->width; x++) { uint32_t p = row[x]; fputc((p >> 16) & 255, f); fputc((p >> 8) & 255, f); fputc(p & 255, f); }
    }
    fclose(f);
    return true;
}

/* ---- audio ---- */
static FILE *wav; static uint32_t wav_frames;
static int16_t audio_buf[48 * 2];
int16_t *host_last_audio = audio_buf;
int host_last_frames;
bool host_wav_open(const char *path) {
    wav = fopen(path, "wb"); wav_frames = 0;
    if (!wav) return false;
    static const uint8_t hdr[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 2,0,
                                     0x80,0xBB,0,0, 0x00,0xEE,0x02,0, 4,0, 16,0, 'd','a','t','a', 0,0,0,0 };
    fwrite(hdr, 1, 44, wav);
    return true;
}
void host_wav_close(void) {
    if (!wav) return;
    uint32_t data = wav_frames * 4, riff = data + 36;
    fseek(wav, 4, SEEK_SET); fwrite(&riff, 4, 1, wav);
    fseek(wav, 40, SEEK_SET); fwrite(&data, 4, 1, wav);
    fclose(wav); wav = 0;
}
void host_audio_ms(void) {
    clock_ms();
    input_ms();
    render(audio_buf, 48);
    host_last_frames = 48;
    if (wav) { fwrite(audio_buf, 4, 48, wav); wav_frames += 48; }
}
/* The way the HDA driver feeds the DMA ring: 128-frame chunks, kept 512 frames ahead of the play position, from the
   1 kHz timer. Events then land on chunk boundaries — this reproduces the timing a listener hears. */
void host_audio_pump_ms(void) {
    static uint32_t played, written;
    static int16_t chunk[128 * 2];
    clock_ms();
    input_ms();
    played += 48;
    while ((int32_t)(written - played) < 512) {
        render(chunk, 128);
        if (wav) { fwrite(chunk, 4, 128, wav); wav_frames += 128; }
        written += 128;
    }
}
void host_run(int ms) {
    for (int i = 0; i < ms; i++) {
        host_now_ms++;
        host_audio_ms();
        app_step(host_now_ms);
    }
}

#ifndef HB_BASELINE
#include "splash.h"
/* the host programs start without the boot's splash (test/video.c --splash asks for one) */
__attribute__((constructor)) static void host_no_splash(void) { splash_mode = SPLASH_OFF; }
#endif

/* a camera for the host programs: 160x120, a bright disc going round a dim gradient, a new picture every 33 ms while on */
bool host_camera_present = true;
static bool cam_streaming;
static uint8_t cam_pic[160 * 120];
bool plat_camera(char *name, int cap) { if (name && cap) snprintf(name, (size_t)cap, "host camera"); return host_camera_present; }
bool plat_camera_on(bool on) { if (!host_camera_present) return false; cam_streaming = on; return true; }
uint32_t plat_camera_frame(const uint8_t **luma, int *w, int *h) {
    if (!host_camera_present || !cam_streaming) return 0;
    uint32_t n = (uint32_t)(host_now_ms / 33) + 1;
    double a = n * 0.2; int cx = 80 + (int)(50 * cos(a)), cy = 60 + (int)(35 * sin(a));
    for (int y = 0; y < 120; y++)
        for (int x = 0; x < 160; x++) {
            int dx = x - cx, dy = y - cy;
            cam_pic[y * 160 + x] = (uint8_t)(dx * dx + dy * dy < 225 ? 240 : 20 + x / 8);
        }
    *luma = cam_pic; *w = 160; *h = 120;
    return n;
}
