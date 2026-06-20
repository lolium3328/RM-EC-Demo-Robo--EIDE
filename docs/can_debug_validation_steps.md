# CAN 调试与验收步骤

本文档用于在 STM32F407 项目中逐步验证 CAN 电机通信链路。建议按阶段检查，不要等电机协议和控制任务全部写完后再一次性联调。

## 阶段 1：确认 CubeMX/HAL 已接入

完成内容：

- `bsp/CubeMX/STM32F407IGHx/Inc/can.h` 已生成。
- `bsp/CubeMX/STM32F407IGHx/Src/can.c` 已生成。
- `main.c` 中包含 `#include "can.h"`。
- `main.c` 中调用了 `MX_CAN1_Init();`。
- `stm32f4xx_hal_conf.h` 中启用了 `HAL_CAN_MODULE_ENABLED`。
- `stm32f4xx_it.c` 中存在 `CAN1_RX0_IRQHandler()`。
- 工程目录中存在 `stm32f4xx_hal_can.c`。

检验方式：

- 使用 EIDE Build 工程。

通过标准：

- 没有 `can.h: No such file or directory`。
- 没有 `undefined reference to MX_CAN1_Init`。
- 没有 `undefined reference to HAL_CAN_Init`。
- 没有 `undefined reference to HAL_CAN_IRQHandler`。
- 没有 `multiple definition of CAN1_RX0_IRQHandler`。
- 最终可以生成 `.elf`。

## 阶段 2：确认 CAN 外设可以启动

完成内容：

- 在 `can_bus::start()` 或等效初始化函数中配置 CAN filter。
- 调用 `HAL_CAN_ConfigFilter(&hcan1, &filter)`。
- 调用 `HAL_CAN_Start(&hcan1)`。
- 调用 `HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING)`。

建议先使用全接收 filter，调通后再按电机 ID 收窄：

```cpp
CAN_FilterTypeDef filter = {};

filter.FilterBank = 0;
filter.FilterMode = CAN_FILTERMODE_IDMASK;
filter.FilterScale = CAN_FILTERSCALE_32BIT;
filter.FilterIdHigh = 0x0000;
filter.FilterIdLow = 0x0000;
filter.FilterMaskIdHigh = 0x0000;
filter.FilterMaskIdLow = 0x0000;
filter.FilterFIFOAssignment = CAN_RX_FIFO0;
filter.FilterActivation = ENABLE;
```

检验方式：

- 在调试器中观察 `can_bus::start()` 的返回值。
- 或临时增加状态变量：

```cpp
volatile bool can_started = false;

can_started = can_bus::start();
```

通过标准：

- `HAL_CAN_ConfigFilter()` 返回 `HAL_OK`。
- `HAL_CAN_Start()` 返回 `HAL_OK`。
- `HAL_CAN_ActivateNotification()` 返回 `HAL_OK`。
- `HAL_CAN_GetState(&hcan1)` 进入可接收状态，例如 `HAL_CAN_STATE_LISTENING`。
- `bus::can::start()` 返回 `true`，`bus::can::is_started()` 返回 `true`。

## 阶段 3：确认 CAN 中断回调能收到帧

编译回归：

- 完整链接必须成功，`task_demo` 中的 `bus::can::receive()` 调用不得出现 `undefined reference`。
- `receive()` 是正常收帧链路的公共接口，删除诊断代码时应保留其声明和实现。

完成内容：

- 实现 `HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)`。
- 在回调中调用 `HAL_CAN_GetRxMessage()`。
- 先记录接收计数和最后一帧数据。

建议临时调试变量：

```cpp
volatile uint32_t can_rx_count = 0;
volatile uint32_t can_last_id = 0;
volatile uint8_t can_last_data[8] = {};
volatile uint8_t can_last_len = 0;
```

回调中的最小验证逻辑：

```cpp
extern "C" void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    if (hcan->Instance != CAN1) {
        return;
    }

    CAN_RxHeaderTypeDef rx_header = {};
    uint8_t rx_data[8] = {};

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK) {
        return;
    }

    can_rx_count++;
    can_last_id = (rx_header.IDE == CAN_ID_EXT) ? rx_header.ExtId : rx_header.StdId;
    can_last_len = rx_header.DLC;

    for (uint8_t i = 0; i < rx_header.DLC && i < 8; ++i) {
        can_last_data[i] = rx_data[i];
    }
}
```

检验方式：

- 使用 USB-CAN 分析仪、另一块 STM32 或电机本身发送测试帧。
- 示例测试帧：

```text
Baud: 1 Mbps
ID:   0x101
DLC:  8
DATA: 01 02 03 04 05 06 07 08
```

通过标准：

- `can_rx_count` 增加。
- `can_last_id == 0x101`。
- `can_last_len == 8`。
- `can_last_data` 等于 `01 02 03 04 05 06 07 08`。

如果没有收到帧，优先检查：

- CAN 波特率是否一致。
- CANH/CANL 是否接反。
- 是否共地。
- 总线两端是否有 120 欧姆终端电阻。
- CAN 收发器是否正常供电。
- CAN1 引脚是否确认为 `PD0 = CAN1_RX`、`PD1 = CAN1_TX`。

## 阶段 4：确认中断数据能进入 RTOS Queue

完成内容：

- `can_bus.cpp` 中创建 RX queue。
- 回调中使用非阻塞方式写入 queue。
- `task_can_rx` 中从 queue 读取 CAN 帧。

回调中不要做复杂解析，只做读 FIFO 和入队：

```cpp
osMessageQueuePut(rx_queue, &frame, 0, 0);
```

任务中维护调试计数：

```cpp
volatile uint32_t task_can_rx_count = 0;
volatile uint32_t task_can_last_id = 0;
```

检验方式：

- 继续使用 USB-CAN 分析仪发送测试帧。
- 同时观察中断侧计数和任务侧计数。

通过标准：

- `can_rx_count` 增加。
- `task_can_rx_count` 也增加。
- `task_can_last_id` 与 `can_last_id` 一致。

如果 `can_rx_count` 增加但 `task_can_rx_count` 不增加，问题通常在：

- RX queue 没有创建。
- 回调中入队失败。
- `task_can_rx` 没有创建。
- `task_can_rx` 优先级或循环逻辑有问题。

## 阶段 5：确认电机协议解析正确

完成内容：

- 实现 `can_motor_protocol::decode_feedback()`。
- 在协议层检查：
  - CAN ID。
  - DLC。
  - CRC。
  - 字节序。
  - 单位换算。

检验方式：

- 先不用真实电机，手动构造协议帧做单元式验证。

```cpp
uint8_t data[8] = { /* 按电机手册填入 */ };
motor::can_protocol::Feedback feedback = {};

bool ok = motor::can_protocol::decode_feedback(0x101, data, 8, feedback);
```

通过标准：

- 正确 ID、正确 DLC、正确 CRC 时，`ok == true`。
- 错误 ID 时，`ok == false`。
- 错误 DLC 时，`ok == false`。
- 错误 CRC 时，`ok == false`。
- 解析出的速度、电流、位置、温度等字段与电机手册一致。

## 阶段 6：确认 CAN 可以发送

完成内容：

- 实现 `can_bus::send_std()` 或等效发送函数。
- 内部调用 `HAL_CAN_AddTxMessage()`。

检验方式：

- STM32 主动发送一帧。
- USB-CAN 分析仪监听总线。

示例测试帧：

```text
ID:   0x200
DLC:  8
DATA: 00 01 00 02 00 03 00 04
```

通过标准：

- `HAL_CAN_AddTxMessage()` 返回 `HAL_OK`。
- USB-CAN 分析仪可以看到 ID 为 `0x200` 的帧。
- DLC 和 data 完全一致。

注意：

- `HAL_CAN_AddTxMessage()` 返回 `HAL_OK` 只说明成功放入发送 mailbox。
- 是否真的出现在 CAN 总线上，仍需要 USB-CAN、另一节点或示波器确认。
- 连续发送时，USB-CAN 或示波器应稳定观察到完整帧。
- 断开对端、终端电阻或收发器后，不应仅依赖 `send_std()` 返回值判断物理层是否成功，应使用 USB-CAN 或示波器复核。

## 阶段 7：接入真实电机联调

完成内容：

- 能稳定接收 CAN 帧。
- Queue 到任务链路正常。
- 协议解析正常。
- 控制帧编码正常。
- CAN 发送正常。

建议联调顺序：

1. 接上电机和 CAN 收发器。
2. 确认 STM32、电机、CAN 工具共地。
3. 确认总线两端有 120 欧姆终端电阻。
4. 确认波特率与电机手册一致。
5. 先只监听电机反馈，不发送控制。
6. 确认反馈 ID、DLC、CRC、字段变化合理。
7. 发送零电流、失能或安全控制命令。
8. 确认电机没有异常动作。
9. 再发送小电流或小速度命令。
10. 验证超时保护：停止接收反馈后，控制输出应归零或失能。

通过标准：

- 能稳定收到电机反馈。
- 反馈 ID 正确。
- 数据变化符合电机状态。
- CRC 校验通过。
- 电机在线状态判断正常。
- 发送控制后电机行为符合预期。
- 通信超时后控制输出进入安全状态。

## 推荐总体验收路线

```text
Build 通过
    ↓
CAN start 返回 HAL_OK
    ↓
USB-CAN 发帧，STM32 can_rx_count 增加
    ↓
task_can_rx_count 增加
    ↓
协议层 decode_feedback 验证通过
    ↓
STM32 发测试帧，USB-CAN 能看到
    ↓
接电机，只监听反馈
    ↓
发零控制或失能命令
    ↓
发小控制量
    ↓
验证超时保护
```
