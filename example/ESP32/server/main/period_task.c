#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_timer.h"
#include "esp_log.h"

/*********************************************************** test ***********************************************************/

static const char* TAG = "periode timer";

static void period_timer_init(esp_timer_handle_t *timer, esp_timer_cb_t cb, const char *timer_name)
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

}


static void timer_start(esp_timer_handle_t timer, uint32_t period_ms)
{
    /* Start the timers */
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, period_ms*1000));
    ESP_LOGI(TAG, "Started timers, time since boot: %lld us", esp_timer_get_time());
}



static void *queue_create(uint32_t num, uint32_t element_size)
{
    return (void *)xQueueCreate(num, element_size);
}

static bool dequeue(void *queue, void *data, uint32_t timeout)
{
    if (xQueueReceive((QueueHandle_t)queue, data, timeout) != pdPASS)
        return false;

    return true;
}

static bool enqueue_back(void *queue, void *data, uint32_t timeout)
{
    if (xQueueSendToBack((QueueHandle_t)queue, data, timeout) != pdPASS)
        return false;

    return true;
}

static bool enqueue_front(void *queue, void *data, uint32_t timeout)
{
    if (xQueueSendToFront((QueueHandle_t)queue, data, timeout) != pdPASS)
        return false;

    return true;
}

static bool queue_reset(void *queue)
{
    xQueueReset((QueueHandle_t)queue);

    return true;
}


/*********************************************************** timer ***********************************************************/
static esp_timer_handle_t periodic_timer;
static void *period_timer_queue = NULL;
static const char *periodic_timer_name = "periodic_timer2";

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



static void (*period_exec)(void) = NULL;

void period_exec_cb_register(void (*period_exec_cb)(void)) {
    period_exec = period_exec_cb;
}

static void period_task(void *arg)
{

    periodic_timer_init();
    timer_start(periodic_timer, 1000);

    while (1)
    {
        uint32_t cur_ticks = 0;
        if (dequeue(period_timer_queue, &cur_ticks, 1000000) == true)
        {
            if (cur_ticks%1 == 0)
            {
        
            }

            if (cur_ticks%5 == 0)
            {
                // 5s
                ESP_LOGI(TAG, "cur_ticks : %d", cur_ticks);

                if (period_exec != NULL) {
                    period_exec();
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
