#include "common.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gattc_demo.h"


/*********************************************************** timer ***********************************************************/
static esp_timer_handle_t periodic_timer;
static void *period_timer_queue = NULL;
static const char *periodic_timer_name = "periodic_timer12";

static void periodic_timer_callback(void* arg)
{
    static uint32_t ticks = 0;
    enqueue_back(period_timer_queue, &ticks, 0);
    ticks++;
}

static void periodic_timer_init(void)
{
    period_timer_queue = queue_create(10, sizeof(uint32_t));
    period_timer_init(&periodic_timer, periodic_timer_callback, periodic_timer_name);
}


static const char* TAG = "PERIOD_TASK";



static void (*period_task_run)(void) = NULL;


void period_task_register(void (*func)(void))
{
    period_task_run = func;
}



static void period_task(void *arg)
{
    periodic_timer_init();
    timer_start(periodic_timer, 1000);

    static uint8_t ble_first_flag = 0;
    static uint8_t first_flag = 0;

    while (1)
    {
        uint32_t cur_ticks = 0;
        if (dequeue(period_timer_queue, &cur_ticks, 1000000) == true)
        {
            if (cur_ticks%1 == 0)
            {
                // 1s
                //ESP_LOGI(TAG, "=== free heap is %d", xPortGetFreeHeapSize());
        
            }

            if (cur_ticks%5 == 0)
            {
                // 5s
                // ESP_LOGI(TAG, "cur_ticks : %d", cur_ticks);
                // resources_update(cur_ticks%255);

                // medium_test();

                printf("cur_ticks : %d\r\n", cur_ticks);

                if (period_task_run) {
                    period_task_run();
                }

            }

            if (cur_ticks%10 == 0)
            {
                // 10s

            }
        }
    }
}


void period_task_create(uint32_t stack_size, uint8_t priority)
{
    xTaskCreate(period_task, "period_task", stack_size, NULL, priority, NULL);
    
}
