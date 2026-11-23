#include <stdio.h>
#include <string.h>
#include "cli.h"
#include "product.h"
#include "selftest.h"
#include "platform.h"

void cli_handle(char c)
{
    switch (c) {
    case 'h': printf("[cli] s=selftest i=i2c-scan m=mpu c=camera b=camera-bus g=camera-frame f=flash-id w=flash-rw p=pwm a=adc x=xclk-toggle k=i2c-stress e=i2c-nack(0x69) r=mpu-stream-60s 1=servo-az 2=servo-el\r\n"); break;
    case 's': selftest_run_all(); break;
    case 'i': selftest_i2c_scan(); break;
    case 'm': selftest_mpu(); break;
    case 'c': selftest_cam(); break;
    case 'b': selftest_cam_bus(); break;
    case 'g': selftest_cam_frame(); break;
    case 'f': selftest_flash_id(); break;
    case 'w': selftest_flash_rw(); break;
    case 'p': selftest_pwm(); break;
    case 'a': selftest_adc(); break;
    case '1': selftest_pwm_az(); break;
    case '2': selftest_pwm_el(); break;
    case 'r': selftest_mpu_stream(); break;
    case 'e': selftest_i2c_nack(); break;
    case 'k': selftest_i2c_stress(); break;
    case 'x': platform_xclk(!platform_xclk_is_on()); printf("[cli] XCLK %s\r\n", platform_xclk_is_on() ? "ON" : "OFF"); break;
    case '\r': case '\n': break;
    default: printf("[cli] unknown '%c' (h = help)\r\n", c); break;
    }
}

/* ---- рядкові команди ---- */
#define LINE_MAX 48
#define ARG_MAX  6

static int tokenize(char *line, char **argv, int max)
{
    int n = 0;
    for (char *p = line; *p && n < max; ) {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        argv[n++] = p;
        while (*p && *p != ' ') { if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32); p++; }   /* команди нечутливі до регістру */
    }
    return n;
}

static void run_line(char *line)
{
    char *v[ARG_MAX];
    int n = tokenize(line, v, ARG_MAX);
    if (n == 0) return;
    product_cmd_started();
    if (!strcmp(v[0], "STATS")) { if (n > 1 && !strcmp(v[1], "RESET")) product_reset_stats(); else product_print_stats(); }
    else if (!strcmp(v[0], "MODE"))  product_print_mode();
    else if (!strcmp(v[0], "SKY")) {
        if (n > 2 && !strcmp(v[1], "FORCE")) {
            int f = !strcmp(v[2], "CLEAR") ? 2 : !strcmp(v[2], "OVERCAST") ? 1 : !strcmp(v[2], "DARK") ? 0 : -1;
            product_sky_force(f); printf("[cli] sky %s\r\n", f < 0 ? "AUTO" : v[2]);
        } else product_print_sky();
    }
    else if (!strcmp(v[0], "MEM"))   product_print_mem();
    else if (!strcmp(v[0], "SNAP"))  { product_snapshot_request(); printf("[cli] snapshot requested\r\n"); }
    else if (!strcmp(v[0], "FRAMES")) {
        if (n > 1) { uint32_t no = 0; for (char *s = v[1]; *s >= '0' && *s <= '9'; s++) no = no * 10 + (uint32_t)(*s - '0'); if (product_frame_show(no)) printf("[frames] no such frame\r\n"); }
        else product_frames_info();
    }
    else if (!strcmp(v[0], "LOG")) {
        if (n > 1 && !strcmp(v[1], "ERASE")) printf("[log] erase %s\r\n", product_log_erase() == 0 ? "ok" : "FAILED");
        else { int cnt = 10; if (n > 1) { cnt = 0; for (char *s = v[1]; *s >= '0' && *s <= '9'; s++) cnt = cnt * 10 + (*s - '0'); } product_log_dump(cnt); }
    }
    else if (!strcmp(v[0], "SERVO")) { if (n > 1 && !strcmp(v[1], "ON")) product_servo_power(1); else product_servo_power(0); printf("[cli] servo %s\r\n", n > 1 && !strcmp(v[1], "ON") ? "ON" : "OFF"); }
    else if (!strcmp(v[0], "CAL")) {
        if (n > 1 && !strcmp(v[1], "SHOW")) product_cal_show();
        else if (n > 1) { int rc = product_cal_cmd(n - 1, v + 1); printf("[cal] %s\r\n", rc == 0 ? "ok" : "ERROR (see HELP)"); }
        else printf("[cal] ERROR (see HELP)\r\n");
    }
    else if (!strcmp(v[0], "CAM")) {
        uint32_t aec = 0, gain = 0;
        for (char *s = n > 2 ? v[2] : ""; *s >= '0' && *s <= '9'; s++) aec = aec * 10 + (uint32_t)(*s - '0');
        for (char *s = n > 3 ? v[3] : ""; *s >= '0' && *s <= '9'; s++) gain = gain * 10 + (uint32_t)(*s - '0');
        printf("[cam] %s\r\n", (n > 3 && !strcmp(v[1], "EXP") && product_cam_exposure(aec, gain) == 0) ? "exposure set" : "ERROR (CAM EXP aec gain)");
    }
    else if (!strcmp(v[0], "VER"))   product_print_version();
    else if (!strcmp(v[0], "FAULT")) { int on = (n > 2 && v[2][0] == '1'); if (n > 1 && !strcmp(v[1], "SDA")) { platform_fault_sda(on); printf("[cli] fault SDA %s\r\n", on ? "ON (замкнено на землю)" : "OFF"); } else printf("[cli] FAULT SDA 1|0\r\n"); }
    else if (!strcmp(v[0], "BTNSIM")) { uint32_t ms = 0; for (char *s = n > 1 ? v[1] : ""; *s >= '0' && *s <= '9'; s++) ms = ms * 10 + (uint32_t)(*s - '0'); product_btnsim(ms); printf("[cli] btnsim %lu ms\r\n", (unsigned long)ms); }
    else if (!strcmp(v[0], "PROBE")) { printf("[cli] probe test 4 s\r\n"); product_probe_test(); printf("[cli] probe test done\r\n"); }
    else if (!strcmp(v[0], "PERIOD")) {
        uint32_t ms = 0; for (char *s = n > 2 ? v[2] : ""; *s >= '0' && *s <= '9'; s++) ms = ms * 10 + (uint32_t)(*s - '0');
        /* імена задач у таблиці малими літерами */
        char name[16]; unsigned k = 0; for (const char *s = n > 1 ? v[1] : ""; *s && k < sizeof name - 1; s++) name[k++] = (char)((*s >= 'A' && *s <= 'Z') ? *s + 32 : *s);
        name[k] = 0;
        printf("[cli] period %s\r\n", (n > 2 && product_set_period(name, ms) == 0) ? "set" : "ERROR (PERIOD task ms)");
    }
    else if (!strcmp(v[0], "HELP")) {
        printf("[cli] STATS [RESET] | MODE | SKY [FORCE CLEAR|OVERCAST|DARK|AUTO] | MEM | SNAP | FRAMES [n] | LOG [n|ERASE] | SERVO ON|OFF\r\n");
        printf("[cli] CAL SHOW | CAL AZ lo hi | CAL EL lo hi | CAL DARK thr | CAL INV 0|1 | CAL SIGN AZ|EL +1|-1 | CAL ZERO | CAL DEADBAND n | CAL SAVE|LOAD|DEFAULT | CAM EXP aec gain | VER | PROBE | PERIOD task ms\r\n");
    }
    else printf("[cli] unknown command (HELP)\r\n");
}

static char line_buf[LINE_MAX]; static int line_len;
void cli_reset(void) { line_len = 0; }

void cli_feed(char c)
{
    char *line = line_buf; int len = line_len;
    if (c == '\r' || c == '\n') { if (len) { line[len] = 0; line_len = 0; run_line(line); } return; }
    if (len == 0 && !(c >= 'A' && c <= 'Z')) { cli_handle(c); return; }       /* однознакова команда */
    if (len < LINE_MAX - 1) { line[len++] = c; line_len = len; }
}
