#pragma once
#include <cstdint>

#include "bsp/buzzer.hpp"
#include "bus/can_bus.hpp"
#include "cmsis_os2.h"

inline static void task_demo(void*) {
    // BUZZER BOOT TEST BEGIN: temporary boot-success beep validation.
    bsp::buzzer::init();
    bsp::buzzer::beep_blocking(100, 100, 3, bsp::buzzer::kDefaultPulse);
    // BUZZER BOOT TEST END

    // CAN BUS TEST BEGIN: temporary manual CAN send/receive validation.
    volatile uint32_t can_test_tx_attempts = 0;
    volatile uint32_t can_test_tx_ok       = 0;
    volatile uint32_t can_test_rx_seen     = 0;
    bus::can::Frame can_test_rx_frame      = {};
    uint8_t can_test_payload[8]            = {0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04};
    // CAN BUS TEST END

    while (true) {
        // CAN BUS TEST BEGIN: sends one standard frame per second and snapshots the last received frame.
        can_test_payload[0] = static_cast<uint8_t>(can_test_tx_attempts & 0xFFU);
        can_test_tx_attempts = can_test_tx_attempts + 1U;
        if (bus::can::send_std(0x200, can_test_payload, 8)) {
            can_test_tx_ok = can_test_tx_ok + 1U;
        }

        if (bus::can::receive(can_test_rx_frame, 0)) {
            can_test_rx_seen = can_test_rx_seen + 1U;
        }
        // CAN BUS TEST END

        osDelay(1000);
    }
}
