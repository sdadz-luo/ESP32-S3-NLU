/**
 * @file    ui.c
 * @brief   屏幕 UI：对话区显示/回溯 + 状态区轮询（320×240 横屏，16px 字号）
 * @author  sdadz-luo
 *
 * 布局：状态区 y 0~31（含分隔线），对话区 y 38~239，行高 18px 共 11 行。
 * 对话历史为环形缓冲（UI_HIST_LINES 行），按键经 ui_q 命令清屏/上滚/下滚/回最新。
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
#define UI_HIST_LINES   128         /* 对话历史深度：滚动可回溯的行数上限（约 40~60 轮） */

/* 配色（lcd.h 已定义宏） */
#define C_BG        BLACK
#define C_DIVIDER   GRAY
#define C_STATUS    WHITE
#define C_INPUT     CYAN
#define C_REPLY     WHITE
#define C_SYS       LGRAY

typedef struct {
    uint8_t kind;               /* ui_cmd_t：TALK 为对话消息，其余为屏幕命令 */
    char    in[128];
    char    out[192];
} ui_msg_t;

typedef struct {
    char     text[UI_LINE_MAX];
    uint16_t color;
} ui_line_t;

static QueueHandle_t s_ui_q;

/* 对话历史环形缓冲：s_head 为最旧行下标，s_cnt 为有效行数 */
static ui_line_t s_lines[UI_HIST_LINES];
static int       s_head;
static int       s_cnt;
static int       s_scroll;      /* 视口距底部的行数，0 = 跟随最新 */

void ui_init(void)
{
    s_ui_q = xQueueCreate(4, sizeof(ui_msg_t));
    assert(s_ui_q != NULL);     /* 创建失败即堆不足，属系统级错误 */
}

void ui_post(const char *in, const char *out)
{
    ui_msg_t msg = {0};     /* kind = UI_CMD_TALK（枚举值 0） */

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

void ui_post_cmd(ui_cmd_t cmd)
{
    ui_msg_t msg = {0};

    if (s_ui_q == NULL) {
        return;
    }

    msg.kind = (uint8_t)cmd;
    if (xQueueSend(s_ui_q, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "ui_q 满，丢弃屏幕命令: %d", (int)cmd);
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

/* 压入一行（历史满则丢弃最旧），确保以 '\0' 结尾 */
static void line_push(const char *s, uint16_t color)
{
    int idx;

    if (s_cnt == UI_HIST_LINES) {
        s_head = (s_head + 1) % UI_HIST_LINES;
        s_cnt--;
    }

    idx = (s_head + s_cnt) % UI_HIST_LINES;
    snprintf(s_lines[idx].text, UI_LINE_MAX, "%s", s);
    s_lines[idx].color = color;
    s_cnt++;
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

/* 视口可上滚的最大行数（历史不足一屏时为 0） */
static int max_scroll(void)
{
    return (s_cnt > UI_TALK_LINES) ? (s_cnt - UI_TALK_LINES) : 0;
}

/* 右缘 2px 指示条：标记视口在历史中的位置；无可滚内容时不画（背景已清） */
static void draw_scrollbar(int first)
{
    int span  = max_scroll();
    int track = UI_TALK_LINES * UI_LINE_H;
    int thumb, y;

    if (span == 0) {
        return;
    }

    thumb = track * UI_TALK_LINES / s_cnt;
    if (thumb < 12) {
        thumb = 12;
    }
    y = UI_TALK_Y + (track - thumb) * first / span;
    fill_rect(UI_SCREEN_W - 2, (uint16_t)y, UI_SCREEN_W - 1, (uint16_t)(y + thumb - 1), GRAY);
}

static void redraw_talk(void)
{
    int first;      /* 视口首行在历史中的序号 */

    if (s_scroll > max_scroll()) {
        s_scroll = max_scroll();    /* 索引兜底，防历史长度变化后越界 */
    }
    first = s_cnt - UI_TALK_LINES - s_scroll;
    if (first < 0) {
        first = 0;
    }

    fill_rect(0, UI_TALK_Y, UI_SCREEN_W - 1, UI_SCREEN_H - 1, C_BG);

    for (int i = 0; i < UI_TALK_LINES && first + i < s_cnt; i++) {
        const ui_line_t *l = &s_lines[(s_head + first + i) % UI_HIST_LINES];

        draw_text(UI_MARGIN, (uint16_t)(UI_TALK_Y + i * UI_LINE_H), l->text, l->color);
    }

    draw_scrollbar(first);
}

/* 屏幕命令：状态有变化才重绘（边界处按键不动作，避免画面闪烁） */
static void on_cmd(ui_cmd_t cmd)
{
    switch (cmd) {
    case UI_CMD_SCROLL_UP:
        if (s_scroll >= max_scroll()) {
            return;                     /* 已在顶部 */
        }
        s_scroll++;
        break;
    case UI_CMD_SCROLL_DOWN:
        if (s_scroll == 0) {
            return;                     /* 已在底部 */
        }
        s_scroll--;
        break;
    case UI_CMD_SCROLL_BOTTOM:
        if (s_scroll == 0) {
            return;                     /* 已在最新 */
        }
        s_scroll = 0;
        break;
    case UI_CMD_CLEAR:
        if (s_cnt == 0) {
            return;                     /* 已是空屏 */
        }
        s_head = 0;
        s_cnt = 0;
        s_scroll = 0;
        break;
    default:
        return;
    }

    redraw_talk();
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
            if (msg.kind != UI_CMD_TALK) {
                on_cmd((ui_cmd_t)msg.kind);
                continue;
            }

            s_scroll = 0;       /* 新对话：视口回到最新 */
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
