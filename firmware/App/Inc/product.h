/**
 * @file product.h — прикладна логіка продукту (ЛР3): таблиця задач super loop, режими, журнал, знімки.
 * Усе, що потрібно CLI, — функції product_*; внутрішній стан прихований у product.c.
 */
#ifndef PRODUCT_H
#define PRODUCT_H
#include <stdint.h>
#include "tracker.h"
#include "sky.h"

void product_init(void);          /* викликається один раз після самоперевірки */
void product_step(void);          /* один прохід super loop */

/* --- керування і діагностика (CLI) --- */
void product_print_stats(void);   /* STAT name=... n=... min_us=... avg_us=... max_us=... */
void product_reset_stats(void);
void product_print_mode(void);
void product_print_sky(void);
void product_print_mem(void);
void product_cal_show(void);
/** cmd: "AZ lo hi" | "EL lo hi" | "DARK thr" | "INV 0|1" | "SIGN AZ|EL +1|-1" | "SAVE" | "LOAD" | "DEFAULT"; повертає 0 при успіху. */
int  product_cal_cmd(int argc, char **argv);
void product_log_dump(int n);
int  product_log_erase(void);
void product_frames_info(void);
int  product_frame_show(uint32_t no);
void product_snapshot_request(void);
void product_servo_power(int on);
int  product_cam_exposure(uint32_t aec, uint32_t gain);
void product_print_version(void);
/** Єдиний буфер кадру 160x120 (19 200 Б): користуються задачі і selftest, щоб не тримати два однакові масиви в RAM. */
uint8_t *product_frame_buf(void);
void product_cmd_started(void);   /* CLI викликає перед виконанням рядкової команди: міряє затримку відповіді */
/** Змінити період задачі за ім'ям (мс); 0 — успіх, -1 — нема такої задачі або період поза 1..60000 мс. Для пошуку точки зламу. */
int  product_set_period(const char *name, uint32_t ms);
#endif
