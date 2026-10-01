/**
 * @file    tools.c
 * @brief   执行层工具函数实现
 * @author  sdadz-luo
 */

#include "tools.h"

#include "app_queues.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static esp_err_t rhythm_send(uint8_t target, uint8_t mode)
{
    rhythm_cmd_t cmd = { .target = target, .mode = mode };

    return (xQueueSend(g_rhythm_q, &cmd, 0) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t tool_led_set(led_mode_t mode)
{
    return rhythm_send(RHYTHM_LED, (uint8_t)mode);
}

esp_err_t tool_beep_set(beep_mode_t mode)
{
    return rhythm_send(RHYTHM_BEEP, (uint8_t)mode);
}

esp_err_t tool_stop_all(void)
{
    esp_err_t err_led = tool_led_set(LED_OFF);
    esp_err_t err_beep = tool_beep_set(BEEP_OFF);

    return (err_led == ESP_OK && err_beep == ESP_OK) ? ESP_OK : ESP_ERR_TIMEOUT;
}
