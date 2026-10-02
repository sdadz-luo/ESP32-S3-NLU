#!/usr/bin/env python3
"""训练字符级 TextCNN（阶段 5）。

读 data/corpus 的语料与词表 → 编码 → 训练（val 早停）→ 评估（test 只考一次）
→ 保存 float 模型与评估报告。量化见 tools/quantize_tflite.py。

用法（工程根目录，用工程 .venv）：
  .venv/Scripts/python.exe tools/train_textcnn.py
  .venv/Scripts/python.exe tools/train_textcnn.py --epochs 50 --seed 7

产物（默认 data/model/）：
  model_float.keras   float32 模型存档
  eval_report.md      训练与评估报告（准确率 / 逐类 / 混淆矩阵 / 拒识两向）
"""
import argparse
import random
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]

import numpy as np

from nlu_data import LABELS, compute_max_len, load_split, load_vocab

import tensorflow as tf
from tensorflow import keras


def set_seed(seed):
    random.seed(seed)
    np.random.seed(seed)
    tf.random.set_seed(seed)


def build_model(vocab_size, n_classes, max_len):
    inp = keras.Input(shape=(max_len,), dtype="int32")
    emb = keras.layers.Embedding(vocab_size, 32)(inp)
    convs = [keras.layers.GlobalMaxPooling1D()(
                 keras.layers.Conv1D(64, k, activation="relu")(emb))
             for k in (2, 3, 4)]
    x = keras.layers.Concatenate()(convs)
    out = keras.layers.Dense(n_classes, activation="softmax")(x)
    model = keras.Model(inp, out)
    model.compile(optimizer="adam",
                  loss="sparse_categorical_crossentropy",
                  metrics=["accuracy"])
    return model


def build_report(args, tf_ver, np_ver, sizes, vocab_n, max_len, best_epoch,
                 params, acc, cm):
    lines = [
        "# 训练评估报告（阶段 5）", "",
        f"- 环境：Python {sys.version.split()[0]} / TensorFlow {tf_ver} / NumPy {np_ver}",
        f"- 语料：train {sizes[0]} / val {sizes[1]} / test {sizes[2]}；"
        f"词表 {vocab_n}；max_len {max_len}",
        f"- 种子 {args.seed}；训练至第 {best_epoch} 轮（上限 {args.epochs}，"
        "取 val_loss 最优权重）",
        f"- 模型参数量：{params:,}", "",
        "## test 评估（只考一次）", "",
        f"- 总准确率：**{acc * 100:.1f}%**（{int(round(acc * sizes[2]))}/{sizes[2]}）",
        "- 规则基线对照：test 82.8%（见 `data/corpus/rule_report.md`）", "",
        "### 逐类召回", "",
        "| 意图 | 召回 | 样本 |", "| --- | --- | --- |",
    ]
    for i, name in enumerate(LABELS):
        total = int(cm[i].sum())
        lines.append(f"| {name} | {cm[i, i] / total * 100:.1f}% | {total} |")
    lines += ["", "### 混淆矩阵（行=真实，列=预测）", "",
              "| 真实 \\ 预测 | " + " | ".join(LABELS) + " |",
              "| --- |" + " --- |" * len(LABELS)]
    for i in range(len(LABELS)):
        cells = [f"**{cm[i, j]}**" if i == j else str(cm[i, j])
                 for j in range(len(LABELS))]
        lines.append(f"| {LABELS[i]} | " + " | ".join(cells) + " |")
    unk_total = int(cm[0].sum())
    unk_leak = int(cm[0, 1:].sum())
    act_total = int(cm[1:].sum())
    act_refuse = int(cm[1:, 0].sum())
    lines += ["", "### 拒识两向", "",
              f"- 漏拒（unknown → 动作，最危险）：{unk_leak}/{unk_total} = "
              f"{unk_leak / unk_total * 100:.1f}%",
              f"- 误拒（动作 → unknown）：{act_refuse}/{act_total} = "
              f"{act_refuse / act_total * 100:.1f}%", ""]
    return "\n".join(lines)


def main():
    root = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description="训练字符级 TextCNN（阶段 5）")
    ap.add_argument("--corpus", default=str(root / "data" / "corpus"), help="语料目录")
    ap.add_argument("--out", default=str(root / "data" / "model"), help="产物目录")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--epochs", type=int, default=100)
    args = ap.parse_args()

    set_seed(args.seed)
    corpus = Path(args.corpus)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    vocab = load_vocab(corpus / "vocab.txt")
    max_len = compute_max_len(corpus)
    x_train, y_train = load_split(corpus, "train", vocab, max_len)
    x_val, y_val = load_split(corpus, "val", vocab, max_len)
    x_test, y_test = load_split(corpus, "test", vocab, max_len)
    print(f"词表 {len(vocab)} ｜ max_len {max_len} ｜ "
          f"train {len(x_train)} / val {len(x_val)} / test {len(x_test)}")

    model = build_model(len(vocab), len(LABELS), max_len)
    print(f"参数量 {model.count_params():,}")

    stop = keras.callbacks.EarlyStopping(monitor="val_loss", patience=5,
                                         restore_best_weights=True)
    hist = model.fit(x_train, y_train, validation_data=(x_val, y_val),
                     epochs=args.epochs, batch_size=32, callbacks=[stop],
                     verbose=2)
    model.save(out_dir / "model_float.keras")

    # test 只考一次
    pred = model.predict(x_test, verbose=0).argmax(axis=1)
    acc = float((pred == y_test).mean())
    cm = np.zeros((len(LABELS), len(LABELS)), dtype=int)
    for t, p in zip(y_test, pred):
        cm[t, p] += 1

    best_epoch = int(np.argmin(hist.history["val_loss"]) + 1)
    report = build_report(args, tf.__version__, np.__version__,
                          (len(x_train), len(x_val), len(x_test)),
                          len(vocab), max_len, best_epoch,
                          model.count_params(), acc, cm)
    (out_dir / "eval_report.md").write_text(report, encoding="utf-8", newline="\n")

    unk_total = int(cm[0].sum())
    act_total = int(cm[1:].sum())
    print(f"test 准确率 {acc * 100:.1f}%（{int((pred == y_test).sum())}/{len(y_test)}）")
    print(f"  漏拒 {int(cm[0, 1:].sum())}/{unk_total} ｜ "
          f"误拒 {int(cm[1:, 0].sum())}/{act_total}")
    print(f"模型与报告: {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
