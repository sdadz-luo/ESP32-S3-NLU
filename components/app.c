/**
 * @file    app.c
 * @brief   应用启动序列的两端：硬件初始化与任务创建
 * @author  sdadz-luo
 *
 * 目标芯片 ESP32-S3（正点原子 DNESP32S3），ESP-IDF v6.1。
 * 任务定版参数见 docs/STAGE1_DESIGN.md（优先级 8/6/5/4，全部绑 Core 1）。
 */

#include <assert.h>

#include "app.h"
#include "input_key.h"
#include "input_serial.h"
#include "nlu.h"
#include "rhythm.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_hw_init(void)
{
    /* 骨架占位：LED / IIC / XL9555 初始化在 BSP 移植后接入 */
}

void app_tasks_start(void)
{
    /* 先建消费者/执行汇点，后建生产者：任何首次投递发生时下游都已存在 */
    assert(xTaskCreatePinnedToCore(task_rhythm, "rhythm", 3072, NULL, 8, NULL, 1) == pdPASS);
    assert(xTaskCreatePinnedToCore(task_nlu,    "nlu",    4096, NULL, 5, NULL, 1) == pdPASS);
    assert(xTaskCreatePinnedToCore(task_key,    "key",    3072, NULL, 6, NULL, 1) == pdPASS);
    assert(xTaskCreatePinnedToCore(task_serial, "serial", 3072, NULL, 4, NULL, 1) == pdPASS);
}
