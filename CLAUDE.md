# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目目标

在正点原子 DNESP32S3 开发板上学习与测试 NLU（自然语言理解）模型：
串口 / WiFi 输入自然语言 → 板载意图识别 → 调用片上工具（LED / 蜂鸣器，
后续加 LCD）。

- **技术路线已定**：文本 NLU。理解层先关键词规则占位，阶段 4~6 用自训
  小模型（字符级 TextCNN → int8 量化 → TFLite Micro）替换，规则降级兜底
- **进度**：阶段 1（四层骨架与执行层）已完成并验收通过
- 分阶段计划见 `docs/ROADMAP.md`；阶段 1 实施依据见 `docs/STAGE1_DESIGN.md`

## 架构与硬件约定

- 四层解耦：输入层 → 理解层（nlu，出意图）→ 响应层（resp，出文本）→
  执行层（tools → rhythm 节奏任务）；工具层只向 `rhythm_q` 投命令、不碰硬件
- **节奏任务（`components/rhythm.c`）是全工程唯一写 XL9555 输出的地方**
  （蜂鸣器）；按键任务只读输入寄存器
- 板载极性：LED（GPIO1）低电平点亮；蜂鸣器经 XL9555（BEEP_IO）写 0 为响
- `components/` 为「目录即组件」：源文件平铺、不再分二级目录；改行为前先读
  `docs/` 对应阶段的细化设计

## 硬件平台

- 正点原子 DNESP32S3，模组 ESP32-S3-WROOM-1-N16R8：16 MB Flash +
  8 MB 八线 PSRAM（OCT 模式，80 MHz）
- 本机参考资料目录 `D:\project\资料\正点原子 DNESP32S3\`：
  - `1，课程源码\`：官方例程（00_basic ~ 17_Bluetooth），每个例程是独立
    工程，组件风格为 `components/BSP`（板级支持包）+ 功能组件
  - `DNESP32S3使用指南-IDF版_V1.7.pdf`：开发指南
  - `4，硬件资料\`：原理图（ATK_DNESP32S3_V1.2）、芯片手册

## 常用命令

需先有 ESP-IDF v6.1 环境（EIM 安装）。命令在工程根目录执行：

```bash
idf.py build                 # 构建
idf.py -p COMx flash monitor # 烧录并监视（退出监视器 Ctrl+]）
idf.py menuconfig            # 配置
idf.py fullclean             # 清理全部构建产物
```

## 配置管理约定

- **`sdkconfig` 不入库**（gitignore 中），芯片级关键配置固化在
  `sdkconfig.defaults`：16 MB Flash、自定义分区表、OCT PSRAM。改动这类
  配置时同步维护 defaults，保证 clone 后可复现。
- **分区表** `partitions-16MiB.csv`：factory 仅 1.9 MB（应用），vfs
  10 MB（FAT）与 storage 4 MB（SPIFFS）为预留数据区。引入较大模型或资源
  时先核对分区容量，必要时调整分区表与 defaults。
- 引入组件管理器（`idf_component.yml`）后：`dependencies.lock` 入库，
  `managed_components/` 不入库。

## 编辑器与语言服务

- `.clangd` 由本机 skill `ESP-clangd-setup` 生成，内容含本机工具链绝对
  路径，不入库。出现 `math.h not found` 之类误报时重跑该 skill；
  target 为 `xtensa-esp32s3-elf`。
- `.vscode/`、`.cache/` 不入库（含本机路径与索引缓存）。

## 版本红线

ESP-IDF v6.0 起有破坏性变更（如 legacy I2C 驱动 EOL、部分符号移除）。
本工程基线 v6.1，**引用网上教程或示例代码前先核对其所基于的 IDF 版本**。
