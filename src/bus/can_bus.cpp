#include "bus/can_bus.hpp"

#include <cstdint>

#include "can.h"
#include "stm32f4xx.h"
#include "stm32f4xx_hal_can.h"
#include "stm32f4xx_hal_def.h"

namespace bus::can {

namespace {

volatile bool g_started            = false;
volatile bool g_rx_available       = false;
volatile Frame g_last_rx_frame     = {};

}

bool init() {
    g_started      = false;
    g_rx_available = false;

    g_last_rx_frame.id = 0;
    g_last_rx_frame.len = 0;
    g_last_rx_frame.extended = false;
    g_last_rx_frame.remote = false;
    for (uint8_t i = 0; i < 8U; ++i) {
        g_last_rx_frame.data[i] = 0U;
    }

    return hcan1.Instance != nullptr;
}

bool start() {
    if (g_started) {
        return true;
    }

    CAN_FilterTypeDef filter    = {};
    filter.FilterBank           = 0;
    filter.FilterMode           = CAN_FILTERMODE_IDMASK;
    filter.FilterScale          = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh         = 0x0000;
    filter.FilterIdLow          = 0x0000;
    filter.FilterMaskIdHigh     = 0x0000;
    filter.FilterMaskIdLow      = 0x0000;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation     = ENABLE;
    filter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK) {
        return false;
    }

    if (HAL_CAN_Start(&hcan1) != HAL_OK) {
        return false;
    }

    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
        return false;
    }

    g_started = true;
    return true;
}

bool is_started() { return g_started; }

// 只接收最新帧
bool receive(Frame& frame, uint32_t timeout_ms) {
    (void)timeout_ms;

    if (!g_rx_available) {
        return false;
    }

    frame.id       = g_last_rx_frame.id;
    frame.len      = g_last_rx_frame.len;
    frame.extended = g_last_rx_frame.extended;
    frame.remote   = g_last_rx_frame.remote;

    const uint8_t len = frame.len <= 8U ? frame.len : 8U;
    for (uint8_t i = 0; i < len; ++i) {
        frame.data[i] = g_last_rx_frame.data[i];
    }
    for (uint8_t i = len; i < 8U; ++i) {
        frame.data[i] = 0U;
    }

    return true;
}

bool send_std(uint16_t id, const uint8_t* data, uint8_t len) {
    if (!g_started || id > 0x7FFU || len > 8U || (data == nullptr && len > 0U)) {
        return false;
    }

    CAN_TxHeaderTypeDef tx_header = {};
    tx_header.StdId              = id;
    tx_header.IDE                = CAN_ID_STD;
    tx_header.RTR                = CAN_RTR_DATA;
    tx_header.DLC                = len;
    tx_header.TransmitGlobalTime = DISABLE;

    uint8_t tx_data[8] = {};
    for (uint8_t i = 0; i < len; ++i) {
        tx_data[i] = data[i];
    }

    uint32_t mailbox = 0;
    return HAL_CAN_AddTxMessage(&hcan1, &tx_header, tx_data, &mailbox) == HAL_OK;
}

}  // namespace bus::can

extern "C" void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef* hcan) {
    if (hcan == nullptr || hcan->Instance != CAN1) {
        return;
    }

    CAN_RxHeaderTypeDef rx_header = {};
    uint8_t rx_data[8]            = {};

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK) {
        return;
    }

    bus::can::g_last_rx_frame.id       = rx_header.IDE == CAN_ID_EXT ? rx_header.ExtId : rx_header.StdId;
    bus::can::g_last_rx_frame.len      = rx_header.DLC <= 8U ? rx_header.DLC : 8U;
    bus::can::g_last_rx_frame.extended = rx_header.IDE == CAN_ID_EXT;
    bus::can::g_last_rx_frame.remote   = rx_header.RTR == CAN_RTR_REMOTE;

    for (uint8_t i = 0; i < bus::can::g_last_rx_frame.len; ++i) {
        bus::can::g_last_rx_frame.data[i] = rx_data[i];
    }
    for (uint8_t i = bus::can::g_last_rx_frame.len; i < 8U; ++i) {
        bus::can::g_last_rx_frame.data[i] = 0U;
    }

    bus::can::g_rx_available = true;
}
