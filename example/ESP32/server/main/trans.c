#include "trans.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_err.h"

#include "gattc_demo.h"

#include "bcp_port.h"
#include "crc16.h"
#include "period_task.h"


//---------------------------------------------------------------------
// semaphore                
//---------------------------------------------------------------------
static int32_t sem_new(void *sem, uint32_t max_count, uint32_t init_count)
{
	int ret = -1;
	SemaphoreHandle_t xsem;
	xsem = xSemaphoreCreateCounting(max_count, init_count);
	{
		if (xsem != NULL) 
		{
			ret = 0;
			*(SemaphoreHandle_t *)sem = xsem;
		}
	}

	return ret;
}

static void sem_give(void *sem)
{
	xSemaphoreGive(*(SemaphoreHandle_t *)sem);
}

static int32_t sem_take(void *sem, uint32_t timeout)
{
	int ret = 0;
	if (timeout >=  0xfffful) 
    {
		if (xSemaphoreTake(*(SemaphoreHandle_t *)sem, portMAX_DELAY) != pdTRUE) 
        {
			ret = -1;
		}
	} else {
		if (xSemaphoreTake(*(SemaphoreHandle_t *)sem, timeout / portTICK_PERIOD_MS) != pdTRUE) 
        {
			ret = -2;
		}
	}
	return ret;
}

static void sem_free(void *sem)
{
	vSemaphoreDelete(*(SemaphoreHandle_t *)sem);
	*(SemaphoreHandle_t *)sem = NULL;
}


static void bcp_sem_init(void)
{
	b_sem_t sem;
	sem.sem_create = sem_new;
	sem.sem_give = sem_give;
	sem.sem_take = sem_take;
	sem.sem_free = sem_free;

	sem_func_register(&sem);
}



//---------------------------------------------------------------------
// mutex               
//---------------------------------------------------------------------

static int32_t mutex_new(void *mutex)
{
    int ret = -1;

    *(xSemaphoreHandle *)mutex = xSemaphoreCreateMutex();
	if (*(xSemaphoreHandle *)mutex != NULL) {
		ret = 0;
	}

    return ret;
}

static int32_t mutex_lock(void *mutex, uint32_t timeout)
{
    int ret = 0;

    if (timeout >=  0xfffful) 
    {
        if (xSemaphoreTake(*(xSemaphoreHandle *)mutex, portMAX_DELAY) != pdTRUE) 
        {
            ret = -1;
        }
    } else {
        if (xSemaphoreTake(*(xSemaphoreHandle *)mutex, timeout / portTICK_PERIOD_MS) != pdTRUE)  
        {
            ret = -2;
        }
    }

    return ret;
}

static void mutex_unlock(void *mutex)
{
    xSemaphoreGive(*(xSemaphoreHandle *)mutex);
}

static void mutex_free(void *mutex)
{
    vSemaphoreDelete(*(xSemaphoreHandle *)mutex);
    *(xSemaphoreHandle *)mutex = NULL;
}


static void bcp_mutex_init(void)
{
    b_mutex_t mutex;
    mutex.mutex_create = mutex_new;
    mutex.mutex_lock = mutex_lock;
    mutex.mutex_unlock = mutex_unlock;
    mutex.mutex_free = mutex_free;

    mutex_func_register(&mutex);
}

//---------------------------------------------------------------------
// mem              
//---------------------------------------------------------------------
static void bcp_mem_func_init(void)
{
    k_allocator(pvPortMalloc, vPortFree);
}

//---------------------------------------------------------------------
// time              
//---------------------------------------------------------------------
static uint32_t get_sys_ms(void)
{
    return esp_timer_get_time()/1000;
}

static void bcp_time_func_init(void)
{
    op_get_ms_register(get_sys_ms);
}

//---------------------------------------------------------------------
// crc              
//---------------------------------------------------------------------
// static void bcp_crc_func_init(void) 
// {

// }

//---------------------------------------------------------------------
// log              
//---------------------------------------------------------------------
static void bcp_log_output(uint32_t level, const char *message)
{
    printf("%d ", level);
    printf("%s", message);
}


//---------------------------------------------------------------------
// ble               
//---------------------------------------------------------------------
#define max_len 4096

typedef struct {
    int32_t bcp_id;
    int32_t conn_id;
} map_table_t;

static map_table_t ble_bcp_map = { -1, -1 };
static esp_timer_handle_t bcp_timer;
static TaskHandle_t *bcp_task_handle = NULL;

static int32_t ble_send(int32_t bcp_id, void *data, uint32_t len)
{
    printf("send, bcp_id : %d, conn_id : %d, len : %d\n", bcp_id, ble_bcp_map.conn_id, len);
    return ble_tx(ble_bcp_map.conn_id, (uint8_t *)data, len);
}

static void recv_data_from_bcp(int32_t bcp_id, void *data, uint32_t len)
{
    printf("-------------------------------- recv data ---------------------------\n");
    printf("len : %d\n", len);
    printf("----------------------------------------------------------------------\n");

    bcp_send(bcp_id, data, len);
}


static void bcp_task(void *arg)
{
    while(1) 
    {
        if (ble_bcp_map.bcp_id >= 0) 
        {
            if (bcp_task_run(ble_bcp_map.bcp_id, 5000) < 0) 
            {
                printf("error!\n");
            }
        } 
        else 
        {
            vTaskDelay(100);
        }
    }
}

static int32_t bcp_task_create(uint32_t stack_size, uint8_t priority)
{
    if (bcp_task_handle == NULL) {
        bcp_task_handle = malloc(sizeof(TaskHandle_t));
        if (bcp_task_handle == NULL) {
            printf("bcp task create error\n");
            return -1;
        }

        int32_t ret = xTaskCreate(bcp_task, "bcp_task", stack_size, NULL, priority, bcp_task_handle);
        vTaskSuspend(*bcp_task_handle);
        printf("bcp task create, ret is %d\n", ret);
        return ret >= 0 ? 0 : -1;
    } else {
        printf("bcp task create, bcp_task_handle != NULL\n");
    }

    return 0;
}

static int32_t bcp_task_resume(void)
{
    if (bcp_task_handle == NULL) {
        printf("bcp handle is NULL\n");
        return -1;
    }

    vTaskResume(*bcp_task_handle);

    return 0;
}

static int32_t bcp_task_suspend(void)
{
    if (bcp_task_handle == NULL) {
        printf("bcp handle is NULL\n");
        return -1;
    }

    vTaskSuspend(*bcp_task_handle);

    return 0;
}


static void bcp_timer_cb(void *arg)
{
    if (ble_bcp_map.bcp_id >= 0) {
        bcp_check(ble_bcp_map.bcp_id);
    }
}


static void bcp_timer_start(void)
{
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = bcp_timer_cb,
        /* name is optional, but may help identify the timer when debugging */
        .name = "bcp_timer",
    };

    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &bcp_timer));

    ESP_ERROR_CHECK(esp_timer_start_periodic(bcp_timer, 100*1000));
}


static uint16_t temp_crc16(void *data, uint32_t len) 
{
    return crc16((const char *)data, len);
}


static void ble_connected_handle(uint8_t conn_id)
{
    bcp_parm_t bcp_parm = 
    {
        .need_ack = 0,
        .mtu = 180,
        .check_multiple = 5,
        .mal = 8192,
    };

    bcp_interface_t bcp_interface = 
    {
        .output = ble_send,
        .data_received = recv_data_from_bcp,
        .crc16_cal = crc16,
    };

    printf("=============================================================\n");
    printf("before start, free mem : %u\n", xPortGetFreeHeapSize());
    printf("=============================================================\n");

    ble_bcp_map.bcp_id = bcp_create(&bcp_parm, &bcp_interface);
    if (ble_bcp_map.bcp_id >= 0) 
    {
        ble_bcp_map.conn_id = conn_id;
        printf("bcp create ok, bcp id is %d, conn_id is %d\n", ble_bcp_map.bcp_id, ble_bcp_map.conn_id);
        bcp_task_resume();
        bcp_timer_start();
        return;
    }

    printf("bcp create failed\n");
}

static void bcp_release_notify(int32_t result)
{
    if (result == 0) 
    {
        ble_bcp_map.bcp_id = -1;

        ESP_ERROR_CHECK(esp_timer_stop(bcp_timer));
        ESP_ERROR_CHECK(esp_timer_delete(bcp_timer));

        printf("bcp release ok\n");

        printf("=============================================================\n");
        printf("after release, free mem : %u\n", xPortGetFreeHeapSize());
        printf("=============================================================\n");

        bcp_task_suspend();

    } else {
        printf("bcp release failed, result is %d\n", result);
    }

    
}

static void ble_disconnected_handle(uint8_t conn_id)
{
    if (ble_bcp_map.bcp_id >= 0) {
        printf("bcp release ...\n");
        bcp_release(ble_bcp_map.bcp_id, bcp_release_notify);
        ble_bcp_map.conn_id = -1;
    
        return;
    }
    
}


static void bcp_rx(uint8_t conn_id, uint8_t *data, uint16_t len)
{
    bcp_input(ble_bcp_map.bcp_id, data, len);
}


static void bcp_test(void)
{
    printf("cur time is %u\n", get_sys_ms());

    printf("=============================================================\n");
    printf("free mem : %u\n", xPortGetFreeHeapSize());
    printf("=============================================================\n");

}

void trans_ble_init(void)
{

	ESP_LOGI("trans", "trans init start ... ");

    bcp_sem_init();
    bcp_mutex_init();
    bcp_mem_func_init();
    bcp_time_func_init();
    bcp_log_level_set(BCP_LOG_TRACE);
    bcp_log_output_register(bcp_log_output);

    ble_rx_register(bcp_rx);

    connect_register(ble_connected_handle, ble_disconnected_handle);

    period_exec_cb_register(bcp_test);

    if (bcp_task_create(4096*2, 2) != 0) {
        printf("bcp task create failed\n");
        return;
    }

    ble_init();
}





