#include "quadrant.h"

uint32_t quadrant_raw_to_mv(uint16_t raw)
{
    return ((uint32_t)raw * QUADRANT_VREF_MV) / QUADRANT_ADC_MAX;
}

quadrant_error_t quadrant_error(const uint16_t q[4])
{
    quadrant_error_t e = {0, 0};
    int32_t tl = q[0], tr = q[1], bl = q[2], br = q[3];
    int32_t sum = tl + tr + bl + br;
    if (sum == 0) return e;
    e.x_permille = ((tr + br) - (tl + bl)) * 1000 / sum;
    e.y_permille = ((tl + tr) - (bl + br)) * 1000 / sum;
    return e;
}

uint32_t servo_angle_to_us(int deg, uint32_t min_us, uint32_t max_us)
{
    if (deg < 0) deg = 0;
    if (deg > 180) deg = 180;
    return min_us + ((max_us - min_us) * (uint32_t)deg) / 180U;
}

uint32_t servo_next_step(uint32_t cur_us, uint32_t target_us, uint32_t step_us)
{
    if (cur_us < target_us) return (target_us - cur_us > step_us) ? cur_us + step_us : target_us;
    if (cur_us > target_us) return (cur_us - target_us > step_us) ? cur_us - step_us : target_us;
    return cur_us;
}
