#include <stdlib.h>
#include "test.h"
#include "w25q.h"
#include "fake_w25q.h"
#include "vectors.h"

static fake_w25q_t f; static w25q_t d;
static void setup(void) { fake_w25q_init(&f); w25q_init_dev(&d, &f.bus); }
static void teardown(void) { fake_w25q_free(&f); }

static int count_op(uint8_t op) { int n = 0; for (int i = 0; i < f.nops; i++) if (f.opcodes[i] == op) n++; return n; }

static void jedec_ok(void)
{
    setup(); uint8_t id[3];
    CHECK_EQ(w25q_read_jedec(&d, id), DEV_OK);
    CHECK(memcmp(id, VEC_JEDEC, 3) == 0);
    CHECK_EQ(f.cs_depth_errors, 0);
    teardown();
}

static void jedec_bad_and_bus_errors(void)
{
    setup(); uint8_t id[3];
    f.jedec[0] = 0xFF; f.jedec[1] = 0xFF; f.jedec[2] = 0xFF;     /* MISO висить у 1 — чіпа немає */
    CHECK_EQ(w25q_read_jedec(&d, id), DEV_ERR_ID);
    f.jedec[0] = 0x00;                                           /* MISO притиснуто до 0 */
    CHECK_EQ(w25q_read_jedec(&d, id), DEV_ERR_ID);
    f.bus_fail = 1;
    CHECK_EQ(w25q_read_jedec(&d, id), DEV_ERR_BUS);
    teardown();
}

static void wait_ready_timeout(void)
{
    setup(); f.stuck_busy = 1;
    CHECK_EQ(w25q_wait_ready(&d, 5), DEV_ERR_TIMEOUT);
    f.stuck_busy = 0; f.busy_polls = 3;
    CHECK_EQ(w25q_wait_ready(&d, 10), DEV_OK);
    f.bus_fail = 1;
    CHECK_EQ(w25q_wait_ready(&d, 10), DEV_ERR_BUS);
    teardown();
}

static void erase_sequence_write_enable_first(void)
{
    setup(); f.busy_after_op = 2;
    memset(f.mem + 0x7FF000, 0x00, 4096);
    CHECK_EQ(w25q_sector_erase(&d, 0x7FF123), DEV_OK);
    CHECK_EQ(f.opcodes[0], 0x06);                 /* WE */
    CHECK_EQ(f.opcodes[1], 0x05);                 /* перевірка WEL */
    CHECK_EQ(f.opcodes[2], 0x20);                 /* Sector Erase */
    CHECK_EQ(f.mem[0x7FF000], 0xFF); CHECK_EQ(f.mem[0x7FFFFF], 0xFF);
    teardown();
}

static void write_enable_before_every_page(void)
{
    setup(); uint8_t data[300], back[300];
    for (int i = 0; i < 300; i++) data[i] = (uint8_t)i;
    CHECK_EQ(w25q_write(&d, 0xF0, data, 300), DEV_OK);
    CHECK_EQ(count_op(0x02), 3);                  /* 16 + 256 + 28 байт: не перетинає межу сторінки */
    CHECK_EQ(count_op(0x06), 3);                  /* WE перед КОЖНОЮ сторінкою */
    CHECK_EQ(f.prog_len[0], 16); CHECK_EQ(f.prog_len[1], 256); CHECK_EQ(f.prog_len[2], 28);
    for (int i = 0, we = 0; i < f.nops; i++) {    /* жодного 0x02 без попереднього 0x06 */
        if (f.opcodes[i] == 0x06) we++;
        if (f.opcodes[i] == 0x02) { CHECK(we > 0); we--; }
    }
    CHECK_EQ(w25q_read(&d, 0xF0, back, 300), DEV_OK);
    CHECK(memcmp(data, back, 300) == 0);
    teardown();
}

static void write_fails_when_wel_not_set(void)
{
    setup(); f.wel_broken = 1; uint8_t b = 0x55;
    CHECK_EQ(w25q_write(&d, 0, &b, 1), DEV_ERR_BUS);
    CHECK_EQ(count_op(0x02), 0);                  /* команду програмування не надіслано */
    CHECK_EQ(w25q_sector_erase(&d, 0), DEV_ERR_BUS);
    CHECK_EQ(count_op(0x20), 0);
    teardown();
}

static void write_timeout_when_stuck_busy(void)
{
    setup(); f.stuck_busy = 1; uint8_t b = 1;
    CHECK_EQ(w25q_write(&d, 0, &b, 1), DEV_ERR_TIMEOUT);
    teardown();
}

static void roundtrip_like_selftest(void)
{
    setup(); uint8_t wr[16], rd[16];
    for (int i = 0; i < 16; i++) wr[i] = (uint8_t)(0xA0 + i);
    CHECK_EQ(w25q_sector_erase(&d, 0x7FF000), DEV_OK);
    CHECK_EQ(w25q_write(&d, 0x7FF000, wr, 16), DEV_OK);
    CHECK_EQ(w25q_read(&d, 0x7FF000, rd, 16), DEV_OK);
    CHECK(memcmp(wr, rd, 16) == 0);
    CHECK_EQ(f.cs_depth_errors, 0);
    teardown();
}

static void wake_and_status(void)
{
    setup(); uint8_t sr = 0xFF;
    CHECK_EQ(w25q_wake(&d), DEV_OK); CHECK_EQ(f.opcodes[0], 0xAB);
    CHECK_EQ(w25q_read_status(&d, &sr), DEV_OK); CHECK_EQ(sr & 1, 0);
    f.bus_fail = 1;
    CHECK_EQ(w25q_wake(&d), DEV_ERR_BUS);
    CHECK_EQ(w25q_read(&d, 0, &sr, 1), DEV_ERR_BUS);
    teardown();
}

void test_w25q(void)
{
    RUN(jedec_ok); RUN(jedec_bad_and_bus_errors); RUN(wait_ready_timeout);
    RUN(erase_sequence_write_enable_first); RUN(write_enable_before_every_page);
    RUN(write_fails_when_wel_not_set); RUN(write_timeout_when_stuck_busy);
    RUN(roundtrip_like_selftest); RUN(wake_and_status);
}
