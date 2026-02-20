#施工中
---


# EasyLoRa (Powered by LoRaPlat)

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-STM32%20%7C%20ESP32%20%7C%20Any%20MCU-green.svg)
![Language](https://img.shields.io/badge/language-C99-orange.svg)

**EasyLoRa** 是一个专为嵌入式系统设计的、**轻量级**、**高可靠**、**硬件无关** 的 UART LoRa 链路层中间件。

它致力于解决廉价 UART LoRa 模组（如 ATK-LORA-01, Ebyte E32 等）在实际工程应用中的痛点，通过软件定义的方式，将一条“不可靠的物理串口线”升级为一条“可靠的逻辑数据链路”。

> **⚠️ 命名说明 (Naming Convention)**:
> 本项目仓库名为 **EasyLoRa**，但在源代码内部，API 和文件前缀仍保留为 **`LoRaPlat`** (e.g., `LoRa_Service_Init`)。这不会影响功能使用。

---

## 1. 核心特性 (Key Features)

*   **🛡️ 可靠传输**: 内置 **Stop-and-Wait ARQ** (停等协议) 与 **ACK 确认机制**，支持超时重传，确保关键数据必达。
*   **🧩 极致解耦**: 采用 **OSAL (操作系统抽象层)** 设计，一套代码无缝运行于 **裸机 (Bare-Metal)** 与 **RTOS** 环境。
*   **⚡ 异步并发**: 全非阻塞 API 设计，内置发送队列与环形缓冲区，自动处理半双工通信时序。
*   **🔧 远程运维**: 支持 **OTA 参数配置** (CMD 指令集)，支持参数掉电保存与系统自愈。
*   **🔒 安全管理**: 支持逻辑 ID (NetID) 过滤、组播 (GroupID) 及 Payload 加密钩子。

---

## 2. 适用场景 (Use Cases)

*   **智能家居/楼宇**: 需要可靠控制（如开关灯必达）的场景。
*   **工业遥测**: 环境恶劣，干扰大，需要数据完整性校验的场景。
*   **低成本组网**: 使用廉价模组快速构建星型网络。

---

## 3. 依赖说明 (Dependencies)

EasyLoRa 对硬件资源要求极低，理论上支持所有 32 位及部分 8 位 MCU。

*   **硬件依赖**:
    *   任意支持 UART 的 MCU (STM32, ESP32, MSP430 等)。
    *   UART LoRa 模组 (推荐 ATK-LORA-01, Ebyte E32 等支持透传/定点模式的模组)。
*   **软件依赖**:
    *   C99 标准编译器。
    *   约 2.5KB RAM (可裁剪)。

👉 **详细移植要求请参考**: [通用移植指南](./docs/porting_guide.md)

---

## 4. 快速开始 (Quick Start)

### 第一步：获取代码
```bash
git clone https://github.com/YourName/EasyLoRa.git
```

### 第二步：适配接口 (Porting)
你需要实现 `lora_port.c` (硬件接口) 和 `lora_osal.c` (系统接口)。

```c
// 示例：在 main.c 中初始化
#include "lora_service.h"

// 1. 定义回调
const LoRa_Callback_t my_cb = {
    .OnRecvData = My_OnRecv, // 接收回调
    .OnEvent    = My_OnEvent // 事件回调
};

int main(void) {
    // 2. 初始化硬件与 OSAL
    BSP_Init();
    LoRa_OSAL_Init(&my_osal_impl);

    // 3. 启动协议栈
    LoRa_Service_Init(&my_cb, 0x0001); // 本机 ID: 1

    while(1) {
        // 4. 周期性轮询
        LoRa_Service_Run();
    }
}
```

### 第三步：发送数据
```c
// 发送 "Hello" 给 ID 为 2 的设备，要求 ACK 确认
LoRa_Service_Send("Hello", 5, 0x0002, LORA_OPT_CONFIRMED);
```

👉 **平台移植教程**:
*   [STM32F103 裸机移植指南](./docs/porting_stm32.md)
*   [ESP32-S3 FreeRTOS 移植指南](./docs/porting_esp32.md)

---

## 5. 软件架构 (Architecture)

EasyLoRa 采用严格的分层架构设计，确保层与层之间依赖清晰：

*   **Service Layer**: 业务逻辑、配置管理、OTA、状态监控。
*   **Manager Layer**: 协议栈核心 (FSM 状态机, ARQ 重传, 队列管理)。
*   **Driver Layer**: 模组 AT 指令驱动 (硬件 MAC 抽象)。
*   **Port Layer**: 硬件抽象层 (BSP)。
*   **OSAL**: 操作系统抽象层。

👉 **深入了解架构设计**: [架构设计文档](./docs/architecture.md)

---

## 6. 资源占用 (Resource Usage)

在 STM32F103 上，默认配置下的资源占用情况：

| 资源类型 | 占用量 (约) | 说明 |
| :--- | :--- | :--- |
| **Flash (Code)** | ~8 KB | 取决于优化等级和启用的功能 (如 OTA, Log) |
| **RAM (Static)** | ~2.5 KB | 包含收发缓冲区 (各 512B) 及去重表 |
| **Stack** | < 512 Bytes | 深度优化，无递归调用 |

👉 **性能分析与裁剪指南**: [性能文档](./docs/performance.md)

---

## 7. API 参考 (API Reference)

核心 API 列表：

*   `LoRa_Service_Init`: 初始化协议栈。
*   `LoRa_Service_Run`: 主循环轮询 (Tick 驱动)。
*   `LoRa_Service_Send`: 发送数据 (支持 Confirmed/Unconfirmed)。
*   `LoRa_Service_CanSleep`: 低功耗休眠判断。

👉 **完整 API 手册**: [API 参考文档](./docs/api_reference.md)

---

## 8. 数据流分析 (Data Flow)

了解数据如何在各层之间流转，有助于你理解 ACK 机制和重传逻辑。

👉 **查看数据流向图**: [数据流分析](./docs/data_flow.md)

---

## 9. 更新计划 (Roadmap)

*   [x] **v3.0**: 基础架构重构，引入 OSAL 和 Port 层。
*   [x] **v3.4**: 增加 ACK 重传机制与发送队列。
*   [x] **v3.9**: 支持 OTA 远程配置与系统自愈。
*   [ ] **v4.0 (Plan)**: 支持多模组并发 (Multi-Instance)。
*   [ ] **v4.1 (Plan)**: 简易网关模式 (Gateway Mode)。

---

**License**: MIT License
**Author**: [Your Name / LoRaPlat Team]