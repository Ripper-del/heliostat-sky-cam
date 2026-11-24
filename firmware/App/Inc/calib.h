/** @file calib.h — калібровка серво і порогів: зберігається в секторі 0 W25Q64 із CRC (FR-09). */
#ifndef CALIB_H
#define CALIB_H
#include <stdint.h>
#include "w25q.h"
#include "tracker.h"
#include "fjob.h"

#define CALIB_ADDR   0x000000UL
#define CALIB_MAGIC  0x43414C31UL     /* 'CAL1' */

/** DEV_OK — прочитано; DEV_ERR_ID — порожньо або CRC не збігся (тоді *c не змінюється). */
int calib_load(w25q_t *f, tracker_cfg_t *c);
int calib_save(w25q_t *f, const tracker_cfg_t *c);
/** Перевірка меж: min < max, у діапазоні апаратних меж 400…2600 мкс. 0 — коректно. */
int calib_check_range(uint16_t lo, uint16_t hi);
#define CALIB_LEN    32U
/* ЛР5: план того самого запису (стерти сектор 0, записати 32 Б); b має жити до кінця завдання */
int calib_plan_save(fjob_t *j, const tracker_cfg_t *c, uint8_t b[CALIB_LEN]);
#endif
