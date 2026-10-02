/**
 * @file    input_key.c
 * @brief   按键任务：轮询 XL9555 → 投屏幕控制命令（清屏 / 滚动）
 * @author  sdadz-luo
 *
 * 映射：KEY0 清屏 / KEY1 下滚 / KEY2 回最新 / KEY3 上滚。
 * 长按连滚：驱动连按模式下按住会持续返回键值，本任务按 KEY_REPEAT_MS
 * 节流——首按立即响应，之后每档一行。
 */

#include "input_key.h"

#include "ui.h"
#include "xl9555.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "key";

#define KEY_REPEAT_MS   150     /* 长按连滚的重复间隔 */

static void act(uint8_t key)
{
    switch (key) {
    case KEY0_PRES:
        ESP_LOGI(TAG, "KEY0 -> 清屏");
        ui_post_cmd(UI_CMD_CLEAR);
        break;
    case KEY1_PRES:
        ESP_LOGI(TAG, "KEY1 -> 下滚");
        ui_post_cmd(UI_CMD_SCROLL_DOWN);
        break;
    case KEY2_PRES:
        ESP_LOGI(TAG, "KEY2 -> 回最新");
        ui_post_cmd(UI_CMD_SCROLL_BOTTOM);
        break;
    case KEY3_PRES:
        ESP_LOGI(TAG, "KEY3 -> 上滚");
        ui_post_cmd(UI_CMD_SCROLL_UP);
        break;
    default:
        break;
    }
}

void task_key(void *arg)
{
    (void)arg;
    uint8_t    last = 0;        /* 最近一次已处理的键值（0 = 松开） */
    TickType_t last_tick = 0;

    for (;;) {
        uint8_t key = xl9555_key_scan(1);   /* mode=1：按住时持续返回键值 */

        if (key == 0) {
            last = 0;                       /* 松开后下次按下立即响应 */
        } else {
            TickType_t now = xTaskGetTickCount();

            if (key != last || (now - last_tick) >= pdMS_TO_TICKS(KEY_REPEAT_MS)) {
                act(key);
                last = key;
                last_tick = now;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
