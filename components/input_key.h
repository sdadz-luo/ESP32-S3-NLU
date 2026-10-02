/**
 * @file    input_key.h
 * @brief   按键任务（屏幕控制：清屏 / 滚动 / 回最新）
 * @author  sdadz-luo
 */

#ifndef INPUT_KEY_H
#define INPUT_KEY_H

/* 轮询 XL9555 按键 → 投 ui_q 屏幕命令；优先级 6，栈 3072，绑 Core 1 */
void task_key(void *arg);

#endif /* INPUT_KEY_H */
