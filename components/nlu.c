/**
 * @file    nlu.c
 * @brief   理解层：任务循环与关键词规则（骨架占位）
 * @author  sdadz-luo
 */

#include "nlu.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 骨架占位：消费 g_cmd_q 的循环与规则匹配在「理解与响应」步骤接入 */
void task_nlu(void *arg)
{
    (void)arg;
    vTaskDelay(portMAX_DELAY);
}

intent_t nlu_understand(const char *text)
{
    (void)text;
    return I_UNKNOWN;
}
