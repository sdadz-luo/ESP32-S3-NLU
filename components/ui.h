/**
 * @file    ui.h
 * @brief   屏幕 UI：ui_q 与显示任务（状态区 + 对话区）
 * @author  sdadz-luo
 *
 * 布局与决策见 docs/STAGE3_DESIGN.md；状态区轮询节奏任务的状态快照。
 * 按键通道经 ui_post_cmd 投屏幕命令（清屏 / 上滚 / 下滚 / 回最新）。
 */

#ifndef UI_H
#define UI_H

/* ui_q 消息类型：TALK 为对话消息（ui_post 产生），其余为屏幕命令 */
typedef enum {
    UI_CMD_TALK = 0,
    UI_CMD_SCROLL_UP,       /* 视口上滚一行（看更旧内容） */
    UI_CMD_SCROLL_DOWN,     /* 视口下滚一行 */
    UI_CMD_SCROLL_BOTTOM,   /* 跳回最新 */
    UI_CMD_CLEAR,           /* 清空对话区（历史一并清掉） */
} ui_cmd_t;

/* 创建 ui_q（须在任务创建前调用） */
void ui_init(void);

/* 投递一条交互（in 为空视为系统消息）；队列满则丢弃 + 告警，不阻塞调用者 */
void ui_post(const char *in, const char *out);

/* 投递屏幕控制命令（不可传 UI_CMD_TALK）；队列满则丢弃 + 告警 */
void ui_post_cmd(ui_cmd_t cmd);

/* 显示任务：优先级 3，栈 4096，绑 Core 1 */
void task_display(void *arg);

#endif /* UI_H */
