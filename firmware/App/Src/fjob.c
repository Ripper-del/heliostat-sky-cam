#include "fjob.h"

void fjob_init(fjob_t *j) { j->n = 0; j->i = 0; j->done = 0; }

static int add(fjob_t *j, uint8_t kind, uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (j->n >= FJOB_MAX_OPS) return -1;
    fop_t *o = &j->op[j->n++];
    o->kind = kind; o->addr = addr; o->data = data; o->len = len;
    return 0;
}

int fjob_erase(fjob_t *j, uint32_t addr) { return add(j, FOP_ERASE, addr, 0, 0); }
int fjob_prog(fjob_t *j, uint32_t addr, const uint8_t *data, uint32_t len) { return add(j, FOP_PROG, addr, data, len); }

int fjob_step(fjob_t *j, w25q_t *f)
{
    while (j->i < j->n) {
        fop_t *o = &j->op[j->i];
        if (o->kind == FOP_ERASE) {
            j->i++; j->done = 0;
            return w25q_erase_start(f, o->addr) == DEV_OK ? 1 : DEV_ERR_BUS;
        }
        if (j->done >= o->len) { j->i++; j->done = 0; continue; }
        uint32_t a = o->addr + j->done, room = W25Q_PAGE_SIZE - (a % W25Q_PAGE_SIZE), left = o->len - j->done;
        uint32_t n = left < room ? left : room;                 /* до кінця сторінки, як у w25q_write */
        if (n > FJOB_CHUNK) n = FJOB_CHUNK;                     /* і не довше за один короткий крок */
        int rc = w25q_program_start(f, a, o->data + j->done, n);
        if (rc != DEV_OK) return rc;
        j->done += n;
        return 1;
    }
    return 0;
}

int fjob_run_blocking(fjob_t *j, w25q_t *f)
{
    for (;;) {
        int rc = fjob_step(j, f);
        if (rc <= 0) return rc;
        rc = w25q_wait_ready(f, 500);
        if (rc != DEV_OK) return rc;
    }
}
