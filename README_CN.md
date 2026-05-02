# JkkSchedule

JkkSchedule 是一个适用于 ESP32 和 ESP-IDF 项目的轻量级调度库。

该库作为可复用的 ESP-IDF 组件打包发布，可通过 ESP Component Registry 获取。

支持功能：
- 按星期几触发的调度
- 按日期触发的调度
- 相对时间间隔调度
- 基于日出/日落时间的调度（支持设备级地理坐标）
- NVS 持久化存储
- 单个共享主定时器，运行时效率高

## 项目状态

本库面向生产环境，从一个较大的私有项目中提取为独立组件。

### 灵感来源与重写说明

JkkSchedule 受到 Espressif 官方 schedule 组件的启发。
本实现并非原版的直接移植，而是经过大幅重构与修正：
包括日历处理修复、日出/日落行为修复，以及重新设计的定时器调度策略。

## 许可证

Apache License 2.0。详见 [LICENSE](LICENSE)。

## 系统要求

- ESP-IDF 5.5、6.0（已测试版本范围）
- FreeRTOS（由 ESP-IDF 提供）
- NVS（`nvs_flash` 组件）

## 安装（ESP-IDF 组件方式）

通过 ESP Component Registry：

```yaml
dependencies:
    MacWyznawca/JkkSchedule: "^1.0.0"
```

或使用 CLI：

```bash
idf.py add-dependency "MacWyznawca/JkkSchedule^1.0.0"
```

手动安装选项：

将本仓库作为组件复制到项目中：
- `components/JkkSchedule`

或将其添加为项目组件目录中的 git 子模块。

在项目源码中引用：

```c
#include "JkkSchedule.h"
```

## 快速开始

```c
jkk_schedules_handle_t *sched = jkk_schedule_init(
    NULL,
    "sched0",
    5212345,   // 纬度 × 100000
    2101234    // 经度 × 100000
);

if (sched) {
    jkk_schedule_get_all(sched);
    jkk_schedule_run_all(sched, false);
}
```

## 基本使用流程

1. 使用 `jkk_schedule_init(...)` 创建调度管理器
2. 使用 `jkk_schedule_get_all(...)` 从 NVS 加载调度
3. 注册回调（`jkk_schedule_callback_all(...)` 或逐个调度注册）
4. 创建或编辑调度项
5. 使用 `jkk_schedule_run_all(...)` 启动运行时定时器

## API 说明

- 坐标以 `int32_t` 类型传入，格式为 E5（即实际值 × 100000）。
- 日出/日落模式使用管理器句柄中设置的坐标。
- 若坐标无效，日出/日落调度将自动回退为固定时刻触发。

## 日出/日落与时间边界

`SUNRISE` 和 `SUNSET` 调度类型支持可选的**时间边界**。
通过在触发配置中设置合法的 `hours`（0–23）和 `minutes`（0–59）字段来启用。
若两个字段均超出范围（默认状态），调度将纯粹按天文时间触发。

**选择规则：**

| 类型 | 有边界时的行为 | 效果 |
|---|---|---|
| `SUNRISE` | `min(日出时间, hours:minutes)` | 不晚于指定时间 |
| `SUNSET` | `max(日落时间, hours:minutes)` | 不早于指定时间 |

**示例 — 冬季傍晚：**
为防止冬季日落过早（可能在 15:30）导致灯光过早切换为暖白色，
可将类型设为 `SUNSET`，边界设为 `18:00`。
冬季将在 18:00 触发，夏季则在日落后触发（当日落晚于 18:00 时）：

```c
jkk_schedule_config_t cfg = {
    .name = "evening_warm_white",
    .trigger = {
        .type    = JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNSET,
        .hours   = 18,   // 不早于 18:00
        .minutes = 0,
        .day.repeat_days = JKK_SCHEDULE_DAY_EVERYDAY,
    },
    .trigger_cb = evening_cb,
};
```

**`SUNRISE` 的类似用法** — `hours:minutes` 作为上限（例如：即使冬季日出较晚，也确保"闹钟不晚于 07:30"触发）。

> 若地理坐标未设置或无效，库将仅使用固定时间 —— `hours:minutes` 此时等同于普通的每日时刻触发。

## 示例

参见：
- [examples/esp-idf/basic](examples/esp-idf/basic)
- [examples/esp-idf/sun_events](examples/esp-idf/sun_events)
- [examples/arduino/basic_not_tested](examples/arduino/basic_not_tested)

## Arduino 兼容性

本库专为 ESP-IDF 组件设计。

实验性 Arduino 示例位于：
- [examples/arduino/basic_not_tested](examples/arduino/basic_not_tested)

状态：
- 未经测试
- 仅作为起点参考

在 Arduino 环境下使用需满足以下条件：
- 目标平台为 ESP32（Arduino-ESP32）
- 项目采用 ESP-IDF 集成方式构建（例如将 Arduino 作为 ESP-IDF 组件使用）
- 在加载调度前已初始化 `nvs_flash`
- 标准 C 时间函数可用，且已配置 SNTP / 时区以确保日出/日落精度

若使用纯 Arduino Library Manager 风格（不依赖 ESP-IDF 组件模型），
则需要进行适配，因为本代码依赖：
- `esp_err.h`
- FreeRTOS 定时器
- ESP-IDF NVS API

本仓库还提供：
- `library.properties`
- `src/JkkSchedule.h` Arduino 风格的包含发现转发头文件

## 仓库结构

- `include/JkkSchedule.h` — 公共 API
- `src/JkkSchedule.c` — 调度核心
- `src/JkkScheduleNvs.c` — NVS 持久化层
- `src/JkkScheduleInternal.h` — 内部声明
- `examples/` — 参考集成示例

## 致谢

- 感谢 Espressif Systems 发布了原始调度组件的设计概念。
- 感谢所有在真实设备上验证行为的贡献者和用户。
