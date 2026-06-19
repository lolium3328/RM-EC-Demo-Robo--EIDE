#include "bus/can_bus.hpp"

#include <cstdint>

#include "can.h"
#include "stm32f4xx.h"
#include "stm32f4xx_hal_can.h"
#include "stm32f4xx_hal_def.h"

namespace bus::can {

namespace {

volatile Diagnostics g_diagnostics = {};
volatile Frame g_last_rx_frame     = {};

}

bool init() {
    g_diagnostics.started             = false;
    g_diagnostics.filter_status       = -1;
    g_diagnostics.start_status        = -1;
    g_diagnostics.notification_status = -1;
    g_diagnostics.hal_state           = static_cast<uint32_t>(HAL_CAN_GetState(&hcan1));
    g_diagnostics.last_step           = StartStep::NotStarted;

    return hcan1.Instance != nullptr;
}

bool start() {
    g_diagnostics.start_attempts = g_diagnostics.start_attempts + 1;

    if (g_diagnostics.started) {
        g_diagnostics.hal_state = static_cast<uint32_t>(HAL_CAN_GetState(&hcan1));
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

    g_diagnostics.filter_status = static_cast<int32_t>(HAL_CAN_ConfigFilter(&hcan1, &filter));
    if (g_diagnostics.filter_status != HAL_OK) {
        g_diagnostics.last_step = StartStep::Failed;
        g_diagnostics.hal_state = static_cast<uint32_t>(HAL_CAN_GetState(&hcan1));
        return false;
    }
    g_diagnostics.last_step = StartStep::FilterConfigured;

    g_diagnostics.start_status = static_cast<int32_t>(HAL_CAN_Start(&hcan1));
    if (g_diagnostics.start_status != HAL_OK) {
        g_diagnostics.last_step = StartStep::Failed;
        g_diagnostics.hal_state = static_cast<uint32_t>(HAL_CAN_GetState(&hcan1));
        return false;
    }
    g_diagnostics.last_step = StartStep::Started;

    g_diagnostics.notification_status =
        static_cast<int32_t>(HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING));
    if (g_diagnostics.notification_status != HAL_OK) {
        g_diagnostics.last_step = StartStep::Failed;
        g_diagnostics.hal_state = static_cast<uint32_t>(HAL_CAN_GetState(&hcan1));
        return false;
    }

    g_diagnostics.started   = true;
    g_diagnostics.last_step = StartStep::NotificationEnabled;
    g_diagnostics.hal_state = static_cast<uint32_t>(HAL_CAN_GetState(&hcan1));
    return true;
}

bool is_started() { return g_diagnostics.started; }

bool receive(Frame& frame, uint32_t timeout_ms) {
    (void)timeout_ms;

    if (g_diagnostics.rx_count == 0) {
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
    if (!g_diagnostics.started || id > 0x7FFU || len > 8U || (data == nullptr && len > 0U)) {
        g_diagnostics.tx_fail_count = g_diagnostics.tx_fail_count + 1U;
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

    uint32_t mailbox       = 0;
    g_diagnostics.tx_status = static_cast<int32_t>(HAL_CAN_AddTxMessage(&hcan1, &tx_header, tx_data, &mailbox));
    g_diagnostics.hal_state = static_cast<uint32_t>(HAL_CAN_GetState(&hcan1));

    if (g_diagnostics.tx_status != HAL_OK) {
        g_diagnostics.tx_fail_count = g_diagnostics.tx_fail_count + 1U;
        return false;
    }

    g_diagnostics.tx_success_count = g_diagnostics.tx_success_count + 1U;
    return true;
}

const volatile Diagnostics& diagnostics() { return g_diagnostics; }

}  // namespace bus::can

extern "C" void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef* hcan) {
    if (hcan == nullptr || hcan->Instance != CAN1) {
        return;
    }

    CAN_RxHeaderTypeDef rx_header = {};
    uint8_t rx_data[8]            = {};

    bus::can::g_diagnostics.rx_status =
        static_cast<int32_t>(HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data));
    bus::can::g_diagnostics.hal_state = static_cast<uint32_t>(HAL_CAN_GetState(hcan));

    if (bus::can::g_diagnostics.rx_status != HAL_OK) {
        bus::can::g_diagnostics.rx_error_count = bus::can::g_diagnostics.rx_error_count + 1U;
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

    bus::can::g_diagnostics.rx_count   = bus::can::g_diagnostics.rx_count + 1U;
    bus::can::g_diagnostics.last_rx_id = bus::can::g_last_rx_frame.id;
    bus::can::g_diagnostics.last_rx_len = bus::can::g_last_rx_frame.len;
}
