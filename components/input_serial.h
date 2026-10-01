/**
 * @file    input_serial.h
 * @brief   串口输入任务
 * @author  sdadz-luo
 */

#ifndef INPUT_SERIAL_H
#define INPUT_SERIAL_H

/* 读一行文本 → g_cmd_q；优先级 4，栈 3072，绑 Core 1 */
void task_serial(void *arg);

#endif /* INPUT_SERIAL_H */
