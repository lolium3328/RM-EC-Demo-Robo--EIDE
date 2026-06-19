#include <task_demo.hpp>

#include "bus/can_bus.hpp"
#include "cubemx_inc/bsp_hooks/general.h"
#include "main.h"

extern "C" void bsp_init_before_rtos_init()
{
    if (!bus::can::init() || !bus::can::start()) { Error_Handler(); }
}

extern "C" void bsp_init_after_rtos_init() { osThreadNew(task_demo, nullptr, nullptr); }

extern "C" void bsp_init_in_rtos_thread() {}
