/**
 * @file    input_serial.c
 * @brief   串口输入任务：fgets 读一行 → g_cmd_q
 * @author  sdadz-luo
 */

#include "input_serial.h"

#include "app_queues.h"
#include "app_types.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "serial";

void task_serial(void *arg)
{
    (void)arg;
    char line[128];

    for (;;) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            /* 读错误 / EOF：清标志后稍候重试，避免忙循环 */
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        line[strcspn(line, "\r\n")] = '\0';     /* 去行尾换行 */
        if (line[0] == '\0') {
            continue;                           /* 空行忽略 */
        }

        cmd_msg_t msg = {0};
        snprintf(msg.text, sizeof(msg.text), "%s", line);
        msg.src = SRC_SERIAL;

        if (xQueueSend(g_cmd_q, &msg, 0) != pdTRUE) {
            ESP_LOGW(TAG, "命令队列满，丢弃: %s", msg.text);
        }
    }
}
