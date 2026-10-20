/** @file w25q.h — драйвер SPI NOR flash Winbond W25Q64JV (datasheet: Instruction Set Table). Лише bus.h. */
#ifndef W25Q_H
#define W25Q_H
#include "bus.h"

#define W25Q_CMD_WRITE_ENABLE  0x06
#define W25Q_CMD_READ_STATUS1  0x05
#define W25Q_CMD_PAGE_PROGRAM  0x02
#define W25Q_CMD_READ_DATA     0x03
#define W25Q_CMD_SECTOR_ERASE  0x20   /* 4 КБ */
#define W25Q_CMD_RELEASE_PD    0xAB
#define W25Q_CMD_JEDEC_ID      0x9F
#define W25Q_SR1_BUSY          0x01
#define W25Q_SR1_WEL           0x02
#define W25Q_PAGE_SIZE         256U
#define W25Q_SECTOR_SIZE       4096U
#define W25Q_JEDEC_MFR         0xEF   /* Winbond */

typedef struct { const spi_bus_t *bus; } w25q_t;

void w25q_init_dev(w25q_t *d, const spi_bus_t *bus);
int  w25q_wake(w25q_t *d);                                  /* Release Power-down (0xAB) */
int  w25q_read_jedec(w25q_t *d, uint8_t id[3]);             /* DEV_ERR_ID, якщо виробник != EF */
int  w25q_read_status(w25q_t *d, uint8_t *sr1);
int  w25q_wait_ready(w25q_t *d, uint32_t timeout_ms);       /* DEV_ERR_TIMEOUT при BUSY */
int  w25q_sector_erase(w25q_t *d, uint32_t addr);           /* WE + 0x20 + 24-бітна адреса + очікування */
/** Запис довільної довжини: розбивається на сторінки по 256 Б; перед КОЖНОЮ сторінкою Write Enable. */
int  w25q_write(w25q_t *d, uint32_t addr, const uint8_t *data, uint32_t len);
int  w25q_read(w25q_t *d, uint32_t addr, uint8_t *buf, uint32_t len);
#endif
