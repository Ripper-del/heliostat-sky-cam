#include <string.h>
#include "mock_sccb.h"

static int m_read(void *ctx, uint8_t reg, uint8_t *val)
{
    mock_sccb_t *m = ctx;
    m->read_calls++;
    if (m->fail_reads > 0) { m->fail_reads--; return DEV_ERR_BUS; }
    if (reg == 0x0A && m->pid_seq_len > 0) { *val = m->pid_seq[--m->pid_seq_len]; return DEV_OK; }
    *val = (reg == 0x0A) ? m->pid : (reg == 0x0B) ? m->ver : 0;
    return DEV_OK;
}

static int m_write(void *ctx, uint8_t reg, uint8_t val)
{
    mock_sccb_t *m = ctx;
    m->write_calls++; m->last_wr_reg = reg; m->last_wr_val = val;
    return DEV_OK;
}

static mock_sccb_t *g_cur;
static void m_delay(uint32_t ms) { (void)ms; if (g_cur) g_cur->delay_calls++; }

void mock_sccb_init(mock_sccb_t *m)
{
    memset(m, 0, sizeof *m);
    m->pid = 0x76; m->ver = 0x73;
    g_cur = m;
    m->bus = (sccb_bus_t){ m, m_read, m_write, m_delay };
}
