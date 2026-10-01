/**
 * @file    tools.h
 * @brief   执行层工具函数：意图 → 节奏命令
 * @author  sdadz-luo
 */

#ifndef TOOLS_H
#define TOOLS_H

#include "esp_err.h"

#include "app_types.h"

/* 工具函数契约：只向 g_rhythm_q 投递命令后立即返回（不阻塞、不做物理输出）。
 * 全工程唯一直接操作 XL9555 输出的任务是节奏任务。 */
esp_err_t tool_led_set(led_mode_t mode);
esp_err_t tool_beep_set(beep_mode_t mode);
esp_err_t tool_stop_all(void); /* LED → OFF，蜂鸣器 → OFF */

#endif /* TOOLS_H */
