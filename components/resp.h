/**
 * @file    resp.h
 * @brief   响应层：回复文本输出与组合式回复
 * @author  sdadz-luo
 */

#ifndef RESP_H
#define RESP_H

#include <stdbool.h>

#include "app_types.h"

/* 发送一行回复：串口 + 屏幕系统消息通道（阶段 7 增 WiFi 分流） */
void resp_send(const char *text);

/* 组合式回复：前缀 × 主体 × 跨通道补语 × 后缀（随机选取，见 STAGE2_DESIGN）；
 * executed=false 时按执行层实时状态说明命令未生效；
 * input 为用户原文（可为 NULL），供屏幕对话区显示「你> / 板>」 */
void resp_compose(intent_t intent, bool executed, const char *input);

#endif /* RESP_H */
