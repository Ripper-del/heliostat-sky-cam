/* ЛР5: налаштування QP/C (шаблон templates/qp_config.h з QP/C 8.1.6; змінено лише те, що в коментарях). */
#ifndef QP_CONFIG_H_
#define QP_CONFIG_H_
#define QP_API_VERSION      0
#define QF_MAX_ACTIVE       12U    /* 12 AO — стільки ж, скільки задач у ЛР4 (configMAX_PRIORITIES >= QF_MAX_ACTIVE) */
#define QF_MAX_EPOOL        3U     /* три пули подій: малі, середні, текстові рядки (bsp.c) */
#define QF_MAX_TICK_RATE    1U     /* одна частота часових подій — тік FreeRTOS */
#define QF_TIMEEVT_CTR_SIZE 4U
#define QF_EQUEUE_CTR_SIZE  1U
#define QF_MPOOL_CTR_SIZE   2U
#define QS_TIME_SIZE        4U
#define QS_CTR_SIZE         2U
#endif
