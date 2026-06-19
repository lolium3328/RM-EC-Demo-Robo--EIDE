#include "bsp/buzzer.hpp"

#include "cmsis_os2.h"
#include "tim.h"

namespace bsp::buzzer {

namespace {

uint32_t g_pulse = kDefaultPulse;

uint32_t clamp_pulse(uint32_t pulse) {
    return pulse > kMaxPulse ? kMaxPulse : pulse;
}

}  // namespace

void init() {
    set_pulse(kDefaultPulse);
    off();
}

void on() {
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, g_pulse);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);
}

void off() {
    HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_3);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);
}

void set_pulse(uint32_t pulse) {
    g_pulse = clamp_pulse(pulse);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, g_pulse);
}

uint32_t pulse() {
    return g_pulse;
}

void beep_blocking(uint32_t on_ms, uint32_t off_ms, uint8_t count) {
    for (uint8_t i = 0; i < count; ++i) {
        on();
        osDelay(on_ms);
        off();
        osDelay(off_ms);
    }
}

void beep_blocking(uint32_t on_ms, uint32_t off_ms, uint8_t count, uint32_t pulse) {
    set_pulse(pulse);
    beep_blocking(on_ms, off_ms, count);
}

}  // namespace bsp::buzzer
