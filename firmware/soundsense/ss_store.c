/* Sound Sense storage in the OSPI1 flash (see ss_store.h). The driver use follows ../flash_test. */
#include "ss_store.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Driver_Flash.h"
#include "Driver_IO.h"
#include "pinconf.h"
#include "ss_board.h"

#define COPY_A        0x00100000u
#define COPY_B        0x00110000u
#define COPY_BYTES    0x10000u
#define SECTOR_BYTES  4096u
#define MAGIC         0x444E5353u   /* "SSND" */
#define VERSION       1u
#define HDR_BYTES     24u
#define PROF_HDR      24u           /* key, icon, urgency, cat, n_ex, n_cx, pad, thr */
#define IMG_MAX       (HDR_BYTES + 8u + SS_STORE_MAX_PROF * (PROF_HDR + SS_EMB * 4u + 2u * SS_STORE_MAX_EX * SS_EMB))

/* pins.h: P15_7 is "OSPI1_RESET" (pack 2.0.4's board_defs.h says P15_5, the LCD reset). */
#define RESET_PORT 15
#define RESET_PIN  7

extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(RESET_PORT);
static ARM_DRIVER_GPIO *const reset_gpio = &ARM_Driver_GPIO_(RESET_PORT);
extern ARM_DRIVER_FLASH ARM_Driver_Flash_(1);
static ARM_DRIVER_FLASH *const flash = &ARM_Driver_Flash_(1);

static uint32_t width;   /* bytes per driver data item */
static uint8_t wbuf[IMG_MAX + 8] __attribute__((aligned(8)));   /* record being written */
static uint8_t pbuf[IMG_MAX + 8] __attribute__((aligned(8)));   /* next record (payload only) */
static uint8_t rbuf[IMG_MAX + 8] __attribute__((aligned(8)));   /* read-back and load */
static uint32_t plen, wlen;
static int pending;
static uint32_t pending_ms;

static enum { IDLE, ERASE, PROGRAM } st = IDLE;
static uint32_t target, n_sect, sect, t_req;
static ss_store_stats stats = {.copy = -1};

static uint32_t copy_base(int c) { return c ? COPY_B : COPY_A; }

static void delay_us(uint32_t us)
{
    const uint32_t t0 = ss_cycles(), n = us * (SystemCoreClock / 1000000u);
    while (ss_cycles() - t0 < n) {}
}

static uint32_t crc32(uint32_t crc, const uint8_t *p, uint32_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t get32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

int ss_store_init(void)
{
    if (pinconf_set(RESET_PORT, RESET_PIN, PINMUX_ALTERNATE_FUNCTION_0, 0) ||
        reset_gpio->Initialize(RESET_PIN, NULL) != ARM_DRIVER_OK ||
        reset_gpio->PowerControl(RESET_PIN, ARM_POWER_FULL) != ARM_DRIVER_OK ||
        reset_gpio->SetDirection(RESET_PIN, GPIO_PIN_DIRECTION_OUTPUT) != ARM_DRIVER_OK ||
        reset_gpio->SetValue(RESET_PIN, GPIO_PIN_OUTPUT_STATE_LOW) != ARM_DRIVER_OK) {
        return -1;
    }
    delay_us(10);
    if (reset_gpio->SetValue(RESET_PIN, GPIO_PIN_OUTPUT_STATE_HIGH) != ARM_DRIVER_OK) return -2;
    delay_us(500);
    if (flash->Initialize(NULL) != ARM_DRIVER_OK || flash->PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK) return -3;
    width = 1u << flash->GetCapabilities().data_width;
    return 0;
}

static int read_bytes(uint32_t addr, uint8_t *dst, uint32_t n)
{
    return flash->ReadData(addr, dst, n / width) == (int32_t)(n / width) ? 0 : -1;
}

/* Validate the copy at addr into rbuf; returns the record length, or 0 if it isn't valid. */
static uint32_t check_copy(uint32_t addr, uint32_t *seq)
{
    if (read_bytes(addr, rbuf, HDR_BYTES)) return 0;
    const uint32_t len = get32(rbuf + 12);
    if (get32(rbuf) != MAGIC || (get32(rbuf + 4) & 0xFFFFu) != VERSION || (get32(rbuf + 4) >> 16) != HDR_BYTES ||
        len > IMG_MAX - HDR_BYTES || (len & 3u))
        return 0;
    if (read_bytes(addr, rbuf, HDR_BYTES + len)) return 0;
    const uint32_t crc = crc32(crc32(0, rbuf, 16), rbuf + HDR_BYTES, len);
    if (crc != get32(rbuf + 16)) return 0;
    *seq = get32(rbuf + 8);
    return HDR_BYTES + len;
}

static int parse(const uint8_t *p, uint32_t len, ss_store_data *d)
{
    const uint8_t *end = p + len;
    memset(d, 0, sizeof(*d));
    if (len < 8) return -1;
    d->builtin_mute = get32(p);
    const uint32_t dim = p[4] | (uint32_t)p[5] << 8, n = p[6];
    if (dim != SS_EMB || n > SS_STORE_MAX_PROF) return -1;
    p += 8;
    for (uint32_t i = 0; i < n; i++) {
        ss_store_prof *s = &d->prof[i];
        if (p + PROF_HDR > end) return -1;
        memcpy(s->key, p, SS_STORE_KEY);
        s->key[SS_STORE_KEY - 1] = 0;
        s->icon = p[12], s->urgency = p[13], s->cat = p[14], s->n_ex = p[15], s->n_cx = p[16];
        memcpy(&s->thr, p + 20, 4);
        p += PROF_HDR;
        if (s->n_ex > SS_STORE_MAX_EX || s->n_cx > SS_STORE_MAX_EX) return -1;
        const uint32_t body = SS_EMB * 4u + (uint32_t)(s->n_ex + s->n_cx) * SS_EMB;
        if (p + body > end) return -1;
        memcpy(s->proto, p, SS_EMB * 4u);
        p += SS_EMB * 4u;
        memcpy(s->ex, p, (size_t)s->n_ex * SS_EMB);
        p += s->n_ex * SS_EMB;
        memcpy(s->cx, p, (size_t)s->n_cx * SS_EMB);
        p += s->n_cx * SS_EMB;
    }
    d->n_prof = (int)n;
    return 0;
}

int ss_store_load(ss_store_data *d)
{
    const uint32_t t0 = ss_cycles();
    uint32_t seq[2] = {0, 0}, len[2];
    for (int c = 0; c < 2; c++) len[c] = check_copy(copy_base(c), &seq[c]);
    int best = -1;
    if (len[0] && len[1]) best = (int32_t)(seq[1] - seq[0]) > 0;
    else if (len[0]) best = 0;
    else if (len[1]) best = 1;
    printf("[STORE] copy A %s (seq %lu), copy B %s (seq %lu)\r\n", len[0] ? "valid" : "invalid",
           (unsigned long)seq[0], len[1] ? "valid" : "invalid", (unsigned long)seq[1]);
    int r = -1;
    if (best >= 0) {
        check_copy(copy_base(best), &seq[best]);   /* back into rbuf: the loop left the last copy read there */
        r = parse(rbuf + HDR_BYTES, len[best] - HDR_BYTES, d);
        if (r == 0) {
            stats.copy = best;
            stats.seq = seq[best];
        }
    }
    if (r != 0) memset(d, 0, sizeof(*d));
    stats.load_us = ss_cyc_to_us(ss_cycles() - t0);
    return r;
}

void ss_store_save(const ss_store_data *d)
{
    uint8_t *p = pbuf;
    put32(p, d->builtin_mute);
    p[4] = SS_EMB & 0xFF, p[5] = SS_EMB >> 8, p[6] = (uint8_t)d->n_prof, p[7] = 0;
    p += 8;
    for (int i = 0; i < d->n_prof && i < SS_STORE_MAX_PROF; i++) {
        const ss_store_prof *s = &d->prof[i];
        const uint8_t n_ex = s->n_ex > SS_STORE_MAX_EX ? SS_STORE_MAX_EX : s->n_ex;
        const uint8_t n_cx = s->n_cx > SS_STORE_MAX_EX ? SS_STORE_MAX_EX : s->n_cx;
        memset(p, 0, PROF_HDR);
        strncpy((char *)p, s->key, SS_STORE_KEY - 1);
        p[12] = s->icon, p[13] = s->urgency, p[14] = s->cat, p[15] = n_ex, p[16] = n_cx;
        memcpy(p + 20, &s->thr, 4);
        p += PROF_HDR;
        memcpy(p, s->proto, SS_EMB * 4u);
        p += SS_EMB * 4u;
        memcpy(p, s->ex, (size_t)n_ex * SS_EMB);
        p += n_ex * SS_EMB;
        memcpy(p, s->cx, (size_t)n_cx * SS_EMB);
        p += n_cx * SS_EMB;
    }
    while ((p - pbuf) & 3) *p++ = 0;
    plen = (uint32_t)(p - pbuf);
    if (!pending) pending_ms = ss_ms();
    pending = 1;
}

int ss_store_busy(void) { return pending || st != IDLE; }

int ss_store_step(void)
{
    const uint32_t t0 = ss_cycles();
    int r = 0;
    if (st == IDLE) {
        if (!pending) return 1;
        /* header + payload into wbuf, to the older copy */
        pending = 0;
        t_req = pending_ms;
        target = stats.copy < 0 ? 0 : 1 - stats.copy;
        const uint32_t seq = stats.seq + 1;
        put32(wbuf, MAGIC);
        put32(wbuf + 4, VERSION | HDR_BYTES << 16);
        put32(wbuf + 8, seq);
        put32(wbuf + 12, plen);
        memcpy(wbuf + HDR_BYTES, pbuf, plen);
        put32(wbuf + 16, crc32(crc32(0, wbuf, 16), wbuf + HDR_BYTES, plen));
        put32(wbuf + 20, 0);
        wlen = HDR_BYTES + plen;
        n_sect = (wlen + SECTOR_BYTES - 1) / SECTOR_BYTES;
        sect = 0;
        st = ERASE;
    }
    if (st == ERASE) {
        if (flash->EraseSector(copy_base((int)target) + sect * SECTOR_BYTES) != ARM_DRIVER_OK) r = -1;
        else if (++sect == n_sect) st = PROGRAM;
    } else if (st == PROGRAM) {
        const uint32_t base = copy_base((int)target);
        uint32_t seq = 0;
        if (flash->ProgramData(base, wbuf, wlen / width) != (int32_t)(wlen / width) ||
            check_copy(base, &seq) != wlen || memcmp(rbuf, wbuf, wlen) != 0) {
            r = -1;
        } else {
            stats.copy = (int)target;
            stats.seq = seq;
            r = 1;
        }
    }
    if (r != 0) {
        st = IDLE;
        stats.saves++;
        if (r < 0) stats.failed++;
        stats.last_ms = ss_ms() - t_req;
        if (stats.last_ms > stats.max_ms) stats.max_ms = stats.last_ms;
        stats.last_len = wlen;
    }
    const uint32_t us = ss_cyc_to_us(ss_cycles() - t0);
    if (us > stats.step_max_us) stats.step_max_us = us;
    return r;
}

const ss_store_stats *ss_store_get_stats(void) { return &stats; }

void ss_store_q8(const float *e, int8_t *q)
{
    for (int i = 0; i < SS_EMB; i++) {
        const float v = roundf(e[i] * 127.0f);
        q[i] = (int8_t)(v > 127.0f ? 127 : v < -127.0f ? -127 : v);
    }
}

void ss_store_f32(const int8_t *q, float *e)
{
    for (int i = 0; i < SS_EMB; i++) e[i] = q[i] / 127.0f;
}
