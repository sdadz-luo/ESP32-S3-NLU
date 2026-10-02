#!/usr/bin/env python3
"""生成 16×16 中文字库（NLUF 格式），供 ESP32-S3-NLU 的 LCD 渲染。

字体源（须为允许使用与衍生的开放字体，SIL OFL 1.1）：
  思源黑体 / Source Han Sans   https://github.com/adobe-fonts/source-han-sans
  或 Google 的 Noto Sans SC    https://fonts.google.com/noto/specimen/Noto+Sans+SC

依赖：pip install pillow

用法：
  python gen_font.py --font NotoSansSC-Regular.otf --out ../data/font16.bin

刷入（storage 分区起始偏移见 partitions-16MiB.csv）：
  python -m esptool --chip esp32s3 -p COM32 write_flash 0xC00000 ../data/font16.bin
"""
import argparse
import struct

from PIL import Image, ImageDraw, ImageFont

# 覆盖区间：中文标点 / CJK 基本区 / 全角字符（固件按码点线性索引）
RANGES = [(0x3000, 0x303F), (0x4E00, 0x9FFF), (0xFF01, 0xFF5E)]
W = H = 16
ROW_BYTES = W // 8


def render_glyph(font, cp, y_offset, threshold):
    """渲染一个码点为 16×16 单色点阵：行优先、MSB 在左（与 asc2_1608 一致）。"""
    img = Image.new("L", (W, H), 0)
    ImageDraw.Draw(img).text((0, y_offset), chr(cp), font=font, fill=255)
    raw = img.tobytes()                 # L 模式：W×H 字节，行优先

    data = bytearray()
    for y in range(H):
        for half in range(ROW_BYTES):
            byte = 0
            for k in range(8):
                if raw[y * W + half * 8 + k] >= threshold:
                    byte |= 1 << (7 - k)
            data.append(byte)
    return bytes(data)


def main():
    ap = argparse.ArgumentParser(description="生成 NLUF 16x16 中文字库")
    ap.add_argument("--font", required=True, help="TTF/OTF 字体文件路径")
    ap.add_argument("--size", type=int, default=16, help="字号（默认 16）")
    ap.add_argument("--y-offset", type=int, default=0, help="纵向微调像素（默认 0）")
    ap.add_argument("--threshold", type=int, default=128, help="二值化阈值（默认 128）")
    ap.add_argument("--weight", type=int, default=400, help="可变字体字重（默认 400 Regular）")
    ap.add_argument("--out", required=True, help="输出 bin 路径")
    args = ap.parse_args()

    font = ImageFont.truetype(args.font, args.size)
    if font.get_variation_axes():           # 可变字体（如 NotoSansSC[wght].ttf）需选字重
        font.set_variation_by_axes([args.weight])
        print(f"可变字体：字重设为 {args.weight}")
    total = sum(hi - lo + 1 for lo, hi in RANGES)
    print(f"字库参数: {W}x{H}, {len(RANGES)} 区间, {total} 字, "
          f"预计 {total * (W // 8) * H / 1024:.0f} KB")

    range_tbl = bytearray()
    data = bytearray()
    for lo, hi in RANGES:
        range_tbl += struct.pack("<III", lo, hi, len(data))
        for cp in range(lo, hi + 1):
            data += render_glyph(font, cp, args.y_offset, args.threshold)
        print(f"  区间 U+{lo:04X}-U+{hi:04X} 完成")

    header = b"NLUF" + struct.pack("<IHHBBH", 1, W, H, len(RANGES), ROW_BYTES, 0)
    with open(args.out, "wb") as f:
        f.write(header + range_tbl + data)

    size = len(header) + len(range_tbl) + len(data)
    print(f"已生成 {args.out}: {size / 1024:.0f} KB "
          f"（头 {len(header)} B + 区间表 {len(range_tbl)} B + 数据 {len(data) / 1024:.0f} KB）")


if __name__ == "__main__":
    main()
