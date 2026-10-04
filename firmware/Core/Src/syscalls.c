/**
 * @file syscalls.c
 * @brief Мінімальні системні виклики для newlib(-nano).
 *
 * Головне тут — _write(): саме її викликає printf() для виводу.
 * Ми перенаправляємо вивід у USART2, а той через ST-Link VCP
 * потрапляє на комп'ютер як віртуальний COM-порт.
 */
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "main.h"

int _write(int fd, char *ptr, int len)
{
    (void)fd;
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, (uint16_t)len, HAL_MAX_DELAY);
    return len;
}

/* Заглушки: бібліотека C їх вимагає при лінкуванні, а нам вони не потрібні. */
int _read(int fd, char *ptr, int len)  { (void)fd; (void)ptr; (void)len; return 0; }
int _close(int fd)                     { (void)fd; return -1; }
int _fstat(int fd, struct stat *st)    { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)                    { (void)fd; return 1; }
int _lseek(int fd, int off, int whence){ (void)fd; (void)off; (void)whence; return 0; }
int _getpid(void)                      { return 1; }
int _kill(int pid, int sig)            { (void)pid; (void)sig; errno = EINVAL; return -1; }
void _exit(int status)                 { (void)status; while (1) {} }

/* _sbrk: виділяє пам'ять для malloc (її використовує printf для буфера).
 * Символ _end задається лінкер-скриптом, кінець купи — _estack - _Min_Stack_Size. */
extern char _end;
extern char _estack;
extern unsigned int _Min_Stack_Size;

void *_sbrk(int incr)
{
    static char *heap_end = 0;
    char *prev;
    if (heap_end == 0) heap_end = &_end;
    if (heap_end + incr > (char *)(&_estack - (unsigned int)&_Min_Stack_Size)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    prev = heap_end;
    heap_end += incr;
    return prev;
}
