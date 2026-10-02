# 阶段 6 细化设计：端侧集成

本文是《项目路线图》阶段 6 的实施依据：接入 esp-tflite-micro，把阶段 5 的
int8 模型编译进固件，用「模型（带阈值）+ 规则兜底」双层决策替换纯规则理解，
完成板端验收。范围与完成标志见 ROADMAP 阶段 6 一节。

> 状态：已完成，2026-10-02 验收通过。板端双层决策运行正常（阈值 0.70，test
> 集 99.1%，规则基线 82.8%）；现编 20 句 18/20 = 90%（纯规则同批 17/20）；
> 降级路径已验证（arena 缩至 1 KB 时正确回退纯规则）；nlu 栈水位余 6548 B；
> 固件 379 KB（分区余 81%）。实施中经历两轮语料迭代（补「叫 / 出声 / 太暗 /
> 不用了」等说法）——阶段 8 迭代闭环的首次实跑，剩余边界句（"吵死了快停"）
> 留待真实输入继续迭代。

## 目标与范围

- C 侧 tokenizer：UTF-8 解码 → 码点查词表 → id 序列 → pad 到 max_len（22）
- TFLM 推理：模型数据编译进固件（flash rodata），MicroInterpreter +
  精确注册所需算子（不用 AllOpsResolver）
- 双层决策：模型 top-1 概率 ≥ 阈值 → 采用；否则规则兜底；都不中 → unknown
  （沿用现有拒识回复，两道闸门落地）
- 阈值调优：PC 侧用 test 集扫参与模拟，板端现编句验证后定稿
- 验收：现编口语 20 句通过率 ≥ 2/3（对照规则基线）
- 不包含：WiFi 通道（阶段 7）、模型热更新（不必要）

## 预研结论（2026-10-02 实测）

- esp-tflite-micro **1.4.0** 经 IDF 组件管理器接入（传递依赖 esp-nn 加速库），
  IDF v6.1 解析正常；`dependencies.lock` 入库、`managed_components/` 不入库
- 模型算子 18 个，TFLM 基本支持；**GATHER 支持 int8 权重 + int32 索引**
  （量化 embedding 的形态）——预判的风险点基本解除
- 实施中发现：`keras.layers.Embedding` 的负索引语义在转换图里引入
  LESS / ADD / SELECT_V2 三个算子，**TFLM 的 SELECT_V2 不支持 int32**，
  板端 Invoke 失败——改用裸 `tf.gather` 的自定义层（nlu_data.CharEmbedding）
  后这组算子消失，算子收敛为 9 种：GATHER / EXPAND_DIMS / CONV_2D /
  RESHAPE / REDUCE_MAX / CONCATENATION / FULLY_CONNECTED / SOFTMAX / DEQUANTIZE
- 模型接口：输入 int32[1, 22]（token id），输出 float32[1, 8]（softmax 概率）

## 模型进固件的方式

`tools/quantize_tflite.py` 扩展：除 `data/model/` 产物（不入库）外，导出
**固件源文件（入库）**：

```text
components/nlu_model_data.cc / .h   g_nlu_model[]（alignas(8)）+ 长度 + 宏
components/nlu_vocab.h              词表码点数组（id = 下标）
```

- 生成文件入库的理由：它们是固件的编译输入（性质同 `lcdfont.h`），保证
  clone 后可构建；模型与源码版本一一对应、可追溯
- 模型数据在 flash rodata：TFLM 直接经 cache 读，零拷贝、不占 RAM
- 更新模型 = 重跑量化脚本 + 重新构建（脚本直接写入 `components/`）

## 模块设计

### nlu_model.c / h（新增）

```c
/* 初始化：建 resolver / interpreter 并 AllocateTensors；失败返回错误码 */
esp_err_t nlu_model_init(void);
/* 推理：文本 → token id → invoke → {intent 索引, 置信度} */
esp_err_t nlu_model_predict(const char *text, int *intent, float *confidence);
```

- tokenizer：UTF-8 逐字节解码（自写约 20 行）→ UTF-8 码点线性查表
  （369 项 × 22 字符/句，微秒级）→ 词表外字符跳过 → 补 pad(0)
- tensor arena：静态数组，16 KB 起步（AllocateTensors 失败即降级）
- 输出类别索引与 `intent_t` 枚举序一致（阶段 5 已按枚举序训练）

### nlu.c（改造）

- task_nlu：`nlu_model_predict` → 置信度 ≥ `NLU_CONF_THRESHOLD` → dispatch；
  否则 `nlu_understand`（规则）→ dispatch；规则也给 unknown 则拒识
- 模型初始化失败（返回非 OK）：永久降级纯规则，启动日志告警
- 日志扩展：`src=0 "开灯" -> led_on (model 0.98)` / `-> led_off (rule)`，
  供阈值分析与验收判读
- task_nlu 栈 4096 → 8192（TFLM 调用链），用高水位实测校准

### tools/tune_threshold.py（新增）

- PC 侧用 TFLite 解释器跑全 test 集，得每句 top-1 置信度
- 模拟双层决策（复用 `rule_baseline.understand` 作兜底），扫阈值 0.3~0.9，
  输出各阈值下准确率 / 漏拒 / 误拒表
- 选定阈值写入 `nlu.c` 的宏（初始值 0.6）

## 内存与性能预算

| 项 | 预算 | 说明 |
| --- | --- | --- |
| Flash 增量 | 约 250 KB（模型 44 KB + TFLM 库） | factory 现余量 1.7 MB+ |
| 内部 RAM | arena 16 KB + interpreter 开销（静态） | 不占 PSRAM |
| 推理耗时 | 未单独计时（验收期间响应即时、无感知延迟） | 需要时可用 esp_timer 打点补测 |

## 实施顺序

1. `quantize_tflite.py` 扩展 → 生成固件源文件到 `components/`
2. `nlu_model.c/h`：tokenizer + TFLM 封装
3. `nlu.c` 改造：双层决策 + 日志 + 降级
4. 构建烧录，串口验证模型生效与降级路径
5. `tune_threshold.py` 调阈值 → 定稿重编译
6. 现编句验收（20 句 ≥ 2/3）+ 阶段 1~5 回归
7. 收尾：README / CLAUDE.md / ROADMAP 同步，提交

## 验收清单

- 构建零警告；固件与 RAM 在预算内
- 串口输入 → 日志显示模型判定与置信度；回复 / 屏幕行为与之前一致
- 阈值调优报告（test 集扫描）在手；现编 20 句 ≥ 2/3，且同批句规则基线明显更低
- 模型 init 失败时降级纯规则、不崩（人为构造验证一次）
- 阶段 1~5 验收项无回归（按键滚动、屏幕状态区、双通道节奏、拒识文案）
- task_nlu 栈余量 ≥ 500 B；堆无持续增长

## 决策记要

| 决策 | 结论（2026-10-02） |
| --- | --- |
| 模型进固件方式 | **编译进固件**（flash rodata，零拷贝）：免运行时加载 / 校验 / 降级复杂度，刷写流程不变；代价是仓库多约 300 KB 生成源，接受 |
| 生成文件入库 | `nlu_model_data.cc/h`、`nlu_vocab.h` 入库（固件编译输入）；`data/model/` 仍不入库 |
| 嵌入层实现 | **裸 `tf.gather` 自定义层**（`nlu_data.CharEmbedding`），替代 Keras Embedding：后者负索引逻辑引入 TFLM 不支持的 SELECT_V2(int32)，板端 Invoke 失败 |
| 算子注册 | MicroMutableOpResolver 精确注册 9 种，不用 AllOpsResolver（省 flash） |
| 决策阈值 | **0.70**——tune_threshold.py 在 test 集扫描，最优区间 0.45~0.75（99.1%），取区间上部（边界置信度交给规则兜底） |
