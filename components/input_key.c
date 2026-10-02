/**
 * @file    input_key.c
 * @brief   按键任务：轮询 XL9555 → 直调工具层（调试通道，绕过理解层）
 * @author  sdadz-luo
 *
 * 映射：KEY0 报警 / KEY1 全停 / KEY2 开灯 / KEY3 关灯
 * （见 docs/STAGE1_DESIGN.md 验收清单）。
 */

#include "input_key.h"

#include "tools.h"
#include "xl9555.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "key";

static void report(const char *action, esp_err_t err)
{
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s", action);
    } else {
        ESP_LOGW(TAG, "%s 下发失败: %s", action, esp_err_to_name(err));
    }
}

void task_key(void *arg)
{
    (void)arg;

    for (;;) {
        switch (xl9555_key_scan(0)) {   /* mode=0 不连按；内含 10ms 去抖 */
        case KEY0_PRES:
            report("KEY0 -> 报警", tool_beep_set(BEEP_ALARM));
            break;
        case KEY1_PRES:
            report("KEY1 -> 全停", tool_stop_all());
            break;
        case KEY2_PRES:
            report("KEY2 -> 开灯", tool_led_set(LED_ON));
            break;
        case KEY3_PRES:
            report("KEY3 -> 关灯", tool_led_set(LED_OFF));
            break;
        default:
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
