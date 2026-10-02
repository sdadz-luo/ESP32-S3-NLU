/**
 * @file    led.c
 * @brief   LED 驱动（GPIO1，低电平点亮）
 * @author  sdadz-luo
 *
 * 移植自正点原子 DNESP32S3 配套例程 08_iic_exio 的 components/BSP；
 * 版权归属与来源声明见仓库 README「第三方代码来源」。
 */

#include "led.h"

void led_on(void)
{
    gpio_set_level(LED_GPIO_PIN, PIN_RESET);  /* 低电平点亮 */
}

void led_off(void)
{
    gpio_set_level(LED_GPIO_PIN, PIN_SET);    /* 高电平熄灭 */
}

void led_toggle(void)
{
    gpio_set_level(LED_GPIO_PIN, !gpio_get_level(LED_GPIO_PIN));  /* LED翻转 */
}

void led_init(void)
{
    gpio_config_t gpio_init_struct = {0};

    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;         /* 失能引脚中断 */
    gpio_init_struct.mode = GPIO_MODE_INPUT_OUTPUT;         /* 输入输出模式 */
    gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;       /* 使能上拉 */
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;  /* 失能下拉 */
    gpio_init_struct.pin_bit_mask = 1ull << LED_GPIO_PIN;   /* 设置的引脚的位掩码 */
    gpio_config(&gpio_init_struct);                         /* 配置GPIO */

    led_off();                                                 /* 关闭LED */
}
