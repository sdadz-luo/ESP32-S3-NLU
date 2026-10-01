/**
 * @file    main.c
 * @brief   程序入口：仅保留启动序列，应用代码在 components/ 下
 * @author  sdadz-luo
 *
 * 目标芯片 ESP32-S3（正点原子 DNESP32S3），ESP-IDF v6.1。
 */

#include "app.h"
#include "resp.h"

void app_main(void)
{
    app_hw_init();      /* LED / IIC / XL9555，单线程阶段完成 */
    app_queues_init();  /* cmd_q / rhythm_q */
    app_tasks_start();  /* rhythm → nlu → key → serial，先汇点后生产者 */
    resp_send("就绪：输入命令，或按 KEY0~3 调试");

    /* 返回后 main 任务自动删除；禁止 while(1) 忙等（会饿死 IDLE0 触发看门狗） */
}
