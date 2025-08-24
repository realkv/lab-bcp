<!--
 * @LastEditors: auto
-->

# BCP ：用于 IoT 点对点通信的 ARQ 协议
![Platform](https://img.shields.io/badge/Platform-RTOS%2FAndroid%2FiOS-green) ![protocol](https://img.shields.io/badge/protocol-BLE-brightgreen)


最新的 BCP 是一个用于 IoT 中点对点通信的可靠传输协议，可用于 BLE、串口和局域网 UDP 等

### 为什么需要它

在物联网（IoT）项目中，特别是涉及到低功耗蓝牙（BLE）通信时，常会遇到一些挑战：
*   **MTU 限制：** BLE 的 MTU（Maximum Transmission Unit）通常很小（例如 20-23 字节），导致一次性传输的数据量受限
*   **不可靠通信：** BLE 的连接不稳定，容易丢包或出现乱序
*   **上层协议臃肿：** 许多自定义或标准协议（如 MQTT、CoAP）在低功耗、低带宽环境下开销过大，不适合直接运行
  
BCP 协议是专为解决这些问题而设计的：
*   **可靠传输：** 实现了 ARQ（Automatic Repeat reQuest）机制，确保数据的可靠性，支持丢包重传和乱序处理
*   **分片与重组：** 能够将大块数据自动分片，适配 BLE 的小 MTU，并在接收端自动重组，对上层透明
*   **轻量级：** 协议本身开销极小，设计上优先考虑资源受限的嵌入式设备
  
BCP 协议定位是：**用于 IoT 中点对点通信的可靠传输协议，不仅可用于 BLE，还可用于串口和局域网 UDP 等**

## 特性
- 可配置的分包组包大小
- 可配置的确认包间隔，兼顾可靠性与效率
- 内存池设计，无内存碎片顾虑，对嵌入式平台友好
- 异步串行设计模式，避免外部使用者处理复杂的资源竞争问题
- 低冗余数据（采用高效的报文封装和极少量的控制信息，最大化数据传输效率）
- 可应用于嵌入式 RTOS、Android 和 iOS 多平台

## 快速开始

为了避免外部使用者处理复杂的多线程环境下资源竞争问题，BCP 使用了异步串行模式，因此在使用前需要注册一些外部平台相关的函数

### 注册外部依赖

BCP 需要访问平台相关的函数。您需要实现并注册一个 `bcp_interface_t` 结构体，该结构体包含了平台适配所需的**回调函数**

```c
// bcp_interface_t
typedef struct {
    // --- Thread Management ---
    struct bcp_thread_t {
        /**
         * @brief Creates a new thread.
         * @param thread Pointer to a pointer that will hold the new thread handle.
         * @param thread_config Configuration for the new thread.
         * @return 0 on success, -1 on failure.
         */
        int32_t (*thread_create)(void **thread, bcp_thread_config_t *thread_config);

        /**
         * @brief Destroys an existing thread.
         * @param thread Pointer to a pointer holding the thread handle to destroy.
         * @return 0 on success, -1 on failure.
         */
        int32_t (*thread_destory)(void **thread);

        /**
         * @brief Exits the current thread.
         * @param thread Pointer to a pointer holding the current thread handle.
         */
        void (*thread_exit)(void **thread);

    } bcp_thread;

    // ... 
} bcp_interface_t;

// 初始化平台适配层，传入实现的 bcp_interface_t
// void bcp_adapter_port_init(const bcp_adapter_port_t *bcp_adapter_port);
```

### 开始使用

1. 创建 bcp 
   
        // 创建一个 bcp_block_t 对象，创建成功会返回指向 bcp_block_t 对象的指针
        // 外部可以建立此对象和实际通信实体（如 GATT Service）的一一对应关系
        bcp_block_t *bcp_block = bcp_create(bcp_parm, bcp_interface, user_data);

    对于 BLE 来说，创建 bcp_block_t 对象的时机在 connenct 成功之后

2. 输入一个底层数据包
   
        // 收到底层数据（如 BLE 数据）时调用
        bcp_input(bcp_block, data, len);

3. 打开传输通道
   
        // 通过回调监听打开的结果
        bcp_open(bcp_block, opened_cb, 2000);

4. 调用 bcp_send 发送上层数据

        // 如果返回值等于 0，表示此次执行成功，数据会被异步发送
        // 如果返回值小于 0，表示出错了，需要再次调用或进行其他错误处理
        bcp_send(bcp_block, data, len);

### 协议配置

BCP 的核心配置参数包含在 bcp_parm_t 结构体中。以下是关键参数及其说明：

**mfs_scale (Max Frame Size Scale):**
- 类型: uint8_t
- 描述: 这是 BCP 内部使用的帧大小的比例因子。BCP 会根据这个值和 mtu 来计算实际的单帧最大传输字节数
- 建议: 建议值为 3 到 5。值越大，每帧携带的数据越多，协议开销相对越小，但重传时需要传输的数据量也越大

**mtu (Maximum Transmission Unit):**
- 类型: uint16_t
- 描述: 指示了底层通信介质实际可用的最大传输单元（MTU）大小
- 特别注意 (BLE): 对于 BLE，你需要考虑协议开销（如 ATT header, L2CAP header 等），并传入实际可用的 MTU。例如，如果 BLE 的 MTU 是 23 字节，通常 实际可用于上层数据 的只有 15-19 字节（取决于属性和连接参数）。请务必根据你的 BLE 栈和连接配置进行准确设置

**mal (Max Amount of Data):**
- 类型: uint32_t
- 描述: 指示了 BCP 在一次 bcp_send 调用中，最多能够处理 的用户数据量（字节）。这个值会影响 BCP 内部缓冲区的分配和处理逻辑
- 建议: 通常设置为你应用场景下，单次交互期望传输的最大数据量


## 示例

详见 example 目录，目前仅有 ESP32 下的示例

