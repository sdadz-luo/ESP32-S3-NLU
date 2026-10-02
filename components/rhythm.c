/**
 * @file    rhythm.c
 * @brief   节奏任务：执行层唯一汇点（节拍引擎 + 物理输出）
 * @author  sdadz-luo
 *
 * 机制见 docs/STAGE1_DESIGN.md「节奏引擎」：两路各自维护
 * {模式, 下次翻转时刻, 半周期}，每轮以「到最近 deadline 的剩余时间」为
 * 超时阻塞在 g_rhythm_q——超时对到期路翻转（next_us 旧值累加，无长期
 * 累积漂移），收到命令立即应用新模式基态（亚毫秒打断）。
 * 本任务是全工程唯一写 XL9555 输出的地方（蜂鸣器）。
 */

#include "rhythm.h"

#include "app_queues.h"
#include "app_types.h"
#include "led.h"
#include "xl9555.h"

#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* 翻转半周期（µs） */
#define LED_BLINK_HALF_US    100000     /* LED 5Hz */
#define BEEP_ALARM_HALF_US   250000     /* 蜂鸣器 2Hz */
#define BEEP_BREATH_HALF_US  1000000    /* 蜂鸣器 0.5Hz */

#define NO_DEADLINE  (-1)               /* next_us 哨兵：无待翻转 */

/* 模式快照：写者唯一 = 本任务（led_apply / beep_apply 内更新），
 * 读者 = 响应层等；单字节对齐读写原子，volatile 防编译器缓存 */
static volatile led_mode_t  s_led_mode  = LED_OFF;
static volatile beep_mode_t s_beep_mode = BEEP_OFF;

/* 一路节奏通道：既有模式、下次翻转时刻、半周期、当前电平（1=亮/响） */
typedef struct {
    uint8_t mode;
    int64_t next_us;
    int64_t half_us;
    uint8_t level;
} rhythm_ch_t;

/* 蜂鸣器经 XL9555 驱动，写 0 为响 */
static void beep_out(uint8_t on)
{
    xl9555_pin_write(BEEP_IO, on ? 0 : 1);
}

/* 收到命令：立即应用新模式基态 */
static void led_apply(rhythm_ch_t *ch, led_mode_t mode, int64_t now)
{
    ch->mode = (uint8_t)mode;
    s_led_mode = mode;

    switch (mode) {
    case LED_OFF:
        led_off();
        ch->level = 0;
        ch->next_us = NO_DEADLINE;
        break;
    case LED_ON:
        led_on();
        ch->level = 1;
        ch->next_us = NO_DEADLINE;
        break;
    case LED_BLINK:                     /* 基态亮起，首翻在 +100ms */
        led_on();
        ch->level = 1;
        ch->half_us = LED_BLINK_HALF_US;
        ch->next_us = now + LED_BLINK_HALF_US;
        break;
    }
}

static void beep_apply(rhythm_ch_t *ch, beep_mode_t mode, int64_t now)
{
    ch->mode = (uint8_t)mode;
    s_beep_mode = mode;

    switch (mode) {
    case BEEP_OFF:
        beep_out(0);
        ch->level = 0;
        ch->next_us = NO_DEADLINE;
        break;
    case BEEP_BREATH:                   /* 基态响起 */
        beep_out(1);
        ch->level = 1;
        ch->half_us = BEEP_BREATH_HALF_US;
        ch->next_us = now + BEEP_BREATH_HALF_US;
        break;
    case BEEP_ALARM:
        beep_out(1);
        ch->level = 1;
        ch->half_us = BEEP_ALARM_HALF_US;
        ch->next_us = now + BEEP_ALARM_HALF_US;
        break;
    }
}

static void led_flip(rhythm_ch_t *ch)
{
    ch->level = !ch->level;
    if (ch->level) {
        led_on();
    } else {
        led_off();
    }
}

static void beep_flip(rhythm_ch_t *ch)
{
    ch->level = !ch->level;
    beep_out(ch->level);
}

/* µs → tick，向上取整（避免提前唤醒导致翻转偏早）；无 deadline 则永久阻塞 */
static TickType_t wait_ticks(int64_t wait_us)
{
    if (wait_us == NO_DEADLINE) {
        return portMAX_DELAY;
    }
    if (wait_us <= 0) {
        return 0;
    }

    return (TickType_t)((wait_us * configTICK_RATE_HZ + 999999) / 1000000);
}

led_mode_t rhythm_led_mode(void)
{
    return s_led_mode;
}

beep_mode_t rhythm_beep_mode(void)
{
    return s_beep_mode;
}

void task_rhythm(void *arg)
{
    (void)arg;

    rhythm_ch_t led  = { .mode = (uint8_t)LED_OFF,  .next_us = NO_DEADLINE };
    rhythm_ch_t beep = { .mode = (uint8_t)BEEP_OFF, .next_us = NO_DEADLINE };
    rhythm_cmd_t cmd;

    for (;;) {
        int64_t now = esp_timer_get_time();

        /* 到点翻转：next_us 旧值累加，长期无累积漂移 */
        if (led.next_us != NO_DEADLINE && led.next_us <= now) {
            led_flip(&led);
            led.next_us += led.half_us;
        }
        if (beep.next_us != NO_DEADLINE && beep.next_us <= now) {
            beep_flip(&beep);
            beep.next_us += beep.half_us;
        }

        /* 取最近 deadline 的剩余时间作为阻塞超时 */
        int64_t wait_us = NO_DEADLINE;
        if (led.next_us != NO_DEADLINE) {
            wait_us = led.next_us - now;
        }
        if (beep.next_us != NO_DEADLINE) {
            int64_t w = beep.next_us - now;
            if (wait_us == NO_DEADLINE || w < wait_us) {
                wait_us = w;
            }
        }

        if (xQueueReceive(g_rhythm_q, &cmd, wait_ticks(wait_us)) == pdTRUE) {
            now = esp_timer_get_time();
            if (cmd.target == RHYTHM_LED) {
                led_apply(&led, (led_mode_t)cmd.mode, now);
            } else if (cmd.target == RHYTHM_BEEP) {
                beep_apply(&beep, (beep_mode_t)cmd.mode, now);
            }
        }
    }
}
