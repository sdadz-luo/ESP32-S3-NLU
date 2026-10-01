/**
 * @file    app_queues.h
 * @brief   命令队列与节奏队列的句柄
 * @author  sdadz-luo
 */

#ifndef APP_QUEUES_H
#define APP_QUEUES_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

extern QueueHandle_t g_cmd_q;    /* 输入层 → 理解层，元素 cmd_msg_t，深度 8 */
extern QueueHandle_t g_rhythm_q; /* 工具层 → 节奏任务，元素 rhythm_cmd_t，深度 4 */

#endif /* APP_QUEUES_H */
