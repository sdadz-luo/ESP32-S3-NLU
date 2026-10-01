/**
 * @file    input_key.h
 * @brief   按键任务（调试通道）
 * @author  sdadz-luo
 */

#ifndef INPUT_KEY_H
#define INPUT_KEY_H

/* 轮询 XL9555 按键 → 直调工具层（绕过理解层）；优先级 6，栈 3072，绑 Core 1 */
void task_key(void *arg);

#endif /* INPUT_KEY_H */
