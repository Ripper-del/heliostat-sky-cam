/**
 * @file evlog.h — журнал подій у W25Q64 записами по 16 байт із CRC-16 (FR-07, FR-08, FR-15).
 * Кільце з EVLOG_SECTORS секторів; перед першим записом у сектор він стирається (найстаріші записи зникають).
 * Запис: type(1) frame(1) seq(2) t_ms(4) a(2) b(2) c(2) crc16(2). Порожній слот = усі 0xFF.
 * Залежить лише від драйвера w25q.
 */
#ifndef EVLOG_H
#define EVLOG_H
#include <stdint.h>
#include "w25q.h"
#include "fjob.h"

#define EVLOG_REC_SIZE      16U
#define EVLOG_BASE_ADDR     0x001000UL      /* сектор 0 — калібровка */
#define EVLOG_SECTORS       4U
#define EVLOG_RECS_PER_SECTOR (W25Q_SECTOR_SIZE / EVLOG_REC_SIZE)
#define EVLOG_CAPACITY      (EVLOG_SECTORS * EVLOG_RECS_PER_SECTOR)
#define EVLOG_NO_FRAME      0xFF

typedef enum { EV_BOOT = 1, EV_MODE = 2, EV_SIGNAL_LOSS = 3, EV_SENSOR_SUSPECT = 4, EV_CAL = 5, EV_USER = 6 } ev_type_t;

typedef struct { uint8_t type, frame; uint16_t seq; uint32_t t_ms; int16_t a, b, c; } ev_rec_t;

typedef struct { w25q_t *flash; uint32_t next_idx; uint16_t next_seq; uint32_t valid; } evlog_t;

/** Сканує область, знаходить найновіший запис (за seq) і наступний слот. valid = кількість записів з коректним CRC. */
int  evlog_open(evlog_t *l, w25q_t *flash);
int  evlog_append(evlog_t *l, uint8_t type, uint8_t frame, uint32_t t_ms, int16_t a, int16_t b, int16_t c);
/** idx = 0..EVLOG_CAPACITY-1; DEV_OK, якщо слот містить коректний запис; DEV_ERR_ID — порожній або CRC не збігся. */
int  evlog_read(evlog_t *l, uint32_t idx, ev_rec_t *out);
/** Індекс слота найновішого запису (для виводу "останні N"), або -1. */
int32_t evlog_newest_idx(const evlog_t *l);
/* ЛР5: той самий запис частинами. plan — вибрати слот і скласти завдання (читання тут коротке), commit — після виконання завдання.
 * buf має жити до кінця завдання. evlog_append = plan + fjob_run_blocking + commit. */
int  evlog_plan_append(evlog_t *l, uint8_t type, uint8_t frame, uint32_t t_ms, int16_t a, int16_t b, int16_t c,
                       fjob_t *j, uint8_t buf[EVLOG_REC_SIZE], uint32_t *idx_out);
void evlog_commit(evlog_t *l, uint32_t idx);
#endif
