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
#include "period_task.h"
#include "common.h"

#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_timer.h"

#include "bcp.h"
#include "bcp_os_adapter.h"



//---------------------------------------------------------------------
// ms get                 
//---------------------------------------------------------------------
static uint32_t osal_get_ms(void)
{
    // return esp_timer_get_time()/1000;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000 + tv.tv_usec/1000);
}

//---------------------------------------------------------------------
// log              
//---------------------------------------------------------------------
static void bcp_log_output(uint32_t level, const char *message)
{
    printf("%d ", level);
    printf("%s", message);
}

//---------------------------------------------------------------------
// interface             
//---------------------------------------------------------------------

static void bcp_init_before(void)
{
    bcp_pre_init();

    bcp_log_level_set(BCP_LOG_NONE);
    bcp_log_output_register(bcp_log_output);
}


//---------------------------------------------------------------------
// test start               
//---------------------------------------------------------------------

static bcp_block_t *bcp_block = NULL;
static volatile uint8_t send_flag = 0;

static int32_t ble_con_id = 0;

static int32_t ble_send(const bcp_block_t *bcp_block, void *data, uint32_t len)
{
    ble_tx(ble_con_id, (uint8_t *)data, len);
    return 0;
}

static void recv_data_from_bcp(const bcp_block_t *sbcp_block, void *data, uint32_t len)
{
    uint8_t buf[10];
    memcpy(buf, data, sizeof(uint32_t));

    if (send_flag != 0) {
        bcp_send(bcp_block, buf, 10);
    }
    
    static uint32_t recv_count = 0;
    printf("-------------------------------- recv data ---------------------------\n");
    printf("len : %d, recv_count : %d\n", len, recv_count++);
    printf("----------------------------------------------------------------------\n");
}



static void bcp_opened_cb(const bcp_block_t *bcp_block, bcp_open_status_t status)
{
    if (BCP_OPEND_OK == status) {
        send_flag = 1;
    }
}


static void ble_connected_handle(uint8_t conn_id)
{
    bcp_parm_t bcp_parm;
    bcp_parm.mal = 4096;
    bcp_parm.mfs_scale = 4;
    bcp_parm.mtu = 497;
    bcp_parm.work_thread_name = "bcp_thread";
    bcp_parm.work_thread_priority = 3;
    bcp_parm.work_thread_stack_size = 4096*3;

    bcp_interface_t bcp_interface;
    bcp_interface.output = ble_send;
    bcp_interface.data_listener = recv_data_from_bcp;

    bcp_block = bcp_create(&bcp_parm, &bcp_interface, &ble_con_id);
    if (bcp_block == NULL) {
        ESP_LOGI("trans", "bcp create fail !!!!!!!!!!!");
        return;
    } else {
        ESP_LOGI("trans", "bcp create ok");
    }

    ble_con_id = conn_id;
    int32_t ret = bcp_open(bcp_block, bcp_opened_cb, 2000);
    if (ret < 0) {
        printf("bcp create failed\n");
    }
    printf("bcp create ok\n");
}



static void ble_disconnected_handle(uint8_t conn_id)
{
    ble_con_id = conn_id;
    send_flag = 0;

    bcp_destory(bcp_block);

}



#define TEST_LEN   1500

static void bcp_test(void)
{
    printf("+++>>>count, free mem is %d, min mem is %d\n", xPortGetFreeHeapSize(), xPortGetMinimumEverFreeHeapSize());
}


static void bcp_rx(uint8_t conn_id, uint8_t *data, uint16_t len)
{
    bcp_input(bcp_block, data, len);
}


void trans_task_init(void)
{

	ESP_LOGI("trans", "trans init start ... ");

    bcp_init_before();

    connect_register(ble_connected_handle, ble_disconnected_handle);
    ble_rx_register(bcp_rx);

     period_task_register(bcp_test);

    ble_init();
}


