/**
 * @file    font16.c
 * @brief   16×16 中文字库：storage 分区直读 + UTF-8 渲染
 * @author  sdadz-luo
 *
 * 字库格式（生成器见 tools/gen_font.py）：
 *   [头 16 B] magic "NLUF" | u32 版本 | u16 宽 | u16 高 | u8 区间数 | u8 行字节 | u16 保留
 *   [区间表]  每区间 { u32 起码点, u32 止码点, u32 数据偏移 }（按码点升序，12 B/项）
 *   [数据区]  每字 32 B，行优先、字节内 MSB 在左（与 asc2_1608 取模一致）
 * ASCII（0x20~0x7E）直接用原厂 asc2_1608，不占字库体积。
 */

#include "font16.h"

#include "lcd.h"

#include "esp_log.h"
#include "esp_partition.h"

#include <string.h>

static const char *TAG = "font16";

#define FONT16_HDR_LEN      16
#define FONT16_MAX_RANGES   8

typedef struct {
    uint32_t first;
    uint32_t last;
    uint32_t data_off;      /* 相对数据区起点的偏移 */
} font16_range_t;

/* 单字渲染缓冲：16×16 RGB565 共 512 B，整字一次 SPI 事务发出 */
static uint8_t s_blit[FONT16_GLYPH_W * FONT16_GLYPH_H * 2];

static const esp_partition_t *s_part;                   /* NULL = 字库不可用 */
static font16_range_t s_ranges[FONT16_MAX_RANGES];
static uint8_t  s_range_cnt;
static uint32_t s_data_base;                            /* 数据区在分区内的偏移 */

/* 无字模时的占位框（空心方框） */
static const uint8_t s_placeholder[FONT16_GLYPH_BYTES] = {
    0xFF, 0xFF, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0xFF, 0xFF,
};

bool font16_load(void)
{
    uint8_t hdr[FONT16_HDR_LEN];
    const esp_partition_t *part;
    uint16_t w, h;
    uint8_t row_bytes;

    s_part = NULL;

    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    if (part == NULL) {
        ESP_LOGW(TAG, "未找到 storage 分区，中文将显示为占位框");
        return false;
    }

    if (esp_partition_read(part, 0, hdr, sizeof(hdr)) != ESP_OK || memcmp(hdr, "NLUF", 4) != 0) {
        ESP_LOGW(TAG, "storage 分区无有效字库（先刷 data/font16.bin），中文将显示为占位框");
        return false;
    }

    w = (uint16_t)(hdr[8] | (hdr[9] << 8));
    h = (uint16_t)(hdr[10] | (hdr[11] << 8));
    s_range_cnt = hdr[12];
    row_bytes = hdr[13];

    if (w != FONT16_GLYPH_W || h != FONT16_GLYPH_H || row_bytes != FONT16_GLYPH_W / 8 ||
        s_range_cnt == 0 || s_range_cnt > FONT16_MAX_RANGES) {
        ESP_LOGW(TAG, "字库头参数不符（%ux%u 行字节 %u 区间 %u）", w, h, row_bytes, s_range_cnt);
        return false;
    }

    /* 区间表为 3 个 u32 连续排布，与生成器的 little-endian 打包一致 */
    if (esp_partition_read(part, FONT16_HDR_LEN, s_ranges,
                           s_range_cnt * sizeof(font16_range_t)) != ESP_OK) {
        ESP_LOGW(TAG, "字库区间表读取失败，中文将显示为占位框");
        return false;
    }

    s_data_base = FONT16_HDR_LEN + s_range_cnt * sizeof(font16_range_t);
    s_part = part;
    ESP_LOGI(TAG, "字库就绪：%u 个区间，数据区偏移 %u", s_range_cnt, (unsigned)s_data_base);
    return true;
}

uint32_t font16_next_cp(const char **utf8)
{
    const uint8_t *p = (const uint8_t *)*utf8;
    uint32_t cp;
    int extra;

    if (p[0] < 0x80) {
        cp = p[0];
        extra = 0;
    } else if ((p[0] & 0xE0) == 0xC0) {
        cp = p[0] & 0x1F;
        extra = 1;
    } else if ((p[0] & 0xF0) == 0xE0) {
        cp = p[0] & 0x0F;
        extra = 2;
    } else if ((p[0] & 0xF8) == 0xF0) {
        cp = p[0] & 0x07;
        extra = 3;
    } else {
        (*utf8)++;
        return 0xFFFD;
    }

    for (int i = 0; i < extra; i++) {
        if ((p[1 + i] & 0xC0) != 0x80) {
            (*utf8)++;
            return 0xFFFD;      /* 非法续字节：按单字节推进，避免死循环 */
        }
        cp = (cp << 6) | (p[1 + i] & 0x3F);
    }

    *utf8 += 1 + extra;
    return cp;
}

uint16_t font16_width(uint32_t cp)
{
    return (cp >= 0x20 && cp <= 0x7E) ? 8 : 16;
}

/* 1 位点阵 → RGB565 缓冲，整字一次发送（背景一并刷掉，UI 底色需一致） */
static void blit(uint16_t x, uint16_t y, const uint8_t *glyph,
                 uint16_t color, uint16_t bg, uint16_t w, uint16_t h)
{
    uint16_t row_bytes = w / 8;
    uint16_t fg16 = color, bg16 = bg;
    uint32_t idx = 0;

    for (uint16_t r = 0; r < h; r++) {
        for (uint16_t b = 0; b < row_bytes; b++) {
            uint8_t bits = glyph[r * row_bytes + b];

            for (int k = 0; k < 8; k++) {
                uint16_t c = (bits & 0x80) ? fg16 : bg16;
                s_blit[idx++] = (uint8_t)(c >> 8);
                s_blit[idx++] = (uint8_t)(c & 0xFF);
                bits = (uint8_t)(bits << 1);
            }
        }
    }

    lcd_set_window(x, y, x + w - 1, y + h - 1);
    lcd_write_data(s_blit, w * h * 2);
}

/* 从字库取一个字的点阵；失败返回 false */
static bool glyph_read(uint32_t cp, uint8_t *out)
{
    if (s_part == NULL) {
        return false;
    }

    for (uint8_t i = 0; i < s_range_cnt; i++) {
        if (cp >= s_ranges[i].first && cp <= s_ranges[i].last) {
            uint32_t off = s_data_base + s_ranges[i].data_off +
                           (cp - s_ranges[i].first) * FONT16_GLYPH_BYTES;
            return esp_partition_read(s_part, off, out, FONT16_GLYPH_BYTES) == ESP_OK;
        }
    }
    return false;
}

uint16_t font16_draw(uint16_t x, uint16_t y, uint32_t cp, uint16_t color, uint16_t bg)
{
    uint8_t glyph[FONT16_GLYPH_BYTES];

    if (cp >= 0x20 && cp <= 0x7E) {                     /* ASCII：原厂 8×16 字模 */
        blit(x, y, lcd_font_ascii16((uint8_t)cp), color, bg, 8, 16);
        return 8;
    }

    if (glyph_read(cp, glyph)) {
        blit(x, y, glyph, color, bg, FONT16_GLYPH_W, FONT16_GLYPH_H);
    } else {
        blit(x, y, s_placeholder, color, bg, FONT16_GLYPH_W, FONT16_GLYPH_H);
    }
    return 16;
}
