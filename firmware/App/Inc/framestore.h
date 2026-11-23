/**
 * @file framestore.h — кільцевий буфер знімків у W25Q64: FRAMESTORE_SLOTS слотів по 5 секторів (FR-15).
 * Слот = frame_no % SLOTS. Заголовок пишеться ОСТАННІМ, тому слот із коректним заголовком завжди повний.
 */
#ifndef FRAMESTORE_H
#define FRAMESTORE_H
#include <stdint.h>
#include "w25q.h"

#define FRAMESTORE_BASE     0x100000UL
#define FRAMESTORE_SLOTS    20U
#define FRAMESTORE_SLOT_SIZE (5U * W25Q_SECTOR_SIZE)
#define FRAMESTORE_HDR      16U
#define FRAMESTORE_MAGIC    0x4652414DUL    /* 'FRAM' */

/** Номери кадрів починаються з 1. */
int framestore_save(w25q_t *f, uint32_t frame_no, const uint8_t *px, uint16_t len);
/** DEV_ERR_ID, якщо слот порожній, у ньому інший кадр або не збігся CRC. */
int framestore_load(w25q_t *f, uint32_t frame_no, uint8_t *px, uint16_t len);
/** Знаходить найбільший збережений номер кадру; *next = він + 1 (1, якщо порожньо). */
int framestore_scan(w25q_t *f, uint32_t *next);
#endif
