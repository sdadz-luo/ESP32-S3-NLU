#!/usr/bin/env python3
"""int8 全整型量化与导出（阶段 5）。

加载 data/model/model_float.keras → 代表数据集（train 分层抽 300 条）→
TFLite 全整型转换：权重 / 激活 int8；**输入输出类型不显式设置**——TF 2.21
不接受 int32 作为 inference_input_type，而不设时输入保持模型原生 int32
（token id 原样进入 embedding，词表 369 超出 int8 范围）、输出 float32
（argmax 取类即可）。转换后对 test 集做量化前后精度对比 → 导出
.tflite 与 C 数组。

用法（工程根目录，用工程 .venv）：
  .venv/Scripts/python.exe tools/quantize_tflite.py

产物（data/model/）：
  model_int8.tflite / model_int8.cc / model_int8.h
  quant_report.md     量化对比报告
"""
import argparse
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]

import numpy as np

from nlu_data import LABELS, compute_max_len, load_split, load_vocab

import tensorflow as tf
from tensorflow import keras

REP_N = 300  # 代表数据集条数（分层）


def make_rep_dataset(x_train, y_train):
    """代表数据集：按类别分层抽样，保证 unknown 等小类覆盖。"""
    rng = np.random.RandomState(42)
    idx = []
    per = max(1, REP_N // len(LABELS))
    for c in range(len(LABELS)):
        cidx = np.where(y_train == c)[0]
        idx.extend(rng.choice(cidx, size=min(per, len(cidx)), replace=False))

    def gen():
        for i in idx:
            yield [x_train[i][None, :]]
    return gen, len(idx)


def run_tflite(tflite_bytes, x_test):
    """用 TFLite 解释器逐条推理，返回预测类别数组。"""
    interp = tf.lite.Interpreter(model_content=tflite_bytes)
    interp.allocate_tensors()
    inp = interp.get_input_details()[0]
    out = interp.get_output_details()[0]
    preds = []
    for x in x_test:
        interp.set_tensor(inp["index"], x[None, :].astype(np.int32))
        interp.invoke()
        preds.append(int(np.argmax(interp.get_tensor(out["index"])[0])))
    return np.array(preds)


def export_c_array(data, cc_path, h_path, name="g_model_int8"):
    """导出 xxd 风格 C 数组（alignas(8)，供 TFLM 直接引用）。"""
    hexs = [f"0x{b:02x}" for b in data]
    rows = ["    " + ", ".join(hexs[i:i + 12]) + "," for i in range(0, len(hexs), 12)]
    cc_path.write_text(
        "// 由 tools/quantize_tflite.py 生成，勿手改。\n"
        f"// int8 全整型量化模型（{len(data)} 字节），评估见 quant_report.md\n"
        f'#include "{h_path.name}"\n\n'
        f"alignas(8) const unsigned char {name}[] = {{\n"
        + "\n".join(rows) + "\n};\n"
        f"const unsigned int {name}_len = {len(data)};\n",
        encoding="utf-8", newline="\n")
    h_path.write_text(
        "// 由 tools/quantize_tflite.py 生成，勿手改。\n"
        "#ifndef MODEL_INT8_H\n#define MODEL_INT8_H\n\n"
        f"extern const unsigned char {name}[];\n"
        f"extern const unsigned int {name}_len;\n\n"
        "#endif\n",
        encoding="utf-8", newline="\n")


def main():
    root = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description="int8 全整型量化与导出（阶段 5）")
    ap.add_argument("--corpus", default=str(root / "data" / "corpus"))
    ap.add_argument("--model", default=str(root / "data" / "model"))
    args = ap.parse_args()

    corpus = Path(args.corpus)
    mdir = Path(args.model)

    vocab = load_vocab(corpus / "vocab.txt")
    max_len = compute_max_len(corpus)
    x_train, y_train = load_split(corpus, "train", vocab, max_len)
    x_test, y_test = load_split(corpus, "test", vocab, max_len)

    model = keras.models.load_model(mdir / "model_float.keras")

    rep, rep_n = make_rep_dataset(x_train, y_train)
    conv = tf.lite.TFLiteConverter.from_keras_model(model)
    conv.optimizations = [tf.lite.Optimize.DEFAULT]
    conv.representative_dataset = rep
    conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    # 不设置 inference_input_type / inference_output_type：TF 2.21 的允许
    # 列表没有 int32，而不设时输入保持模型原生 int32（token id 原样）、
    # 输出 float32（argmax 取类，与 int8 输出等价）
    tflite_bytes = conv.convert()
    (mdir / "model_int8.tflite").write_bytes(tflite_bytes)
    export_c_array(tflite_bytes, mdir / "model_int8.cc", mdir / "model_int8.h")

    pred_f = model.predict(x_test, verbose=0).argmax(axis=1)
    pred_q = run_tflite(tflite_bytes, x_test)
    acc_f = float((pred_f == y_test).mean())
    acc_q = float((pred_q == y_test).mean())
    drop = (acc_f - acc_q) * 100

    rep_lines = [
        "# 量化对比报告（阶段 5）", "",
        f"- 环境：Python {sys.version.split()[0]} / TensorFlow {tf.__version__}",
        "- 转换：TFLite 全整型（权重 / 激活 int8；输入 int32 token id 原样；输出 float32）",
        f"- 代表数据集：train 分层抽取 {rep_n} 条（不使用 test）",
        f"- 模型体积：int8 {len(tflite_bytes) / 1024:.1f} KB"
        f"（float32 权重约 {model.count_params() * 4 / 1024:.0f} KB）", "",
        "## test 精度对比（同一测试集）", "",
        f"- float32 模型：{acc_f * 100:.1f}%",
        f"- int8 模型：{acc_q * 100:.1f}%",
        f"- 掉点：{drop:.2f} 个百分点（验收标准 ≤ 2）", "",
        f"## 判定：{'达标' if drop <= 2 else '**未达标，需排查**'}", "",
        "产物：`model_int8.tflite` / `model_int8.cc` / `model_int8.h`"
        "（C 数组，阶段 6 编译进固件）", "",
    ]
    (mdir / "quant_report.md").write_text("\n".join(rep_lines),
                                          encoding="utf-8", newline="\n")

    print(f"int8 模型 {len(tflite_bytes) / 1024:.1f} KB")
    print(f"test：float {acc_f * 100:.1f}% → int8 {acc_q * 100:.1f}%（掉点 {drop:.2f}pp）")
    print(f"报告与产物: {mdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
