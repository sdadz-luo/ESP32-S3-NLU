/**
 * @file    input_key.c
 * @brief   按键任务（骨架占位）
 * @author  sdadz-luo
 */

#include "input_key.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 骨架占位：XL9555 轮询与按键映射（KEY0 报警 / KEY1 全停 / KEY2 开灯 / KEY3 关灯）后续接入 */
void task_key(void *arg)
{
    (void)arg;
    vTaskDelay(portMAX_DELAY);
}
