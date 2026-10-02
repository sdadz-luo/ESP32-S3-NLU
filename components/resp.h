/**
 * @file    resp.h
 * @brief   响应层：回复文本输出与组合式回复
 * @author  sdadz-luo
 */

#ifndef RESP_H
#define RESP_H

#include <stdbool.h>

#include "app_types.h"

/* 发送一行回复（阶段 1 = 串口 stdout；阶段 7 按来源通道分流） */
void resp_send(const char *text);

/* 组合式回复：前缀 × 主体 × 跨通道补语 × 后缀（随机选取）；
 * executed=false 时按执行层实时状态说明命令未生效（见 docs/STAGE2_DESIGN.md） */
void resp_compose(intent_t intent, bool executed);

#endif /* RESP_H */
