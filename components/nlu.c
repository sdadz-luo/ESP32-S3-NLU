/**
 * @file    nlu.c
 * @brief   理解层：cmd_q 消费循环 + 关键词规则（阶段 6 由模型替换）
 * @author  sdadz-luo
 *
 * 规则表与匹配优先级见 docs/STAGE1_DESIGN.md「行为规格」：按序 strstr
 * 子串匹配（UTF-8 安全）。阶段 1 占位：阶段 4 以语料为准精化，阶段 6
 * 由端侧模型替换、规则降级为兜底。
 */

#include "nlu.h"

#include "app_queues.h"
#include "app_types.h"
#include "resp.h"
#include "tools.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "nlu";

/* 关键词池：说法池可按需扩充（如「停止闪烁 / 别闪了」归 led_off） */
static const char *const W_STOP[]   = { "停止", "停下", "暂停", "全停", "别", "关闭" };
static const char *const W_LED[]    = { "灯", "LED", "闪" };
static const char *const W_BEEP[]   = { "响", "蜂鸣", "叫", "报警" };
static const char *const W_ALARM[]  = { "报警", "警报", "急促" };
static const char *const W_BREATH[] = { "呼吸", "慢速" };
static const char *const W_ON[]     = { "开", "亮", "点" };
static const char *const W_OFF[]    = { "关", "灭" };

static int has_any(const char *text, const char *const *pool, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (strstr(text, pool[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

#define HAS(text, pool)  has_any((text), (pool), sizeof(pool) / sizeof((pool)[0]))

intent_t nlu_understand(const char *text)
{
    int stop   = HAS(text, W_STOP);
    int led    = HAS(text, W_LED);
    int beep   = HAS(text, W_BEEP);
    int alarm  = HAS(text, W_ALARM);
    int breath = HAS(text, W_BREATH);

    if (stop && beep) {                 /* 1 停止词 + 蜂鸣器词（“停止报警”） */
        return I_BEEP_OFF;
    }
    if (stop && led) {                  /* 2 停止词 + 灯词（“停止闪烁 / 别闪了”） */
        return I_LED_OFF;
    }
    if (stop) {                         /* 3 裸停止词 → 全停 */
        return I_STOP;
    }
    if (strstr(text, "闪") != NULL) {   /* 4 含“闪” */
        return I_LED_BLINK;
    }
    if (beep && alarm) {                /* 5 */
        return I_BEEP_ALARM;
    }
    if (breath) {                       /* 6 呼吸词单独命中（“呼吸”不在蜂鸣器词池） */
        return I_BEEP_BREATH;
    }
    if (led && HAS(text, W_ON)) {       /* 7 */
        return I_LED_ON;
    }
    if (led && HAS(text, W_OFF)) {      /* 8 */
        return I_LED_OFF;
    }
    if (alarm) {                        /* 9 单独“报警 / 警报” */
        return I_BEEP_ALARM;
    }
    return I_UNKNOWN;                   /* 10 其余 */
}

/* 意图 → 工具调用；I_UNKNOWN 无动作，视为成功 */
static esp_err_t dispatch(intent_t intent)
{
    switch (intent) {
    case I_LED_ON:      return tool_led_set(LED_ON);
    case I_LED_OFF:     return tool_led_set(LED_OFF);
    case I_LED_BLINK:   return tool_led_set(LED_BLINK);
    case I_BEEP_BREATH: return tool_beep_set(BEEP_BREATH);
    case I_BEEP_ALARM:  return tool_beep_set(BEEP_ALARM);
    case I_BEEP_OFF:    return tool_beep_set(BEEP_OFF);
    case I_STOP:        return tool_stop_all();
    default:            return ESP_OK;
    }
}

static const char *const INTENT_NAME[] = {
    "unknown", "led_on", "led_off", "led_blink",
    "beep_breath", "beep_alarm", "beep_off", "stop",
};

void task_nlu(void *arg)
{
    (void)arg;
    cmd_msg_t msg;

    for (;;) {
        if (xQueueReceive(g_cmd_q, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        intent_t intent = nlu_understand(msg.text);
        esp_err_t err = dispatch(intent);

        ESP_LOGI(TAG, "src=%u \"%s\" -> %s", (unsigned)msg.src, msg.text,
                 INTENT_NAME[intent]);  /* 串口日志兼作阶段 4 语料来源 */

        if (err != ESP_OK) {
            /* rhythm_q 满属异常（深度 4） */
            ESP_LOGW(TAG, "命令下发失败: %s", esp_err_to_name(err));
        }
        resp_compose(intent, err == ESP_OK); /* 组合式回复（阶段 2） */
    }
}
