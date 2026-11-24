/**
 * @file fjob.h — ЛР5: робота з flash частинами без очікування.
 * Завдання (job) — короткий список операцій: стерти сектор або записати байти. fjob_step() запускає ОДНУ коротку
 * операцію (стирання сектора або запис до FJOB_CHUNK байтів у межах сторінки) і повертається; поки flash зайнята, власник flash
 * робить інші справи і перевіряє готовність окремим кроком (w25q_busy). Тому довге стирання (десятки мс)
 * не займає процесор і не блокує задачу.
 * Чиста логіка поверх w25q.h: тестується на хості з fake-flash (tests/test_storage.c).
 */
#ifndef FJOB_H
#define FJOB_H
#include <stdint.h>
#include "w25q.h"

#define FJOB_MAX_OPS 8
/* Найбільший шматок запису за один крок. Сторінка (256 Б) при SPI 390 кГц — 5,4 мс передачі; власник flash має високий
 * пріоритет, і такі кроки підряд забирали процесор у cli та imu на весь запис кадру. 64 Б — 1,3 мс на крок. */
#ifndef FJOB_CHUNK
#define FJOB_CHUNK 64U
#endif

typedef enum { FOP_ERASE = 1, FOP_PROG = 2 } fop_kind_t;
typedef struct { uint8_t kind; uint32_t addr; const uint8_t *data; uint32_t len; } fop_t;
typedef struct {
    fop_t    op[FJOB_MAX_OPS];
    uint8_t  n, i;          /* кількість операцій, поточна */
    uint32_t done;          /* скільки байтів поточної FOP_PROG уже записано */
} fjob_t;

void fjob_init(fjob_t *j);
int  fjob_erase(fjob_t *j, uint32_t addr);                                     /* додати операцію; -1, якщо місця немає */
int  fjob_prog(fjob_t *j, uint32_t addr, const uint8_t *data, uint32_t len);
/** Запустити наступну коротку операцію. 1 — запущено (далі чекати, поки w25q_busy скаже 0); 0 — завдання виконано; <0 — помилка flash. */
int  fjob_step(fjob_t *j, w25q_t *f);
/** Виконати завдання до кінця з очікуванням (для хостових тестів і старту до планувальника). */
int  fjob_run_blocking(fjob_t *j, w25q_t *f);
#endif
