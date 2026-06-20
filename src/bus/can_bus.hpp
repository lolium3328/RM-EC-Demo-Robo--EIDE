// src/bus/can_bus.hpp
#pragma once

#include <cstdint>

namespace bus::can {

struct Frame {
    uint32_t id = 0;
    uint8_t data[8] = {};
    uint8_t len = 0;
    bool extended = false;
    bool remote = false;
};

bool init();
bool start();
bool is_started();
bool receive(Frame& frame, uint32_t timeout_ms);
bool send_std(uint16_t id, const uint8_t* data, uint8_t len);

}
