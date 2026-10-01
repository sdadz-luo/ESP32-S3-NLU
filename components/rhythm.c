/**
 * @file    rhythm.c
 * @brief   节奏任务（骨架占位）
 * @author  sdadz-luo
 */

#include "rhythm.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 骨架占位：节拍引擎与物理输出（LED GPIO1 / XL9555 蜂鸣器）在「执行层」步骤接入 */
void task_rhythm(void *arg)
{
    (void)arg;
    vTaskDelay(portMAX_DELAY);
}
