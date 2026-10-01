/**
 * @file    app_queues.c
 * @brief   队列创建（app_queues_init 的声明见 app.h）
 * @author  sdadz-luo
 */

#include <assert.h>

#include "app.h"
#include "app_queues.h"
#include "app_types.h"

QueueHandle_t g_cmd_q = NULL;
QueueHandle_t g_rhythm_q = NULL;

void app_queues_init(void)
{
    g_cmd_q = xQueueCreate(8, sizeof(cmd_msg_t));
    g_rhythm_q = xQueueCreate(4, sizeof(rhythm_cmd_t));

    /* 创建失败即堆内存不足，属系统级错误 */
    assert(g_cmd_q != NULL && g_rhythm_q != NULL);
}
