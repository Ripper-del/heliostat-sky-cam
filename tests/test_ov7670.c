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

static int wrote(uint8_t reg, uint8_t val)
{
    for (int i = 0; i < m.write_calls && i < 64; i++) if (m.wr_regs[i] == reg && m.wr_vals[i] == val) return 1;
    return 0;
}

static void config_qqvga_resets_then_writes_table(void)
{
    setup();
    CHECK_EQ(ov7670_config_qqvga_yuv(&d), DEV_OK);
    CHECK_EQ(m.wr_regs[0], 0x12); CHECK_EQ(m.wr_vals[0], 0x80);   /* спочатку скидання */
    CHECK(m.write_calls > 20);
    CHECK(wrote(OV7670_REG_COM10, 0x20));                          /* PCLK не тактується в паузі */
    CHECK(wrote(OV7670_REG_COM14, 0x1A));                          /* PCLK /4 */
    CHECK(wrote(0x13, 0x80));                                      /* автоекспозиція вимкнена */
}

static void config_qqvga_stops_on_bus_error(void)
{
    setup(); m.fail_write_at = 5;
    CHECK_EQ(ov7670_config_qqvga_yuv(&d), DEV_ERR_BUS);
    CHECK_EQ(m.write_calls, 5);                                    /* далі не пишемо */
}

static void test_pattern_on_off(void)
{
    setup();
    CHECK_EQ(ov7670_set_test_pattern(&d, 1), DEV_OK);
    CHECK(wrote(OV7670_REG_XSC, 0xBA)); CHECK(wrote(OV7670_REG_YSC, 0xB5));
    setup();
    CHECK_EQ(ov7670_set_test_pattern(&d, 0), DEV_OK);
    CHECK(wrote(OV7670_REG_XSC, 0x3A)); CHECK(wrote(OV7670_REG_YSC, 0x35));
    setup(); m.fail_write_at = 1;
    CHECK_EQ(ov7670_set_test_pattern(&d, 1), DEV_ERR_BUS);
    CHECK_EQ(m.write_calls, 1);
}

static void exposure_splits_aec_into_registers(void)
{
    setup();
    CHECK_EQ(ov7670_set_exposure(&d, 0x1234, 0x2A), DEV_OK);
    CHECK(wrote(0x00, 0x2A));
    CHECK(wrote(0x07, 0x04));       /* (0x1234 >> 10) & 0x3F */
    CHECK(wrote(0x10, 0x8D));       /* (0x1234 >> 2) & 0xFF */
    CHECK(wrote(0x04, 0x00));       /* 0x1234 & 3 */
    setup(); m.fail_write_at = 3;
    CHECK_EQ(ov7670_set_exposure(&d, 0x1234, 0x2A), DEV_ERR_BUS);
    CHECK_EQ(m.write_calls, 3);
}

void test_ov7670(void)
{
    RUN(identify_ok); RUN(identify_retries_after_nack); RUN(identify_gives_up);
    RUN(identify_unexpected_id); RUN(identify_unexpected_id_is_retried); RUN(soft_reset_writes_com7);
    RUN(config_qqvga_resets_then_writes_table); RUN(config_qqvga_stops_on_bus_error); RUN(test_pattern_on_off); RUN(exposure_splits_aec_into_registers);
}
