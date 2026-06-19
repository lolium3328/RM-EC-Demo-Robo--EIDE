# 蜂鸣器 PWM 配置与烧录后三声提示计划

## Summary

目标是在 RoboMaster 开发板 C 型的板载无源蜂鸣器上实现启动提示音：程序烧录并复位运行后，蜂鸣器以 4000 Hz PWM 响三声。

当前工程 CAN 已占用 `PD0/PD1`。蜂鸣器按开发板手册原理图使用 `TIM4_CH3`，本工程推荐配置到 `PD14`，不与当前 CAN 引脚冲突。

注意：“烧录成功后响三声”的实际触发点是程序复位并成功运行到 FreeRTOS 任务后发声，不是 OpenOCD 烧录工具直接触发。

## CubeMX 配置方法

在 `bsp/CubeMX/STM32F407IGHx/CubeMXProject_STM32F407IGHx.ioc` 中配置：

- 打开 CubeMX，找到 `PD14` 引脚，配置为 `TIM4_CH3`。
- 在左侧 `Timers -> TIM4` 中启用 `Channel3`，模式选择 `PWM Generation CH3`。
- `Clock Source` 选择 `Internal Clock`。
- 其他通道保持 `Disable`。
- `Slave Mode` 和 `Trigger Source` 保持 `Disable`。

当前 APB1 Timer Clock 是 `84 MHz`。为了输出约 `4000 Hz`：

```text
Prescaler = 83
Counter Period = 249
PWM mode = PWM mode 1
```

频率计算：

```text
84 MHz / (83 + 1) / (249 + 1) = 4000 Hz
```
推荐 PWM Channel 3 参数：

```text
Mode = PWM mode 1
Output compare preload = Disable
Fast Mode = Disable
CH Polarity = High
```

推荐 GPIO 参数：

```text
GPIO mode = Alternate Function Push Pull
GPIO Pull-up/Pull-down = No pull-up and no pull-down
Maximum output speed = Low 或 Medium
User Label = BUZZER_PWM
```

不需要配置：

```text
TIM4 NVIC interrupt
TIM4 DMA
```

因为启动提示音只需要 `HAL_TIM_PWM_Start()` 和 `HAL_TIM_PWM_Stop()`。

生成代码后确认新增或更新：

- `bsp/CubeMX/STM32F407IGHx/Inc/tim.h`
- `bsp/CubeMX/STM32F407IGHx/Src/tim.c`
- `bsp/CubeMX/STM32F407IGHx/Src/main.c` 包含 `#include "tim.h"`
- `main.c` 在 RTOS 前调用 `MX_TIM4_Init();`

`MX_TIM4_Init();` 应位于 `bsp_init_before_rtos_init();` 之前，例如：

```c
MX_GPIO_Init();
MX_USART6_UART_Init();
MX_CAN1_Init();
MX_TIM4_Init();

bsp_init_before_rtos_init();
```

## Implementation Changes

新增蜂鸣器封装，例如：

- `src/bsp/buzzer.hpp`
- `src/bsp/buzzer.cpp`

建议接口：

```cpp
namespace bsp::buzzer {

void init();
void on();
void off();
void set_pulse(uint32_t pulse);
uint32_t pulse();
void beep_blocking(uint32_t on_ms, uint32_t off_ms, uint8_t count);
void beep_blocking(uint32_t on_ms, uint32_t off_ms, uint8_t count, uint32_t pulse);

}
```

行为要求：

- `init()` 不重复初始化 TIM4，只确保蜂鸣器默认关闭，并把占空比设为默认值。
- `set_pulse()` 使用 `__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, pulse)` 修改 PWM 占空比。
- `pulse` 合法范围为 `0..249`，超过 `249` 时限制到 `249`。
- 默认 `pulse = 125`，对应约 50% 占空比。
- `on()` 调用 `HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3)`。
- `off()` 调用 `HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_3)`。
- `beep_blocking(on_ms, off_ms, count)` 使用当前 pulse。
- `beep_blocking(on_ms, off_ms, count, pulse)` 先设置指定 pulse，再响指定次数。

在当前 `task_demo` 中加入启动提示测试：

```cpp
// BUZZER BOOT TEST BEGIN
bsp::buzzer::init();
bsp::buzzer::beep_blocking(100, 100, 3, 125);
// BUZZER BOOT TEST END
```

蜂鸣器测试代码必须使用明显注释包住，方便后续删除：

```cpp
// BUZZER BOOT TEST BEGIN
...
// BUZZER BOOT TEST END
```

## Test Plan

编译验证：

- EIDE Build 不应出现 `tim.h` 找不到。
- 不应出现 `htim4` 未定义。
- 不应出现 `HAL_TIM_PWM_Start` 或 `HAL_TIM_PWM_Stop` 未定义。
- 不应出现 `bsp::buzzer` 未定义。

烧录验证：

- ST-Link 烧录日志出现：

```text
** Programming Finished **
** Verified OK **
```

- 板子复位运行后，蜂鸣器应响三声。

调试验证：

- 如果无声，先确认程序进入了 `task_demo`。
- 确认 `MX_TIM4_Init()` 已在 `bsp_init_before_rtos_init()` 之前执行。
- 用示波器或逻辑分析仪测 `PD14`，应看到约 `4000 Hz` PWM。
- 修改 `beep_blocking(..., pulse)` 的 pulse 参数后，`PD14` 的占空比应随之变化。

回归验证：

- 蜂鸣器响完后，现有 CAN 测试仍每秒发送 `ID 0x200`。
- CAN 启动诊断仍为 `started == true`，`filter_status == 0`，`start_status == 0`，`notification_status == 0`。

## Assumptions

- 按 RoboMaster C 型开发板手册，蜂鸣器输入连接到 `TIM4_CH3`，本工程选择 `PD14`。
- 蜂鸣器是无源蜂鸣器，必须用 PWM 驱动，不能只拉高 GPIO。
- 启动提示音暂时使用阻塞式 `osDelay()`，因为这是启动验证功能，不影响后续正式控制逻辑。
- 后续如果需要运行时多种提示音，应改成非阻塞状态机或独立蜂鸣器任务。
