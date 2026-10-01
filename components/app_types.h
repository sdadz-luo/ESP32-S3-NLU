/**
 * @file    app_types.h
 * @brief   应用层公共类型：设备模式、意图、队列元素
 * @author  sdadz-luo
 */

#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>

/* 执行层：LED 模式 */
typedef enum {
    LED_OFF = 0,        /* 常灭 */
    LED_ON,             /* 常亮 */
    LED_BLINK,          /* 5Hz 闪烁（翻转间隔 100ms） */
} led_mode_t;

/* 执行层：蜂鸣器模式 */
typedef enum {
    BEEP_OFF = 0,       /* 停止 */
    BEEP_BREATH,        /* 0.5Hz 呼吸（翻转间隔 1000ms） */
    BEEP_ALARM,         /* 2Hz 报警（翻转间隔 250ms） */
} beep_mode_t;

/* 节奏通道标识（rhythm_cmd_t.target） */
typedef enum {
    RHYTHM_LED = 0,
    RHYTHM_BEEP,
} rhythm_target_t;

/* 理解层：意图 */
typedef enum {
    I_UNKNOWN = 0,
    I_LED_ON,
    I_LED_OFF,
    I_LED_BLINK,
    I_BEEP_BREATH,
    I_BEEP_ALARM,
    I_BEEP_OFF,
    I_STOP,             /* 停止类命令：全部停止 */
} intent_t;

/* 命令来源（cmd_msg_t.src） */
typedef enum {
    SRC_SERIAL = 0,
    SRC_WIFI,           /* 阶段 7 启用 */
} cmd_source_t;

/* cmd_q 元素：输入层 → 理解层 */
typedef struct {
    char    text[128];  /* UTF-8 一行，不含换行 */
    uint8_t src;        /* cmd_source_t */
} cmd_msg_t;

/* rhythm_q 元素：工具层 → 节奏任务 */
typedef struct {
    uint8_t target;     /* rhythm_target_t */
    uint8_t mode;       /* led_mode_t 或 beep_mode_t，按 target 解释 */
} rhythm_cmd_t;

#endif /* APP_TYPES_H */
