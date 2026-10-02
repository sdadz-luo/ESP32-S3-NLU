/**
 * @file    ui.h
 * @brief   屏幕 UI：ui_q 与显示任务（状态区 + 对话区）
 * @author  sdadz-luo
 *
 * 布局与决策见 docs/STAGE3_DESIGN.md；状态区轮询节奏任务的状态快照。
 */

#ifndef UI_H
#define UI_H

/* 创建 ui_q（须在任务创建前调用） */
void ui_init(void);

/* 投递一条交互（in 为空视为系统消息）；队列满则丢弃 + 告警，不阻塞调用者 */
void ui_post(const char *in, const char *out);

/* 显示任务：优先级 3，栈 4096，绑 Core 1 */
void task_display(void *arg);

#endif /* UI_H */
