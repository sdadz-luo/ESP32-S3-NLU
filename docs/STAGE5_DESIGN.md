# 阶段 5 细化设计：模型训练与量化

本文是《项目路线图》阶段 5 的实施依据：用阶段 4 的语料训练字符级 TextCNN，
评估达标后做 int8 全整型量化，导出可进固件的模型产物（.tflite 与 C 数组），
并与规则基线对比。范围与完成标志见 ROADMAP 阶段 5 一节。

> 状态：已完成，2026-10-02 验收通过。test 准确率 98.9%（目标 ≥95%；规则基线
> 82.8%，高出 16.1pp）；int8 零掉点（43.8 KB &lt; 100 KB）；同种子重跑结果
> 完全一致；产物在 `data/model/`（不入库）。

## 目标与范围

- 训练：字符级 TextCNN（Embedding 32 维 → Conv1D k=2/3/4 × 64 → GlobalMaxPool
  → Dense 8 + softmax），与 ROADMAP / 指南页所述结构一致
- 评估：测试集准确率（目标 ≥95%）、逐类召回、8×8 混淆矩阵、
  拒识两向指标（漏拒 / 误拒）
- 量化：int8 全整型 + 量化前后精度对比（掉点 ≤2 个百分点），
  导出 `model_int8.tflite` 与 C 数组
- 对比：与规则基线（test 82.8%）对照，验证「明显优于纯规则」的走向
- 不包含：C 侧推理集成与阈值调优（阶段 6）；模型侧数据增强（语料侧已做）

## 训练环境（2026-10-02 查证）

Windows 原生 TensorFlow 现状：**CPU-only**——TF 2.10 之后原生 Windows 不再
提供 GPU 支持（GPU 走 WSL2），CPU 版（Intel 维护的 tensorflow-intel 构建，
Python 3.9–3.13、win_amd64 wheel）持续可用。本项目是小模型 + 4000 条语料，
CPU 训练足够，不与 WSL2 折腾。

安装方案（实施中按实测调整）：

- 工程内 `.venv`（gitignore 已忽略），基于本机 Anaconda Python 3.12 创建——
  隔离且免额外下载（conda 官方频道建环境实测十余分钟未完成，已放弃）
- `pip install tensorflow` 经清华 PyPI 镜像（`-i https://pypi.tuna.tsinghua.edu.cn/simple`，
  官方源国内过慢）；含 Keras 与 TFLite 转换器，是唯一依赖
- 安装后记录 `tensorflow.__version__` 到评估报告，保证可追溯

## 数据编码

- 字符 → `vocab.txt` 行号；词表外字符 → `<unk>`(1)；pad → `0`
- `max_len = 22`（阶段 4 报告的全语料最长句，训练与部署一致）
- 标签 = 意图索引（`intent_t` 枚举序：unknown=0 … stop=7）
- 编码逻辑训练与阶段 6 的 C 侧 tokenizer 对齐：UTF-8 逐字符、查表、跳过词表外

## 模型与超参

结构与参数量（≈ 36,000，约 36 KB int8）见指南页；训练配置：

| 项 | 值 |
| --- | --- |
| batch_size | 32 |
| epochs | 100 + EarlyStopping(monitor=val_loss, patience=5, restore_best_weights) |
| 优化器 / 损失 | Adam（默认学习率）/ sparse_categorical_crossentropy |
| 随机种子 | Python / NumPy / TF 全固定（seed=42），保证可复现 |
| 评估纪律 | 调参只动 val；test 只考一次 |

## 评估标准

- 主指标：test 准确率 ≥ 95%（分类别看，重点 unknown / stop 不低于 90%）
- 混淆矩阵 8×8 写进评估报告，非对角线看「谁混谁」
- 拒识两向：漏拒（unknown → 动作类，最危险）与误拒（动作类 → unknown）
- 与规则基线对照：模型 test 准确率 vs 规则 82.8%（阶段 4 实测）

## 量化与导出

- 全整型 int8：权重 / 激活——**输入输出不显式量化**（TF 2.21 不接受 int32
  输入类型；不设定时输入保持原生 int32 token id、输出 float32，实测零掉点，
  详见 STAGE6_DESIGN 预研）；代表数据集 = train 分层抽 300 条（各类覆盖，
  绝不使用 test）
- 量化前后对比：同一 test 集跑 float 与 int8，掉点 ≤2 个百分点
- 导出：`model_int8.tflite` + C 数组（`model_int8.cc/h`，const 数组，
  阶段 6 编译进固件或放分区）

## 脚本与产物

```text
tools/train_textcnn.py      # 读语料 → 训练 → 评估 → 存 float 模型 + 报告
tools/quantize_tflite.py    # 加载 float 模型 → 量化 → 对比 → 导出 tflite + C 数组
data/model/                 # 产物目录（不入库，gitignore）
├── model_float.keras
├── model_int8.tflite
├── model_int8.cc / model_int8.h
└── eval_report.md          # 准确率 / 混淆矩阵 / 拒识 / 量化对比 / 环境版本
```

## 实施顺序

1. 建 `.venv` + 装 TF（import 验证 + 记录版本）
2. `train_textcnn.py` 训练 → 读评估报告；不达 95% 先查语料（模板重叠 / 标签），
   再动超参
3. `quantize_tflite.py` 量化 → 精度对比 → 导出
4. 收尾：.gitignore / README / CLAUDE.md / ROADMAP 同步，提交

## 验收清单

- test 准确率 ≥ 95%；unknown 漏拒率有记录且低于规则基线
- int8 掉点 ≤ 2pp；模型体积 < 100 KB
- 同种子重跑，准确率波动 < 0.5pp（可复现）
- 评估报告完整（含 TF 版本、参数量、两向拒识指标）
- 阶段 4 产物未改动（训练只读 data/corpus/）

## 决策记要

| 决策 | 结论（2026-10-02） |
| --- | --- |
| 训练环境 | **Windows 直装**（已确认）：工程内 `.venv`（Anaconda 3.12 创建）+ pip tensorflow（CPU，清华镜像） |
| max_len | 22（取自阶段 4 报告，训练与部署一致） |
| 量化代表集 | train 分层抽 300 条（不用 test） |
| 产物目录 | `data/model/`（不入库，与 data/corpus 同理） |
| 模型文件格式 | `.keras`（float 存档）+ `.tflite`（int8 交付）+ `.cc/.h`（固件用） |
