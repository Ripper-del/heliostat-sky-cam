/**
 * @file ov7670.h — драйвер OV7670 поверх SCCB (OmniVision OV7670/OV7171 datasheet; SCCB Spec).
 *
 * SCCB ≠ I²C: 9-й біт ("ACK") — don't care, немає repeated-START. Це приховано в реалізації
 * sccb_bus_t (Platform); драйвер лише читає/пише регістри.
 */
#ifndef OV7670_H
#define OV7670_H
#include "bus.h"

#define OV7670_ADDR7     0x21     /* 0x42 write / 0x43 read */
#define OV7670_REG_PID   0x0A
#define OV7670_REG_VER   0x0B
#define OV7670_REG_COM7  0x12
#define OV7670_PID_VALUE 0x76
#define OV7670_VER_VALUE 0x73

typedef struct { const sccb_bus_t *bus; } ov7670_t;

void ov7670_init_dev(ov7670_t *d, const sccb_bus_t *bus);
/** Читає PID/VER (до 3 спроб: після подачі XCLK камері треба час). DEV_ERR_ID, якщо PID != 0x76. */
int  ov7670_identify(ov7670_t *d, uint8_t *pid, uint8_t *ver);
/** Програмне скидання (COM7[7] = 1) та пауза. */
int  ov7670_soft_reset(ov7670_t *d);
#endif
