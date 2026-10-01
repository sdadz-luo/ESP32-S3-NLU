# ESP32-S3-NLU

基于正点原子 DNESP32S3 开发板的学习型工程：在 ESP32-S3 上跑 NLU（自然语言
理解）模型，用于学习与测试。工程处于起步阶段（空模板），技术路线探索中。

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

## 目录结构

```text
.
├── main/                  # 应用入口（app_main）
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

正点原子配套资料（课程源码、使用指南、原理图、芯片手册）不随本仓库分发，
位于开发主机 `D:\project\资料\正点原子 DNESP32S3\`。

## 许可证

[MIT](LICENSE)
