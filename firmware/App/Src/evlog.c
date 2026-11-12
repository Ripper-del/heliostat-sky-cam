#include <string.h>
#include "evlog.h"
#include "crc16.h"

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static void encode(const ev_rec_t *r, uint8_t b[EVLOG_REC_SIZE])
{
    b[0] = r->type; b[1] = r->frame; put16(b + 2, r->seq); put32(b + 4, r->t_ms);
    put16(b + 8, (uint16_t)r->a); put16(b + 10, (uint16_t)r->b); put16(b + 12, (uint16_t)r->c);
    put16(b + 14, crc16(b, 14));
}

static int decode(const uint8_t b[EVLOG_REC_SIZE], ev_rec_t *r)
{
    if (get16(b + 14) != crc16(b, 14)) return DEV_ERR_ID;
    if (b[0] == 0xFF) return DEV_ERR_ID;                      /* порожній слот: CRC всіх 0xFF не збігається, але перестрахуємось */
    r->type = b[0]; r->frame = b[1]; r->seq = get16(b + 2); r->t_ms = get32(b + 4);
    r->a = (int16_t)get16(b + 8); r->b = (int16_t)get16(b + 10); r->c = (int16_t)get16(b + 12);
    return DEV_OK;
}

static uint32_t slot_addr(uint32_t idx) { return EVLOG_BASE_ADDR + idx * EVLOG_REC_SIZE; }

static int is_blank(const uint8_t b[EVLOG_REC_SIZE])
{
    for (unsigned i = 0; i < EVLOG_REC_SIZE; i++) if (b[i] != 0xFF) return 0;
    return 1;
}

int evlog_read(evlog_t *l, uint32_t idx, ev_rec_t *out)
{
    uint8_t b[EVLOG_REC_SIZE];
    if (idx >= EVLOG_CAPACITY) return DEV_ERR_ARG;
    int rc = w25q_read(l->flash, slot_addr(idx), b, EVLOG_REC_SIZE);
    return rc != DEV_OK ? rc : decode(b, out);
}

int evlog_open(evlog_t *l, w25q_t *flash)
{
    l->flash = flash; l->next_idx = 0; l->next_seq = 1; l->valid = 0;
    int32_t newest = -1; uint16_t newest_seq = 0;
    uint8_t page[W25Q_PAGE_SIZE];
    for (uint32_t off = 0; off < EVLOG_CAPACITY * EVLOG_REC_SIZE; off += W25Q_PAGE_SIZE) {
        int rc = w25q_read(flash, EVLOG_BASE_ADDR + off, page, W25Q_PAGE_SIZE);
        if (rc != DEV_OK) return rc;
        for (unsigned i = 0; i < W25Q_PAGE_SIZE; i += EVLOG_REC_SIZE) {
            ev_rec_t r;
            if (decode(page + i, &r) != DEV_OK) continue;
            l->valid++;
            uint32_t idx = (off + i) / EVLOG_REC_SIZE;
            if (newest < 0 || (int16_t)(r.seq - newest_seq) > 0) { newest = (int32_t)idx; newest_seq = r.seq; }
        }
    }
    if (newest >= 0) { l->next_idx = ((uint32_t)newest + 1) % EVLOG_CAPACITY; l->next_seq = (uint16_t)(newest_seq + 1); }
    return DEV_OK;
}

int32_t evlog_newest_idx(const evlog_t *l)
{
    if (l->valid == 0) return -1;
    return (int32_t)((l->next_idx + EVLOG_CAPACITY - 1) % EVLOG_CAPACITY);
}

int evlog_append(evlog_t *l, uint8_t type, uint8_t frame, uint32_t t_ms, int16_t a, int16_t b, int16_t c)
{
    uint8_t buf[EVLOG_REC_SIZE];
    for (uint32_t tries = 0; tries <= EVLOG_RECS_PER_SECTOR; tries++) {
        uint32_t idx = l->next_idx;
        int rc;
        if (idx % EVLOG_RECS_PER_SECTOR == 0) {
            rc = w25q_sector_erase(l->flash, slot_addr(idx));       /* новий сектор: стираємо (довга операція) */
            if (rc != DEV_OK) return rc;
        } else {
            rc = w25q_read(l->flash, slot_addr(idx), buf, EVLOG_REC_SIZE);
            if (rc != DEV_OK) return rc;
            if (!is_blank(buf)) { l->next_idx = (idx + 1) % EVLOG_CAPACITY; continue; }   /* недописаний запис після втрати живлення */
        }
        ev_rec_t r = { type, frame, l->next_seq, t_ms, a, b, c };
        encode(&r, buf);
        rc = w25q_write(l->flash, slot_addr(idx), buf, EVLOG_REC_SIZE);
        if (rc != DEV_OK) return rc;
        l->next_idx = (idx + 1) % EVLOG_CAPACITY; l->next_seq++; l->valid++;
        return DEV_OK;
    }
    return DEV_ERR_BUS;
}
