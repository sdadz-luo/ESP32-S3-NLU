/**
 * @file    app.c
 * @brief   应用启动序列的两端：硬件初始化与任务创建
 * @author  sdadz-luo
 *
 * 目标芯片 ESP32-S3（正点原子 DNESP32S3），ESP-IDF v6.1。
 * 任务定版参数见 docs/STAGE1_DESIGN.md（优先级 8/6/5/4，全部绑 Core 1）。
 */

#include <assert.h>

#include "app.h"
#include "font16.h"
#include "iic.h"
#include "input_key.h"
#include "input_serial.h"
#include "lcd.h"
#include "led.h"
#include "nlu.h"
#include "rhythm.h"
#include "spi.h"
#include "ui.h"
#include "xl9555.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app";

void app_hw_init(void)
{
    led_init();                             /* LED（GPIO1，低电平点亮） */

    i2c_obj_t i2c0 = iic_init(I2C_NUM_0);   /* I2C0（GPIO41/42，400kHz，legacy 驱动） */
    xl9555_init(i2c0);                      /* XL9555：配置 IO 方向、蜂鸣器置停 */

    /* 上电自检：读 XL9555 输入寄存器验证 I2C 读写通路
     * （P1.4~P1.7 为按键，未按时为 1，其余按 0xF003 配置） */
    uint8_t in[2] = {0};
    esp_err_t err = xl9555_read_byte(in, 2);
    ESP_LOGI(TAG, "XL9555 self-check: %s, input reg = 0x%04X",
             esp_err_to_name(err), (unsigned)(in[1] << 8 | in[0]));

    led_on();                                /* LED 亮 300ms：视觉自检 */
    vTaskDelay(pdMS_TO_TICKS(300));
    led_off();

    xl9555_pin_write(BEEP_IO, 0);           /* 蜂鸣器短鸣 150ms：验证 XL9555 写通路 */
    vTaskDelay(pdMS_TO_TICKS(150));
    xl9555_pin_write(BEEP_IO, 1);

    /* LCD：SPI2 总线 + ILI9341 屏。须在 xl9555_init 之后——GPIO40 会被
     * 从 XL9555 INT（输入）改配为 LCD DC（输出），见 docs/STAGE3_DESIGN.md */
    spi2_init();
    lcd_init();
    if (!font16_load()) {
        ESP_LOGW(TAG, "中文字库不可用，中文将显示为占位框（需刷入 data/font16.bin）");
    }
}

void app_tasks_start(void)
{
    /* 先建消费者/执行汇点，后建生产者：任何首次投递发生时下游都已存在。
     * 栈为 2026-10-02 实测校准（最小余量：rhythm 1968 / nlu 2684 / key 1832 /
     * serial 1724 / display 2400 B，要求 >=500 B）；nlu 余量留给阶段 6 推理 */
    assert(xTaskCreatePinnedToCore(task_rhythm,  "rhythm",  3072, NULL, 8, NULL, 1) == pdPASS);
    assert(xTaskCreatePinnedToCore(task_nlu,     "nlu",     4096, NULL, 5, NULL, 1) == pdPASS);
    assert(xTaskCreatePinnedToCore(task_key,     "key",     3072, NULL, 6, NULL, 1) == pdPASS);
    assert(xTaskCreatePinnedToCore(task_serial,  "serial",  3072, NULL, 4, NULL, 1) == pdPASS);
    assert(xTaskCreatePinnedToCore(task_display, "display", 4096, NULL, 3, NULL, 1) == pdPASS);
}
