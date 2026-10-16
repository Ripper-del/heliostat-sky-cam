/* FAKE SPI-флеш W25Q: модель команд 0x06/0x05/0x9F/0x20/0x02/0x03 з WEL, BUSY, AND-семантикою
 * програмування (1 -> 0), загортанням адреси в межах сторінки та журналом опкодів. */
#ifndef FAKE_W25Q_H
#define FAKE_W25Q_H
#include "bus.h"

#define FAKE_FLASH_SIZE (8U * 1024U * 1024U)

typedef struct {
    uint8_t *mem;
    uint8_t jedec[3];
    uint8_t sr;                 /* біт0 BUSY, біт1 WEL */
    int busy_polls;             /* скільки читань статусу ще повертатимуть BUSY */
    int busy_after_op;          /* BUSY на стільки читань після erase/program */
    int stuck_busy;             /* BUSY ніколи не знімається */
    int wel_broken;             /* Write Enable не виставляє WEL */
    int bus_fail;               /* xfer повертає помилку */
    int in_xact;
    uint8_t cmd; uint32_t addr; int hdr; uint32_t data_off;
    uint8_t opcodes[128]; int nops;      /* журнал опкодів у порядку виконання */
    uint32_t prog_len[32]; int nprog;    /* довжини кожної Page Program */
    int cs_depth_errors;                 /* xfer поза begin/end */
    spi_bus_t bus;
} fake_w25q_t;

void fake_w25q_init(fake_w25q_t *f);
void fake_w25q_free(fake_w25q_t *f);
#endif
