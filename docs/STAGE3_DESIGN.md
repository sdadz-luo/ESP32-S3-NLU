# 阶段 3 细化设计：屏幕接入

本文是《项目路线图》阶段 3 的实施依据：移植正点原子 SPI LCD BSP，自制中文字库并放入 flash 分区，实现「状态区 + 对话区」两区 UI，把阶段 2 的组合回复实时显示到 2.4 寸屏上。范围与完成标志见 ROADMAP 阶段 3 一节。

> 状态：设计稿，待确认后实施。

## 目标与范围

- 移植 SPI + ILI9341 LCD BSP（2.4 寸 320×240 横屏），上电自检可显示
- 中文字库：生成 12×12 点阵字库放 `storage` 分区，按 Unicode 码点取模渲染
- UI 两区布局：状态区（灯 / 蜂鸣器当前模式，轮询阶段 2 的状态快照）+ 对话区（最近交互滚动）
- 不包含：LVGL、触摸、背光控制（硬件常亮）、WiFi（阶段 7）

## 硬件事实与原厂例程核查

- LCD：ILI9341 SPI 屏；SPI2（MOSI=GPIO11 / CLK=GPIO12 / MISO=GPIO13），DC=GPIO40、CS=GPIO21；电源与复位经 XL9555（`SLCD_PWR_IO` / `SLCD_RST_IO`）
- **GPIO40 是复用脚**：`xl9555_init` 把它配成输入（XL9555 INT），`lcd_init` 会改配为输出（LCD DC）——初始化顺序必须 `xl9555` 在前、`lcd` 在后。本项目未使用 XL9555 中断（按键走 I2C 轮询），无功能损失
- 背光 `LCD_BL_IO` 原厂例程从未使用（该屏硬件常亮）→ 本阶段不控制，**XL9555 运行时写者仍只有节奏任务**，不引入互斥层；若将来要控背光（息屏），按 STAGE1_DESIGN 预案收口 io_ext
- 屏幕型号：`SPI_LCD_TYPE=1` 为 2.4 寸（320×240）；若手上是 1.3 寸屏需改宏（初始化寄存器组不同）

## BSP 移植与裁剪

- 移植 `spi.c/h`、`lcd.c/h`、`lcdfont.h`（ASCII 12/16/24/32 点阵）到 `components/` 平铺，自有文件头注释，来源登记进 README「第三方代码来源」
- **裁剪 `lcd_buf`**：原厂为全屏缓冲 153,600 B（`LCD_TOTAL_BUF_SIZE`）→ 缩到 15,360 B（`LCD_BUF_SIZE`），`lcd_clear` 改为循环多次分块写；省约 138 KB 内部 RAM，刷屏耗时不变（DMA 分块）
- SPI 参数保持原值：SPI2_HOST、60 MHz、模式 0、DMA 自动通道、轮询事务（阻塞，写屏期间独占任务）
- 不引入新组件：`driver` 已含 spi_master；`lcd.c` 只依赖 gpio/spi/xl9555

## 中文字库

**字库文件格式**（生成产物，本机存放，不入库）：

```text
[头 16 B]  magic "NLUF" | u32 版本 | u16 宽 | u16 高 | u8 区间数 | u8 行字节 | 保留
[区间表]   每区间 { u32 起码点, u32 止码点, u32 数据偏移 }（按码点升序）
[数据区]   每字 行字节×高 字节，行优先、字节内 MSB 在左（与 asc2_1608 取模一致）
```

- 覆盖区间：U+3000–303F（中文标点）、U+4E00–9FFF（CJK 基本区 20902 字）、U+FF01–FF5E（全角字符）——保证任意用户输入都能显示
- 体积：12×12 → 24 B/字，三段合计约 2.9 万字 ≈ 690 KB，`storage` 分区 4 MB 放得下
- **存储方式：`storage` 分区原始块（偏移 0）**，固件用 `esp_partition_read` 直读（无文件系统开销、免挂载）；SPIFFS / FAT 留到阶段 6 有模型文件等真实多文件需求时再启用
- 刷写：`python -m esptool -p COM32 write_flash 0xC00000 data/font12.bin`（storage 起始偏移取自分区表）
- 生成：`tools/gen_font.py`（Python + Pillow）。字体源推荐**缝合像素字体（Fusion Pixel Font，SIL OFL 1.1，12px 点阵原生设计，边缘锐利）**；备选思源黑体 / Noto Sans SC（OFL 1.1，16px 轮廓渲染，字形更饱满但略糊）。脚本头注明字体名 / 版本 / 许可；**生成产物不入库**（体积大且为派生数据）
- 降级：字库缺失或校验失败时，中文渲染为占位方块并打启动告警，ASCII 与 UI 不崩

## 渲染与 UI

**font12 模块（新增）**：UTF-8 解码（自写约 20 行）；CJK 码点走字库分区取 12×12 字模，ASCII 走 `asc2_1206`（同为 12px 高，配对自然）；命中失败画占位块。

**ui 模块（新增）**：

- `ui_q`：深度 4，元素 `{ char in[128]; char out[192]; }`（一条交互的输入与回复）
- `task_display`：优先级 3、栈 4096（起手值）、Core 1。阻塞在 `ui_q`（超时 200 ms）——收到交互则追加对话区并滚动；超时则轮询 `rhythm_led_mode()` / `rhythm_beep_mode()`，变化时重绘状态区（与节奏引擎同一套「超时轮询」模式）
- 布局（320×240 横屏，12px 字号）：状态区 y 0–23（如「灯: 常亮   蜂鸣器: 停止」），分隔线 y 24，对话区 y 26–239（约 15 行）；对话行格式「你> …」（青色）/「板> …」（白色），长文本按 320 px 宽折行，满屏上滚
- 局部重绘：状态区与对话区各自独立刷新，不做全屏重绘（避免闪烁）

**接口变化**：

```c
/* font12.h */
bool font12_load(void);                                              /* 查找并校验字库分区 */
/* ui.h */
void ui_post(const char *in, const char *out);                       /* 投递一条交互（满则丢弃+告警） */
void task_display(void *arg);
/* resp.h（签名扩展） */
void resp_send(const char *text);                    /* 行为：串口 + ui_post("", text) */
void resp_compose(intent_t intent, bool executed, const char *input); /* 增加 input 供对话区显示 */
```

## 并发与资源

- LCD / SPI 唯一写者 = `task_display`；`lcd_init` 在任务创建前（单线程阶段）完成；XL9555 运行时写者不变（仍只有节奏任务）
- 内存增量：静态约 15 KB（裁剪后 `lcd_buf`）+ 队列 1.3 KB + 任务栈 4 KB + 对话行缓冲约 1 KB；字库逐字读（24 B/字），无大缓冲
- App 镜像增量约 110 KB（BSP 代码 + ASCII 字模），factory 1.9 MB 余量充足

## 实施顺序

1. 移植 spi / lcd / lcdfont（含缓冲裁剪）→ 上电自检：清屏 + 一行 ASCII 文本
2. font12 模块 + `tools/gen_font.py` + 刷写流程 → 屏上中文渲染验证
3. ui 模块：`ui_q` + `task_display` + 两区静态框架
4. 接入数据流：`resp_send` / `resp_compose` 投递 ui 消息；状态区轮询联动
5. 验收清单 + 文档收尾（README / CLAUDE.md / ROADMAP）

## 验收清单

- 上电：屏亮、两区框架与「就绪」正常显示
- 串口「开灯」→ 对话区出现「你> 开灯」「板> …灯已点亮…」；状态区「灯: 常亮」同步
- 「闪 / 报警 / 停止」→ 状态区实时变化；按键通道（KEY0~3）同样反映到屏上
- 中文渲染无乱码、标点正确、长回复折行、多轮交互滚动正常
- 字库缺失（未刷 font12.bin）：中文显示占位块 + 启动告警，界面不崩
- 连跑 10 分钟：无 task_wdt；`task_display` 栈余量 ≥500 B（用阶段 1 同款水位打印复核）
- 阶段 1 / 2 验收项无回归；构建零告警

## 待定决策点

| 决策 | 倾向 |
| --- | --- |
| 字体源：缝合像素字体 12px vs 思源黑体 16px | 缝合像素字（OFL 1.1、点阵原生、体积小）；若嫌小再换 16px |
| 字库覆盖：三段全量 vs 仅收录固件用字 | 三段全量（约 690 KB），支持任意用户输入 |
| 背光控制（息屏 / 调光） | 本阶段不做；要做时先收口 io_ext 互斥层 |
