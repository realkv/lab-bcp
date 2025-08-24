/*
 * @Author: your name
 * @Date: 2021-10-12 11:57:19
 * @LastEditTime: 2022-02-07 20:04:10
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: \esp32-new\sources\coap_test\main\common.c
 */

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

#include "common.h"


/*********************************************************** timer ***********************************************************/


static const char* TAG = "period timer";

void period_timer_init(esp_timer_handle_t *timer, timer_cb cb, const char *timer_name)
{
    /* Create two timers:
     * 1. a periodic timer which will run every 0.5s, and print a message
     */

    const esp_timer_create_args_t periodic_timer_args = {
            .callback = cb,
            /* name is optional, but may help identify the timer when debugging */
            .name = timer_name
    };

    
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, timer));
    /* The timer has been created but is not running yet */


/*     ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(PERIOD));
    esp_light_sleep_start();

    ESP_LOGI(TAG, "Woke up from light sleep, time since boot: %lld us",
                esp_timer_get_time());

    Let the timer run for a little bit more
    usleep(2000000);

    Clean up and finish the example
    ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
    ESP_ERROR_CHECK(esp_timer_delete(periodic_timer));
    ESP_ERROR_CHECK(esp_timer_delete(oneshot_timer));
    ESP_LOGI(TAG, "Stopped and deleted timers"); */
}


void timer_start(esp_timer_handle_t timer, uint32_t period_ms)
{
    /* Start the timers */
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, period_ms*1000));
    ESP_LOGI(TAG, "Started timers, time since boot: %lld us", esp_timer_get_time());
}


/*********************************************************** queue ***********************************************************/

void *queue_create(uint32_t num, uint32_t element_size)
{
    return (void *)xQueueCreate(num, element_size);
}

bool dequeue(void *queue, void *data, uint32_t timeout)
{
    if (xQueueReceive((QueueHandle_t)queue, data, timeout) != pdPASS)
        return false;

    return true;
}

bool enqueue_back(void *queue, void *data, uint32_t timeout)
{
    if (xQueueSendToBack((QueueHandle_t)queue, data, timeout) != pdPASS)
        return false;

    return true;
}

bool enqueue_front(void *queue, void *data, uint32_t timeout)
{
    if (xQueueSendToFront((QueueHandle_t)queue, data, timeout) != pdPASS)
        return false;

    return true;
}

bool queue_reset(void *queue)
{
    xQueueReset((QueueHandle_t)queue);

    return true;
}



/*********************************************************** sempher ***********************************************************/

//信号量、互斥锁资源注册
int32_t sem_new(void *sem, uint32_t max_count, uint32_t init_count)
{
    int ret = -1;
    SemaphoreHandle_t *xsem = (SemaphoreHandle_t *)sem;
    if (xsem) {
        *xsem = xSemaphoreCreateCounting(max_count, init_count);
        if ((*xsem) != NULL) {
            ret = 0;
        }
    }

    return ret;
}

void sem_give(void *sem)
{
    xSemaphoreGive(*(SemaphoreHandle_t *)sem);
}

int32_t sem_take(void *sem, uint32_t timeout)
{
    int ret = 0;

    if (timeout ==  0xfffffffful) {
        if (xSemaphoreTake(*(SemaphoreHandle_t *)sem, portMAX_DELAY) != pdTRUE) {
            ret = -1;
        }
    } else {
        if (xSemaphoreTake(*(SemaphoreHandle_t *)sem, timeout / portTICK_PERIOD_MS) != pdTRUE) {
            ret = -2;
        }
    }

    return ret;
}

// Deallocates a semaphore
void sem_free(void *sem)
{
    vSemaphoreDelete(*(SemaphoreHandle_t *)sem);
    *(SemaphoreHandle_t *)sem = NULL;
}


