/**
 * @file    app.h
 * @brief   应用启动接口（main.c 的调用面）
 * @author  sdadz-luo
 */

#ifndef APP_H
#define APP_H

/* 启动序列：main.c 按序调用 */
void app_hw_init(void);     /* LED / IIC / XL9555 初始化（单线程阶段完成） */
void app_queues_init(void); /* 创建 cmd_q 与 rhythm_q */
void app_tasks_start(void); /* 创建任务：rhythm → nlu → key → serial */

#endif /* APP_H */
