
# BCP: An ARQ Protocol for Peer-to-Peer Communication in IoT
![Platform](https://img.shields.io/badge/Platform-RTOS%2FAndroid%2FiOS-green) ![protocol](https://img.shields.io/badge/protocol-BLE-brightgreen)


BCP is a reliable transport protocol designed for peer-to-peer communication in IoT, supporting various underlying transports such as BLE, serial communication, and LAN UDP.

### Why is it needed?

In IoT projects, especially those involving Bluetooth Low Energy (BLE) communication, several challenges often arise:
*   **MTU Limitations:** BLE's MTU (Maximum Transmission Unit) is typically very small (e.g., 20-23 bytes), limiting the amount of data that can be transmitted in a single go.
*   **Unreliable Communication:** BLE connections can be unstable, prone to packet loss or out-of-order delivery.
*   **Bloated Upper-Layer Protocols:** Many custom or standard protocols (e.g., MQTT, CoAP) incur significant overhead in low-power, low-bandwidth environments, making them unsuitable for direct use.

The BCP protocol is specifically designed to address these challenges:
*   **Reliable Transmission:** Implements an ARQ (Automatic Repeat reQuest) mechanism to ensure data reliability, supporting packet retransmission and out-of-order handling.
*   **Fragmentation and Reassembly:** Automatically fragments large data blocks to fit small BLE MTUs and reassembles them at the receiver, transparent to the upper layer.
*   **Lightweight:** The protocol itself has minimal overhead, prioritizing resource-constrained embedded devices in its design.

BCP's core positioning is: **A reliable transport protocol for peer-to-peer communication in IoT, applicable not only to BLE but also to serial communication, LAN UDP, and more.**

## Features
- Configurable packet fragmentation and reassembly size
- Configurable acknowledgment packet interval, balancing reliability and efficiency
- Memory pool design, eliminating memory fragmentation concerns, friendly to embedded platforms
- Asynchronous serial design pattern, preventing external users from dealing with complex resource contention issues
- Low data redundancy (utilizes efficient packet encapsulation and minimal control information to maximize data transmission efficiency)
- Applicable across multiple platforms: embedded RTOS, Android, and iOS

## Quick Start

To prevent external users from dealing with complex resource contention issues in multi-threaded environments, BCP employs an asynchronous serial mode. Therefore, some external platform-specific functions need to be registered before use.

### Registering External Dependencies

BCP requires access to platform-specific functions. You need to implement and register a `bcp_interface_t` structure, which contains the **callback functions** required for platform adaptation.

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

// Initialize the platform adaptation layer, passing the implemented bcp_interface_t
// void bcp_adapter_port_init(const bcp_adapter_port_t *bcp_adapter_port);
```

### Getting Started

1.  Create a BCP Instance

        // Create a bcp_block_t object. On successful creation, a pointer to the bcp_block_t object will be returned.
        // Externally, a one-to-one mapping can be established between this object and the actual communication entity (e.g., a GATT Service).
        bcp_block_t *bcp_block = bcp_create(bcp_parm, bcp_interface, user_data);

    For BLE, the `bcp_block_t` object should be created after a successful connection.

2.  Input a Low-Level Data Packet

        // Call this when low-level data (e.g., BLE data) is received.
        bcp_input(bcp_block, data, len);

3.  Open the Transmission Channel

        // Listen for the open result via callback.
        bcp_open(bcp_block, opened_cb, 2000);

4.  Call `bcp_send` to Transmit Upper-Layer Data

        // If the return value is 0, it indicates successful execution, and the data will be sent asynchronously.
        // If the return value is less than 0, an error occurred, requiring a retry or other error handling.
        bcp_send(bcp_block, data, len);

### Protocol Configuration

BCP's core configuration parameters are contained within the `bcp_parm_t` structure. Key parameters and their descriptions are as follows:

**mfs_scale (Max Frame Size Scale):**
- Type: `uint8_t`
- Description: This is a scaling factor for the internal frame size used by BCP. BCP calculates the actual maximum transmission bytes per single frame based on this value and `mtu`.
- Recommendation: Recommended values are 3 to 5. A larger value means more data per frame, leading to relatively lower protocol overhead, but also a larger amount of data to retransmit if needed.

**mtu (Maximum Transmission Unit):**
- Type: `uint16_t`
- Description: Indicates the actual maximum transmission unit (MTU) size available on the underlying communication medium.
- Special Note (BLE): For BLE, you need to consider protocol overhead (e.g., ATT header, L2CAP header, etc.) and pass the *actual usable* MTU. For example, if the BLE MTU is 23 bytes, typically only 15-19 bytes are *actually available for upper-layer data* (depending on attributes and connection parameters). Ensure accurate setting based on your BLE stack and connection configuration.

**mal (Max Amount of Data):**
- Type: `uint32_t`
- Description: Indicates the maximum amount of user data (in bytes) that BCP can handle in a single `bcp_send` call. This value affects BCP's internal buffer allocation and processing logic.
- Recommendation: Typically set to the maximum data volume expected for a single interaction in your application scenario.

## Examples

See the `example` directory. Currently, only an ESP32 example is available.
