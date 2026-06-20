// src/bus/can_bus.hpp
#pragma once

#include <cstdint>
#include "cmsis_os2.h"

namespace bus::can {

enum class StartStep : uint8_t {
    NotStarted = 0,
    FilterConfigured,
    Started,
    NotificationEnabled,
    Failed,
};

struct Frame {
    uint32_t id = 0;
    uint8_t data[8] = {};
    uint8_t len = 0;
    bool extended = false;
    bool remote = false;
};

struct Diagnostics {
    bool started = false;
    int32_t filter_status = -1;
    int32_t start_status = -1;
    int32_t notification_status = -1;
    int32_t tx_status = -1;
    int32_t rx_status = -1;
    int32_t last_error = -1;
    uint32_t hal_state = 0;
    uint32_t can_esr = 0;
    uint32_t start_attempts = 0;
    uint32_t tx_success_count = 0;
    uint32_t tx_fail_count = 0;
    uint32_t rx_count = 0;
    uint32_t rx_error_count = 0;
    uint32_t ack_error_count = 0;
    uint32_t bus_off_count = 0;
    uint8_t tx_error_counter = 0;
    uint8_t rx_error_counter = 0;
    bool error_warning = false;
    bool error_passive = false;
    bool bus_off = false;
    uint8_t last_error_code = 0;
    uint32_t last_rx_id = 0;
    uint8_t last_rx_len = 0;
    StartStep last_step = StartStep::NotStarted;
};

bool init();
bool start();
bool is_started();
bool receive(Frame& frame, uint32_t timeout_ms);
bool send_std(uint16_t id, const uint8_t* data, uint8_t len);
const volatile Diagnostics& diagnostics();

}
