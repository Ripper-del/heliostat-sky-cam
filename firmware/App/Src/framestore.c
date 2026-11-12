#include "framestore.h"
#include "crc16.h"

static uint32_t slot_addr(uint32_t frame_no) { return FRAMESTORE_BASE + (frame_no % FRAMESTORE_SLOTS) * FRAMESTORE_SLOT_SIZE; }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int framestore_save(w25q_t *f, uint32_t frame_no, const uint8_t *px, uint16_t len)
{
    if (len == 0 || (uint32_t)len + FRAMESTORE_HDR > FRAMESTORE_SLOT_SIZE) return DEV_ERR_ARG;
    uint32_t a = slot_addr(frame_no);
    for (uint32_t s = 0; s < FRAMESTORE_SLOT_SIZE; s += W25Q_SECTOR_SIZE) {
        int rc = w25q_sector_erase(f, a + s);
        if (rc != DEV_OK) return rc;
    }
    int rc = w25q_write(f, a + FRAMESTORE_HDR, px, len);
    if (rc != DEV_OK) return rc;
    uint8_t h[FRAMESTORE_HDR] = {0};
    put32(h, FRAMESTORE_MAGIC); put32(h + 4, frame_no);
    h[8] = (uint8_t)len; h[9] = (uint8_t)(len >> 8);
    uint16_t c = crc16(px, len);
    h[10] = (uint8_t)c; h[11] = (uint8_t)(c >> 8);
    return w25q_write(f, a, h, FRAMESTORE_HDR);
}

static int read_hdr(w25q_t *f, uint32_t addr, uint32_t *frame_no, uint16_t *len, uint16_t *crc)
{
    uint8_t h[FRAMESTORE_HDR];
    int rc = w25q_read(f, addr, h, FRAMESTORE_HDR);
    if (rc != DEV_OK) return rc;
    if (get32(h) != FRAMESTORE_MAGIC) return DEV_ERR_ID;
    *frame_no = get32(h + 4); *len = (uint16_t)(h[8] | (h[9] << 8)); *crc = (uint16_t)(h[10] | (h[11] << 8));
    return DEV_OK;
}

int framestore_load(w25q_t *f, uint32_t frame_no, uint8_t *px, uint16_t len)
{
    uint32_t no; uint16_t l, c;
    int rc = read_hdr(f, slot_addr(frame_no), &no, &l, &c);
    if (rc != DEV_OK) return rc;
    if (no != frame_no || l != len) return DEV_ERR_ID;
    rc = w25q_read(f, slot_addr(frame_no) + FRAMESTORE_HDR, px, len);
    if (rc != DEV_OK) return rc;
    return crc16(px, len) == c ? DEV_OK : DEV_ERR_ID;
}

int framestore_scan(w25q_t *f, uint32_t *next)
{
    uint32_t best = 0;
    for (uint32_t s = 0; s < FRAMESTORE_SLOTS; s++) {
        uint32_t no; uint16_t l, c;
        int rc = read_hdr(f, FRAMESTORE_BASE + s * FRAMESTORE_SLOT_SIZE, &no, &l, &c);
        if (rc == DEV_ERR_ID) continue;
        if (rc != DEV_OK) return rc;
        if (no > best) best = no;
    }
    *next = best + 1;
    return DEV_OK;
}
