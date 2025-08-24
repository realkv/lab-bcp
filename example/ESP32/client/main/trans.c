#include "trans.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
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

#include "bcp.h"
#include "bcp_os_adapter.h"



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

static uint8_t ble_con_id = 0;
static uint32_t start_time_ms = 0;

static int32_t ble_send(const bcp_block_t *bcp_block, void *data, uint32_t len)
{
    uint8_t *conn_id = (uint8_t *)(bcp_block->user_data);
    ble_tx(*conn_id, (uint8_t *)data, len);
    return 0;
}

static void recv_data_from_bcp(const bcp_block_t *bcp_block, void *data, uint32_t len)
{
    uint32_t s_time_ms = 0;
    memcpy(&s_time_ms, data, sizeof(s_time_ms));

    int32_t delta_ms = osal_get_ms() - s_time_ms;
    float throughput = 4 * 1000 * 1.0 / delta_ms;

    uint8_t *conn_id = (uint8_t *)(bcp_block->user_data);

    printf("-------------------------------- recv data ---------------------------\n");
    printf("len : %d, delta_ms : %d, throughput : %f KB\n", len, delta_ms, throughput);

    printf("conn_id : %d\n", *conn_id);
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
    ble_con_id = conn_id;

    bcp_parm_t bcp_parm;
    bcp_parm.mal = 4096;
    bcp_parm.mfs_scale = 9;
    bcp_parm.mtu = 497;
    bcp_parm.work_thread_name = "bcp_thread";
    bcp_parm.work_thread_priority = 3;
    bcp_parm.work_thread_stack_size = 4096*3;

    bcp_interface_t bcp_interface;
    bcp_interface.output = ble_send;
    bcp_interface.data_listener = recv_data_from_bcp;

    bcp_block  = bcp_create(&bcp_parm, &bcp_interface, &ble_con_id);
    if (bcp_block == NULL) {
        ESP_LOGI("trans", "bcp create fail !!!!!!!!!!!");
        return;
    }

    int32_t ret = bcp_open(bcp_block, bcp_opened_cb, 2000);
    if (ret < 0) {
        printf("bcp open failed\n");
    } else {
        printf("bcp open ok\n");
    }
    
}


static void ble_disconnected_handle(uint8_t conn_id)
{
    ble_con_id = conn_id;
    send_flag = 0;

    bcp_destory(bcp_block);
}



#define TEST_LEN   4096

static void bcp_test(void)
{
    static uint8_t test_data[TEST_LEN];
    for (uint32_t i = 0; i < TEST_LEN; i++)
    {
        test_data[i] = i%255;
    }

    if (send_flag != 0) 
    {
        printf("start test data send, len is %d\n", TEST_LEN);

        start_time_ms = osal_get_ms();
        memcpy(test_data, &start_time_ms, sizeof(start_time_ms));

        bcp_send(bcp_block, test_data, TEST_LEN);

        // bcp_send(ble_bcp_map.bcp_id, test_data, TEST_LEN);
        // bcp_send(ble_bcp_map.bcp_id, test_data, TEST_LEN);
    } else {
        printf("start test data send fail, send_flag is %d\n", send_flag);
    }

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


