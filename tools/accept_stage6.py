#!/usr/bin/env python3
"""阶段 6 验收：现编口语 20 句过串口实测（模型 + 规则兜底双层）。

句子为现编（不照抄训练语料，含词表外字符 / 超口语变体 / 空 token 句），
覆盖 8 类意图；同时用 rule_baseline.understand 算「纯规则」判定作对照，
验证「明显优于纯规则」。验收标准：通过率 ≥ 2/3。

用法（工程根目录，板子连 COM 口并已烧录）：
  .venv/Scripts/python.exe tools/accept_stage6.py
产物：data/model/acceptance_stage6.md
"""
import re
import sys
import time
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rule_baseline import understand

import serial

PORT = "COM32"
BAUD = 115200

# 现编句集（阶段 8 回归可复用；预期为标注者意图）
CASES = [
    ("帮我开一下灯呗", "led_on"),
    ("屋子里太暗了把灯弄亮", "led_on"),
    ("灯给我开上", "led_on"),
    ("把灯给我灭了啊", "led_off"),
    ("看完书了灯不用了", "led_off"),
    ("让灯给我闪起来", "led_blink"),
    ("灯调成闪烁", "led_blink"),
    ("让蜂鸣器慢慢叫", "beep_breath"),
    ("嘀嘀嘀快响起来", "beep_alarm"),
    ("别叫了行不行", "beep_off"),
    ("吵死了快停", "beep_off"),
    # 「什么声音都别出了」指向蜂鸣器（声音域）而非全停，期望为 beep_off
    ("什么声音都别出了", "beep_off"),
    ("全部消停", "stop"),
    ("帮我订一个明天早上的闹钟", "unknown"),
    ("这首歌叫什么名字", "unknown"),
    ("你能给我讲个冷笑话吗", "unknown"),
    ("把窗帘拉一下", "unknown"),
    ("今天星期几", "unknown"),
    ("哒哒哒哒", "unknown"),
    ("你好呀小助手", "unknown"),
]


def main():
    root = Path(__file__).resolve().parent.parent
    mdir = root / "data" / "model"

    ser = serial.Serial(PORT, BAUD, timeout=0.05)
    time.sleep(3.0)
    ser.reset_input_buffer()

    results = []
    for text, expected in CASES:
        ser.reset_input_buffer()
        ser.write(text.encode("utf-8") + b"\n")
        ser.flush()
        pat = re.compile(re.escape('"' + text + '"') + r"\s*->\s*(\w+)(?:\s*\(([^)]*)\))?")
        got, via, buf = None, "", ""
        t0 = time.time()
        while time.time() - t0 < 3.0:
            chunk = ser.read(4096)
            if chunk:
                buf += chunk.decode("utf-8", "ignore")
                m = pat.search(buf)
                if m:
                    got = m.group(1)
                    via = m.group(2) or ""
                    break
            else:
                time.sleep(0.02)
        rule_got = understand(text)
        results.append((text, expected, got, via, rule_got))
        mark = "OK " if got == expected else "XX "
        print(f"{mark}{text}  期望 {expected}  双层 {got} ({via})  纯规则 {rule_got}")
        time.sleep(0.15)
    ser.close()

    ok = sum(1 for _, e, g, _, _ in results if e == g)
    ok_rule = sum(1 for _, e, _, _, r in results if e == r)

    lines = ["# 阶段 6 验收报告（现编句双层决策）", "",
             f"- 端口 {PORT}；{len(results)} 句现编口语（含词表外字符与超口语变体）",
             f"- 双层决策（模型阈值见 nlu.c 的 NLU_CONF_THRESHOLD + 规则兜底）：**{ok}/{len(results)}**"
             f" = {ok / len(results) * 100:.1f}%（验收线 ≥2/3）",
             f"- 纯规则对照：{ok_rule}/{len(results)} = {ok_rule / len(results) * 100:.1f}%", "",
             "| # | 输入 | 期望 | 双层判定 | 来源 | 纯规则 |", "| --- | --- | --- | --- | --- | --- |"]
    for i, (text, e, g, via, r) in enumerate(results, 1):
        lines.append(f"| {i} | {text} | {e} | {g} | {via} | {r} |")
    lines.append("")
    (mdir / "acceptance_stage6.md").write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"\n双层 {ok}/{len(results)} = {ok / len(results) * 100:.1f}% ｜ "
          f"纯规则 {ok_rule}/{len(results)} = {ok_rule / len(results) * 100:.1f}%")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
