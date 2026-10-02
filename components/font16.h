/**
 * @file    font16.h
 * @brief   16×16 中文字库：storage 分区直读 + UTF-8 渲染接口
 * @author  sdadz-luo
 *
 * 字库文件格式与生成方式见 docs/STAGE3_DESIGN.md；缺失时中文渲染占位框。
 */

#ifndef FONT16_H
#define FONT16_H

#include <stdbool.h>
#include <stdint.h>

#define FONT16_GLYPH_W      16
#define FONT16_GLYPH_H      16
#define FONT16_GLYPH_BYTES  (FONT16_GLYPH_W / 8 * FONT16_GLYPH_H)   /* 32 B/字 */

/* 查找并校验 storage 分区中的字库；返回是否可用 */
bool font16_load(void);

/* 解码一个 UTF-8 码点并前移指针；非法序列返回 0xFFFD 并前移 1 字节 */
uint32_t font16_next_cp(const char **utf8);

/* 码点显示宽度（px）：ASCII 8，其余 16 */
uint16_t font16_width(uint32_t cp);

/* 在 (x,y) 处渲染一个码点（y 为行顶），背景刷 bg 色；返回占用宽度 */
uint16_t font16_draw(uint16_t x, uint16_t y, uint32_t cp, uint16_t color, uint16_t bg);

#endif /* FONT16_H */
