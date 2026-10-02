# data/ 说明

本地生成的数据产物与第三方源文件，**不入库**（体积大，且为派生数据）。

## 中文字库

- `font16.bin`：16×16 中文字库（NLUF 格式），由 `tools/gen_font.py` 生成
- 字体源：Noto Sans SC / 思源黑体（SIL OFL 1.1），本地保存并随附许可文本

生成与刷写：

```bash
python tools/gen_font.py --font data/NotoSansSC.ttf --out data/font16.bin
python -m esptool --chip esp32s3 -p COM32 write_flash 0xC00000 data/font16.bin
```

storage 分区起始偏移 `0xC00000` 见 `partitions-16MiB.csv`；字库缺失时固件自动降级为占位框（不影响 ASCII 与 UI 框架）。

## 训练语料（阶段 4）

- `corpus/`：由 `tools/gen_corpus.py` 从语料源 `tools/intents.json` 确定性生成
  （固定种子，TSV / 词表可逐字节复现），供阶段 5 训练消费
- 内容：`train.tsv` / `val.tsv` / `test.tsv`（句子 + 意图标签）、
  `vocab.txt`（字符词表，行号即 id）、`report.md`（统计与自检报告）

```bash
python tools/gen_corpus.py          # 重新生成全部产物
```

语料源入库、产物不入库：阶段 5 训练前重跑一次即可保证与源同步。
