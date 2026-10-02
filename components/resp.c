/**
 * @file    resp.c
 * @brief   响应层：组合式回复（前缀 × 主体 × 跨通道补语 × 后缀）
 * @author  sdadz-luo
 *
 * 结构与决策见 docs/STAGE2_DESIGN.md：状态取自执行层快照
 * （rhythm_led_mode / rhythm_beep_mode），模板池全静态、无动态分配，
 * 随机源为硬件 RNG esp_random()。词池措辞为首版，阶段 3 做屏幕时再精化。
 */

#include "resp.h"

#include "rhythm.h"
#include "ui.h"

#include "esp_random.h"

#include <stdio.h>

/* 统一出口：串口一行 + 屏幕一条 UI 消息（in 为空表示系统消息） */
static void out_line(const char *in, const char *text)
{
    printf("%s\n", text); /* 串口 stdout（UART0 console） */
    ui_post(in, text);
}

void resp_send(const char *text)
{
    out_line(NULL, text);
}

/* ---- 前后缀池（NULL 结尾，pool_pick 计数）---- */
static const char *const PRE[] = { "", "好的，", "收到，", "没问题，", "好嘞，", "明白，", "让我看看...", NULL };
static const char *const SUF[] = { "。", "，还需要什么吗？", "，随时吩咐。", "，搞定。", "，还要调整吗？", "，随时听候差遣。", NULL };

/* ---- 主体模板池 ---- */
static const char *const BODY_LED_ON[] = {
    "灯已点亮", "灯切换为常亮模式", "LED 已经亮起来了", NULL,
};
static const char *const BODY_LED_OFF[] = {
    "灯已关闭", "灯已熄灭", "LED 已关掉", NULL,
};
static const char *const BODY_LED_BLINK[] = {
    "灯开始闪烁", "灯切换为 5Hz 闪烁模式", "LED 闪起来了", "灯已闪烁", NULL,
};
static const char *const BODY_BEEP_BREATH[] = {
    "已开启呼吸提示音", "蜂鸣器开始 0.5Hz 呼吸", "呼吸提示音已打开", NULL,
};
static const char *const BODY_BEEP_ALARM[] = {
    "已开启报警提示音", "蜂鸣器进入 2Hz 报警", "报警声已响起", NULL,
};
static const char *const BODY_BEEP_OFF[] = {
    "蜂鸣器已关闭", "蜂鸣器已停止", "提示音已停下", NULL,
};
static const char *const BODY_STOP[] = {
    "已全部停止", "灯和蜂鸣器都停了", "全部设备已停下", NULL,
};
static const char *const BODY_UNKNOWN[] = {
    "抱歉，我没听懂", "这句我还没学会", "暂时理解不了这个说法", NULL,
};

/* 池内随机取一条；池规模小，取模偏差可忽略 */
static const char *pool_pick(const char *const *pool)
{
    size_t n = 0;

    while (pool[n] != NULL) {
        n++;
    }

    return pool[esp_random() % n];
}

static const char *const *body_pool(intent_t intent)
{
    switch (intent) {
    case I_LED_ON:      return BODY_LED_ON;
    case I_LED_OFF:     return BODY_LED_OFF;
    case I_LED_BLINK:   return BODY_LED_BLINK;
    case I_BEEP_BREATH: return BODY_BEEP_BREATH;
    case I_BEEP_ALARM:  return BODY_BEEP_ALARM;
    case I_BEEP_OFF:    return BODY_BEEP_OFF;
    case I_STOP:        return BODY_STOP;
    default:            return BODY_UNKNOWN;
    }
}

/* 模式快照 → 中文描述（失败路径与跨通道补语共用） */
static const char *led_state_text(led_mode_t mode)
{
    switch (mode) {
    case LED_ON:    return "常亮";
    case LED_BLINK: return "闪烁（5Hz）";
    default:        return "熄灭";
    }
}

static const char *beep_state_text(beep_mode_t mode)
{
    switch (mode) {
    case BEEP_BREATH: return "呼吸提示（0.5Hz）";
    case BEEP_ALARM:  return "报警（2Hz）";
    default:          return "停止";
    }
}

/* 跨通道补语：命令作用于一个设备、另一设备非 OFF 时补一句实时状态，否则 NULL */
static const char *cross_clause(intent_t intent)
{
    switch (intent) {
    case I_LED_ON:
    case I_LED_OFF:
    case I_LED_BLINK:
        switch (rhythm_beep_mode()) {
        case BEEP_ALARM:  return "（蜂鸣器仍在报警中）";
        case BEEP_BREATH: return "（蜂鸣器仍在呼吸提示中）";
        default:          return NULL;
        }
    case I_BEEP_BREATH:
    case I_BEEP_ALARM:
    case I_BEEP_OFF:
        switch (rhythm_led_mode()) {
        case LED_ON:    return "（灯仍亮着）";
        case LED_BLINK: return "（灯仍在闪烁）";
        default:        return NULL;
        }
    default:
        return NULL;
    }
}

/* 失败路径：命令未送达执行层，按实时状态说明「没有生效」，不谎报 */
static void compose_failed(intent_t intent, char *buf, size_t size)
{
    switch (intent) {
    case I_LED_ON:
    case I_LED_OFF:
    case I_LED_BLINK:
        snprintf(buf, size, "命令没送达，灯仍是「%s」状态", led_state_text(rhythm_led_mode()));
        break;
    case I_BEEP_BREATH:
    case I_BEEP_ALARM:
    case I_BEEP_OFF:
        snprintf(buf, size, "命令没送达，蜂鸣器仍是「%s」状态", beep_state_text(rhythm_beep_mode()));
        break;
    case I_STOP:
        snprintf(buf, size, "命令没送达，设备状态未变（灯%s，蜂鸣器%s）",
                 led_state_text(rhythm_led_mode()), beep_state_text(rhythm_beep_mode()));
        break;
    default:
        snprintf(buf, size, "内部错误");
        break;
    }
}

void resp_compose(intent_t intent, bool executed, const char *input)
{
    char buf[192];

    if (!executed) {
        compose_failed(intent, buf, sizeof(buf));
        out_line(input, buf);
        return;
    }

    const char *clause = cross_clause(intent);
    snprintf(buf, sizeof(buf), "%s%s%s%s",
             pool_pick(PRE), pool_pick(body_pool(intent)),
             clause != NULL ? clause : "", pool_pick(SUF));
    out_line(input, buf);
}
