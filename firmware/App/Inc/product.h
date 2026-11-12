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
#endif
