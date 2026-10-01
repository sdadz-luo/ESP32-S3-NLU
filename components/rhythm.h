/**
 * @file    rhythm.h
 * @brief   节奏任务：执行层唯一汇点
 * @author  sdadz-luo
 */

#ifndef RHYTHM_H
#define RHYTHM_H

/* 维护 LED / 蜂鸣器两路模式与下次翻转时刻；阻塞在 g_rhythm_q 上，
 * 超时到点翻转、收到命令立即应用新模式基态。
 * 优先级 8，栈 3072，绑 Core 1（机制详见 docs/STAGE1_DESIGN.md 节奏引擎）。 */
void task_rhythm(void *arg);

#endif /* RHYTHM_H */
