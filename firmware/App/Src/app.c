#include <stdio.h>
#include "app.h"
#include "cli.h"
#include "selftest.h"
#include "platform.h"
#include "quadrant.h"

static uint32_t last_tick, seconds;
static int btn_prev;

void app_init(void)
{
    selftest_run_all();
    printf("[cli] type 'h' for help\r\n");
}

void app_step(void)
{
    platform_led_toggle();
    platform_delay_ms(100);

    int btn = platform_button_pressed();
    if (btn && !btn_prev) printf("[btn ] B1 pressed\r\n");
    btn_prev = btn;

    int c = platform_uart_getc();
    if (c >= 0) cli_handle((char)c);

    if (platform_millis() - last_tick >= 1000) {
        last_tick = platform_millis();
        seconds++;
        uint16_t q[4];
        platform_adc_read_quadrant(q);
        quadrant_error_t e = quadrant_error(q);
        printf("tick %lu (uptime %lu s, TL=%u TR=%u BL=%u BR=%u, ex=%ld ey=%ld)\r\n",
               (unsigned long)platform_millis(), (unsigned long)seconds,
               q[0], q[1], q[2], q[3], (long)e.x_permille, (long)e.y_permille);
    }
}
