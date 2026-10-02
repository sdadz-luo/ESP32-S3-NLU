/**
 * @file    ui.c
 * @brief   屏幕 UI：对话区滚动 + 状态区轮询（320×240 横屏，16px 字号）
 * @author  sdadz-luo
 *
 * 布局：状态区 y 0~31（含分隔线），对话区 y 38~239，行高 18px 共 11 行。
 * 状态来源为节奏任务的状态快照（阶段 2），本任务只读。
 */

#include "ui.h"

#include "app_types.h"
#include "font16.h"
#include "lcd.h"
#include "rhythm.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui";

/* 布局参数 */
#define UI_SCREEN_W            320
#define UI_SCREEN_H            240
#define UI_MARGIN       6
#define UI_STATUS_Y     8
#define UI_DIVIDER_Y    32
#define UI_TALK_Y       38
#define UI_LINE_H       18
#define UI_TALK_LINES   ((UI_SCREEN_H - UI_TALK_Y - UI_MARGIN) / UI_LINE_H)    /* 11 行 */
#define UI_LINE_MAX     56          /* 单行 UTF-8 字节上限（折行后片段） */

/* 配色（lcd.h 已定义宏） */
#define C_BG        BLACK
#define C_DIVIDER   GRAY
#define C_STATUS    WHITE
#define C_INPUT     CYAN
#define C_REPLY     WHITE
#define C_SYS       LGRAY

typedef struct {
    char in[128];
    char out[192];
} ui_msg_t;

typedef struct {
    char     text[UI_LINE_MAX];
    uint16_t color;
} ui_line_t;

static QueueHandle_t s_ui_q;

/* 对话区行缓冲（环形，满则整体上滚一行） */
static ui_line_t s_lines[UI_TALK_LINES];
static int       s_line_cnt;

void ui_init(void)
{
    s_ui_q = xQueueCreate(4, sizeof(ui_msg_t));
    assert(s_ui_q != NULL);     /* 创建失败即堆不足，属系统级错误 */
}

void ui_post(const char *in, const char *out)
{
    ui_msg_t msg = {0};

    if (s_ui_q == NULL) {
        return;                 /* 任务启动前（理论上不会）静默丢弃 */
    }

    snprintf(msg.out, sizeof(msg.out), "%s", out);
    if (in != NULL) {
        snprintf(msg.in, sizeof(msg.in), "%s", in);
    }

    if (xQueueSend(s_ui_q, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "ui_q 满，丢弃显示: %s", msg.out);
    }
}

/* 矩形填色：与 lcd_clear 同款分块写（lcd_buf 仅本任务使用） */
static void fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    uint32_t total = (uint32_t)(x1 - x0 + 1) * (y1 - y0 + 1) * 2;
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);

    for (uint32_t i = 0; i < LCD_BUF_SIZE / 2; i++) {
        lcd_buf[i * 2] = hi;
        lcd_buf[i * 2 + 1] = lo;
    }

    lcd_set_window(x0, y0, x1, y1);
    while (total) {
        uint32_t chunk = (total > LCD_BUF_SIZE) ? LCD_BUF_SIZE : total;
        lcd_write_data(lcd_buf, (int)chunk);
        total -= chunk;
    }
}

/* 单行绘制（不折行；调用方保证宽度）；返回结束 x */
static uint16_t draw_text(uint16_t x, uint16_t y, const char *utf8, uint16_t color)
{
    while (*utf8 != '\0') {
        uint32_t cp = font16_next_cp(&utf8);
        uint16_t w = font16_width(cp);

        if (x + w > UI_SCREEN_W - UI_MARGIN) {
            break;
        }
        font16_draw(x, y, cp, color, C_BG);
        x += w;
    }
    return x;
}

/* 压入一行（含上滚），确保以 '\0' 结尾 */
static void line_push(const char *s, uint16_t color)
{
    if (s_line_cnt == UI_TALK_LINES) {
        memmove(&s_lines[0], &s_lines[1], sizeof(ui_line_t) * (UI_TALK_LINES - 1));
        s_line_cnt--;
    }

    snprintf(s_lines[s_line_cnt].text, UI_LINE_MAX, "%s", s);
    s_lines[s_line_cnt].color = color;
    s_line_cnt++;
}

/* 按像素宽折行后压入（prefix 只加在首行）；长度双保险不越界 */
static void push_text(const char *prefix, const char *text, uint16_t color)
{
    char     line[UI_LINE_MAX];
    uint16_t px = 0;
    size_t   used = 0;

    line[0] = '\0';

    while (prefix != NULL && *prefix != '\0' && used < UI_LINE_MAX - 4) {
        const char *q = prefix;
        uint32_t cp = font16_next_cp(&q);
        size_t n = (size_t)(q - prefix);

        memcpy(line + used, prefix, n);
        used += n;
        px = (uint16_t)(px + font16_width(cp));
        prefix = q;
    }

    while (*text != '\0') {
        const char *q = text;
        uint32_t cp = font16_next_cp(&q);
        size_t n = (size_t)(q - text);
        uint16_t w = font16_width(cp);

        if (px + w > UI_SCREEN_W - 2 * UI_MARGIN || used + n >= UI_LINE_MAX) {
            line[used] = '\0';
            line_push(line, color);
            used = 0;
            px = 0;
            line[0] = '\0';
        }

        memcpy(line + used, text, n);
        used += n;
        px = (uint16_t)(px + w);
        text = q;
    }

    if (used > 0) {
        line[used] = '\0';
        line_push(line, color);
    }
}

static void redraw_talk(void)
{
    fill_rect(0, UI_TALK_Y, UI_SCREEN_W - 1, UI_SCREEN_H - 1, C_BG);

    for (int i = 0; i < s_line_cnt; i++) {
        draw_text(UI_MARGIN, (uint16_t)(UI_TALK_Y + i * UI_LINE_H),
                  s_lines[i].text, s_lines[i].color);
    }
}

/* 状态描述与回应层失败文案同源（词池编辑器「状态描述」一节的取值） */
static const char *led_text(led_mode_t mode)
{
    switch (mode) {
    case LED_ON:    return "常亮";
    case LED_BLINK: return "闪烁（5Hz）";
    default:        return "熄灭";
    }
}

static const char *beep_text(beep_mode_t mode)
{
    switch (mode) {
    case BEEP_ALARM:  return "报警（2Hz）";
    case BEEP_BREATH: return "呼吸提示（0.5Hz）";
    default:          return "停止";
    }
}

static void redraw_status(void)
{
    char buf[64];

    fill_rect(0, 0, UI_SCREEN_W - 1, UI_DIVIDER_Y - 1, C_BG);
    snprintf(buf, sizeof(buf), "灯: %s   蜂鸣器: %s",
             led_text(rhythm_led_mode()), beep_text(rhythm_beep_mode()));
    draw_text(UI_MARGIN, UI_STATUS_Y, buf, C_STATUS);
    fill_rect(0, UI_DIVIDER_Y, UI_SCREEN_W - 1, UI_DIVIDER_Y + 1, C_DIVIDER);
}

void task_display(void *arg)
{
    (void)arg;
    ui_msg_t msg;
    led_mode_t  led  = (led_mode_t)0xFF;    /* 初值取非法值，强制首轮重绘 */
    beep_mode_t beep = (beep_mode_t)0xFF;

    fill_rect(0, 0, UI_SCREEN_W - 1, UI_SCREEN_H - 1, C_BG);
    redraw_status();

    for (;;) {
        if (xQueueReceive(s_ui_q, &msg, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (msg.in[0] != '\0') {
                push_text("你> ", msg.in, C_INPUT);
                push_text("板> ", msg.out, C_REPLY);
            } else {
                push_text(NULL, msg.out, C_SYS);
            }
            redraw_talk();
        } else {
            /* 超时：轮询状态快照，变化才重绘（与节奏引擎同款超时模式） */
            if (rhythm_led_mode() != led || rhythm_beep_mode() != beep) {
                led = rhythm_led_mode();
                beep = rhythm_beep_mode();
                redraw_status();
            }
        }
    }
}
