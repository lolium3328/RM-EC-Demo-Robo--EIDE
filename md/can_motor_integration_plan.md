# CAN 通信电机接入实施计划

## 目标

在当前 STM32F407 + CubeMX + FreeRTOS + EIDE 工程中，接入一个带自定义 CAN 通信协议的电机。本文只规划改动范围和实施步骤，不直接绑定某个具体电机协议字段；拿到电机协议手册后，应把协议 ID、帧格式、控制周期、反馈周期和安全策略补充到对应模块。

## 当前项目状态

项目主体代码较薄，当前更接近一个可运行的基础模板：

- `src/` 只有 `task_demo.hpp`，当前任务只是每 1000 ms 延时一次。
- `bsp/impl_init.cpp` 通过 BSP hook 在 `MX_FREERTOS_Init()` 里创建 `task_demo`。
- CubeMX 当前初始化了 GPIO、USART6、USB CDC 和 FreeRTOS。
- 当前没有 `can.c` / `can.h`，也没有现成 CAN 总线抽象、电机协议解析或电机控制任务。
- `stm32f4xx_hal_conf.h` 中 `HAL_CAN_MODULE_ENABLED` 仍被注释，说明 HAL CAN 模块尚未启用。
- HAL 驱动目录当前未包含 `stm32f4xx_hal_can.c`，后续需要由 CubeMX 生成或手动补入同版本 HAL 驱动文件。

关键现有入口：

- `bsp/CubeMX/STM32F407IGHx/Src/main.c`
  - `MX_GPIO_Init()` 和 `MX_USART6_UART_Init()` 已在 RTOS 前调用。
  - `bsp_init_before_rtos_init()` 位于外设初始化后、RTOS 初始化前。
  - `MX_FREERTOS_Init()` 后启动调度器。
- `bsp/CubeMX/STM32F407IGHx/Src/freertos.c`
  - `bsp_init_after_rtos_init()` 在 RTOS 对象创建阶段调用。
  - `bsp_init_in_rtos_thread()` 在默认任务里、USB 初始化后调用，随后默认任务删除自身。
- `.eide/eide.yml`
  - `srcDirs` 已包含 `bsp`、`src`、`lib`。
  - include path 已包含 `bsp/CubeMX/STM32F407IGHx/Inc`、HAL Driver、FreeRTOS、`src`、`lib`、`lib-config`。
  - 因此新增到 `src/` 下的 `.cpp/.hpp` 通常会被 EIDE 自动纳入构建；CubeMX 生成到 `bsp/CubeMX/...` 下的 CAN 文件也会被扫描。

## 需要确认的硬件和协议信息

实施前必须确认这些内容，否则 CAN 参数和协议层无法正确落地：

1. CAN 外设选择：使用 `CAN1` 还是 `CAN2`。STM32F407 上 `CAN2` 依赖 `CAN1` 时钟，优先建议先用 `CAN1`。
2. CAN 引脚：板上 CAN 收发器实际连接到哪组引脚，例如常见 `CAN1_RX/CAN1_TX` 可用 `PA11/PA12`、`PB8/PB9`、`PD0/PD1` 等复用组合。当前 `PA11/PA12` 已用于 USB FS，不能用于 CAN1；`PB8/PB9` 当前在 `gpio.c` 中作为模拟输入空闲，可能是优先候选，但必须以原理图为准。
3. CAN 波特率：常见 RoboMaster 电机是 1 Mbps，但新电机必须按协议手册确认。
4. 标准帧还是扩展帧：11-bit Standard ID 或 29-bit Extended ID。
5. 控制帧 ID 和反馈帧 ID：包括单电机 ID、广播 ID、反馈 ID 范围、是否需要 ID 设置流程。
6. 数据格式：每个字节/位的含义、大小端、比例系数、符号位、单位。
7. 控制模式：电流、速度、位置、MIT 模式或厂商私有模式。
8. 控制周期和超时要求：例如 1 ms、2 ms、5 ms、10 ms；反馈丢失多久进入失能。
9. 上电/使能/失能流程：是否需要握手、清错、零点设置、心跳或 watchdog。
10. 总线拓扑：是否和其他 CAN 设备共线、终端电阻是否已经存在、收发器供电和待机脚是否需要 MCU 控制。

## 可能需要改动的部分

### 1. CubeMX 外设配置

需要在 `bsp/CubeMX/STM32F407IGHx/CubeMXProject_STM32F407IGHx.ioc` 中启用 CAN：

- 增加 CAN IP，例如 `CAN1`。
- 配置 CAN RX/TX 引脚为对应 AF。
- 配置 bit timing，使实际波特率匹配电机协议。
- 开启 CAN RX FIFO 中断，通常至少启用 `CAN1_RX0_IRQn`。
- 重新生成代码，生成或更新：
  - `bsp/CubeMX/STM32F407IGHx/Inc/can.h`
  - `bsp/CubeMX/STM32F407IGHx/Src/can.c`
  - `bsp/CubeMX/STM32F407IGHx/Src/main.c`
  - `bsp/CubeMX/STM32F407IGHx/Src/stm32f4xx_it.c`
  - `bsp/CubeMX/STM32F407IGHx/Src/stm32f4xx_hal_msp.c`
  - `bsp/CubeMX/STM32F407IGHx/Inc/stm32f4xx_hal_conf.h`

注意：

- `PA11/PA12` 已被 USB FS 占用，不应复用给 CAN。
- 当前 `PB8/PB9` 被配置为模拟输入空闲，如果硬件连接允许，可用于 `CAN1_RX/CAN1_TX`。
- CubeMX 生成区尽量只通过 CubeMX 修改，手写代码放在 `src/` 或 `bsp/impl_*.cpp`。

### 2. HAL CAN 驱动启用

需要确认以下内容：

- `bsp/CubeMX/STM32F407IGHx/Inc/stm32f4xx_hal_conf.h` 中启用：
  - `#define HAL_CAN_MODULE_ENABLED`
- HAL Driver 源码目录中需要存在并参与构建：
  - `bsp/CubeMX/STM32F407IGHx/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_can.c`
- HAL Driver 头文件目录中需要存在：
  - `stm32f4xx_hal_can.h`

当前工程没有搜索到 `stm32f4xx_hal_can.c`，所以仅取消宏注释还不够；必须由 CubeMX 生成或补入同一 HAL 包版本的 CAN 驱动源文件。

### 3. BSP CAN 总线封装

建议新增一个很薄的 CAN 总线封装，不要让业务层直接散落 `HAL_CAN_AddTxMessage()` 和 `HAL_CAN_GetRxMessage()`。

建议文件：

- `src/bus/can_bus.hpp`
- `src/bus/can_bus.cpp`

职责：

- 初始化 CAN filter。
- 启动 CAN：`HAL_CAN_Start()`。
- 开启接收中断：`HAL_CAN_ActivateNotification()`。
- 提供发送接口，例如：
  - `bool can_send_std(uint16_t id, const uint8_t data[8], uint8_t dlc)`
  - `bool can_send_ext(uint32_t id, const uint8_t data[8], uint8_t dlc)`
- 在 HAL RX callback 中读取帧，并放入 FreeRTOS 队列或无锁环形队列。
- 提供业务层读取接口，例如 `bool can_receive(CanFrame& frame, uint32_t timeout_ms)`。
- 记录错误状态，例如 bus-off、error passive、tx mailbox full。

建议不要在中断回调里直接解析电机协议或做复杂控制计算。中断里只收帧、入队、唤醒任务。

### 4. 电机协议层

建议新增协议适配层，把“电机协议”与“CAN 总线收发”分离。

建议文件：

- `src/motor/can_motor_protocol.hpp`
- `src/motor/can_motor_protocol.cpp`

职责：

- 定义电机反馈结构：
  - 转角/位置
  - 速度
  - 电流/力矩
  - 温度
  - 错误码
  - 最近更新时间
- 定义控制命令结构：
  - 目标模式
  - 目标电流/速度/位置/力矩
  - 使能/失能
- 提供编码函数：把控制命令编码成 CAN 帧。
- 提供解码函数：把 CAN 帧解析为反馈结构。
- 做范围限制和单位换算。
- 做 ID 匹配和帧合法性检查。

这层不应该依赖 FreeRTOS 任务，也不应该直接调用 HAL；这样后续可以单独用离线数据或主机测试验证协议编解码。

### 5. 电机对象/管理层

建议新增电机驱动对象，管理单个或多个电机状态。

建议文件：

- `src/motor/can_motor.hpp`
- `src/motor/can_motor.cpp`

职责：

- 保存电机配置：
  - CAN 通道
  - 电机 ID
  - 控制帧 ID
  - 反馈帧 ID
  - 控制周期
  - 超时时间
- 保存最新反馈。
- 提供上层接口：
  - `enable()`
  - `disable()`
  - `set_current()`
  - `set_velocity()`
  - `set_position()`
  - `update_from_frame()`
  - `tick_control()`
  - `is_online()`
- 在离线时自动输出安全命令，例如零电流或失能帧。

如果未来要接多个同协议电机，应把协议常量和电机实例配置分开，不要把 ID 写死在解析函数里。

### 6. RTOS 任务层

当前 `bsp/impl_init.cpp` 只创建 `task_demo`。接入电机后建议新增独立任务：

- `src/tasks/task_can_rx.hpp/.cpp`
- `src/tasks/task_motor_control.hpp/.cpp`

最低可行结构：

- CAN RX 任务：从 CAN 队列读取帧，分发给电机对象。
- 电机控制任务：固定周期发送控制命令，检查在线状态，处理失能。

也可以先合并为一个任务，后续再拆：

- 低风险起步：一个 `task_motor_demo`，周期性读取队列、更新反馈、发送零电流/低速测试命令。
- 稳定后再拆成 RX 任务和控制任务。

任务创建位置建议放在：

- `bsp/impl_init.cpp` 的 `bsp_init_after_rtos_init()` 中创建 RTOS 任务。
- CAN 外设启动可放在 `bsp_init_before_rtos_init()`，因为此时 HAL 和外设已初始化，RTOS 尚未启动。
- 如果 CAN 接收依赖 FreeRTOS 队列，队列创建要在启动 CAN 中断前完成；可以在 `bsp_init_after_rtos_init()` 里创建队列后再启动 CAN，或把 CAN 启动延后到任务开始时。

### 7. 安全策略

电机接入必须先实现安全边界，再做高性能控制：

- 启动默认不使能电机。
- 未收到有效反馈前不发送运动命令。
- 收到反馈超时后进入失能或零输出。
- 控制目标限幅。
- 总线发送失败计数，持续失败后进入故障状态。
- 解析失败计数，错误帧不更新状态。
- 提供急停接口，至少能立即停止继续发送非零控制命令。

### 8. 调试输出

当前工程已有 USB CDC 和 USART6，但没有统一日志模块。

建议短期：

- 使用 USB CDC 或 USART6 打印电机在线状态、原始反馈 ID、错误计数。
- 不在 CAN RX 中断里直接打印。

建议后续：

- 新增轻量日志封装，例如 `src/debug/log.hpp`。
- 支持关闭日志，避免影响控制周期。

## 推荐目录结构

建议新增：

```text
src/
  bus/
    can_bus.hpp
    can_bus.cpp
  motor/
    can_motor_protocol.hpp
    can_motor_protocol.cpp
    can_motor.hpp
    can_motor.cpp
  tasks/
    task_can_rx.hpp
    task_can_rx.cpp
    task_motor_control.hpp
    task_motor_control.cpp
```

如果先做最小版本，也可以：

```text
src/
  bus/
    can_bus.hpp
    can_bus.cpp
  motor/
    can_motor_protocol.hpp
    can_motor_protocol.cpp
  tasks/
    task_motor_demo.hpp
    task_motor_demo.cpp
```

## 分阶段实施计划

### 阶段 0：确认硬件和协议

交付物：

- 确认 CAN 外设、引脚、波特率、标准帧/扩展帧。
- 确认电机控制帧和反馈帧格式。
- 确认收发器型号、终端电阻、待机脚或使能脚。

验收：

- 用示波器或 CAN 分析仪确认总线物理层正常。
- 明确不能与 USB FS 的 `PA11/PA12` 冲突。

### 阶段 1：启用 CAN 外设

操作：

1. 用 CubeMX 打开 `bsp/CubeMX/STM32F407IGHx/CubeMXProject_STM32F407IGHx.ioc`。
2. 启用 `CAN1`。
3. 配置 RX/TX 引脚，优先按原理图选择；若使用 `PB8/PB9`，配置为 `CAN1_RX/CAN1_TX`。
4. 配置 CAN bit timing。
5. 开启 RX FIFO0 中断。
6. 生成代码。
7. 检查生成结果是否包含 `can.c/can.h` 和 HAL CAN 驱动源文件。

验收：

- 工程能编译。
- `main.c` 中出现 `MX_CAN1_Init()` 且在 RTOS 前调用。
- `stm32f4xx_it.c` 中出现 CAN RX IRQ handler。
- `stm32f4xx_hal_conf.h` 中 `HAL_CAN_MODULE_ENABLED` 已启用。

### 阶段 2：实现 CAN 总线封装

操作：

1. 新增 `src/bus/can_bus.*`。
2. 定义 `CanFrame` 数据结构。
3. 实现 CAN filter，先可全接收，稳定后按反馈 ID 收窄。
4. 实现发送接口。
5. 实现 RX callback 到队列。
6. 实现错误状态读取。

验收：

- 上电后 CAN 能启动。
- CAN 分析仪能看到测试帧。
- MCU 能收到分析仪发送的测试帧。
- 中断接收不会阻塞或打印。

### 阶段 3：实现电机协议编解码

操作：

1. 新增 `src/motor/can_motor_protocol.*`。
2. 根据协议手册定义常量和结构。
3. 实现 `encode_*()` 和 `decode_feedback()`。
4. 对 ID、DLC、数据范围、大小端做严格校验。
5. 把单位换算集中在协议层。

验收：

- 给定协议手册中的样例帧，解析结果正确。
- 给定目标命令，编码结果与手册一致。
- 错误 ID、错误 DLC、越界值会被拒绝。

### 阶段 4：实现电机对象和任务

操作：

1. 新增 `src/motor/can_motor.*`。
2. 新增 `src/tasks/task_motor_control.*`。
3. 在 `bsp/impl_init.cpp` 创建电机任务。
4. 初始只发送安全命令，例如失能或零电流。
5. 收到有效反馈后再允许进入测试控制模式。

验收：

- 电机未连接时系统不死机，状态显示离线。
- 电机连接后能识别在线。
- 只发送零输出时电机无异常动作。
- 人为断开 CAN 后能进入离线/失能状态。

### 阶段 5：闭环或上层控制接入

操作：

1. 根据需求接入速度/位置/力矩控制。
2. 设计控制周期和任务优先级。
3. 加入限幅、斜坡、故障降级。
4. 若有多个电机，设计统一配置表。

验收：

- 控制周期稳定。
- 电机响应符合预期。
- 离线、堵转、过温、错误码等状态能被处理。

## 建议的最小验证程序

第一版不要直接让电机运动。建议顺序：

1. 只启动 CAN，不发送电机命令。
2. 打印收到的原始 CAN ID 和 DLC，确认电机反馈帧存在。
3. 加入协议解析，确认位置/速度/温度数值合理。
4. 发送失能或零输出命令，确认总线发送正常。
5. 发送极小目标值，短时间测试。
6. 加入超时保护后再进入持续控制。

## 风险点

- 引脚冲突：`PA11/PA12` 已用于 USB FS，不能再用于 CAN。
- HAL 驱动缺失：当前工程未包含 `stm32f4xx_hal_can.c`，必须补齐。
- CubeMX 覆盖：`bsp/CubeMX/` 下手写改动可能被重新生成覆盖，应尽量放到 USER CODE 区或 `src/`。
- 中断优先级：CAN IRQ 若调用 FreeRTOS FromISR API，优先级不能高于 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 允许范围。
- 发送邮箱满：控制周期过快或总线异常时 `HAL_CAN_AddTxMessage()` 可能失败，必须处理。
- 总线错误：bus-off 后需要恢复策略。
- 电机上电默认状态不明：必须按协议执行使能/清错流程。
- 比例系数错误：协议大小端、符号位、单位换算错会导致危险输出。

## 建议改动清单

必须改：

- `bsp/CubeMX/STM32F407IGHx/CubeMXProject_STM32F407IGHx.ioc`
- `bsp/CubeMX/STM32F407IGHx/Inc/stm32f4xx_hal_conf.h`
- `bsp/CubeMX/STM32F407IGHx/Src/main.c`
- `bsp/CubeMX/STM32F407IGHx/Src/stm32f4xx_hal_msp.c`
- `bsp/CubeMX/STM32F407IGHx/Src/stm32f4xx_it.c`
- 新增 `bsp/CubeMX/STM32F407IGHx/Inc/can.h`
- 新增 `bsp/CubeMX/STM32F407IGHx/Src/can.c`
- 新增或确认 `bsp/CubeMX/STM32F407IGHx/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_can.c`

建议新增：

- `src/bus/can_bus.hpp`
- `src/bus/can_bus.cpp`
- `src/motor/can_motor_protocol.hpp`
- `src/motor/can_motor_protocol.cpp`
- `src/motor/can_motor.hpp`
- `src/motor/can_motor.cpp`
- `src/tasks/task_motor_control.hpp`
- `src/tasks/task_motor_control.cpp`

可能改：

- `bsp/impl_init.cpp`：创建 CAN/电机相关任务，安排初始化顺序。
- `src/task_demo.hpp`：后续可删除或保留为模板。
- `.eide/eide.yml`：通常无需改；只有 EIDE 没有自动纳入新文件时才调整。
- `.clangd`：通常无需改；构建后 EIDE 会更新 `compile_commands.json`。

## 推荐落地顺序

1. 先用 CubeMX 正确生成 CAN 外设代码并保证空工程可编译。
2. 再写 `can_bus`，用 CAN 分析仪验证收发。
3. 再写协议编解码，先用固定样例帧测试。
4. 再创建电机任务，先只识别反馈和发送零输出。
5. 最后加入运动控制和上层业务。

