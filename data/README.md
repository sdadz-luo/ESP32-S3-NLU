# data/ 说明

本地生成的字库产物与字体源，**不入库**（体积大，且为派生/第三方数据）。

- `font16.bin`：16×16 中文字库（NLUF 格式），由 `tools/gen_font.py` 生成
- 字体源：Noto Sans SC / 思源黑体（SIL OFL 1.1），本地保存并随附许可文本

生成与刷写：

```bash
python tools/gen_font.py --font data/NotoSansSC.ttf --out data/font16.bin
python -m esptool --chip esp32s3 -p COM32 write_flash 0xC00000 data/font16.bin
```

storage 分区起始偏移 `0xC00000` 见 `partitions-16MiB.csv`；字库缺失时固件自动降级为占位框（不影响 ASCII 与 UI 框架）。
