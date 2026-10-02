/**
 * @file    spi.h
 * @brief   SPI2 主机驱动接口（引脚定义与收发函数）
 * @author  sdadz-luo
 *
 * 移植自正点原子 DNESP32S3 配套例程 10_spilcd 的 components/BSP；
 * 版权归属与来源声明见仓库 README「第三方代码来源」。
 */

#ifndef __SPI_H
#define __SPI_H

#include <string.h>
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"


/* 引脚定义 */
#define SPI_MOSI_GPIO_PIN   GPIO_NUM_11         /* SPI2_MOSI */
#define SPI_CLK_GPIO_PIN    GPIO_NUM_12         /* SPI2_CLK */
#define SPI_MISO_GPIO_PIN   GPIO_NUM_13         /* SPI2_MISO */

/* 函数声明 */
void spi2_init(void);                                                               /* 初始化SPI2 */
void spi2_write_cmd(spi_device_handle_t handle, uint8_t cmd);                       /* SPI发送命令 */
void spi2_write_data(spi_device_handle_t handle, const uint8_t *data, int len);     /* SPI发送数据 */
uint8_t spi2_transfer_byte(spi_device_handle_t handle, uint8_t byte);               /* SPI处理数据 */
#endif
