#include "ov7670.h"

void ov7670_init_dev(ov7670_t *d, const sccb_bus_t *bus) { d->bus = bus; }

int ov7670_identify(ov7670_t *d, uint8_t *pid, uint8_t *ver)
{
    int rc = DEV_ERR_BUS;
    for (int attempt = 0; attempt < 3; attempt++) {
        uint8_t p = 0, v = 0;
        rc = d->bus->read_reg(d->bus->ctx, OV7670_REG_PID, &p);
        if (rc == DEV_OK) rc = d->bus->read_reg(d->bus->ctx, OV7670_REG_VER, &v);
        if (rc == DEV_OK) {
            *pid = p; *ver = v;
            if (p == OV7670_PID_VALUE) return DEV_OK;
            rc = DEV_ERR_ID;      /* 0xFF/0x00 = порожня шина; інше значення = збій кадру: пробуємо ще */
        }
        if (d->bus->delay_ms) d->bus->delay_ms(5);
    }
    return rc;
}

int ov7670_soft_reset(ov7670_t *d)
{
    int rc = d->bus->write_reg(d->bus->ctx, OV7670_REG_COM7, 0x80);
    if (d->bus->delay_ms) d->bus->delay_ms(10);
    return rc;
}

/* Регістри за OV7670 datasheet / Implementation Guide: пари {адреса, значення}. */
static const uint8_t qqvga_yuv_regs[][2] = {
    {0x12, 0x00},   /* COM7: YUV, без CIF/QVGA — розмір задає масштабування нижче */
    {0x11, 0x03},   /* CLKRC: внутрішній дільник /4 (XCLK 16 МГц -> 4 МГц) */
    {0x3A, 0x04},   /* TSLB: порядок Y U Y V */
    {0x40, 0xC0},   /* COM15: повний діапазон 00..FF */
    {0x15, 0x20},   /* COM10: PCLK не тактується, поки HREF = 0 */
    {0x0C, 0x04},   /* COM3: DCW увімкнено */
    {0x3E, 0x1A},   /* COM14: ручне масштабування, PCLK /4 -> 1 МГц */
    {0x70, 0x3A},   /* SCALING_XSC */
    {0x71, 0x35},   /* SCALING_YSC */
    {0x72, 0x22},   /* SCALING_DCWCTR: DCW /4 по обох осях */
    {0x73, 0xF2},   /* SCALING_PCLK_DIV */
    {0xA2, 0x02},   /* SCALING_PCLK_DELAY */
    {0x17, 0x16}, {0x18, 0x04}, {0x32, 0x24},   /* HSTART, HSTOP, HREF */
    {0x19, 0x02}, {0x1A, 0x7A}, {0x03, 0x0A},   /* VSTART, VSTOP, VREF */
    {0x13, 0x80},   /* COM8: AGC, AEC, AWB вимкнені */
    {0x00, 0x10},   /* GAIN: фіксоване */
    {0x07, 0x00}, {0x10, 0x40}, {0x04, 0x00},   /* AECHH, AECH, COM1: фіксована експозиція */
};

int ov7670_config_qqvga_yuv(ov7670_t *d)
{
    int rc = ov7670_soft_reset(d);
    for (unsigned i = 0; rc == DEV_OK && i < sizeof qqvga_yuv_regs / sizeof qqvga_yuv_regs[0]; i++)
        rc = d->bus->write_reg(d->bus->ctx, qqvga_yuv_regs[i][0], qqvga_yuv_regs[i][1]);
    return rc;
}

int ov7670_set_test_pattern(ov7670_t *d, int color_bar)
{
    int rc = d->bus->write_reg(d->bus->ctx, OV7670_REG_XSC, color_bar ? 0xBA : 0x3A);   /* біт 7 + значення масштабу */
    if (rc == DEV_OK) rc = d->bus->write_reg(d->bus->ctx, OV7670_REG_YSC, color_bar ? 0xB5 : 0x35);
    return rc;
}

int ov7670_set_exposure(ov7670_t *d, uint16_t aec, uint8_t gain)
{
    int rc = d->bus->write_reg(d->bus->ctx, 0x00, gain);                                  /* GAIN */
    if (rc == DEV_OK) rc = d->bus->write_reg(d->bus->ctx, 0x07, (uint8_t)((aec >> 10) & 0x3F));   /* AECHH */
    if (rc == DEV_OK) rc = d->bus->write_reg(d->bus->ctx, 0x10, (uint8_t)((aec >> 2) & 0xFF));    /* AECH */
    if (rc == DEV_OK) rc = d->bus->write_reg(d->bus->ctx, 0x04, (uint8_t)(aec & 0x03));           /* COM1[1:0] */
    return rc;
}
