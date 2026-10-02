#!/usr/bin/env python3
"""训练侧公共数据工具（阶段 5）：词表 / 编码 / 语料读取。

训练脚本（train_textcnn.py）与量化脚本（quantize_tflite.py）共用。
编码规则须与阶段 6 的 C 侧 tokenizer 一致：UTF-8 逐字符查表 →
词表外字符映射 <unk>(1) → 截断并填充到 max_len。
"""
from pathlib import Path

import tensorflow as tf
from tensorflow import keras

# 标签索引 = intent_t 枚举序（components/app_types.h），改枚举须同步此处
LABELS = ["unknown", "led_on", "led_off", "led_blink",
          "beep_breath", "beep_alarm", "beep_off", "stop"]

PAD_ID = 0
UNK_ID = 1


class CharEmbedding(keras.layers.Layer):
    """字符查表嵌入：等价 Keras Embedding，但用裸 tf.gather。

    不用 keras.layers.Embedding 的原因：其负索引语义会在转换后的图里引入
    LESS / ADD / SELECT_V2 三个算子，而 TFLM 的 SELECT_V2 不支持 int32
    （token id 输入正是 int32），板端 Invoke 会失败。裸 gather 无此逻辑。
    训练（train_textcnn）与量化（quantize_tflite 反序列化）共用。
    """

    def __init__(self, vocab_size, dim, **kwargs):
        super().__init__(**kwargs)
        self.vocab_size = vocab_size
        self.dim = dim

    def build(self, _input_shape):
        self.table = self.add_weight(name="table", shape=(self.vocab_size, self.dim),
                                     initializer="uniform", trainable=True)

    def call(self, x):
        return tf.gather(self.table, x)


def load_vocab(path):
    """vocab.txt：行号即 id。返回 {字符: id}。"""
    lines = Path(path).read_text(encoding="utf-8").splitlines()
    return {ch: i for i, ch in enumerate(lines)}


def encode(text, vocab, max_len):
    """一句文本 → 定长 id 列表（截断 + pad）。"""
    ids = [vocab.get(ch, UNK_ID) for ch in text][:max_len]
    return ids + [PAD_ID] * (max_len - len(ids))


def load_split(corpus_dir, name, vocab, max_len):
    """读 {name}.tsv → (ids (N, max_len), labels (N,))，均为 numpy int32。"""
    import numpy as np

    xs, ys = [], []
    for line in (Path(corpus_dir) / f"{name}.tsv").read_text(encoding="utf-8").splitlines():
        if not line:
            continue
        text, label = line.split("\t")
        xs.append(encode(text, vocab, max_len))
        ys.append(LABELS.index(label))
    return np.array(xs, dtype=np.int32), np.array(ys, dtype=np.int32)


def compute_max_len(corpus_dir):
    """全语料最长句字符数——训练与部署一致的序列长度约定。"""
    longest = 0
    for name in ("train", "val", "test"):
        for line in (Path(corpus_dir) / f"{name}.tsv").read_text(encoding="utf-8").splitlines():
            if line:
                longest = max(longest, len(line.split("\t")[0]))
    return longest
