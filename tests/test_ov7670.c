#include "test.h"
#include "ov7670.h"
#include "mock_sccb.h"
#include "vectors.h"

static mock_sccb_t m; static ov7670_t d;
static void setup(void) { mock_sccb_init(&m); ov7670_init_dev(&d, &m.bus); }

static void identify_ok(void)
{
    setup(); uint8_t pid = 0, ver = 0;
    CHECK_EQ(ov7670_identify(&d, &pid, &ver), DEV_OK);
    CHECK_EQ(pid, VEC_OV7670_ID[0]); CHECK_EQ(ver, VEC_OV7670_ID[1]);
    CHECK_EQ(m.read_calls, 2); CHECK_EQ(m.delay_calls, 0);
}

static void identify_retries_after_nack(void)
{
    setup(); m.fail_reads = 2; uint8_t pid = 0, ver = 0;     /* камера прокидається після XCLK */
    CHECK_EQ(ov7670_identify(&d, &pid, &ver), DEV_OK);
    CHECK_EQ(m.delay_calls, 2);                               /* пауза після кожної невдалої спроби */
}

static void identify_gives_up(void)
{
    setup(); m.fail_reads = 100; uint8_t pid = 0, ver = 0;
    CHECK_EQ(ov7670_identify(&d, &pid, &ver), DEV_ERR_BUS);
    CHECK_EQ(m.read_calls, 3);                                /* рівно 3 спроби */
}

static void identify_unexpected_id(void)
{
    setup(); m.pid = 0xFF; m.ver = 0xFF; uint8_t pid = 0, ver = 0;   /* порожня шина читається як 0xFF */
    CHECK_EQ(ov7670_identify(&d, &pid, &ver), DEV_ERR_ID);
    CHECK_EQ(pid, 0xFF); CHECK_EQ(m.read_calls, 6);     /* 3 спроби по 2 читання */
    m.pid = 0x00;
    CHECK_EQ(ov7670_identify(&d, &pid, &ver), DEV_ERR_ID);
    m.pid = 0x77;                                             /* інший чип, напр. OV7725 */
    CHECK_EQ(ov7670_identify(&d, &pid, &ver), DEV_ERR_ID);
}

static void identify_unexpected_id_is_retried(void)
{
    setup(); uint8_t pid = 0, ver = 0;
    m.pid_seq[0] = 0x73; m.pid_seq_len = 1;          /* збій кадру: перший PID = 0x73 (значення VER), далі норма */
    CHECK_EQ(ov7670_identify(&d, &pid, &ver), DEV_OK);
    CHECK_EQ(pid, 0x76);
    CHECK_EQ(m.delay_calls, 1);
}

static void soft_reset_writes_com7(void)
{
    setup();
    CHECK_EQ(ov7670_soft_reset(&d), DEV_OK);
    CHECK_EQ(m.write_calls, 1); CHECK_EQ(m.last_wr_reg, 0x12); CHECK_EQ(m.last_wr_val, 0x80);
    CHECK_EQ(m.delay_calls, 1);
}

void test_ov7670(void)
{
    RUN(identify_ok); RUN(identify_retries_after_nack); RUN(identify_gives_up);
    RUN(identify_unexpected_id); RUN(identify_unexpected_id_is_retried); RUN(soft_reset_writes_com7);
}
