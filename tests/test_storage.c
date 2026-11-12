#include <string.h>
#include "test.h"
#include "fake_w25q.h"
#include "evlog.h"
#include "framestore.h"
#include "calib.h"

static fake_w25q_t ff; static w25q_t fl; static evlog_t log_;
static void setup(void) { fake_w25q_init(&ff); w25q_init_dev(&fl, &ff.bus); }
static void teardown(void) { fake_w25q_free(&ff); }

/* ---- журнал ---- */
static void log_empty_flash_opens_clean(void)
{
    setup();
    CHECK_EQ(evlog_open(&log_, &fl), DEV_OK);
    CHECK_EQ(log_.valid, 0); CHECK_EQ(evlog_newest_idx(&log_), -1);
    ev_rec_t r; CHECK_EQ(evlog_read(&log_, 0, &r), DEV_ERR_ID);
    CHECK_EQ(evlog_read(&log_, EVLOG_CAPACITY, &r), DEV_ERR_ARG);
    teardown();
}

static void log_append_and_survive_reopen(void)
{
    setup(); evlog_open(&log_, &fl);
    for (int i = 0; i < 30; i++) CHECK_EQ(evlog_append(&log_, EV_MODE, EVLOG_NO_FRAME, 1000 + i, i, -i, 7), DEV_OK);
    evlog_t again; CHECK_EQ(evlog_open(&again, &fl), DEV_OK);    /* "після вимкнення живлення" */
    CHECK_EQ(again.valid, 30); CHECK_EQ(again.next_idx, 30); CHECK_EQ(again.next_seq, 31);
    ev_rec_t r; CHECK_EQ(evlog_read(&again, 29, &r), DEV_OK);
    CHECK_EQ(r.type, EV_MODE); CHECK_EQ(r.t_ms, 1029); CHECK_EQ(r.a, 29); CHECK_EQ(r.b, -29); CHECK_EQ(r.c, 7); CHECK_EQ(r.seq, 30);
    CHECK_EQ(evlog_newest_idx(&again), 29);
    teardown();
}

static void log_wraps_and_overwrites_oldest_sector(void)
{
    setup(); evlog_open(&log_, &fl);
    for (uint32_t i = 0; i < EVLOG_CAPACITY + 10; i++) evlog_append(&log_, EV_USER, 3, i, 0, 0, 0);
    CHECK_EQ(log_.next_idx, 10);
    ev_rec_t r;
    CHECK_EQ(evlog_read(&log_, 0, &r), DEV_OK); CHECK_EQ(r.t_ms, EVLOG_CAPACITY);       /* перший слот перезаписано новим */
    CHECK_EQ(evlog_read(&log_, 9, &r), DEV_OK);
    CHECK_EQ(evlog_read(&log_, 10, &r), DEV_ERR_ID);        /* сектор стирався при wrap: слот 10 ще порожній... */
    teardown();
}

static void log_skips_torn_record_after_power_loss(void)
{
    setup(); evlog_open(&log_, &fl);
    evlog_append(&log_, EV_BOOT, 0xFF, 1, 0, 0, 0);
    uint8_t torn[EVLOG_REC_SIZE] = { 0xA5, 0xFF, 0xFF, 0xFF, 0x12, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    w25q_write(&fl, EVLOG_BASE_ADDR + 1 * EVLOG_REC_SIZE, torn, sizeof torn);          /* слот 1: недописаний */
    evlog_t again; evlog_open(&again, &fl);
    CHECK_EQ(again.valid, 1); CHECK_EQ(again.next_idx, 1);
    CHECK_EQ(evlog_append(&again, EV_BOOT, 0xFF, 2, 0, 0, 0), DEV_OK);
    CHECK_EQ(again.next_idx, 3);                            /* слот 1 пропущено, запис у слот 2 */
    ev_rec_t r; CHECK_EQ(evlog_read(&again, 2, &r), DEV_OK); CHECK_EQ(r.t_ms, 2);
    teardown();
}

static void log_reports_bus_errors(void)
{
    setup(); evlog_open(&log_, &fl);
    ff.bus_fail = 1;
    CHECK(evlog_append(&log_, EV_USER, 0, 0, 0, 0, 0) != DEV_OK);
    ev_rec_t r; CHECK(evlog_read(&log_, 0, &r) != DEV_OK);
    evlog_t o; CHECK(evlog_open(&o, &fl) != DEV_OK);
    ff.bus_fail = 0;
    evlog_append(&log_, EV_USER, 0, 0, 0, 0, 0);
    ff.bus_fail = 1;
    CHECK(evlog_append(&log_, EV_USER, 0, 0, 0, 0, 0) != DEV_OK);     /* слот не на межі сектора: читання бланка падає */
    teardown();
}

static void log_seq_wrap_picks_newest(void)
{
    setup(); evlog_open(&log_, &fl);
    log_.next_seq = 65534;
    for (int i = 0; i < 4; i++) evlog_append(&log_, EV_USER, 0, i, 0, 0, 0);      /* seq 65534, 65535, 0, 1 */
    evlog_t again; evlog_open(&again, &fl);
    CHECK_EQ(evlog_newest_idx(&again), 3);
    CHECK_EQ(again.next_seq, 2);
    teardown();
}

/* ---- кадри (кільце 20) ---- */
static void frames_ring_keeps_last_20_of_25(void)
{
    setup();
    static uint8_t px[1000], back[1000];
    for (uint32_t n = 1; n <= 25; n++) {
        memset(px, (int)n, sizeof px);
        CHECK_EQ(framestore_save(&fl, n, px, sizeof px), DEV_OK);
    }
    for (uint32_t n = 1; n <= 5; n++) CHECK_EQ(framestore_load(&fl, n, back, sizeof back), DEV_ERR_ID);    /* перезаписані */
    for (uint32_t n = 6; n <= 25; n++) {
        CHECK_EQ(framestore_load(&fl, n, back, sizeof back), DEV_OK);
        CHECK_EQ(back[0], n); CHECK_EQ(back[999], n);
    }
    uint32_t next = 0; CHECK_EQ(framestore_scan(&fl, &next), DEV_OK); CHECK_EQ(next, 26);
    teardown();
}

static void frames_detect_corruption_and_bad_args(void)
{
    setup();
    static uint8_t px[100], back[100], big[100];
    memset(px, 0x5A, sizeof px);
    framestore_save(&fl, 1, px, sizeof px);
    uint8_t flip = 0x00;
    w25q_write(&fl, FRAMESTORE_BASE + 1 * FRAMESTORE_SLOT_SIZE + FRAMESTORE_HDR + 10, &flip, 1);   /* псуємо піксель: 0x5A & 0x00 */
    CHECK_EQ(framestore_load(&fl, 1, back, sizeof back), DEV_ERR_ID);        /* CRC не збігся */
    CHECK_EQ(framestore_load(&fl, 1, back, 50), DEV_ERR_ID);                 /* інша довжина */
    CHECK_EQ(framestore_load(&fl, 21, back, sizeof back), DEV_ERR_ID);       /* слот 1 займає кадр 1, не 21 */
    CHECK_EQ(framestore_save(&fl, 2, big, 0), DEV_ERR_ARG);
    CHECK_EQ(framestore_save(&fl, 2, big, 65535), DEV_ERR_ARG);
    uint32_t next = 0; CHECK_EQ(framestore_scan(&fl, &next), DEV_OK); CHECK_EQ(next, 2);
    ff.bus_fail = 1;
    CHECK(framestore_save(&fl, 3, px, sizeof px) != DEV_OK);
    CHECK(framestore_load(&fl, 1, back, sizeof back) != DEV_OK);
    CHECK(framestore_scan(&fl, &next) != DEV_OK);
    teardown();
}

static void frames_header_is_written_last(void)
{
    setup();
    static uint8_t px[300], back[300];
    memset(px, 1, sizeof px);
    framestore_save(&fl, 4, px, sizeof px);
    memset(ff.mem + FRAMESTORE_BASE + 4 * FRAMESTORE_SLOT_SIZE, 0xFF, FRAMESTORE_HDR);   /* "живлення зникло до запису заголовка" */
    CHECK_EQ(framestore_load(&fl, 4, back, sizeof back), DEV_ERR_ID);
    uint32_t next = 0; framestore_scan(&fl, &next); CHECK_EQ(next, 1);
    teardown();
}

/* ---- калібровка ---- */
static void calib_roundtrip_and_defaults(void)
{
    setup();
    tracker_cfg_t c, out; tracker_default_cfg(&c); tracker_default_cfg(&out);
    CHECK_EQ(calib_load(&fl, &out), DEV_ERR_ID);                              /* порожньо */
    c.az_min_us = 700; c.az_max_us = 2300; c.el_min_us = 800; c.el_max_us = 2100; c.dark_light_thr = 777; c.sign_az = -1; c.invert_light = 1; c.ex_off = -57; c.ey_off = 83;
    CHECK_EQ(calib_save(&fl, &c), DEV_OK);
    CHECK_EQ(calib_load(&fl, &out), DEV_OK);
    CHECK_EQ(out.az_min_us, 700); CHECK_EQ(out.az_max_us, 2300);
    CHECK_EQ(out.el_min_us, 800); CHECK_EQ(out.el_max_us, 2100);                 /* різні межі для двох серво (FR-09) */
    CHECK_EQ(out.ex_off, -57); CHECK_EQ(out.ey_off, 83);
    CHECK_EQ(out.dark_light_thr, 777); CHECK_EQ(out.sign_az, -1); CHECK_EQ(out.invert_light, 1);
    ff.mem[10] ^= 0x01;                                                         /* псуємо байт: CRC має це помітити */
    ff.mem[10] &= 0xFF;
    uint8_t zero = 0; w25q_write(&fl, 10, &zero, 1);
    CHECK_EQ(calib_load(&fl, &out), DEV_ERR_ID);
    ff.bus_fail = 1;
    CHECK(calib_load(&fl, &out) != DEV_OK);
    CHECK(calib_save(&fl, &c) != DEV_OK);
    teardown();
}

static void calib_range_check(void)
{
    CHECK_EQ(calib_check_range(500, 2500), 0);
    CHECK_EQ(calib_check_range(2500, 500), -1);
    CHECK_EQ(calib_check_range(300, 2000), -1);
    CHECK_EQ(calib_check_range(500, 2700), -1);
    CHECK_EQ(calib_check_range(1000, 1000), -1);
}

void test_storage(void)
{
    RUN(log_empty_flash_opens_clean); RUN(log_append_and_survive_reopen); RUN(log_wraps_and_overwrites_oldest_sector);
    RUN(log_skips_torn_record_after_power_loss); RUN(log_reports_bus_errors); RUN(log_seq_wrap_picks_newest);
    RUN(frames_ring_keeps_last_20_of_25); RUN(frames_detect_corruption_and_bad_args); RUN(frames_header_is_written_last);
    RUN(calib_roundtrip_and_defaults); RUN(calib_range_check);
}
