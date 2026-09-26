#include "vmmouse.h"
#include "io.h"
#include "log.h"

#define MAGIC 0x564D5868u
#define PORT  0x5658
#define CMD_GETVERSION 10
#define CMD_ABS_DATA 39
#define CMD_ABS_STATUS 40
#define CMD_ABS_COMMAND 41
#define ABS_ENABLE 0x45414552u
#define ABS_REQUEST_ABSOLUTE 0x53424152u
#define VMMOUSE_ERROR 0xffff0000u
#define VERSION_ID 0x3442554au

static bool active;

static void cmd(uint32_t c, uint32_t in1, uint32_t *o1, uint32_t *o2, uint32_t *o3, uint32_t *o4) {
    uint32_t a = MAGIC, b = in1, cc = c, d = PORT;
    __asm__ volatile("inl %%dx, %%eax" : "+a"(a), "+b"(b), "+c"(cc), "+d"(d) :: "memory");
    if (o1) *o1 = a; if (o2) *o2 = b; if (o3) *o3 = cc; if (o4) *o4 = d;
}

bool vmmouse_init(void) {
    uint32_t v = 0, dummy;
    cmd(CMD_GETVERSION, 0, &v, &dummy, 0, 0);
    if (v == 0xFFFFFFFFu || dummy != MAGIC) return false;       /* no vmport */
    cmd(CMD_ABS_COMMAND, ABS_ENABLE, 0, 0, 0, 0);
    uint32_t status = 0; cmd(CMD_ABS_STATUS, 0, &status, 0, 0, 0);
    if ((status & 0xffff0000u) == VMMOUSE_ERROR) return false;
    uint32_t ver = 0; cmd(CMD_ABS_DATA, 1, &ver, 0, 0, 0);
    if (ver != VERSION_ID) return false;
    cmd(CMD_ABS_COMMAND, ABS_REQUEST_ABSOLUTE, 0, 0, 0, 0);
    active = true;
    logf("vmmouse: absolute pointer enabled");
    return true;
}
bool vmmouse_active(void) { return active; }

bool vmmouse_poll(struct pointer_event *ev) {
    if (!active) return false;
    uint32_t status = 0; cmd(CMD_ABS_STATUS, 0, &status, 0, 0, 0);
    if ((status & 0xffff0000u) == VMMOUSE_ERROR) { active = false; vmmouse_init(); return false; }
    if ((status & 0xffff) < 4) return false;
    uint32_t st, x, y, z; cmd(CMD_ABS_DATA, 4, &st, &x, &y, &z);
    uint8_t buttons = ((st & 0x20) ? 1 : 0) | ((st & 0x10) ? 2 : 0) | ((st & 0x08) ? 4 : 0);
    *ev = (struct pointer_event){ 0, 0, (uint16_t)(x >> 1), (uint16_t)(y >> 1), 1, buttons };
    return true;
}
