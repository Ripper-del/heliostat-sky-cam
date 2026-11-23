#include <stdio.h>
#include "app.h"
#include "cli.h"
#include "selftest.h"
#include "product.h"

void app_init(void)
{
    selftest_run_all();
    product_init();
    printf("[cli] type 'h' or HELP\r\n");
}

void app_step(void) { }          /* ЛР4: головного циклу немає, виконання — у задачах FreeRTOS (Core/Src/product_rtos.c) */
