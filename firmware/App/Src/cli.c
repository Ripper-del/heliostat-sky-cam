#include <stdio.h>
#include "cli.h"
#include "selftest.h"
#include "platform.h"

void cli_handle(char c)
{
    switch (c) {
    case 'h': printf("[cli] s=selftest i=i2c-scan m=mpu c=camera f=flash-id w=flash-rw p=pwm a=adc x=xclk-toggle k=i2c-stress e=i2c-nack(0x69) 1=servo-az 2=servo-el\r\n"); break;
    case 's': selftest_run_all(); break;
    case 'i': selftest_i2c_scan(); break;
    case 'm': selftest_mpu(); break;
    case 'c': selftest_cam(); break;
    case 'f': selftest_flash_id(); break;
    case 'w': selftest_flash_rw(); break;
    case 'p': selftest_pwm(); break;
    case 'a': selftest_adc(); break;
    case '1': selftest_pwm_az(); break;
    case '2': selftest_pwm_el(); break;
    case 'e': selftest_i2c_nack(); break;
    case 'k': selftest_i2c_stress(); break;
    case 'x': platform_xclk(!platform_xclk_is_on()); printf("[cli] XCLK %s\r\n", platform_xclk_is_on() ? "ON" : "OFF"); break;
    case '\r': case '\n': break;
    default: printf("[cli] unknown '%c' (h = help)\r\n", c); break;
    }
}
