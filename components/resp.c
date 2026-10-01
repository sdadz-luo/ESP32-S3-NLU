/**
 * @file    resp.c
 * @brief   响应层实现
 * @author  sdadz-luo
 */

#include "resp.h"

#include <stdio.h>

void resp_send(const char *text)
{
    printf("%s\n", text); /* 阶段 1：直接写 stdout（UART0 console） */
}

const char *resp_for_intent(intent_t intent)
{
    switch (intent) {
    case I_LED_ON:      return "灯已点亮";
    case I_LED_OFF:     return "灯已关闭";
    case I_LED_BLINK:   return "灯开始闪烁";
    case I_BEEP_BREATH: return "已开启呼吸提示音";
    case I_BEEP_ALARM:  return "已开启报警提示音";
    case I_BEEP_OFF:    return "蜂鸣器已关闭";
    case I_STOP:        return "已全部停止";
    default:            return "抱歉，我没听懂";
    }
}
