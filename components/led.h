/**
 * @file    led.h
 * @brief   LED 驱动接口（GPIO1，低电平点亮）
 * @author  sdadz-luo
 *
 * 移植自正点原子 DNESP32S3 配套例程 08_iic_exio 的 components/BSP；
 * 版权归属与来源声明见仓库 README「第三方代码来源」。
 */

#ifndef __LED_H_
#define __LED_H_

#include "driver/gpio.h"


/* 引脚定义 */
#define LED_GPIO_PIN    GPIO_NUM_1  /* LED连接的GPIO端口 */

/* 引脚的输出的电平状态 */
enum GPIO_OUTPUT_STATE
{
    PIN_RESET,
    PIN_SET
};

/* 函数声明*/
void led_init(void);    /* 初始化LED */
void led_on(void);     /* 打开LED */
void led_off(void);    /* 关闭LED */
void led_toggle(void);  /* LED翻转 */

#endif
