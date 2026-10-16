#ifndef CLI_H
#define CLI_H
/** Однолітерні команди UART для запису саме тієї транзакції, яку треба зловити аналізатором:
 *  h help | s selftest | i I2C scan | m MPU (WHO_AM_I + 6-байтне читання) | c камера PID/VER
 *  f JEDEC flash | w flash erase+write+read | p PWM 1000/2000 мкс | a ADC + маркер D13 */
void cli_handle(char c);
#endif
