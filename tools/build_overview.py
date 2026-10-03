#!/usr/bin/env python3
"""把训练产物的权重与词表注入 docs/project-overview.html（工程导览页）。

页面里的「现场演示」与「推理管线」直接运行训练产物的 float32 权重（浏览器内前向；
板端为等价的 int8 量化版本，测试集精度相同、判定一致），因此模型更新后需要重跑
本脚本刷新注入的数据。脚本幂等：首次替换 __MODEL_BLOB__ 占位符，其后按 id 匹配
已有的数据块重写。

依赖本机产物（均不入库，需先跑 gen_corpus / train_textcnn）：
  data/corpus/vocab.txt          字符词表（373 行，行号即 id）
  data/model/model_float.keras   训练产出的 float32 模型

用法（工程根目录，工程 .venv）：
  .venv/Scripts/python.exe tools/build_overview.py
  .venv/Scripts/python.exe tools/build_overview.py --check   # 只报告大小
"""
import argparse
import base64
import json
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]

import numpy as np
from tensorflow import keras

sys.path.insert(0, str(Path(__file__).resolve().parent))
from nlu_data import CharEmbedding  # noqa: E402

BLOB_RE = re.compile(r'(<script id="nlu-blob" type="application/json">)(.*?)(</script>)', re.S)


def b64(a: np.ndarray) -> str:
  return base64.b64encode(a.astype(np.float32).tobytes()).decode("ascii")


def build_blob(root: Path) -> dict:
  """从模型与词表构造页面数据块（键名与页面脚本的读取侧一致）。"""
  vocab = (root / "data" / "corpus" / "vocab.txt").read_text(encoding="utf-8").splitlines()
  if len(vocab) != 373:
    raise SystemExit(f"词表 {len(vocab)} 行，预期 373——语料或词表已变化，请核对页面说明")

  model = keras.models.load_model(root / "data" / "model" / "model_float.keras",
                                  custom_objects={"CharEmbedding": CharEmbedding})
  layers = {l.name: l for l in model.layers}
  emb = layers["char_embedding"].get_weights()[0]
  convs = [layers[n].get_weights() for n in ("conv1d", "conv1d_1", "conv1d_2")]
  dw, db = layers["dense"].get_weights()

  return {
      "maxLen": 22,
      "vocab": vocab,
      "emb": {"b64": b64(emb)},
      "convs": [{"k": w.shape[0], "w": b64(w), "b": b64(b)} for w, b in convs],
      "dense": {"w": b64(dw), "b": b64(db)},
  }


def main() -> int:
  root = Path(__file__).resolve().parent.parent
  ap = argparse.ArgumentParser(description="注入权重与词表到工程导览页")
  ap.add_argument("--page", default=str(root / "docs" / "project-overview.html"))
  ap.add_argument("--check", action="store_true", help="只报告，不写入")
  args = ap.parse_args()

  page = Path(args.page)
  html = page.read_text(encoding="utf-8")
  blob = json.dumps(build_blob(root), ensure_ascii=False, separators=(",", ":"))
  new_html, n = BLOB_RE.subn(lambda m: m.group(1) + blob + m.group(3), html)

  if n == 0:
    print("未找到数据块（页面可能缺少占位符或 script id）")
    return 1
  print(f"数据块 {'已存在，重写' if '__MODEL_BLOB__' not in html else '首次注入'}"
        f"：{len(blob) / 1024:.0f} KB → 页面 {len(new_html) / 1024:.0f} KB")
  if args.check:
    return 0
  page.write_text(new_html, encoding="utf-8", newline="\n")
  print(f"已写入 {page}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
