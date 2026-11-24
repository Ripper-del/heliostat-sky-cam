/* ЛР5: AO log (задача log у ЛР4) — ЄДИНИЙ власник SPI-flash W25Q64 (у ЛР4 — м'ютекс flash_mx).
 * Журнал, кадри і калібровку інші AO не пишуть самі: вони надсилають log подію-запит і отримують подію-відповідь.
 *
 * Ієрархічна машина: serving { idle, working { busy, reading } } і fault.
 *   serving — суперстан: LOG_REQ у будь-якому підстані лише копіюється у власну чергу записів (ring);
 *   idle    — flash вільна: новий запит одразу стає завданням (fjob) -> busy; короткі читання (LOG n, CAL LOAD) — тут;
 *   working — суперстан «flash зайнята»: великі запити (кадр, калібровка, стирання журналу, FRAMES n) відкладаються
 *             (QActive_defer) і повертаються в idle; описано один раз для обох підстанів;
 *   busy    — flash виконує стирання або запис: кожну 1 мс POLL перевіряє BUSY і запускає наступну коротку операцію.
 *             Записи журналу вклинюються між кроками кадру: очікування запису журналу — одна операція flash, а не весь кадр;
 *   reading — FRAMES n: кадр читається шматками по 1 КБ, по шматку на POLL (19 КБ одним кроком — 0,4 с при SPI 390 кГц);
 *   fault   — flash не відповіла на старті: на всі запити одразу відповідь з помилкою, щоб ніхто не чекав вічно.
 * Довгого очікування в обробниках немає: стирання (десятки мс) іде у flash, поки процесор робить інше. */
#include <string.h>
#include "ao.h"
#include "meas.h"
#include "platform.h"
#include "w25q.h"
#include "evlog.h"
#include "framestore.h"
#include "calib.h"
#include "fjob.h"

#define RING_N   16U
#define RD_CHUNK 1024U
typedef struct { uint8_t type, frame; int16_t a, b, c; uint32_t t_ms, post_cyc; } rec_t;
enum { BIG_NONE, BIG_FRAME, BIG_CAL, BIG_ERASE };

typedef struct {
    QActive  super;
    QTimeEvt poll;
    w25q_t   flash;
    evlog_t  elog;
    uint32_t frame_next;
    QEQueue  deferQ;
    QEvtPtr  deferSto[6];
    rec_t    ring[RING_N]; uint8_t rh, rn;            /* записи журналу, що чекають на flash */
    fjob_t   ljob; uint8_t lbuf[EVLOG_REC_SIZE]; uint32_t lidx, lpost; uint8_t lact;
    fjob_t   bjob; uint8_t bkind; QActive *breply;
    uint8_t  hdr[FRAMESTORE_HDR], calbuf[CALIB_LEN];
    FrameEvt frame;                                    /* копія параметрів кадру, що пишеться */
    tracker_cfg_t cal;
    uint8_t *rd_buf; uint32_t rd_no, rd_addr, rd_off; uint16_t rd_crc;
    uint8_t  errs;                                     /* помилок flash поспіль; 3 — перехід у fault */
} Log;
static Log l_log;
QActive * const AO_Log = &l_log.super;

static QState Log_initial(Log *me, void const *par);
static QState Log_serving(Log *me, QEvt const *e);
static QState Log_idle(Log *me, QEvt const *e);
static QState Log_working(Log *me, QEvt const *e);
static QState Log_busy(Log *me, QEvt const *e);
static QState Log_reading(Log *me, QEvt const *e);
static QState Log_fault(Log *me, QEvt const *e);

void Log_ctor(void)
{
    QActive_ctor(&l_log.super, Q_STATE_CAST(&Log_initial));
    QTimeEvt_ctorX(&l_log.poll, &l_log.super, POLL_SIG, 0U);
    QEQueue_init(&l_log.deferQ, l_log.deferSto, Q_DIM(l_log.deferSto));
}

/* Старт (як product_init у ЛР4): прочитати калібровку, відкрити журнал, знайти наступний номер кадру.
 * Викликається з bsp.c ДО QF_init: драйвер ще може робити паузу через затримку HAL (після першої критичної секції ядра
 * і до старту планувальника переривання вимкнені, і така пауза зависла б). Результат іде іншим AO через BootInfo. */
void Log_boot(BootInfo *b)
{
    Log *me = &l_log;
    uint8_t id[3];
    w25q_init_dev(&me->flash, platform_spi2_flash());
    w25q_wake(&me->flash);
    b->flash_ok = w25q_read_jedec(&me->flash, id) == DEV_OK;
    tracker_default_cfg(&b->cfg);
    me->frame_next = 1;
    if (b->flash_ok) {
        b->cal_defaults = calib_load(&me->flash, &b->cfg) != DEV_OK;
        uint32_t c0 = platform_cycles();
        if (evlog_open(&me->elog, &me->flash) != DEV_OK) b->flash_ok = 0;
        framestore_scan(&me->flash, &me->frame_next);
        b->open_ms = (platform_cycles() - c0) / (platform_cpu_hz() / 1000U);
        b->log_records = me->elog.valid;
    }
    b->frame_next = me->frame_next;
}

static QState Log_initial(Log *me, void const *par)
{
    Q_UNUSED_PAR(me);
    return ((BootInfo const *)par)->flash_ok ? Q_TRAN(&Log_idle) : Q_TRAN(&Log_fault);
}

static void reply_frame(Log *me, uint8_t sig, QActive *to, int16_t rc, uint32_t no)
{
    Q_UNUSED_PAR(me);                                  /* відправник потрібен лише трасуванню QS */
    FrameEvt *f = Q_NEW_X(FrameEvt, 1U, sig);
    if (!f) { g_ev_lost++; return; }
    f->rc = rc; f->no = no; f->buf = 0;
    if (!QACTIVE_POST_X(to, &f->super, 0U, &me->super)) g_ev_lost++;
}

static void reply_cal(Log *me, int16_t rc, uint8_t load, tracker_cfg_t const *cfg)
{
    Q_UNUSED_PAR(me);
    CalEvt *c = Q_NEW_X(CalEvt, 1U, CAL_DONE_SIG);
    if (!c) { g_ev_lost++; return; }
    c->rc = rc; c->load = load; c->cfg = *cfg;
    if (!QACTIVE_POST_X(AO_Control, &c->super, 0U, &me->super)) g_ev_lost++;
}

static void push(Log *me, uint8_t type, uint8_t frame, int16_t a, int16_t b, int16_t c, uint32_t t_ms, uint32_t post_cyc)
{
    if (me->rn >= RING_N) { g_pend_lost++; return; }
    rec_t *r = &me->ring[(me->rh + me->rn) % RING_N]; me->rn++;
    r->type = type; r->frame = frame; r->a = a; r->b = b; r->c = c; r->t_ms = t_ms; r->post_cyc = post_cyc;
}

static void flash_error(int rc) { g_flash_err++; g_flash_last_rc = rc; l_log.errs++; }

/* крок flash з урахуванням сценарію 3.8 (імітований збій шини: кожна операція — помилка) */
static int fstep(fjob_t *j, w25q_t *f) { return g_test.flash_fail ? DEV_ERR_BUS : fjob_step(j, f); }

static void big_done(Log *me, int rc)
{
    switch (me->bkind) {
    case BIG_FRAME: {
        uint32_t no = me->frame_next;
        if (rc == 0) {
            me->frame_next++;
            /* як у ЛР4: після кадру — запис журналу з його номером (у ЛР4 — під тим самим м'ютексом) */
            push(me, EV_SIGNAL_LOSS, (uint8_t)(no & 0xFF), (int16_t)(me->frame.light / 4), (int16_t)me->frame.mode, me->frame.mean,
                 me->frame.t_ms, platform_cycles());
        } else flash_error(rc);
        reply_frame(me, SAVE_DONE_SIG, me->breply, (int16_t)rc, no);
        break;
    }
    case BIG_CAL:
        if (rc != 0) flash_error(rc);
        reply_cal(me, (int16_t)rc, 0, &me->cal);
        break;
    case BIG_ERASE:
        if (rc == 0) rc = evlog_open(&me->elog, &me->flash);    /* 16 КБ читання: службова команда LOG ERASE */
        ao_say(&me->super, "[log] erase %s", rc == 0 ? "ok" : "FAILED");
        break;
    default: break;
    }
    me->bkind = BIG_NONE;
}

/* Запустити наступну коротку операцію flash. 1 — операцію запущено (чекати BUSY), 0 — роботи немає.
 * Порядок: спершу записи журналу (короткі), потім наступний крок великого завдання. Викликається, лише коли flash вільна. */
static int kick(Log *me)
{
    for (;;) {
        if (me->lact) {
            int rc = fstep(&me->ljob, &me->flash);
            if (rc == 1) return 1;
            me->lact = 0;
            if (rc == 0) { evlog_commit(&me->elog, me->lidx); meas_add(M_FLASH_WAIT, platform_cycles() - me->lpost); me->errs = 0; }
            else flash_error(rc);
            continue;
        }
        if (me->rn) {
            rec_t *r = &me->ring[me->rh]; me->rh = (uint8_t)((me->rh + 1U) % RING_N); me->rn--;
            int rc = evlog_plan_append(&me->elog, r->type, r->frame, r->t_ms, r->a, r->b, r->c, &me->ljob, me->lbuf, &me->lidx);
            if (rc != DEV_OK) { flash_error(rc); continue; }
            me->lpost = r->post_cyc; me->lact = 1;
            continue;
        }
        if (me->bkind != BIG_NONE) {
            int rc = fstep(&me->bjob, &me->flash);
            if (rc == 1) return 1;
            if (rc == 0) me->errs = 0;
            big_done(me, rc);
            continue;
        }
        return 0;
    }
}

static void log_dump(Log *me, int n)
{
    int32_t idx = evlog_newest_idx(&me->elog);
    ao_say(&me->super, "[log] %lu records in flash (capacity %u)", (unsigned long)me->elog.valid, (unsigned)EVLOG_CAPACITY);
    for (int i = 0; i < n && i < 16 && idx >= 0; i++, idx = (int32_t)((idx + EVLOG_CAPACITY - 1) % EVLOG_CAPACITY)) {
        ev_rec_t r;
        if (evlog_read(&me->elog, (uint32_t)idx, &r) != DEV_OK) break;
        ao_say(&me->super, "[log] #%u t=%lu ms type=%u frame=%u a=%d b=%d c=%d", r.seq, (unsigned long)r.t_ms, r.type, r.frame, r.a, r.b, r.c);
    }
}

/* суперстан: запис журналу приймається в будь-якому підстані — лише копія в ring, без роботи з flash */
static QState Log_serving(Log *me, QEvt const *e)
{
    switch (e->sig) {
    case LOG_REQ_SIG: {
        LogEvt const *l = Q_EVT_CAST(LogEvt);
        push(me, l->type, l->frame, l->a, l->b, l->c, l->t_ms, l->post_cyc);
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState Log_idle(Log *me, QEvt const *e)
{
    uint32_t t0 = platform_cycles();
    switch (e->sig) {
    case Q_ENTRY_SIG: {
        if (QActive_recall(&me->super, &me->deferQ)) return Q_HANDLED();   /* відкладений великий запит — наступним */
        if (kick(me)) {                                    /* записи журналу, що прийшли під час reading: запис уже запущено */
            static QEvt const pollEvt = QEVT_INITIALIZER(POLL_SIG);
            QACTIVE_POST(&me->super, &pollEvt, &me->super);  /* з entry перейти не можна: перехід у busy — наступним кроком */
        }
        return Q_HANDLED();
    }
    case POLL_SIG:
        return Q_TRAN(&Log_busy);
    case LOG_REQ_SIG: {
        LogEvt const *l = Q_EVT_CAST(LogEvt);              /* та сама копія, що в суперстані, плюс одразу почати запис */
        push(me, l->type, l->frame, l->a, l->b, l->c, l->t_ms, l->post_cyc);
#ifdef LR5_FLASH_MUTEX
        (void)l; Log_direct_flush(); meas_step(AO_LOG, t0); return Q_HANDLED();
#else
        int r = kick(me); meas_step(AO_LOG, t0);
        if (me->errs >= 3U) return Q_TRAN(&Log_fault);
        return r ? Q_TRAN(&Log_busy) : Q_HANDLED();
#endif
    }
    case SAVE_FRAME_SIG: {
        me->frame = *Q_EVT_CAST(FrameEvt); me->breply = AO_Snapshot; me->bkind = BIG_FRAME;
        int rc = framestore_plan_save(&me->bjob, me->frame_next, me->frame.buf, FRAME_PIXELS, me->hdr);
        if (rc != DEV_OK) { big_done(me, rc); meas_step(AO_LOG, t0); return Q_HANDLED(); }
        if (g_test.one_step_save) {                        /* сценарій 3: увесь кадр одним кроком (з очікуванням flash у драйвері) */
            int rr = fjob_run_blocking(&me->bjob, &me->flash);
            big_done(me, rr); (void)kick(me);              /* kick допише запис журналу про кадр */
            while (me->lact || me->rn) { int b = 1; while (b) { if (w25q_busy(&me->flash, &b) != DEV_OK) break; } (void)kick(me); }
            meas_step(AO_LOG, t0);
            return Q_HANDLED();
        }
        int r = kick(me); meas_step(AO_LOG, t0);
        if (me->errs >= 3U) return Q_TRAN(&Log_fault);
        return r ? Q_TRAN(&Log_busy) : Q_HANDLED();
    }
    case CAL_SAVE_SIG: {
        me->cal = Q_EVT_CAST(CalEvt)->cfg; me->bkind = BIG_CAL;
        calib_plan_save(&me->bjob, &me->cal, me->calbuf);
        int r = kick(me); meas_step(AO_LOG, t0);
        return r ? Q_TRAN(&Log_busy) : Q_HANDLED();
    }
    case CAL_LOAD_SIG: {
        tracker_cfg_t c = Q_EVT_CAST(CalEvt)->cfg;
        int rc = calib_load(&me->flash, &c);
        reply_cal(me, (int16_t)rc, 1, &c);
        meas_step(AO_LOG, t0);
        return Q_HANDLED();
    }
    case FRAME_LOAD_SIG: {                                 /* FRAMES n: заголовок тут, дані — шматками в reading */
        FrameEvt const *f = Q_EVT_CAST(FrameEvt);
        me->rd_buf = f->buf; me->rd_no = f->no; me->rd_off = 0;
        int rc = framestore_load_begin(&me->flash, f->no, FRAME_PIXELS, &me->rd_addr, &me->rd_crc);
        meas_step(AO_LOG, t0);
        if (rc != DEV_OK) { reply_frame(me, FRAME_LOADED_SIG, AO_Camera, (int16_t)rc, f->no); return Q_HANDLED(); }
        return Q_TRAN(&Log_reading);
    }
    case CMD_SIG: {
        CmdEvt const *c = Q_EVT_CAST(CmdEvt);
        if (c->code == CMD_LOG_DUMP) log_dump(me, (int)c->a);
        else if (c->code == CMD_FRAMES_INFO)
            ao_say(&me->super, "[frames] next=%lu, ring of %u slots, frame %u x %u px, %u bytes", (unsigned long)me->frame_next, (unsigned)FRAMESTORE_SLOTS,
                   (unsigned)FRAME_W, (unsigned)FRAME_H, (unsigned)FRAME_PIXELS);
        else if (c->code == CMD_LOG_ERASE) {
            fjob_init(&me->bjob); me->bkind = BIG_ERASE;
            for (uint32_t i = 0; i < EVLOG_SECTORS; i++) fjob_erase(&me->bjob, EVLOG_BASE_ADDR + i * W25Q_SECTOR_SIZE);
            int r = kick(me); meas_step(AO_LOG, t0);
            return r ? Q_TRAN(&Log_busy) : Q_HANDLED();
        }
        meas_step(AO_LOG, t0);
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&Log_serving);
    }
}

/* суперстан «flash зайнята»: великі запити чекають у черзі відкладених подій, поки log не повернеться в idle */
static QState Log_working(Log *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG:
        QTimeEvt_armX(&me->poll, 1U, 0U);
        return Q_HANDLED();
    case Q_EXIT_SIG:
        (void)QTimeEvt_disarm(&me->poll);
        return Q_HANDLED();
    case SAVE_FRAME_SIG: case CAL_SAVE_SIG: case CAL_LOAD_SIG: case FRAME_LOAD_SIG: case CMD_SIG:
        if (!QActive_defer(&me->super, &me->deferQ, e)) {
            g_ev_lost++;                                   /* черга відкладених повна: відповісти помилкою, щоб ніхто не чекав */
            if (e->sig == SAVE_FRAME_SIG) reply_frame(me, SAVE_DONE_SIG, AO_Snapshot, -1, 0);
            else if (e->sig == FRAME_LOAD_SIG) reply_frame(me, FRAME_LOADED_SIG, AO_Camera, -1, Q_EVT_CAST(FrameEvt)->no);
            else if (e->sig != CMD_SIG) reply_cal(me, -1, e->sig == CAL_LOAD_SIG, &Q_EVT_CAST(CalEvt)->cfg);
        }
        meas_defer_min((uint16_t)(Q_DIM(me->deferSto) - me->deferQ.nMin), (uint16_t)Q_DIM(me->deferSto));
        return Q_HANDLED();
    default:
        return Q_SUPER(&Log_serving);
    }
}

static QState Log_busy(Log *me, QEvt const *e)
{
    switch (e->sig) {
    case POLL_SIG: {
        uint32_t t0 = platform_cycles();
        int busy = 0;
        if (w25q_busy(&me->flash, &busy) != DEV_OK) { flash_error(DEV_ERR_BUS); busy = 0; }
        if (g_test.block_log) {                            /* сценарій 2: ЗАБОРОНЕНЕ очікування з блокуванням в обробнику */
            while (busy) { meas_block_ms(1); if (w25q_busy(&me->flash, &busy) != DEV_OK) { flash_error(DEV_ERR_BUS); busy = 0; } }
        }
        if (g_test.owner_wait_ms) meas_spin_ms(g_test.owner_wait_ms);   /* сценарій 2: власник тримає процесор N мс у кожному кроці */
        if (busy) { QTimeEvt_armX(&me->poll, 1U, 0U); meas_step(AO_LOG, t0); return Q_HANDLED(); }
        int r = kick(me);
        meas_step(AO_LOG, t0);
        if (me->errs >= 3U) return Q_TRAN(&Log_fault);
        if (r) { QTimeEvt_armX(&me->poll, 1U, 0U); return Q_HANDLED(); }
        return Q_TRAN(&Log_idle);
    }
    default:
        return Q_SUPER(&Log_working);
    }
}

static QState Log_reading(Log *me, QEvt const *e)
{
    switch (e->sig) {
    case POLL_SIG: {
        uint32_t t0 = platform_cycles();
        uint32_t n = FRAME_PIXELS - me->rd_off; if (n > RD_CHUNK) n = RD_CHUNK;
        int rc = w25q_read(&me->flash, me->rd_addr + me->rd_off, me->rd_buf + me->rd_off, n);
        if (rc == DEV_OK) me->rd_off += n;
        if (rc == DEV_OK && me->rd_off < FRAME_PIXELS) { QTimeEvt_armX(&me->poll, 1U, 0U); meas_step(AO_LOG, t0); return Q_HANDLED(); }
        if (rc == DEV_OK) rc = framestore_load_check(me->rd_buf, FRAME_PIXELS, me->rd_crc);
        reply_frame(me, FRAME_LOADED_SIG, AO_Camera, (int16_t)rc, me->rd_no);
        meas_step(AO_LOG, t0);
        return Q_TRAN(&Log_idle);
    }
    default:
        return Q_SUPER(&Log_working);
    }
}

/* fault: flash не відповідає (на старті або після трьох помилок поспіль). Ніхто не чекає: на кожен запит одразу помилка.
 * Раз на секунду пробуємо прочитати JEDEC ID; відповіла (і сценарій збою вимкнено) — назад у idle. */
static QState Log_fault(Log *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG:
        while (QActive_recall(&me->super, &me->deferQ)) { }   /* відкладені запити отримають відповідь з помилкою */
        me->lact = 0; me->rn = 0; me->bkind = BIG_NONE;
        QTimeEvt_armX(&me->poll, MS(1000), 0U);
        ao_say(&me->super, "[log] flash fault: requests answered with errors");
        return Q_HANDLED();
    case Q_EXIT_SIG:
        (void)QTimeEvt_disarm(&me->poll);
        me->errs = 0;
        ao_say(&me->super, "[log] flash recovered");
        return Q_HANDLED();
    case POLL_SIG: {
        uint8_t id[3];
        if (!g_test.flash_fail && w25q_read_jedec(&me->flash, id) == DEV_OK) return Q_TRAN(&Log_idle);
        QTimeEvt_armX(&me->poll, MS(1000), 0U);
        return Q_HANDLED();
    }
    case LOG_REQ_SIG: g_pend_lost++; return Q_HANDLED();  /* без flash журнал не пишеться (як у ЛР4), але видно в лічильнику */
    case SAVE_FRAME_SIG: reply_frame(me, SAVE_DONE_SIG, AO_Snapshot, -1, 0); return Q_HANDLED();
    case FRAME_LOAD_SIG: reply_frame(me, FRAME_LOADED_SIG, AO_Camera, -1, Q_EVT_CAST(FrameEvt)->no); return Q_HANDLED();
    case CAL_SAVE_SIG: case CAL_LOAD_SIG: reply_cal(me, -1, e->sig == CAL_LOAD_SIG, &Q_EVT_CAST(CalEvt)->cfg); return Q_HANDLED();
    case CMD_SIG: ao_say(&me->super, "[log] flash unavailable"); return Q_HANDLED();
    default: return Q_SUPER(&QHsm_top);
    }
}

#ifdef LR5_FLASH_MUTEX
/* ===== Сценарій 3.1 (навмисно неправильний варіант): клієнти пишуть flash САМІ під м'ютексом, як у ЛР4.
 * Записи журналу пише log, кадр — snapshot; обидва беруть м'ютекс і чекають flash блокуючим драйвером. ===== */
int Log_direct_save(FrameEvt const *f, uint32_t *no)
{
    Log *me = &l_log;
    meas_flash_lock();
    *no = me->frame_next;
    int rc = framestore_save(&me->flash, me->frame_next, f->buf, FRAME_PIXELS);
    if (rc == DEV_OK) {
        me->frame_next++;
        rc = evlog_append(&me->elog, EV_SIGNAL_LOSS, (uint8_t)(*no & 0xFF), f->t_ms, (int16_t)(f->light / 4), (int16_t)f->mode, f->mean);
    }
    meas_flash_unlock();
    if (rc != DEV_OK) flash_error(rc);
    return rc;
}

static void direct_append_ring(Log *me)
{
    while (me->rn) {
        rec_t *r = &me->ring[me->rh]; me->rh = (uint8_t)((me->rh + 1U) % RING_N); me->rn--;
        meas_flash_lock();
        int rc = evlog_append(&me->elog, r->type, r->frame, r->t_ms, r->a, r->b, r->c);
        meas_flash_unlock();
        if (rc == DEV_OK) meas_add(M_FLASH_WAIT, platform_cycles() - r->post_cyc); else flash_error(rc);
    }
}
void Log_direct_flush(void) { direct_append_ring(&l_log); }
#endif
