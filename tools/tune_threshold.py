#!/usr/bin/env python3
"""阈值调优（阶段 6）：在 test 集上模拟「模型 + 规则兜底」双层决策。

用 int8 tflite 解释器（与板端一致的形态）跑全部 test 句，得每句 top-1
置信度；按不同阈值模拟 nlu.c 的 decide() 逻辑（模型高置信优先，否则规则
兜底），扫描阈值输出准确率 / 漏拒 / 误拒表，选优写入报告。

用法（工程 .venv）：
  .venv/Scripts/python.exe tools/tune_threshold.py

产物：data/model/threshold_report.md
"""
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

import numpy as np

from nlu_data import LABELS, compute_max_len, load_vocab
from rule_baseline import understand  # 规则兜底：与 nlu.c 双副本同步维护

import tensorflow as tf

THRESHOLDS = [0.0, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.9]


def load_test(corpus):
    """读 test.tsv 原文与标签索引（顺序与文件一致）。"""
    texts, labels = [], []
    for line in (corpus / "test.tsv").read_text(encoding="utf-8").splitlines():
        if line:
            t, l = line.split("\t")
            texts.append(t)
            labels.append(LABELS.index(l))
    return texts, np.array(labels)


def model_probs(tflite_path, corpus):
    """int8 解释器逐句推理，返回 (N, 8) 概率矩阵。"""
    interp = tf.lite.Interpreter(model_path=str(tflite_path))
    interp.allocate_tensors()
    inp = interp.get_input_details()[0]
    out = interp.get_output_details()[0]

    from nlu_data import load_split
    vocab = load_vocab(corpus / "vocab.txt")
    max_len = compute_max_len(corpus)
    x, _ = load_split(corpus, "test", vocab, max_len)

    probs = []
    for row in x:
        interp.set_tensor(inp["index"], row[None, :].astype(np.int32))
        interp.invoke()
        probs.append(interp.get_tensor(out["index"])[0].copy())
    return np.array(probs)


def main():
    root = Path(__file__).resolve().parent.parent
    corpus = root / "data" / "corpus"
    mdir = root / "data" / "model"

    texts, y = load_test(corpus)
    probs = model_probs(mdir / "model_int8.tflite", corpus)
    pred_model = probs.argmax(axis=1)
    conf = probs.max(axis=1)
    pred_rule = np.array([LABELS.index(understand(t)) for t in texts])

    unk_mask = y == 0
    act_mask = ~unk_mask

    lines = ["# 阈值调优报告（阶段 6）", "",
             f"- 样本：test 集 {len(y)} 句；模型为 int8 tflite（与板端一致）",
             "- 决策模拟：top-1 置信度 ≥ T 采用模型，否则规则兜底", "",
             "| 阈值 | 准确率 | 漏拒（unknown→动作） | 误拒（动作→unknown） | 模型占比 |",
             "| --- | --- | --- | --- | --- |"]
    results = []
    for t in THRESHOLDS:
        use_model = conf >= t
        final = np.where(use_model, pred_model, pred_rule)
        acc = float((final == y).mean())
        leak = int(((final != 0) & unk_mask).sum())
        unk_n = int(unk_mask.sum())
        refuse = int(((final == 0) & act_mask).sum())
        act_n = int(act_mask.sum())
        ratio = float(use_model.mean())
        results.append((t, acc, leak, unk_n, refuse, act_n, ratio))
        lines.append(f"| {t:.2f} | {acc * 100:.1f}% | {leak}/{unk_n} | {refuse}/{act_n} | "
                     f"{ratio * 100:.0f}% |")

    max_acc = max(r[1] for r in results)
    tied = [r[0] for r in results if abs(r[1] - max_acc) < 1e-9]
    suggest = 0.60 if tied[0] <= 0.60 <= tied[-1] else (tied[0] + tied[-1]) / 2
    lines += ["",
              f"**test 集最优区间：{tied[0]:.2f} ~ {tied[-1]:.2f}（{max_acc * 100:.1f}%）**", "",
              f"test 集上模型置信度分布极端（模板语料），区间内准确率无差异；建议取 "
              f"**{suggest:.2f}**（区间中部的保守值），最终由现编句验收确认分布外兜底行为。", ""]
    (mdir / "threshold_report.md").write_text("\n".join(lines), encoding="utf-8", newline="\n")

    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
