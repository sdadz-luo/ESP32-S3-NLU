/**
 * @file    input_serial.c
 * @brief   串口输入任务（骨架占位）
 * @author  sdadz-luo
 */

#include "input_serial.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 骨架占位：fgets 读行与投递 g_cmd_q 在「理解与响应」步骤接入 */
void task_serial(void *arg)
{
    (void)arg;
    vTaskDelay(portMAX_DELAY); /* 永久阻塞，不占 CPU */
}
