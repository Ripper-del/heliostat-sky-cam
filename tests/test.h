/* Мінімальний тестовий каркас на assert-логіці (без Unity/CMock): лічильники + RUN().
 * Чого це не дає порівняно з Unity/CMock: автоматичних моків з генерацією, багатого набору
 * ASSERT_*, ізоляції тестів у окремих процесах. Для наших розмірів цього достатньо. */
#ifndef TEST_H
#define TEST_H
#include <stdio.h>
#include <string.h>

extern int t_checks, t_fails;

#define CHECK(cond) do { t_checks++; if (!(cond)) { t_fails++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long _a = (long)(a), _b = (long)(b); t_checks++; if (_a != _b) { t_fails++; \
    printf("  FAIL %s:%d: %s == %s (%ld != %ld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define RUN(fn) do { int _f = t_fails; fn(); printf("[%s] %s\n", t_fails == _f ? " ok " : "FAIL", #fn); } while (0)
#endif
