# 蜂鸣器启动三声验证总结

## 当前实现

- TIM4 已由 CubeMX 配置为 `PD14 -> TIM4_CH3`，PWM 频率约 `4000 Hz`。
- `TIM4` 初始 `Pulse = 0`，系统上电后默认静音。
- `src/bsp/buzzer.hpp` 和 `src/bsp/buzzer.cpp` 提供蜂鸣器封装。
- `bsp::buzzer::set_pulse(uint32_t pulse)` 可动态修改占空比。
- `pulse` 有效范围为 `0..249`，超过 `249` 会自动限幅到 `249`。
- `task_demo` 启动时会执行一次三声提示：

```cpp
// BUZZER BOOT TEST BEGIN
bsp::buzzer::init();
bsp::buzzer::beep_blocking(100, 100, 3, bsp::buzzer::kDefaultPulse);
// BUZZER BOOT TEST END
```

## 编译验证

在 EIDE 中执行 Build。

通过标准：

- 不出现 `tim.h: No such file or directory`。
- 不出现 `htim4` 未定义。
- 不出现 `HAL_TIM_PWM_Start` 或 `HAL_TIM_PWM_Stop` 未定义。
- 不出现 `bsp::buzzer` 未定义。
- 最终生成 `.elf`。

## 烧录验证

使用 ST-Link 烧录。

通过标准：

```text
** Programming Finished **
** Verify Started **
** Verified OK **
```

烧录完成并复位运行后，蜂鸣器应响三声。每声约 `100 ms`，间隔约 `100 ms`。

## 波形验证

如果听不到声音，用示波器或逻辑分析仪测量 `PD14`。

通过标准：

- 蜂鸣器发声期间，`PD14` 有约 `4000 Hz` PWM。
- 默认 `pulse = 125` 时，占空比约 `50%`。
- 调整 `beep_blocking(..., pulse)` 或 `set_pulse(pulse)` 后，占空比随 pulse 变化。

## 调试器验证

在 `task_demo` 开头或 `bsp::buzzer::beep_blocking()` 内打断点。

检查：

- 程序进入 `task_demo`。
- `MX_TIM4_Init()` 已在 `bsp_init_before_rtos_init()` 之前执行。
- `bsp::buzzer::pulse()` 返回当前设置值。

## 回归验证

蜂鸣器三声提示结束后，现有 CAN 测试仍应继续运行。

通过标准：

- USB-CAN 分析仪每秒看到一帧 `ID 0x200`。
- `bus::can::start()` 和 `bus::can::is_started()` 均表明 CAN 已成功启动。

## 后续删除方法

如果后续不再需要启动蜂鸣器测试，只删除 `task_demo` 中：

```cpp
// BUZZER BOOT TEST BEGIN
...
// BUZZER BOOT TEST END
```

保留 `src/bsp/buzzer.*` 可作为正式蜂鸣器驱动继续使用。
