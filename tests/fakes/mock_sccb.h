/* MOCK SCCB: перевіряє ВЗАЄМОДІЮ (які регістри й скільки разів читали/писали, скільки пауз),
 * а не лише результат — на відміну від fake, що імітує поведінку пристрою. */
#ifndef MOCK_SCCB_H
#define MOCK_SCCB_H
#include "bus.h"

typedef struct {
    uint8_t pid, ver;
    uint8_t pid_seq[4]; int pid_seq_len;   /* перші читання PID повертають ці значення (збій кадру) */
    int fail_reads;            /* перші N read_reg -> DEV_ERR_BUS */
    int read_calls, write_calls, delay_calls;
    uint8_t last_wr_reg, last_wr_val;
    sccb_bus_t bus;
} mock_sccb_t;

void mock_sccb_init(mock_sccb_t *m);
#endif
