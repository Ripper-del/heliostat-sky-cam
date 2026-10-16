#include <stdio.h>
#include "test.h"
int t_checks, t_fails;
void test_mpu6050(void); void test_w25q(void); void test_ov7670(void); void test_quadrant(void);
int main(void)
{
    test_mpu6050(); test_w25q(); test_ov7670(); test_quadrant();
    printf("\n%d checks, %d failed\n", t_checks, t_fails);
    return t_fails ? 1 : 0;
}
