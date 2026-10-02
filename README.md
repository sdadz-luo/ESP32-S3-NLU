# ESP32-S3-NLU

基于正点原子 DNESP32S3 开发板的学习型工程：在 ESP32-S3 上跑 NLU（自然语言理解）模型——串口 / WiFi 输入自然语言，板载意图识别后调用片上工具（LED / 蜂鸣器，后续加 LCD）。技术路线：理解层先关键词规则占位，阶段 4~6 用自训 int8 小模型（TFLite Micro）替换。

当前进度：阶段 1（骨架与执行层）、阶段 2（响应层 2.0）已完成并验收通过。

## 硬件平台

- 开发板：正点原子 DNESP32S3（模组 ESP32-S3-WROOM-1-N16R8）
- 主控：ESP32-S3，Xtensa LX7 双核 @ 240 MHz
- 存储：16 MB Flash + 8 MB 八线 PSRAM
- 板载音频编解码：ES8388

## 软件环境

- ESP-IDF v6.1（EIM 安装，VS Code 用 ESP-IDF 扩展打开本工程）
- 目标芯片：`esp32s3`

## 构建与烧录

```bash
# 构建
idf.py build

# 烧录并打开串口监视器（把 COM5 换成实际端口）
idf.py -p COM5 flash monitor
```

退出串口监视器：`Ctrl+]`。

## 当前功能（阶段 1）

串口监视器里直接输入并回车，或按板载按键：

| 输入说法 | 动作 |
| --- | --- |
| 开灯 / 请帮我点灯 / LED亮 | LED 常亮 |
| 闪 / 闪烁 | LED 5Hz 闪烁 |
| 关灯 / 停止闪烁 / 别闪了 | LED 常灭 |
| 报警 / 警报 | 蜂鸣器 2Hz |
| 呼吸 / 慢速 | 蜂鸣器 0.5Hz |
| 停止 / 全停 / 暂停 | LED 与蜂鸣器全停 |
| 停止报警 / 别响了 | 蜂鸣器停止 |
| 其他 | 拒识（“抱歉，我没听懂”） |

按键为调试通道（绕过理解层）：KEY0 报警 / KEY1 全停 / KEY2 开灯 / KEY3 关灯。回复为组合式文本（前缀 × 主体 × 跨通道补语 × 后缀，措辞随机），细节见 `docs/STAGE2_DESIGN.md`。

## 目录结构

```text
.
├── main/                  # 应用入口（app_main）
├── components/            # 四层应用代码（目录即组件，源文件平铺）
├── docs/                  # 路线图与各阶段细化设计
├── partitions-16MiB.csv   # 自定义分区表
├── sdkconfig.defaults     # 固化的关键配置（Flash / PSRAM / 分区表）
├── sdkconfig              # 本机生成的完整配置（不入库）
└── .clangd                # clangd 配置（本机生成，不入库）
```

## 分区表

`partitions-16MiB.csv` 的划分（16 MB Flash）：

| 分区 | 类型 | 大小 | 用途 |
| --- | --- | --- | --- |
| nvs | data/nvs | 24 KB | 键值存储 |
| phy_init | data/phy | 4 KB | RF 校准数据 |
| factory | app | 1.9 MB | 应用程序 |
| vfs | data/fat | 10 MB | FAT 文件系统（预留） |
| storage | data/spiffs | 4 MB | SPIFFS 文件系统（预留） |

vfs 与 storage 为预留数据区，可存放模型文件与音频资源。

## 资料

正点原子配套资料（课程源码、使用指南、原理图、芯片手册）不随本仓库分发，位于开发主机 `D:\project\资料\正点原子 DNESP32S3\`。

## 第三方代码来源

`components/` 下的 BSP 驱动（`led.c/h`、`iic.c/h`、`xl9555.c/h`）移植自正点原子 DNESP32S3 配套例程 `08_iic_exio`（`components/BSP`），版权归广州市星翼电子科技有限公司所有，此处仅作学习用途；本地适配见各文件头注释。

## 许可证

[MIT](LICENSE)
