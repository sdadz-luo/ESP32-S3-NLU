#!/usr/bin/env python3
"""规则基线：在语料上评估 nlu.c 的关键词规则（阶段 4）。

逐行镜像 components/nlu.c 的 nlu_understand()——词池与 10 条优先级均须
与固件同步手工维护（文件内各池与 C 源码为同一份数据的两个副本，改一处
必须改两处）。产出混淆矩阵与准确率：

  - 阶段 6 对比基线：验证「模型明显优于纯规则」
  - 误判清单反哺词池精化

精化取舍原则：规则宁可漏认（落到 unknown 拒识），不可误动作。
「把空调关掉」这类跨域句即使加词池能提分也不加——把未知输入判成
stop 会执行错误动作，比拒识更糟。

用法：
  python tools/rule_baseline.py                      # 读 data/corpus/，打印摘要
  python tools/rule_baseline.py --out data/corpus/rule_report.md
"""
import argparse
from collections import defaultdict
from pathlib import Path

# ---- 与 components/nlu.c 同步维护的词池（手工核对，改一处必须改两处）----
W_STOP = ["停止", "停下", "停掉", "暂停", "全停", "别", "关闭",
          "安静", "静音", "不要", "全都", "全部", "结束", "收工", "打住"]
W_LED = ["灯", "LED", "闪"]
W_BEEP = ["响", "蜂鸣", "叫", "报警",
          "喇叭", "提示音", "音", "声", "吵", "闹", "安静"]
W_ALARM = ["报警", "警报", "急促", "不停", "快点"]
W_BREATH = ["呼吸", "慢速", "慢慢", "调慢"]
W_ON = ["开", "亮", "点"]
W_OFF = ["关", "灭", "熄"]

INTENTS = ["led_on", "led_off", "led_blink", "beep_breath",
           "beep_alarm", "beep_off", "stop", "unknown"]
ACTION_INTENTS = [i for i in INTENTS if i != "unknown"]


def has_any(text, pool):
    return any(w in text for w in pool)


def understand(text):
    """nlu_understand() 的逐行镜像。"""
    stop = has_any(text, W_STOP)
    led = has_any(text, W_LED)
    beep = has_any(text, W_BEEP)
    alarm = has_any(text, W_ALARM)
    breath = has_any(text, W_BREATH)

    if stop and beep:
        return "beep_off"
    if stop and led:
        return "led_off"
    if stop:
        return "stop"
    if "闪" in text:
        return "led_blink"
    if beep and alarm:
        return "beep_alarm"
    if breath:
        return "beep_breath"
    if led and has_any(text, W_ON):
        return "led_on"
    if led and has_any(text, W_OFF):
        return "led_off"
    if alarm:
        return "beep_alarm"
    return "unknown"


def load_tsv(path):
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line:
            continue
        text, label = line.split("\t")
        rows.append((text, label))
    return rows


def confusion_of(rows):
    cm = defaultdict(int)          # (真实, 预测) -> 次数
    for text, label in rows:
        cm[(label, understand(text))] += 1
    return cm


def accuracy(cm):
    total = sum(cm.values())
    correct = sum(v for (t, p), v in cm.items() if t == p)
    return correct, total


def build_report(splits, cm_all, wrong):
    lines = []
    lines.append("# 规则基线报告（nlu.c 关键词规则）")
    lines.append("")
    lines.append("> 由 `tools/rule_baseline.py` 生成；词池与 `components/nlu.c` 同步维护。")
    lines.append("> 用途：阶段 6 对比基线 + 词池精化依据。")
    lines.append("")

    lines.append("## 准确率")
    lines.append("")
    lines.append("| 集合 | 正确 | 总数 | 准确率 |")
    lines.append("| --- | --- | --- | --- |")
    for name, cm in splits:
        c, t = accuracy(cm)
        lines.append(f"| {name} | {c} | {t} | {c / t * 100:.1f}% |")
    c, t = accuracy(cm_all)
    lines.append(f"| **合计** | {c} | {t} | **{c / t * 100:.1f}%** |")
    lines.append("")

    lines.append("## 混淆矩阵（合计，行=真实，列=预测）")
    lines.append("")
    lines.append("| 真实 \\ 预测 | " + " | ".join(INTENTS) + " |")
    lines.append("| --- |" + " --- |" * len(INTENTS))
    for t in INTENTS:
        cells = []
        for p in INTENTS:
            n = cm_all.get((t, p), 0)
            if t == p:
                cells.append(f"**{n}**")
            else:
                cells.append(str(n))
        lines.append(f"| {t} | " + " | ".join(cells) + " |")
    lines.append("")

    lines.append("## 每类召回率")
    lines.append("")
    for t in INTENTS:
        row = sum(cm_all.get((t, p), 0) for p in INTENTS)
        hit = cm_all.get((t, t), 0)
        lines.append(f"- {t}：{hit}/{row} = {hit / row * 100:.1f}%")
    lines.append("")

    lines.append("## 误判明细（按方向分组）")
    lines.append("")
    for (t, p), w in sorted(wrong.items(), key=lambda kv: -kv[1]["n"]):
        if t == "unknown":
            kind = "漏拒（无关句被判成动作，最危险）"
        elif p == "unknown":
            kind = "漏认（命令未识别，落到拒识，安全侧）"
        else:
            kind = "错认（动作间混淆）"
        lines.append(f"### {t} → {p}：{w['n']} 条（{kind}）")
        lines.append("")
        for s in w["samples"]:
            lines.append(f"- {s}")
        lines.append("")
    return "\n".join(lines)


def main():
    root = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description="规则基线评估（阶段 4）")
    ap.add_argument("--corpus", default=str(root / "data" / "corpus"), help="语料目录")
    ap.add_argument("--out", default=None, help="报告输出路径（默认不写文件）")
    args = ap.parse_args()

    corpus = Path(args.corpus)
    splits = []
    all_rows = []
    for name in ("train", "val", "test"):
        rows = load_tsv(corpus / f"{name}.tsv")
        all_rows.extend(rows)
        splits.append((name, confusion_of(rows)))
    cm_all = confusion_of(all_rows)

    wrong = defaultdict(lambda: {"n": 0, "samples": []})
    for s, lab in all_rows:
        pred = understand(s)
        if pred != lab:
            w = wrong[(lab, pred)]
            w["n"] += 1
            if len(w["samples"]) < 5:
                w["samples"].append(s)

    c, t = accuracy(cm_all)
    print(f"规则准确率（合计 {t} 条）：{c / t * 100:.1f}%")
    for name, cm in splits:
        cc, tt = accuracy(cm)
        print(f"  {name}: {cc / tt * 100:.1f}%（{cc}/{tt}）")
    for (t_, p_), w in sorted(wrong.items(), key=lambda kv: -kv[1]["n"])[:5]:
        print(f"  误判 {t_} → {p_}: {w['n']} 条")

    if args.out:
        report = build_report(splits, cm_all, wrong)
        Path(args.out).write_text(report, encoding="utf-8", newline="\n")
        print(f"报告: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
