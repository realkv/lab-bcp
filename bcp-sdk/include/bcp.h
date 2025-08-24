#ifndef __BCP_H__
#define __BCP_H__

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *thread_name;
    int32_t thread_priority;
    uint32_t thread_stack_size;
    void (*thread_func)(void *arg);
    void *arg;
} bcp_thread_config_t;

// Structure defining the platform adapter port for BCP.
// This structure holds function pointers for various system-level operations
// that are abstracted away from the core BCP logic.
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

    // --- Queue Management ---
    struct bcp_queue_t {
        /**
         * @brief Initializes a queue.
         * @param queue Pointer to a pointer that will hold the new queue handle.
         * @param item_num The maximum number of items the queue can hold.
         * @param item_size The size of each item in the queue.
         * @return 0 on success, -1 on failure.
         */
        int32_t (*queue_create)(void **queue, uint32_t item_num, uint32_t item_size);

        /**
         * @brief Sends data to a queue with a specified timeout.
         * @param queue Pointer to a pointer holding the queue handle.
         * @param data Pointer to the data to send.
         * @param size The size of the data to send.
         * @param timeout The timeout in milliseconds to wait if the queue is full.
         * @return 0 on success, -1 on failure.
         */
        int32_t (*queue_send)(void **queue, void *data, uint32_t size, uint32_t timeout);

        /**
         * @brief Sends data with higher priority to a queue with a specified timeout.
         * @param queue Pointer to a pointer holding the queue handle.
         * @param data Pointer to the data to send.
         * @param size The size of the data to send.
         * @param timeout The timeout in milliseconds to wait if the queue is full.
         * @return 0 on success, -1 on failure.
         */
        int32_t (*queue_send_prior)(void **queue, void *data, uint32_t size, uint32_t timeout);

        /**
         * @brief Receives data from a queue with a specified timeout.
         * @param queue Pointer to a pointer holding the queue handle.
         * @param data Pointer to a buffer to store the received data.
         * @param size The maximum size of the data to receive.
         * @param timeout The timeout in milliseconds to wait if the queue is empty.
         * @return 0 on success, -1 on failure.
         */
        int32_t (*queue_recv)(void **queue, void *data, uint32_t size, uint32_t timeout);

        /**
         * @brief Releases resources associated with a queue.
         * @param queue Pointer to a pointer holding the queue handle to destroy.
         */
        void (*queue_destory)(void **queue);
    } bcp_queue;

    // --- Time Management ---
    struct bcp_time_t {
        /**
         * @brief Gets the current time in milliseconds.
         * @return The current time in milliseconds.
         */
        uint32_t (*get_ms)(void);;

        /**
         * @brief Delays the current thread for a specified number of milliseconds.
         * @param ms The number of milliseconds to delay.
         */
        void (*delay_ms)(uint32_t ms);
    } bcp_time;

    // --- Timer Management ---
    struct bcp_timer_t {
        /**
         * @brief Creates a periodic timer.
         * @param timer Pointer to a pointer that will hold the new timer handle.
         * @param period_cb The callback function to be executed when the timer elapses.
         * @param arg The argument to be passed to the callback function.
         * @return 0 on success, -1 on failure.
         */
        int32_t (*timer_create)(void **timer, void (*period_cb)(void *arg), void *arg);

        /**
         * @brief Starts a timer with a specified period.
         * @param timer Pointer to a pointer holding the timer handle.
         * @param period_ms The period of the timer in milliseconds.
         * @return 0 on success, -1 on failure.
         */
        int32_t (*timer_start)(void **timer, uint32_t period_ms);

        /**
         * @brief Stops a timer.
         * @param timer Pointer to a pointer holding the timer handle to stop.
         * @return 0 on success, -1 on failure.
         */
        int32_t (*timer_stop)(void **timer);

        /**
         * @brief Destroys a timer and releases its resources.
         * @param timer Pointer to a pointer holding the timer handle to destroy.
         * @return 0 on success, -1 on failure.
         */
        int32_t (*timer_destory)(void **timer);
    } bcp_timer;

    // --- Memory Management ---
    struct bcp_mem_t {

        /**
         * @brief Allocates a block of memory.
         * @param size The number of bytes to allocate.
         * @return A pointer to the allocated memory block, or NULL if allocation fails.
         */
        void *(*bcp_malloc)(size_t size);

        /**
         * @brief Frees a previously allocated block of memory.
         * @param mem Pointer to the memory block to free.
         */
        void (*bcp_free)(void *mem);
    } bcp_mem;

    // --- Critical Section Management ---
    struct bcp_critical_t {
        /**
         * @brief Creates a critical section object.
         * @param critical_section Pointer to a pointer that will hold the new critical section handle.
         * @return 0 on success, -1 on failure.
         */
        int32_t (*critical_section_create)(void **critical_section);

        /**
         * @brief Destroys a critical section object.
         * @param critical_section Pointer to a pointer holding the critical section handle to destroy.
         */
        void (*critical_section_destory)(void **critical_section);

        /**
         * @brief Enters a critical section, preventing concurrent access.
         * @param critical_section Pointer to a pointer holding the critical section handle.
         */
        void (*enter_critical_section)(void **critical_section);

        /**
         * @brief Leaves a critical section, allowing concurrent access.
         * @param critical_section Pointer to a pointer holding the critical section handle.
         */
        void (*leave_critical_section)(void **critical_section);
    } bcp_critical;

    struct bcp_crc_t {
        /**
         * @brief Calculates the CRC-16 checksum for a given data buffer.
         * @param data Pointer to the data buffer.
         * @param len The length of the data buffer in bytes.
         * @return The calculated CRC-16 checksum.
         */
        uint16_t (*crc16_cal)(void *data, uint32_t len);
    } bcp_crc;

} bcp_adapter_port_t;


typedef struct _bcp_t bcp_t;

typedef struct {
    bcp_t *bcp;
    void *user_data;
} bcp_block_t;

typedef struct {
    uint8_t  mfs_scale;                 // Number of mtu packets for crc inspection and retransmission each time.The recommended value is less than 5.
    uint16_t mtu;                       // The true effective value of mtu, such as 20 for ble4.0
    uint32_t mal;                       // The maximum amount of data sent by the upper layer each time. This value will affect the memory consumption. 
                                        // It is recommended that it not exceed 8192.

    char *work_thread_name;
    int32_t work_thread_priority;
    uint32_t work_thread_stack_size;
} bcp_parm_t;

typedef enum {

    BCP_OPEND_ERROR_RSP_TIMEOUT = -5,
    BCP_OPEND_ERROR_SEND_FAIL,

    BCP_OPEND_ERROR_MEM_FAIL,
    BCP_OPEND_OK = 0,

} bcp_open_status_t;

typedef struct {

    /**
     * @description: The interface of low level data output.
     * @param {bcp_block_t} *bcp_block Pointer to the BCP block instance.
     * @param {void} *data Indicates the pointer to the data to send.
     * @param {uint32_t} len  Indicates the data size. 
     * @return {*} 0 will be returned if the execution is successful, otherwise, values less than 0 will be returned. 
     */
    int32_t (*output)(const bcp_block_t *bcp_block, void *data, uint32_t len);

    /**
     * @description: Notify the upper layer of data arrival.
     * @param {bcp_block_t} *bcp_block Pointer to the BCP block instance.
     * @param {void} *data  Indicates the pointer to the data.
     * @param {uint32_t} len Indicates the data size. 
     * @return {*}
     */
    void (*data_listener)(const bcp_block_t *bcp_block, void *data, uint32_t len);
} bcp_interface_t;


typedef enum {
    BCP_LOG_NONE = 0,
    
    BCP_LOG_FAULT,
    BCP_LOG_ERROR,
    BCP_LOG_WARN,
    BCP_LOG_INFO,
    BCP_LOG_DEBUG,
    BCP_LOG_TRACE,
    
} bcp_log_level_t;

void bcp_log_level_set(bcp_log_level_t level);

void bcp_log_output_register(void (*log_output)(bcp_log_level_t level, const char *message));

/**
 * @brief Initializes a BCP adapter port with specific configuration.
 * @param bcp_adapter_port A pointer to a constant `bcp_adapter_port_t` structure
 *                         containing the configuration and callbacks for the
 *                         adapter port to be initialized.
 */
void bcp_adapter_port_init(const bcp_adapter_port_t *bcp_adapter_port);

/**
 * @brief Creates a BCP block object.
 *
 * This function creates a BCP block object. It should be called 
 * only once during protocol initialization. Upon successful creation, 
 * an internal worker thread will be spawned to handle the protocol logic.
 *
 * @param bcp_parm Pointer to the BCP parameters structure, which defines
 *                 configuration settings for the protocol.
 * @param bcp_interface Pointer to the BCP interface structure, which provides
 *                      function pointers for data output and data listening.
 * @param user_data A pointer to user-defined data that can be passed to the
 *                  interface callbacks. This allows associating custom data
 *                  with the BCP block.
 *
 * @return A pointer to the newly created bcp_block_t object if the creation
 *         is successful. Returns NULL if the creation fails.
 */
bcp_block_t *bcp_create(const bcp_parm_t *bcp_parm, const bcp_interface_t *bcp_interface, const void *user_data);

/**
 * @brief Destroys a BCP block object.
 *
 * This function releases all resources associated with the given BCP block object,
 * including its internal worker thread and any allocated memory.
 * After calling this function, the BCP block object becomes invalid and
 * should not be used.
 *
 * @param bcp_block A pointer to the BCP block object to be destroyed.
 */
void bcp_destory(bcp_block_t *bcp_block);

/**
 * @brief Opens the communication channel for the BCP block.
 *
 * This function must be called before sending any actual data to establish
 * the communication channel. It should be called only once for a given BCP
 * block. The function is executed asynchronously. The result of the operation
 * will be communicated via the provided callback function.
 *
 * @param bcp_block A pointer to the BCP block object for which to open the channel.
 * @param opened_cb A callback function that will be invoked upon completion of
 *                  the channel opening operation. It receives the BCP block
 *                  pointer and the status of the operation (bcp_open_status_t).
 * @param timeout_ms The maximum time in milliseconds to wait for the channel
 *                   to open.
 *
 * @return 0 if the channel opening request was successfully initiated.
 *         A negative value if the request failed to initiate (e.g., invalid
 *         parameters or BCP block not ready). The actual success or failure
 *         of the opening process will be reported via the `opened_cb` callback.
 */
int32_t bcp_open(bcp_block_t *bcp_block, void (*opened_cb)(const bcp_block_t *bcp_block, bcp_open_status_t status), uint32_t timeout_ms);

/**
 * @brief Sends data through the BCP block's communication channel.
 *
 * This function is used to send data after the communication channel has been
 * successfully opened using `bcp_open`. The data is sent asynchronously.
 *
 * @param bcp_block A pointer to the BCP block object to send data through.
 * @param data A pointer to the data buffer to be sent.
 * @param len The number of bytes in the data buffer to send.
 *
 * @return 0 if the data was successfully queued for sending.
 *         A negative value if the sending operation failed to initiate
 *         (e.g., the channel is not open, invalid parameters, or no buffer
 *         available). The success or failure of the actual data transmission
 *         is typically handled by the underlying BCP implementation.
 */
int32_t bcp_send(bcp_block_t *bcp_block, void *data, uint32_t len);

/**
 * @brief Inputs data received from an underlying protocol to the BCP block.
 *
 * This function serves as the entry point for data received from an underlying
 * communication protocol (e.g., a BLE stack). When the underlying protocol
 * receives data, it should call this function to pass the data to the BCP
 * for further processing.
 *
 * @param bcp_block A pointer to the BCP block object that will process the input data.
 * @param data A pointer to the buffer containing the received data.
 * @param len The number of bytes of data received.
 *
 * @return 0 if the data was successfully accepted by the BCP block for processing.
 *         A negative value if the input operation failed (e.g., BCP block is
 *         not initialized, no buffer available for processing, or invalid parameters).
 */
int32_t bcp_input(bcp_block_t *bcp_block, void *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif