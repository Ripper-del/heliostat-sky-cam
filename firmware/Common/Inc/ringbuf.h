/** @file ringbuf.h — кільцевий буфер байтів: один записувач (ISR) і один читач (цикл). Лічить втрати при переповненні. */
#ifndef RINGBUF_H
#define RINGBUF_H
#include <stdint.h>

typedef struct {
    uint8_t *buf; uint16_t size;
    volatile uint16_t head, tail;     /* head пише лише записувач, tail — лише читач */
    volatile uint32_t overflow;       /* скільки байтів загублено через повний буфер */
} ring_t;

void ring_init(ring_t *r, uint8_t *storage, uint16_t size);
int  ring_put(ring_t *r, uint8_t b);   /* 1 — записано, 0 — повний (байт втрачено) */
int  ring_get(ring_t *r);              /* байт або -1 */
uint16_t ring_level(const ring_t *r);
#endif
