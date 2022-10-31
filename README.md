# BCP ：A ARQ protocol for IoT&BLE communication

![Platform](https://img.shields.io/badge/Platform-RTOS%2FAndroid%2FiOS-green) ![protocol](https://img.shields.io/badge/protocol-BLE-brightgreen)

BCP is a reliable transmission protocol for BLE communication in IoT

In many IoT projects, there is a need for BLE based communication. For upper layer data, the MTU of BLE is too small, and subcontracting is required. In addition, in order to improve the communication rate as much as possible, in practice, the BLE Notify and Write_with_no_response method is used for communication. In this way, there is no underlying acknowledgement packet, and packet loss may occur in some cases.

BCP enables upper layer users to send data much larger than MTU at one time, without considering how to sub package and group packets. The internal packet loss detection and retransmission mechanism ensures the transmission reliability, and the transmission efficiency is higher than that of using Indicate and Write_with_response method.

## Features

- Configurable subcontracting size
- NACK confirmation retransmission mechanism for fast data transmission ensures packet loss retransmission in a short time and provides reliable communication services as much as possible
- The ACK confirmation retransmission mechanism for absolute reliable transmission provides efficient and absolute reliable communication services through fast retransmission and selective retransmission
- Asynchronous serial design pattern to avoid external users from dealing with complex resource competition problems
- Low redundancy data
- Applicable to embedded RTOS, Android and iOS multi platforms

## Quick Start

In order to avoid external users from dealing with resource competition in a complex multithreaded environment, BCP uses the asynchronous serial mode, so it is necessary to register some functions related to the external platform before using them.

### Register External Dependencies

1. Implement and register semaphore

        // According to your systems, realize the create, give, take and free of semaphore
        b_sem_t sem;
        sem.sem_create = sem_new;
        sem.sem_give = sem_give;
        sem.sem_take = sem_take;
        sem.sem_free = sem_free;

        sem_func_register(&sem);

2. Implement and register mutex

        // According to your systems, realize the create, lock, unlock and free of mutex
        b_mutex_t mutex;
        mutex.mutex_create = mutex_new;
        mutex.mutex_lock = mutex_lock;
        mutex.mutex_unlock = mutex_unlock;
        mutex.mutex_free = mutex_free;

        mutex_func_register(&mutex);

3. Register memory management functions

        k_allocator(malloc, free);

    If it is a FreeRTOS system, it is as follows

        k_allocator(pvPortMalloc, vPortFree);

4. Implement and register to obtain system millisecond level time function

        op_get_ms_register(get_sys_ms);

### How to use

1. Create bcp
   
        // Create a bcp object. If the creation is successful, it will return an ID no less than 0.
        // External users may need to establish a one-to-one correspondence between this ID and the actual communication entity (such as GATT Service)
        int32_t bcp_id = bcp_create(bcp_parm, bcp_interface);

    For BLE, the time to create the bcp object is after the successful connect.

2. Loop call bcp_task_run in a thread/task
   
        // It is blocked during the timeout period.
        bcp_task_run(bcp_id, timeout_ms);

3. Cyclically call bcp_check
   
        // The call cycle is generally recommended to be 100 ms
        bcp_check(bcp_id);

4. Input a lower layer data packet
   
        // Need to call when a lower layer data packet (such as BLE packet)is received
        bcp_input(bcp_id, data, len);

5. Call bcp_send to send upper layer data

        // If the return value is equal to 0, the execution is successful and the data will be sent asynchronously
        // If the return value is less than 0, it indicates that an error has occurred and needs to be called again or other error handling
        bcp_send(bcp_id, data, len);

### Protocol Configuration

#### Protocol Mode

Configuration through bcp_parm_t::need_ack field, 0: NACK, 1: ACK

The protocol includes NACK mode and ACK mode. The differences between the two modes are as follows

NACK mode : normally receiving data packets will not give any response to the other party. Only when a frame loss is detected, the sender will send a NACK frame to the other party. After receiving the NACK frame, the sender will retransmit all subsequent frames from the lost frame.

ACK mode : Each time the receiver receives a frame, it will return an ACK frame to the receiver. These ACK frames will be based on the bcp_ The time of check is sent separately or aggregated in one MTU packet. Frames that do not receive ACK within a certain time (usually 1500ms, which can be adjusted by MAX_INTERVAL_RTO) will be retransmitted.

The design differences of the two modes correspond to two application scenarios :
The NACK mode is applicable to scenarios where large amounts of data are transmitted quickly, such as nine axis raw data acquisition based on BLE, but general protocol communication is also applicable, because packet loss is not easy to occur.
ACK mode is applicable to absolutely reliable scenarios, such as some protocols that require absolutely reliable transmission.

#### Unit size of frame loss detection

BCP's packet loss detection and retransmission are based on frames. The frame size is 1 to N MTUs. The frame size affects the transmission efficiency to a certain extent. If the frame size is set to 1 MTU, ACK mode has no advantage over GATT's Indicator and Write_with_response methods.

The frame size is passed through bcp_parm_t::check_ The multiple field is generally set to 3~5, that is, a frame is 3 to 5 MTU packet sizes.

#### Size of MTU

Set via bcp_parm_t::mtu field.

The MTU size is the actual available size. For example, for the BLE4.0 system, the MTU of the system is 23, and the actual available size is 20, then the MTU should be set to 20.

#### Maximum amount of data sent or received in a single time

Set via bcp_parm_t::mal field.

BCP will request a block of memory to temporarily store the received frame data, and there is a call to bcp_ The release exists before the BCP object is destroyed. The size of the data sent in a single time cannot exceed the size of the memory block, otherwise an error will occur in sending or receiving.

For memory sensitive embedded devices, the size of the memory block can be adjusted according to the actual situation.

## Example

See the example directory for details. At present, there are only examples under ESP32.