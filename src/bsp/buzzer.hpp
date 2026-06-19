#pragma once

#include <cstdint>

namespace bsp::buzzer {

constexpr uint32_t kMaxPulse = 249U;
constexpr uint32_t kDefaultPulse = 125U;

void init();
void on();
void off();
void set_pulse(uint32_t pulse);
uint32_t pulse();
void beep_blocking(uint32_t on_ms, uint32_t off_ms, uint8_t count);
void beep_blocking(uint32_t on_ms, uint32_t off_ms, uint8_t count, uint32_t pulse);

}  // namespace bsp::buzzer
