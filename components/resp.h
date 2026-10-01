/**
 * @file    resp.h
 * @brief   响应层：回复文本输出与意图回复映射
 * @author  sdadz-luo
 */

#ifndef RESP_H
#define RESP_H

#include "app_types.h"

/* 发送一行回复（阶段 1 = 串口 stdout；阶段 7 按来源通道分流） */
void resp_send(const char *text);

/* 意图 → 固定回复文本（阶段 2 升级为模板组合） */
const char *resp_for_intent(intent_t intent);

#endif /* RESP_H */
