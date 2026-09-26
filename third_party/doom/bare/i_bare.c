// Doom's platform layer on BARE!: what Chocolate Doom's SDL files (i_system, i_timer, i_video, i_input, i_sound, …)
// did, handed to BARE! through core/doomhost.h — the clock, the screen, the keys, the sound, the WAD in memory, and the
// engine started and run a tic at a time from BARE!'s main loop. GPL-2.0-or-later, like the engine.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "doomtype.h"
#include "doomkeys.h"
#include "d_event.h"
#include "d_main.h"
#include "i_joystick.h"
#include "i_endoom.h"
#include "i_sound.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_menu.h"
#include "m_misc.h"
#include "tables.h"
#include "v_video.h"
#include "w_file.h"
#include "w_wad.h"
#include "z_zone.h"

// BARE!'s key names (keys.h) and Doom's (doomkeys.h) overlap: Doom's values are kept under names of their own first
enum { KEY_ENTER_DOOM = KEY_ENTER, KEY_BACKSPACE_DOOM = KEY_BACKSPACE, KEY_TAB_DOOM = KEY_TAB, KEY_RSHIFT_DOOM = KEY_RSHIFT,
       KEY_RCTRL_DOOM = KEY_RCTRL, KEY_RALT_DOOM = KEY_RALT, KEY_HOME_DOOM = KEY_HOME, KEY_END_DOOM = KEY_END,
       KEY_PGUP_DOOM = KEY_PGUP, KEY_PGDN_DOOM = KEY_PGDN };
#undef KEY_ENTER
#undef KEY_BACKSPACE
#undef KEY_TAB
#undef KEY_RSHIFT
#undef KEY_RCTRL
#undef KEY_RALT
#undef KEY_LALT
#undef KEY_HOME
#undef KEY_END
#undef KEY_PGUP
#undef KEY_PGDN
#undef KEY_F1
#undef KEY_F2
#undef KEY_F3
#undef KEY_F4
#undef KEY_F5
#undef KEY_F6
#undef KEY_F7
#undef KEY_F8
#undef KEY_F9
#undef KEY_F10
#undef KEY_F11
#undef KEY_F12
#include "doomhost.h"
#include "keys.h"

void doom_libc_flush(void);
void doomgeneric_Tick(void);
void D_DoomMain(void);

// ---- system ----
boolean drone = false, net_client_connected = false;

void I_Init(void) {}
void I_AtExit(atexit_func_t func, boolean run_if_error) {}       // nothing to shut down: BARE! goes on
void I_Tactile(int on, int off, int total) {}
boolean I_ConsoleStdout(void) { return false; }
boolean I_GetMemoryValue(unsigned int offset, void *value, int size) { return false; }
void I_PrintBanner(char *msg) { printf("%s\n", msg); }
void I_PrintDivider(void) { printf("----\n"); }
void I_PrintStartupBanner(char *gamedescription) { printf("%s\n", gamedescription); }
void I_BindVariables(void) {}

byte *I_ZoneBase(int *size) {
    uint32_t bytes;
    byte *zone = doom_zone(&bytes);
    *size = (int)bytes;
    return zone;
}

void I_Quit(void) { doom_quit(); }                              // back to BARE! once this tic is over

void I_Error(char *error, ...) {
    char msg[160];
    va_list ap;
    va_start(ap, error);
    vsnprintf(msg, sizeof msg, error, ap);
    va_end(ap);
    doom_fatal(msg);
}

void I_Endoom(byte *data) {}
void I_InitJoystick(void) {}
void I_ShutdownJoystick(void) {}
void I_UpdateJoystick(void) {}
void I_BindJoystickVariables(void) {}

// ---- the clock: BARE!'s, which stands still while Doom is left ----
int I_GetTimeMS(void) { return (int)doom_clock_ms(); }
int I_GetTime(void) { return (int)((uint64_t)doom_clock_ms() * TICRATE / 1000); }
void I_Sleep(int ms) { doom_sleep_ms(ms); }
void I_WaitVBL(int count) { doom_sleep_ms(count * 1000 / 70); }
void I_InitTimer(void) {}

// ---- the screen ----
byte *I_VideoBuffer = NULL;
boolean screenvisible = true, screensaver_mode = false;
int usegamma = 0, usemouse = 0, mouse_acceleration = 200, mouse_threshold = 10, vanilla_keyboard_mapping = 1;
char *video_driver = "";
static byte palette[768];

void I_InitGraphics(void) {
    I_VideoBuffer = Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);
    memset(I_VideoBuffer, 0, SCREENWIDTH * SCREENHEIGHT);
    screenvisible = true;
    I_SetPalette(W_CacheLumpName("PLAYPAL", PU_CACHE));
}
void I_ShutdownGraphics(void) {}
void I_SetPalette(byte *doompalette) {
    for (int i = 0; i < 768; i++) palette[i] = gammatable[usegamma][doompalette[i]];
    doom_palette(palette);
}
int I_GetPaletteIndex(int r, int g, int b) {
    int best = 0, best_d = 1 << 30;
    for (int i = 0; i < 256; i++) {
        int dr = r - palette[3 * i], dg = g - palette[3 * i + 1], db = b - palette[3 * i + 2], d = dr * dr + dg * dg + db * db;
        if (d < best_d) { best_d = d; best = i; }
        if (!d) break;
    }
    return best;
}
void I_UpdateNoBlit(void) {}
void I_FinishUpdate(void) { doom_frame(I_VideoBuffer); }
void I_ReadScreen(byte *scr) { memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT); }
void I_BeginRead(void) {}
void I_EndRead(void) {}
void I_SetWindowTitle(char *title) {}
void I_CheckIsScreensaver(void) {}
void I_SetGrabMouseCallback(grabmouse_callback_t func) {}
void I_DisplayFPSDots(boolean dots_on) {}
void I_BindVideoVariables(void) {}
void I_GraphicsCheckCommandLine(void) {}
void I_EnableLoadingDisk(void) {}
void I_StartFrame(void) {}

// ---- keys: BARE!'s codes to Doom's, and the character each types ----
static int shifts;
static int doom_code(uint8_t c) {
    switch (c) {
    case KEY_ENTER:     return KEY_ENTER_DOOM;
    case KEY_BACKSPACE: return KEY_BACKSPACE_DOOM;
    case KEY_ESC:       return KEY_ESCAPE;
    case KEY_TAB:       return KEY_TAB_DOOM;
    case KEY_UP:        return KEY_UPARROW;
    case KEY_DOWN:      return KEY_DOWNARROW;
    case KEY_LEFT:      return KEY_LEFTARROW;
    case KEY_RIGHT:     return KEY_RIGHTARROW;
    case KEY_HOME:      return KEY_HOME_DOOM;
    case KEY_END:       return KEY_END_DOOM;
    case KEY_PGUP:      return KEY_PGUP_DOOM;
    case KEY_PGDN:      return KEY_PGDN_DOOM;
    case KEY_INSERT:    return KEY_INS;
    case KEY_DELETE:    return KEY_DEL;
    case KEY_LSHIFT: case KEY_RSHIFT: return KEY_RSHIFT_DOOM;
    case KEY_LCTRL: case KEY_RCTRL:   return KEY_RCTRL_DOOM;
    case KEY_LALT: case KEY_RALT:     return KEY_RALT_DOOM;
    case KEY_CAPS:      return KEY_CAPSLOCK;
    case KEY_PRTSC:     return KEY_PRTSCR;
    case KEY_SCROLL:    return KEY_SCRLCK;
    }
    return c >= 32 && c < 127 ? c : 0;
}
static int typed(int c) {
    static const char from[] = "1234567890-=[];'`\\,./", to[] = "!@#$%^&*()_+{}:\"~|<>?";
    if (!shifts || c < 32 || c >= 127) return c < 128 ? c : 0;
    if (c >= 'a' && c <= 'z') return c - 32;
    for (int i = 0; from[i]; i++) if (from[i] == c) return to[i];
    return c;
}
void I_StartTic(void) {
    uint8_t code; boolean down;
    bool d;
    while (doom_key_next(&code, &d)) {
        down = d;
        int k = doom_code(code);
        if (!k) continue;
        if (k == KEY_RSHIFT_DOOM) shifts = down ? 1 : 0;
        event_t ev;
        ev.type = down ? ev_keydown : ev_keyup;
        ev.data1 = k; ev.data2 = down ? typed(k) : 0; ev.data3 = ev.data4 = 0;
        D_PostEvent(&ev);
    }
}

// ---- sound: the effects are the WAD's own samples; the music, BARE!'s synth (core/doomsnd.c) ----
int snd_musicdevice = SNDDEVICE_SB, snd_sfxdevice = SNDDEVICE_SB;
static boolean sfx_prefix;

void I_InitSound(boolean use_sfx_prefix) { sfx_prefix = use_sfx_prefix; }
void I_ShutdownSound(void) {}
void I_BindSoundVariables(void) {}
int I_GetSfxLumpNum(sfxinfo_t *sfx) {
    char name[16];
    if (sfx->link) sfx = sfx->link;
    snprintf(name, sizeof name, sfx_prefix ? "ds%s" : "%s", sfx->name);
    return W_CheckNumForName(name);
}
void I_UpdateSound(void) {}
void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds) {}
int I_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep) {
    if (sfx->lumpnum < 0) return -1;
    const byte *d = W_CacheLumpNum(sfx->lumpnum, PU_STATIC);
    int len = W_LumpLength(sfx->lumpnum);
    if (len < 8 || d[0] != 3 || d[1] != 0) return -1;             // a DMX sound: format 3, rate, length, 16 bytes of pad each side
    uint32_t rate = d[2] | d[3] << 8, n = d[4] | d[5] << 8 | (uint32_t)d[6] << 16 | (uint32_t)d[7] << 24;
    if (n > (uint32_t)len - 8 || n <= 48) return -1;
    doomsnd_sfx_start(channel, d + 8 + 16, n - 32, rate, vol, sep);
    return channel;
}
void I_StopSound(int channel) { doomsnd_sfx_stop(channel); }
boolean I_SoundIsPlaying(int channel) { return doomsnd_sfx_playing(channel); }
void I_UpdateSoundParams(int channel, int vol, int sep) { doomsnd_sfx_update(channel, vol, sep); }

void I_InitMusic(void) {}
void I_ShutdownMusic(void) { doomsnd_music_stop(); }
void I_SetMusicVolume(int volume) { doomsnd_music_volume(volume); }
void I_PauseSong(void) { doomsnd_music_hold(true); }
void I_ResumeSong(void) { doomsnd_music_hold(false); }
void *I_RegisterSong(void *data, int len) { return doomsnd_music_load(data, (uint32_t)len) ? (void *)1 : NULL; }
void I_UnRegisterSong(void *handle) {}
void I_PlaySong(void *handle, boolean looping) { if (handle) doomsnd_music_play(looping); }
void I_StopSong(void) { doomsnd_music_stop(); }
boolean I_MusicIsPlaying(void) { return doomsnd_music_playing(); }

// ---- the WAD: read whole into memory, the engine then uses it in place ----
extern wad_file_class_t bare_wad_file;
static wad_file_t *wad_open(char *path) {
    uint32_t size;
    byte *data = doom_wad_load(path, &size);
    if (!data) return NULL;
    wad_file_t *w = Z_Malloc(sizeof *w, PU_STATIC, 0);
    w->file_class = &bare_wad_file;
    w->mapped = data;
    w->length = size;
    return w;
}
static void wad_close(wad_file_t *w) { Z_Free(w); }
static size_t wad_read(wad_file_t *w, unsigned int offset, void *buffer, size_t len) {
    if (offset >= w->length) return 0;
    if (len > w->length - offset) len = w->length - offset;
    memcpy(buffer, w->mapped + offset, len);
    return len;
}
wad_file_class_t bare_wad_file = { wad_open, wad_close, wad_read };

// ---- the engine: started once, then a tic at a time ----
void doom_engine_start(const char *iwad) {
    static char *argv[4];
    argv[0] = "doom"; argv[1] = "-iwad"; argv[2] = (char *)iwad; argv[3] = NULL;
    myargc = 3; myargv = argv;
    D_DoomMain();                                               // returns after its first tic
    doom_libc_flush();
}
extern boolean menuactive;
void doom_engine_resume(void) { if (!menuactive) M_StartControlPanel(); }   // back from BARE!: paused, at its menu
void doom_engine_tick(void) {
    static int last = -1;
    int t = I_GetTime();
    if (t != last) { last = t; doomgeneric_Tick(); }
    doom_libc_flush();
}
