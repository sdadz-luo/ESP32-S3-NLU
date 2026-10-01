/**
 * @file    nlu.h
 * @brief   理解层：任务循环与关键词规则
 * @author  sdadz-luo
 */

#ifndef NLU_H
#define NLU_H

#include "app_types.h"

/* 从 g_cmd_q 取命令 → 理解 → 调工具 → 回复；优先级 5，栈 4096，绑 Core 1 */
void task_nlu(void *arg);

/* 关键词规则理解（阶段 6 由端侧模型替换，规则降级为兜底） */
intent_t nlu_understand(const char *text);

#endif /* NLU_H */
