#include <stdlib.h>
#include <string.h>
#include "fake_w25q.h"

static void f_begin(void *ctx)
{
    fake_w25q_t *f = ctx;
    f->in_xact = 1; f->cmd = 0; f->hdr = 0; f->addr = 0; f->data_off = 0;
}

static void f_end(void *ctx)
{
    fake_w25q_t *f = ctx;
    f->in_xact = 0;
    if (f->cmd == 0x06 && !f->wel_broken) f->sr |= 0x02;
    if (f->cmd == 0x20 && (f->sr & 0x02)) {
        memset(f->mem + (f->addr & ~0xFFFU), 0xFF, 4096);
        f->sr &= (uint8_t)~0x02; f->busy_polls = f->busy_after_op;
    }
    if (f->cmd == 0x02 && (f->sr & 0x02)) { f->sr &= (uint8_t)~0x02; f->busy_polls = f->busy_after_op; }
}

static int f_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    fake_w25q_t *f = ctx;
    if (!f->in_xact) f->cs_depth_errors++;
    if (f->bus_fail) return DEV_ERR_BUS;
    if (tx) {
        for (uint16_t i = 0; i < len; i++) {
            if (f->hdr == 0) {
                f->cmd = tx[i]; f->hdr = 1;
                if (f->nops < 128) f->opcodes[f->nops++] = f->cmd;
                if (f->cmd == 0x02 && f->nprog < 32) f->prog_len[f->nprog++] = 0;
            } else if ((f->cmd == 0x20 || f->cmd == 0x02 || f->cmd == 0x03) && f->hdr <= 3) {
                f->addr = (f->addr << 8) | tx[i]; f->hdr++;
            } else if (f->cmd == 0x02) {      /* дані Page Program: адреса загортається в межах сторінки */
                uint32_t page = f->addr & ~0xFFU, off = (f->addr + f->data_off) & 0xFFU;
                f->mem[page + off] &= tx[i];
                f->data_off++;
                f->prog_len[f->nprog - 1]++;
            }
        }
    }
    if (rx) {
        for (uint16_t i = 0; i < len; i++) {
            switch (f->cmd) {
            case 0x9F: rx[i] = f->jedec[i % 3]; break;
            case 0x05:
                if (f->stuck_busy || f->busy_polls > 0) { rx[i] = f->sr | 0x01; if (!f->stuck_busy) f->busy_polls--; }
                else rx[i] = f->sr & (uint8_t)~0x01;
                break;
            case 0x03: rx[i] = f->mem[(f->addr + f->data_off++) % FAKE_FLASH_SIZE]; break;
            default: rx[i] = 0xFF;
            }
        }
    }
    return DEV_OK;
}

static void f_delay(uint32_t ms) { (void)ms; }

void fake_w25q_init(fake_w25q_t *f)
{
    memset(f, 0, sizeof *f);
    f->mem = malloc(FAKE_FLASH_SIZE);
    memset(f->mem, 0xFF, FAKE_FLASH_SIZE);
    f->jedec[0] = 0xEF; f->jedec[1] = 0x40; f->jedec[2] = 0x17;
    f->bus = (spi_bus_t){ f, f_begin, f_xfer, f_end, f_delay };
}

void fake_w25q_free(fake_w25q_t *f) { free(f->mem); f->mem = NULL; }
