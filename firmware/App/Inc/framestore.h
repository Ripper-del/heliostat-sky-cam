/**
 * @file framestore.h — кільцевий буфер знімків у W25Q64: FRAMESTORE_SLOTS слотів по 5 секторів (FR-15).
 * Слот = frame_no % SLOTS. Заголовок пишеться ОСТАННІМ, тому слот із коректним заголовком завжди повний.
 */
#ifndef FRAMESTORE_H
#define FRAMESTORE_H
#include <stdint.h>
#include "w25q.h"
#include "fjob.h"

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
/* ЛР5: план того самого запису (5 стирань, піксели, заголовок останнім); hdr і px мають жити до кінця завдання */
int framestore_plan_save(fjob_t *j, uint32_t frame_no, const uint8_t *px, uint16_t len, uint8_t hdr[FRAMESTORE_HDR]);
/* ЛР5: читання кадру частинами (замість одного довгого кроку): заголовок, далі w25q_read шматками, наприкінці CRC */
int framestore_load_begin(w25q_t *f, uint32_t frame_no, uint16_t len, uint32_t *data_addr, uint16_t *crc);
int framestore_load_check(const uint8_t *px, uint16_t len, uint16_t crc);
#endif
