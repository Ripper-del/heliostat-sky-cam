#include "ringbuf.h"

void ring_init(ring_t *r, uint8_t *storage, uint16_t size)
{
    r->buf = storage; r->size = size; r->head = r->tail = 0; r->overflow = 0;
}

int ring_put(ring_t *r, uint8_t b)
{
    uint16_t next = (uint16_t)((r->head + 1) % r->size);
    if (next == r->tail) { r->overflow++; return 0; }
    r->buf[r->head] = b;
    r->head = next;
    return 1;
}

int ring_get(ring_t *r)
{
    if (r->tail == r->head) return -1;
    uint8_t b = r->buf[r->tail];
    r->tail = (uint16_t)((r->tail + 1) % r->size);
    return b;
}

uint16_t ring_level(const ring_t *r)
{
    return (uint16_t)((r->head + r->size - r->tail) % r->size);
}
