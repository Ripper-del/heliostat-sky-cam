#include <string.h>
#include "rtstats.h"

void rts_init(rtstats_t *s, uint32_t cyc)
{
    memset(s, 0, sizeof *s);
    s->last_cyc = cyc;
}

static rts_task_t *find(rtstats_t *s, uint32_t id, int create)
{
    for (int i = 0; i < s->n; i++) if (s->t[i].id == id) return &s->t[i];
    if (!create || s->n >= RTS_MAX_TASKS) return 0;
    s->t[s->n] = (rts_task_t){ .id = id };
    return &s->t[s->n++];
}

void rts_flush(rtstats_t *s, uint32_t cyc)
{
    rts_task_t *p = s->cur_id ? find(s, s->cur_id, 1) : 0;
    if (p) p->run_cyc += (uint32_t)(cyc - s->last_cyc);      /* різниця беззнакових 32-бітних коректна після переповнення DWT */
    s->last_cyc = cyc;
}

void rts_switch(rtstats_t *s, uint32_t id, uint32_t cyc)
{
    rts_flush(s, cyc);
    rts_task_t *n = find(s, id, 1);
    if (n) n->switches++; else s->unknown++;
    s->cur_id = id;
    s->total_switches++;
    if (s->trace_on) {
        if (s->ev_count < RTS_TRACE_N) s->ev[s->ev_count++] = (rts_ev_t){ cyc, id };
        else s->trace_on = 0;
    }
}

void rts_reset(rtstats_t *s, uint32_t cyc)
{
    for (int i = 0; i < s->n; i++) { s->t[i].run_cyc = 0; s->t[i].switches = 0; }
    s->total_switches = 0; s->unknown = 0;
    s->last_cyc = cyc;
}

uint64_t rts_run_cyc(const rtstats_t *s, uint32_t id)
{
    for (int i = 0; i < s->n; i++) if (s->t[i].id == id) return s->t[i].run_cyc;
    return 0;
}

uint64_t rts_total_cyc(const rtstats_t *s)
{
    uint64_t sum = 0;
    for (int i = 0; i < s->n; i++) sum += s->t[i].run_cyc;
    return sum;
}

void rts_trace_arm(rtstats_t *s) { s->ev_count = 0; s->trace_on = 1; }
