# 阶段 1 细化设计：工程骨架与执行层

本文是《项目路线图》阶段 1 的实施依据：在 ESP-IDF 工程内搭起四层骨架（输入 / 理解 / 响应 / 执行），用 FreeRTOS 任务与命令队列把执行层串成可运行的系统。范围与完成标志见 ROADMAP 阶段 1 一节。

> 状态：已完成，2026-10-02 验收通过。

## 目标与范围

- 执行层：LED（常亮 / 常灭 / 5Hz）与蜂鸣器（0.5Hz / 2Hz / 停止）两类工具，节奏可被停止类命令即时终止
- 输入层：串口任务（读一行文本）、按键任务（轮询 XL9555，调试通道直调工具）
- 理解层：关键词规则占位（阶段 6 原位替换为端侧模型，规则保留为兜底）
- 响应层：固定回复文本（阶段 2 升级为模板组合）
- 不包含：屏幕、WiFi、模型（分别为阶段 3 / 7 / 4~6）

## 目录与文件结构

```text
main/
├── CMakeLists.txt       # 保持现状，不动
└── main.c               # 只保留启动序列（见「初始化顺序」）
components/
├── CMakeLists.txt       # 组件注册（列出全部源文件）
├── app.c / app.h        # 启动：硬件初始化、队列、任务创建
├── app_types.h          # 枚举、队列元素类型、常量
├── app_queues.c / .h    # cmd_q 与 rhythm_q 的创建与句柄
├── input_serial.c / .h  # task_serial：fgets 读一行 → cmd_q
├── input_key.c / .h     # task_key：轮询按键 → 直调工具
├── nlu.c / nlu.h        # task_nlu 循环 + 关键词规则（阶段 6 替换点）
├── resp.c / resp.h      # resp_send + 意图→回复文本
├── tools.c / tools.h    # 工具函数（投 rhythm_q，不阻塞）
├── rhythm.c / rhythm.h  # task_rhythm：节拍引擎 + 物理输出（唯一执行者）
├── led.c / led.h        # BSP 移植：LED（GPIO1）
├── iic.c / iic.h        # BSP 移植：I2C 主机
└── xl9555.c / xl9555.h  # BSP 移植：IO 扩展（蜂鸣器 / 按键）
```

布局说明：components/ 采用 ESP-IDF 的「目录即组件」模式——目录内有 CMakeLists.txt 时不再扫描其子目录，目录自身注册为一个组件（依据：ESP-IDF `tools/cmake/project.cmake` 中 `__project_component_dir()`，目录含 CMakeLists.txt 时以目录自身为组件；工程 main/ 走同一路径注册）。因此所有新建文件平铺在 components/ 下、不再分二级目录；组件名取自目录名，即 `components`。

## 构建与配置

components/CMakeLists.txt（新建）：

```cmake
idf_component_register(
    SRCS
        "app.c" "app_queues.c" "input_serial.c" "input_key.c"
        "nlu.c" "resp.c" "tools.c" "rhythm.c"
        "led.c" "iic.c" "xl9555.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver esp_timer
)
```

main/CMakeLists.txt 保持现状（main 组件自动依赖所有组件，无需加 REQUIRES）。

sdkconfig.defaults 增加两条：

```text
CONFIG_FREERTOS_HZ=1000              # tick=1ms，固化，防重建 sdkconfig 时静默回落
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y  # 关闭 USB Serial/JTAG 次级 console（无主机消费时写阻塞 50ms/次）
```

注意：defaults 只对缺失的 sdkconfig 生效，修改后需删除现有 sdkconfig 重建。

## 行为规格（命令语义）

- 设备通道相互独立：`led_*` 只作用于灯、`beep_*` 只作用于蜂鸣器；两者可同时运行（灯闪 + 报警），互不干扰
- 停止统一由停止类命令完成：裸停止词（停止 / 停下 / 暂停 / 全停）→ 全部停止；带对象的停止说法（"停止闪烁 / 别闪了"归 led_off，"停止报警 / 别响了"归 beep_off）——说法池扩充，不新增意图
- 同设备新模式命令 = 该设备模式切换（闪烁中"开灯"→ 常亮），不涉及跨设备影响

意图表（8 个）与匹配优先级（按序匹配，顺序 strstr，UTF-8 子串匹配安全）：

| 优先级 | 条件 | 意图 |
| --- | --- | --- |
| 1 | 停止词 + 蜂鸣器词（响 / 蜂鸣 / 叫 / 报警） | beep_off |
| 2 | 停止词 + 灯词（灯 / LED / 闪） | led_off |
| 3 | 停止词单独出现 | stop（全停） |
| 4 | 含「闪」 | led_blink |
| 5 | 蜂鸣器词 + 报警词（报警 / 警报 / 急促） | beep_alarm |
| 6 | 蜂鸣器词 + 呼吸词 | beep_breath |
| 7 | 灯词 + 开 / 亮 / 点 | led_on |
| 8 | 灯词 + 关 / 灭 | led_off |
| 9 | 单独「报警 / 警报」 | beep_alarm |
| 10 | 其余 | unknown（回复"抱歉，我没听懂"） |

规则为阶段 1 占位：阶段 4 以语料为准精化，阶段 6 由模型替换、规则降级为兜底。

## 任务与通信

| 任务 | 优先级 | 栈（字节） | 核 | 阻塞点（让出时机） |
| --- | --- | --- | --- | --- |
| task_rhythm | 8 | 3072 | 1 | xQueueReceive(rhythm_q, 到下次翻转的剩余时间) |
| task_key | 6 | 3072 | 1 | vTaskDelay(20ms)（key_scan 内另有 10ms 去抖） |
| task_nlu | 5 | 4096 | 1 | xQueueReceive(cmd_q, portMAX_DELAY) |
| task_serial | 4 | 3072 | 1 | fgets(stdin) |

- 优先级取 4~8：内置任务占 lwIP 18 / esp_event 20 / esp_timer 22 / WiFi 23，应用任务远离该区间，9~17 留给阶段 6/7
- 全部绑 Core 1：WiFi / lwIP / esp_timer 在 Core 0（物理隔离）；阶段 6 的 TFLite 使用 float 时内核会钉核，显式绑定避免行为漂移
- 栈为起手值，用 uxTaskGetStackHighWaterMark 校准（保留 >= 500 B）

队列：

- cmd_q：深度 8，元素 `{ char text[128]; uint8_t src; }`；写者 task_serial（阶段 7 增 WiFi），读者 task_nlu；满时丢弃 + 日志，不阻塞输入
- rhythm_q：深度 4，元素 `{ uint8_t target; uint8_t mode; }`；写者 tools 层（task_nlu / task_key 经由），读者 task_rhythm；满属异常，丢弃 + 日志
- 按键不进队列：调试通道语义，task_key 直接调工具函数；工具函数只投队列后立即返回

节奏引擎（task_rhythm 核心机制）：

- 两路各自维护 `{ mode, next_us, half_us }`；翻转间隔：LED 闪烁 100ms；蜂鸣器报警 250ms、呼吸 1000ms
- 每轮取最近 deadline，以「到 deadline 的剩余时间」为超时阻塞在 rhythm_q：超时 → 对到期路翻转电平、next_us += half_us（旧值累加，长期无累积漂移）；收到命令 → 立即应用新模式基态（打断延迟亚毫秒）
- 模式基态：OFF → 灭 / 停；ON / BLINK → 亮起（BLINK 首翻在 +100ms）；BREATH / ALARM → 响起
- 物理电平：LED（GPIO1）低电平点亮；蜂鸣器（XL9555，BEEP_IO）写 0 为响

并发纪律：

- XL9555 输出寄存器只允许 task_rhythm 写（蜂鸣器）；按键只读输入寄存器，两者寄存器不同且单笔事务有驱动锁，交错无害
- xl9555_key_scan 内部含函数级 static 状态，全工程唯一调用者 = task_key
- 阶段 3 引入 LCD 背光（第二输出写者）时，把 XL9555 输出访问收口为带互斥的 io_ext 层

## 关键接口

```c
/* app_types.h */
typedef enum { LED_OFF, LED_ON, LED_BLINK } led_mode_t;        /* BLINK = 5Hz */
typedef enum { BEEP_OFF, BEEP_BREATH, BEEP_ALARM } beep_mode_t; /* 0.5Hz / 2Hz */
typedef enum { I_UNKNOWN, I_LED_ON, I_LED_OFF, I_LED_BLINK,
               I_BEEP_BREATH, I_BEEP_ALARM, I_BEEP_OFF, I_STOP } intent_t;
typedef struct { char text[128]; uint8_t src; } cmd_msg_t;      /* src 预留 SRC_WIFI */
typedef struct { uint8_t target; uint8_t mode; } rhythm_cmd_t;
```

```c
/* app.h    */ void app_hw_init(void);
               void app_queues_init(void);
               void app_tasks_start(void);   /* rhythm → nlu → key → serial */
/* nlu.h    */ intent_t nlu_understand(const char *text);
/* tools.h  */ esp_err_t tool_led_set(led_mode_t m);
               esp_err_t tool_beep_set(beep_mode_t m);
               esp_err_t tool_stop_all(void);
/* resp.h   */ void resp_send(const char *text);
```

## 初始化顺序

main/main.c 保留为唯一入口，内容即启动序列：

```c
#include "app.h"

void app_main(void)
{
    app_hw_init();      /* LED / IIC / XL9555，单线程阶段完成 */
    app_queues_init();  /* cmd_q / rhythm_q */
    app_tasks_start();  /* rhythm → nlu → key → serial，先汇点后生产者 */
    resp_send("就绪：输入命令，或按 KEY0~3 调试");
    /* 返回后 main 任务自动删除；禁止 while(1) 忙等（会饿死 IDLE0 触发看门狗） */
}
```

## 实施顺序

1. 配置：defaults 增加两条 + 删除 sdkconfig 重建，空工程 build 通过
2. 骨架：components/ 目录 + CMakeLists + 空实现文件，build 通过（验证「目录即组件」生效）
3. BSP：移植 LED / IIC / XL9555，串口打点验证读写
4. 执行层：队列 + 四任务 + 工具层 + 节奏引擎，验收节奏与停止
5. 理解与响应：关键词规则 + 串口输入 + 按键映射，走完整验收清单
6. 更新 README / CLAUDE.md，提交

每步验证后提交一次。

## 验收清单

- 串口：「开灯」→ 常亮；「闪」→ 5Hz；「关灯」→ 灭；「报警」→ 2Hz；「呼吸」→ 1s 翻转；「停止」→ 全停
- 按键（调试通道）：KEY0 → 报警；KEY1 → 全停；KEY2 → 开灯；KEY3 → 关灯
- 双通道并行：灯闪 + 报警同时运行互不干扰；「停止」后全部停止（亚毫秒级响应）
- 连跑 10 分钟：无 task_wdt 告警；各任务栈余量 >= 500 B；两队列无丢弃
- 节奏精度：翻转间隔抖动为毫秒级（1ms tick 量级）

## 附录：关键事实

- 板载电平：LED 接 GPIO1、低电平点亮；蜂鸣器经 XL9555（BEEP_IO = 0x0008）驱动、写 0 为响；按键 KEY0~3 在 XL9555 输入寄存器
- BSP 来源：移植自配套例程 08_iic_exio 的 components/BSP（LED / IIC / XL9555），文件头为自有注释、版权与来源声明集中在 README；该 BSP 使用 legacy I2C 驱动（v6.1 弃用告警、v7.0 移除），阶段 1 接受
- 组件机制依据：ESP-IDF `tools/cmake/project.cmake` 的 `__project_component_dir()`——目录含 CMakeLists.txt 时以目录自身为组件（"The directory itself is a valid idf component"），工程 components/ 与 main/ 均经此路径注册
