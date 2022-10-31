
# BCP ：用于 IoT&BLE 通信的 ARQ 协议
![Platform](https://img.shields.io/badge/Platform-RTOS%2FAndroid%2FiOS-green) ![protocol](https://img.shields.io/badge/protocol-BLE-brightgreen)


BCP 是一个用于 IoT 中 BLE 通信的可靠传输协议

### 为什么需要它

在许多 IoT 项目中，都有基于 BLE 通信的需求，对于上层数据来说，BLE 的 MTU 太小，需要进行分包组包。另外，为了尽可能提高通信速率，在实际中，多使用 BLE 的 Notify 和 Write_with_no_response 方法来进行通信，这种方式没有底层的确认数据包，在某些情况下会产生丢包

BCP 使得上层用户可以一次性发送远大于 MTU 大小的数据，而不用考虑如何进行分包组包，且内部的丢包检测与重传机制保证了传输可靠性，传输效率高于使用 Indicate 和 Write_with_response 方法

## 特性
- 可配置的分包组包大小
- 针对快速数据传输的 NACK 确认重传机制，保证短时间内丢包重传，提供尽可能的可靠通信服务
- 针对绝对可靠传输的 ACK 确认重传机制，通过快速重传和选择重传，提供高效的绝对可靠通信服务
- 异步串行设计模式，避免外部使用者处理复杂的资源竞争问题
- 低冗余数据
- 可应用于嵌入式 RTOS、Android 和 iOS 多平台

## 快速开始

为了避免外部使用者处理复杂的多线程环境下资源竞争问题，BCP 使用了异步串行模式，因此在使用前需要注册一些外部平台相关的函数

### 注册外部依赖

1. 实现并注册 semaphore
   
        // 根据各自系统，实现 semaphore 的 create、give、take 和 free
        b_sem_t sem;
        sem.sem_create = sem_new;
        sem.sem_give = sem_give;
        sem.sem_take = sem_take;
        sem.sem_free = sem_free;

        sem_func_register(&sem);
        
2. 实现并注册 mutex
   
        // 根据各自系统，实现 mutex 的 create、unlock、lock 和 free
        b_mutex_t mutex;
        mutex.mutex_create = mutex_new;
        mutex.mutex_lock = mutex_lock;
        mutex.mutex_unlock = mutex_unlock;
        mutex.mutex_free = mutex_free;

        mutex_func_register(&mutex);

3. 注册内存分配相关函数
   
        // 注册系统的内存分配函数
        k_allocator(malloc, free);

    如果是 FreeRTOS 系统，则是如下

        k_allocator(pvPortMalloc, vPortFree);

4. 实现并注册获取系统毫秒级时间函数
   
        // 注册系统的毫秒级时间获取函数
        op_get_ms_register(get_sys_ms);

### 开始使用

1. 创建 bcp
   
        // 创建一个 bcp 对象，如果创建成功，会返回一个不小于 0 的 id
        // 外部可以建立此 id 和实际通信实体（如 GATT Service）的一一对应关系
        int32_t bcp_id = bcp_create(bcp_parm, bcp_interface);

    对于 BLE 来说，创建 bcp 对象的时机在 connenct 成功之后

2. 在一个 thread/task 中循环调用 bcp_task_run
   
        // 输入 bcp_create 返回的 bcp_id 作为参数，在 timeout_ms 时间内处于阻塞状态
        bcp_task_run(bcp_id, timeout_ms);

3. 周期性调用 bcp_check
   
        // 输入 bcp_create 返回的 bcp_id 作为参数
        // 调用周期一般建议为 100 ms
        bcp_check(bcp_id);

4. 输入一个底层数据包
   
        // 收到底层数据（如 BLE 数据）时调用
        bcp_input(bcp_id, data, len);

5. 调用 bcp_send 发送上层数据

        // 如果返回值等于 0，表示此次执行成功，数据会被异步发送
        // 如果返回值小于 0，表示出错了，需要再次调用或进行其他错误处理
        bcp_send(bcp_id, data, len);

### 协议配置

1. 协议模式  
   
    通过 bcp_parm_t::need_ack 字段配置，0 ：NACK，1 ：ACK  

    协议有 NACK 模式和 ACK 模式，两种模式的差别如下

    NACK 模式 ：正常接收数据包不会给对方任何回应，只有在检测到丢帧时，才会给对方发送 NACK 帧，
    发送方收到 NACK 帧后，从丢失的帧开始重传后续的全部帧

    ACK 模式 ：接收方每收到一帧，会回给对方一个 ACK 帧，这些 ACK 帧会根据 bcp_check 的时机，
    采取单独发送或聚合在一个 MTU 包内发送，一定时间（一般为 1500ms，可通过 MAX_INTERVAL_RTO 调节）
    内没有收到 ACK 的帧会重传

    两种模式设计上的差异，分别对应两种应用场景 ：

    NACK 模式适用于大数据量快速传输的场景，如基于 BLE 的九轴原始数据采集，但一般的协议通信也适用，因为丢包并不容易发生

    ACK 模式适用于绝对可靠场景，如某些要求绝对可靠传输的协议

2. 丢帧检测的单位大小
   
    BCP 的丢包检测和重传是以帧为单位，帧的大小为 1 到 N 个 MTU 大小，帧的大小在一定程度上影响了传输效率，如帧的大小设置为 1 个 MTU，则 ACK 模式相比 GATT 的 Indicate 和 Write_with_response 方法就没有优势

    帧的大小通过 bcp_parm_t::check_multiple 字段设置，一般情况下设置为 3 ~ 5，即一帧为 3 到 5 个 MTU 包大小

3. MTU 的大小
   
    通过 bcp_parm_t::mtu 字段设置

    该 MTU 大小为实际可用大小，如对于 BLE4.0 系统，系统的 MTU 为 23，实际可用为 20，则该 MTU 应设置为 20

4. 单次发送或接收的最大数据量

    通过 bcp_parm_t::mal 字段设置
   
    BCP 内部会申请一块内存来暂存接收到的帧数据，该内存在调用 bcp_release 销毁 BCP 对象前都存在，单次发送的数据量大小不能超过该内存块大小，否则发送或接收会出错
    对于内存敏感的嵌入式设备，可根据实际情况调整该内存块大小


## 示例

详见 example 目录，目前仅有 ESP32 下的示例

