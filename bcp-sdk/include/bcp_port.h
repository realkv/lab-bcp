#ifndef __BCP_PORT_H__
#define __BCP_PORT_H__

#ifdef __cplusplus
extern "C" {
#endif

//------------------------------------------------------------------
// log level define                 
//---------------------------------------------------------------------
typedef enum {
    BCP_LOG_FAULT = 0,
    BCP_LOG_ERROR,
    BCP_LOG_WARN,
    BCP_LOG_INFO,
    BCP_LOG_DEBUG,
    BCP_LOG_TRACE,
    BCP_LOG_NONE,
} bcp_log_level_t;

//---------------------------------------------------------------------
// semaphore                
//---------------------------------------------------------------------
typedef struct
{
    // Semaphore is created. If it succeeds, 0 is returned. If it fails, - 1 is returned.
    int32_t (*sem_create)(void *sem, uint32_t max_count, uint32_t init_count);

    // Give semaphore.
    void (*sem_give)(void *sem);

    // Take semaphore. If it succeeds, 0 is returned. If it fails, - 1 is returned.
    int32_t (*sem_take)(void *sem, uint32_t timeout);

    // Semaphore destruction.
    void (*sem_free)(void *sem);
} b_sem_t;


//---------------------------------------------------------------------
// mutex                 
//---------------------------------------------------------------------
typedef struct
{
    // If mutex is created, 0 will be returned if success, and - 1 if failure.
    int32_t (*mutex_create)(void *mutex);

    // Mutex unlocking
    void (*mutex_unlock)(void *mutex);

    // Mutex is locked. If it succeeds, 0 is returned. If it fails, - 1 is returned.
    int32_t (*mutex_lock)(void *mutex, uint32_t timeout);

    // Mutex destruction.
    void (*mutex_free)(void *mutex);
} b_mutex_t;


//---------------------------------------------------------------------
// Interfaces related to system resources               
//---------------------------------------------------------------------

// semaphore
void sem_func_register(const b_sem_t *b_sem);

// mutex 
void mutex_func_register(const b_mutex_t *b_mutex);

// mem
void k_allocator(void* (*new_malloc)(size_t), void (*new_free)(void*));

// system millisecond time
void op_get_ms_register(uint32_t (*get_current_ms)(void));


//---------------------------------------------------------------------
// BCP related interfaces                 
//---------------------------------------------------------------------
// Number of lost frames for fast retransmission in ACK mode
#define MAX_ACK_LOST_NUM        3 

// Assumed maximum single round trip time, which affects 
// retransmission rate and frame memory release rate
#define MAX_INTERVAL_RTO        1500 

// Maximum number of BCP objects that can be created
#define MAX_BCP_NUM             4

typedef struct {
    uint8_t need_ack;                   // 0 : nack mode, 1 : ack mode
    uint8_t check_multiple;             // Number of mtu packets for crc inspection and retransmission each time.The recommended value is less than 5.
    uint16_t mtu;                       // The true effective value of mtu, such as 20 for ble4.0
    uint32_t mal;                       // The maximum amount of data sent by the upper layer each time. This value will affect the memory consumption. 
                                        // It is recommended that it not exceed 8192.
} bcp_parm_t;

typedef struct {

    /**
     * @description: The interface of low level data output.
     * @param {int32_t} bcp_id Indicates the bcp id, which can be used to map external object entities.
     * @param {void} *data Indicates the pointer to the data to send.
     * @param {uint32_t} len  Indicates the data size. 
     * @return {*} 0 will be returned if the execution is successful, otherwise, values less than 0 will be returned. 
     */
    int32_t (*output)(int32_t bcp_id, void *data, uint32_t len);

    /**
     * @description: Notify the upper layer of data arrival.
     * @param {int32_t} bcp_id Indicates the bcp id.
     * @param {void} *data  Indicates the pointer to the data.
     * @param {uint32_t} len Indicates the data size. 
     * @return {*}
     */
    void (*data_received)(int32_t bcp_id, void *data, uint32_t len);

    /**
     * @description: Perform crc16 calculation.
     * @param {void} *data Indicates the pointer to the data.
     * @param {uint32_t} len Indicates the data size. 
     * @return {*} Returns the calculated value calculated by crc16.
     */
    uint16_t (*crc16_cal)(void *data, uint32_t len);
} bcp_interface_t;

void bcp_log_level_set(bcp_log_level_t level);

void bcp_log_output_register(void (*log_output)(bcp_log_level_t level, const char *message));

/**
 * @description: It is responsible for processing bcp tasks and needs to be called circularly. Note that it is blocked during the timeout period.
 * @param {int32_t} bcp_id  Indicates the bcp id, which is determined by @bcp_create generation.
 * @param {uint32_t} timeout_ms Timeout in milliseconds. If this value is 0, it will not block. If it is set to 0xfffful, it will block permanently.
 * @return {*} Return 0 if normal, return - 1 if abnormal.
 */
int32_t bcp_task_run(int32_t bcp_id, uint32_t timeout_ms);

/**
 * @description: Create a new bcp object and return the bcp id. The maximum number of objects created is limited by @MAX_BCP_NUM.
 * @param {bcp_parm_t} *bcp_parm  See @bcp_parm_t.
 * @param {bcp_interface_t} *bcp_interface  See @bcp_interface_t.
 * @return {*} If successful, it returns a bcp id greater than or equal to 0. If unsuccessful, it returns a value less than 0.
 */
int32_t bcp_create(const bcp_parm_t *bcp_parm, const bcp_interface_t *bcp_interface);

/**
 * @description: Release the bcp object. Note that this is executed asynchronously, 
 * and the execution result is notified through the callback function.
 * @param {int32_t} bcp_id  Indicates the bcp id.
 * @param {int32_t} *result_notify  The callback function of the execution result. If it succeeds, 
 * the parameter is 0. Otherwise, it is - 1.
 * @return {*}  0 will be returned if the execution is successful, otherwise, values less than 0 will be returned. 
 */
int32_t bcp_release(int32_t bcp_id, void (*result_notify)(int32_t result));

/**
 * @description: User/upper level send.
 * @param {int32_t} bcp_id  Indicates the bcp id, which is determined by @bcp_create generation.
 * @param {void} *data  Indicates the pointer to the data.
 * @param {uint32_t} len Indicates the data size. The maximum length should not exceed  the setting of @mal (@bcp_parm_t).
 * @return {*} 0 will be returned if the execution is successful, otherwise, values less than 0 will be returned. 
 */
int32_t bcp_send(int32_t bcp_id, void *data, uint32_t len);

/**
 * @description: Call it when receiving low-level data input, such as when receiving ble data.
 * @param {int32_t} bcp_id  Indicates the bcp id.
 * @param {void} *data  Indicates the pointer to the data.
 * @param {uint32_t} len  Indicates the data size. 
 * @return {*}  0 will be returned if the execution is successful, otherwise, values less than 0 will be returned.
 */
int32_t bcp_input(int32_t bcp_id, void *data, uint32_t len);

/**
 * @description: Check state (call it repeatedly, every 10ms-100ms)
 * @param {int32_t} bcp_id  Indicates the bcp id.
 * @return {*}
 */
void bcp_check(int32_t bcp_id);


// Redefined memory request function for external use
void* k_malloc(size_t size);

// Redefined memory release function for external use
void k_free(void *ptr);


#ifdef __cplusplus
}
#endif


#endif /* __PORT_H__ */
