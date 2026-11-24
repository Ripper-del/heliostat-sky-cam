#ifndef APP_H
#define APP_H
void app_init(void);   /* банер не тут; запускає самоперевірку */
void app_step(void);   /* один крок головного циклу: LED, кнопка, CLI, щосекундний лог */
#endif
