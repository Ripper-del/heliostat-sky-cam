#include "w25q.h"

void w25q_init_dev(w25q_t *d, const spi_bus_t *bus) { d->bus = bus; }

static int cmd_only(w25q_t *d, uint8_t cmd)
{
    d->bus->begin(d->bus->ctx);
    int rc = d->bus->xfer(d->bus->ctx, &cmd, NULL, 1);
    d->bus->end(d->bus->ctx);
    return rc == DEV_OK ? DEV_OK : DEV_ERR_BUS;
}

static int cmd_addr(w25q_t *d, uint8_t cmd, uint32_t addr)
{
    uint8_t h[4] = { cmd, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr };
    return d->bus->xfer(d->bus->ctx, h, NULL, 4) == DEV_OK ? DEV_OK : DEV_ERR_BUS;
}

int w25q_wake(w25q_t *d)
{
    int rc = cmd_only(d, W25Q_CMD_RELEASE_PD);
    if (d->bus->delay_ms) d->bus->delay_ms(1);   /* tRES1 ≈ 3 мкс, беремо 1 мс із запасом */
    return rc;
}

int w25q_read_jedec(w25q_t *d, uint8_t id[3])
{
    uint8_t cmd = W25Q_CMD_JEDEC_ID;
    d->bus->begin(d->bus->ctx);
    int rc = d->bus->xfer(d->bus->ctx, &cmd, NULL, 1);
    if (rc == DEV_OK) rc = d->bus->xfer(d->bus->ctx, NULL, id, 3);
    d->bus->end(d->bus->ctx);
    if (rc != DEV_OK) return DEV_ERR_BUS;
    return id[0] == W25Q_JEDEC_MFR ? DEV_OK : DEV_ERR_ID;
}

int w25q_read_status(w25q_t *d, uint8_t *sr1)
{
    uint8_t cmd = W25Q_CMD_READ_STATUS1;
    d->bus->begin(d->bus->ctx);
    int rc = d->bus->xfer(d->bus->ctx, &cmd, NULL, 1);
    if (rc == DEV_OK) rc = d->bus->xfer(d->bus->ctx, NULL, sr1, 1);
    d->bus->end(d->bus->ctx);
    return rc == DEV_OK ? DEV_OK : DEV_ERR_BUS;
}

int w25q_wait_ready(w25q_t *d, uint32_t timeout_ms)
{
    for (uint32_t t = 0; t <= timeout_ms; t++) {
        uint8_t sr;
        if (w25q_read_status(d, &sr) != DEV_OK) return DEV_ERR_BUS;
        if (!(sr & W25Q_SR1_BUSY)) return DEV_OK;
        if (d->bus->delay_ms) d->bus->delay_ms(1);
    }
    return DEV_ERR_TIMEOUT;
}

static int write_enable(w25q_t *d)
{
    int rc = cmd_only(d, W25Q_CMD_WRITE_ENABLE);
    if (rc != DEV_OK) return rc;
    uint8_t sr = 0;
    rc = w25q_read_status(d, &sr);
    if (rc != DEV_OK) return rc;
    return (sr & W25Q_SR1_WEL) ? DEV_OK : DEV_ERR_BUS;   /* WEL не встановився — запис не дозволено */
}

int w25q_sector_erase(w25q_t *d, uint32_t addr)
{
    int rc = write_enable(d);
    if (rc != DEV_OK) return rc;
    d->bus->begin(d->bus->ctx);
    rc = cmd_addr(d, W25Q_CMD_SECTOR_ERASE, addr);
    d->bus->end(d->bus->ctx);
    if (rc != DEV_OK) return rc;
    return w25q_wait_ready(d, 500);               /* tSE typ 45 мс, max 400 мс */
}

int w25q_write(w25q_t *d, uint32_t addr, const uint8_t *data, uint32_t len)
{
    while (len) {
        uint32_t room = W25Q_PAGE_SIZE - (addr % W25Q_PAGE_SIZE);   /* не перетинати межу сторінки */
        uint32_t n = len < room ? len : room;
        int rc = write_enable(d);
        if (rc != DEV_OK) return rc;
        d->bus->begin(d->bus->ctx);
        rc = cmd_addr(d, W25Q_CMD_PAGE_PROGRAM, addr);
        if (rc == DEV_OK) rc = d->bus->xfer(d->bus->ctx, data, NULL, (uint16_t)n) == DEV_OK ? DEV_OK : DEV_ERR_BUS;
        d->bus->end(d->bus->ctx);
        if (rc != DEV_OK) return rc;
        rc = w25q_wait_ready(d, 10);              /* tPP typ 0.4 мс, max 3 мс */
        if (rc != DEV_OK) return rc;
        addr += n; data += n; len -= n;
    }
    return DEV_OK;
}

int w25q_read(w25q_t *d, uint32_t addr, uint8_t *buf, uint32_t len)
{
    d->bus->begin(d->bus->ctx);
    int rc = cmd_addr(d, W25Q_CMD_READ_DATA, addr);
    if (rc == DEV_OK && len) rc = d->bus->xfer(d->bus->ctx, NULL, buf, (uint16_t)len) == DEV_OK ? DEV_OK : DEV_ERR_BUS;
    d->bus->end(d->bus->ctx);
    return rc;
}
