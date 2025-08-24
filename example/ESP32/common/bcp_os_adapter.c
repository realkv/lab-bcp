#include "bcp_os_adapter.h"
#include "bcp.h"

#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#else
#include <pthread.h>
#include <semaphore.h>
#include <mqueue.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/select.h>
#include <unistd.h>
#endif

#ifdef OSAL_PLATFORM_IOS
#include <dispatch/dispatch.h>
#endif





#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
#define IS_INTER  xPortInIsrContext()
#endif
//---------------------------------------------------------------------
// queue                 
//---------------------------------------------------------------------
#ifndef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
typedef struct {
    char name[32];
    mqd_t mq;
} _mq_blk_t;
#endif

static int32_t bcp_queue_create(void **queue, uint32_t item_num, uint32_t item_size)
{
	int32_t ret = -1;

#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
	*(QueueHandle_t *)queue = xQueueCreate(item_num, item_size);
	if (*(QueueHandle_t *)queue != NULL) {
		ret = 0;
	}
#else
    _mq_blk_t *mq_blk = (_mq_blk_t *)malloc(sizeof(_mq_blk_t));
    if (mq_blk == NULL) {
        return ret;
    }

    sprintf(mq_blk->name, "%s_%d", "bcp_mq", (uint32_t)mq_blk);
    struct mq_attr attr;
    attr.mq_maxmsg = item_num;
    attr.mq_msgsize = item_size;
    mq_blk->mq = mq_open(mq_blk->name, O_CREAT | O_RDWR | O_EXCL, 0644, &attr);
    if (mq_blk->mq == (mqd_t)-1) {
        perror("bcp mq_open fail");
        return ret - 1;
    }

    *(_mq_blk_t **)queue = mq_blk;
#endif

	return ret;
}

static int32_t bcp_queue_send(void **queue, void *data, uint32_t size, uint32_t timeout)
{
	int32_t ret = -1;

#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
	if (IS_INTER) {
		static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		if (xQueueSendFromISR(*(QueueHandle_t *)queue, data, &xHigherPriorityTaskWoken) == pdTRUE) {
			ret = 0;
		}
	} else {
		if (xQueueSend(*(QueueHandle_t *)queue, data, timeout/portTICK_PERIOD_MS) == pdTRUE) {
			ret = 0;
		}
	}
#else
    _mq_blk_t *mq_blk = *(_mq_blk_t **)queue;
    ret = mq_send(mq_blk->mq, data, size, 0);
#endif

	return ret;
}


static int32_t bcp_queue_send_prior(void **queue, void *data, uint32_t size, uint32_t timeout)
{
	int32_t ret = -1;

#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
	if (IS_INTER) {
		static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		if (xQueueSendToFrontFromISR(*(QueueHandle_t *)queue, data, &xHigherPriorityTaskWoken) == pdTRUE) {
			ret = 0;
		}
	} else {
		if (xQueueSendToFront(*(QueueHandle_t *)queue, data, timeout/portTICK_PERIOD_MS) == pdTRUE) {
			ret = 0;
		}
	}
#else
    _mq_blk_t *mq_blk = *(_mq_blk_t **)queue;
    ret = mq_send(mq_blk->mq, data, size, 1);
#endif

	return ret;
}

static int32_t bcp_queue_recv(void **queue, void *data, uint32_t size, uint32_t timeout)
{
	int32_t ret = -1;

#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
	if (IS_INTER) {
		static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		if (xQueueReceiveFromISR(*(QueueHandle_t *)queue, data, &xHigherPriorityTaskWoken) == pdTRUE) {
			ret = 0;
		}
	} else {
		if (xQueueReceive(*(QueueHandle_t *)queue, data, timeout/portTICK_PERIOD_MS) == pdTRUE) {
			ret = 0;
		}
	}
#else
    _mq_blk_t *mq_blk = *(_mq_blk_t **)queue;
    uint32_t priority;
    ret = mq_receive(mq_blk->mq, data, size, &priority);
#endif

	return ret;

}

static void bcp_queue_destory(void **queue)
{
#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
	vQueueDelete(*(QueueHandle_t *)queue);
	*(QueueHandle_t *)queue = NULL;
#else
    _mq_blk_t *mq_blk = *(_mq_blk_t **)queue;
    mq_close(mq_blk->mq);
    mq_unlink(mq_blk->name);
    free(mq_blk);
    mq_blk = NULL;
#endif
}


//---------------------------------------------------------------------
// timer                 
//---------------------------------------------------------------------
static int32_t bcp_timer_create(void *timer, void (*period_cb)(void *arg), void *arg)
{
    int ret = -1;
#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
    esp_timer_handle_t *periodic_timer = (esp_timer_handle_t *)malloc(sizeof(esp_timer_handle_t));
    if (periodic_timer != NULL) {
        const esp_timer_create_args_t periodic_timer_args = {
            .callback = period_cb,
            /* name is optional, but may help identify the timer when debugging */
            .name = "periodic",
			.arg = arg,
        };

        ret = esp_timer_create(&periodic_timer_args, periodic_timer);
        *(esp_timer_handle_t **)timer = periodic_timer;
    }
#elif defined OSAL_PLATFORM_IOS
    dispatch_source_t *periodic_timer = (dispatch_source_t *)malloc(sizeof(dispatch_source_t));
    if (periodic_timer != NULL) {
        dispatch_queue_t queue = dispatch_get_global_queue(0, 0);
        *periodic_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
        dispatch_source_set_event_handler(*periodic_timer , ^{
            period_cb(arg);
        });

        ret = 0;
        *(dispatch_source_t **)timer = periodic_timer;
    }
#else
    timer_t *periodic_timer = (timer_t *)malloc(sizeof(timer_t));
    if (periodic_timer != NULL) {
        struct sigevent evp;
        memset(&evp, 0, sizeof(struct sigevent));
        evp.sigev_value.sival_int = 111;
		evp.sigev_value.sival_ptr = arg; 
        evp.sigev_notify = SIGEV_THREAD;
        evp.sigev_notify_function = period_cb;

        ret = timer_create(CLOCK_REALTIME, &evp, periodic_timer);
        *(timer_t **)timer = periodic_timer;
    }
#endif

    return ret;
}


static int32_t bcp_timer_start(void *timer, uint32_t period_ms)
{
    int ret = 0;

#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
    esp_timer_handle_t *periodic_timer = *(esp_timer_handle_t **)timer;
    ret = esp_timer_start_periodic(*periodic_timer, period_ms*1000);
#elif defined OSAL_PLATFORM_IOS
    dispatch_source_t *periodic_timer = *(dispatch_source_t **)timer;
	dispatch_time_t start = dispatch_walltime(NULL, 0);
    dispatch_source_set_timer(*periodic_timer, start, period_ms*NSEC_PER_MSEC, 0);
    dispatch_resume(*periodic_timer);
#else
    timer_t *periodic_timer = *(timer_t **)timer;
    struct itimerspec it;
    it.it_interval.tv_sec = period_ms/1000;
    it.it_interval.tv_nsec = (period_ms%1000) * 1000 * 1000;
    it.it_value.tv_sec = period_ms/1000;
    it.it_value.tv_nsec = (period_ms%1000) * 1000 * 1000;
    ret = timer_settime(*periodic_timer, 0, &it, NULL);
#endif

    return ret;
}


static int32_t bcp_timer_stop(void *timer)
{
    int ret = 0;

#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
    esp_timer_handle_t *periodic_timer = *(esp_timer_handle_t **)timer;
    ret = esp_timer_stop(*periodic_timer);
#elif defined OSAL_PLATFORM_IOS
    dispatch_source_t *periodic_timer = *(dispatch_source_t **)timer;
    dispatch_suspend(*periodic_timer);
#else
    timer_t *periodic_timer = *(timer_t **)timer;
    struct itimerspec it;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_nsec = 0;
    it.it_value.tv_sec = 0;
    it.it_value.tv_nsec = 0;
    ret = timer_settime(*periodic_timer, 0, &it, NULL);
#endif

    return ret;
}


static int32_t bcp_timer_destory(void *timer)
{
    int ret = 0;

#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
    esp_timer_handle_t *periodic_timer = *(esp_timer_handle_t **)timer;
    ret = esp_timer_delete(*periodic_timer);
    free(periodic_timer);
    periodic_timer = NULL;
#elif defined OSAL_PLATFORM_IOS
    dispatch_source_t *periodic_timer = *(dispatch_source_t **)timer;
    dispatch_source_cancel(*periodic_timer);
    free(periodic_timer);
    periodic_timer = NULL;
#else
    timer_t *periodic_timer = *(timer_t **)timer;
    ret = timer_delete(*periodic_timer);
    free(periodic_timer);
    periodic_timer = NULL;
#endif

    return ret;
}


//---------------------------------------------------------------------
// time                 
//---------------------------------------------------------------------
static uint32_t bcp_get_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000 + tv.tv_usec/1000);
}

static void bcp_delay_ms(uint32_t ms)
{
#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
	vTaskDelay(ms);
#else
	struct timeval delay;
	delay.tv_sec = ms/1000;
	delay.tv_usec = (ms%1000) *1000; 
	select(0, NULL, NULL, NULL, &delay);
#endif
}

//---------------------------------------------------------------------
// thread                 
//---------------------------------------------------------------------

static int32_t bcp_thread_create(void **thread, bcp_thread_config_t *thread_config)
{   
    int ret = -1;

#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
	TaskHandle_t *thread_handle = (TaskHandle_t *)malloc(sizeof(TaskHandle_t));
    if (thread_handle != NULL) {

        ret = xTaskCreate(thread_config->thread_func, thread_config->thread_name, thread_config->thread_stack_size/4, 
        thread_config->arg, thread_config->thread_priority, thread_handle);

		ret = ret == pdPASS ? 0 : -1;

        *(TaskHandle_t **)thread = thread_handle;
    }
#else
    pthread_t *thread_handle = (pthread_t *)osal_malloc(sizeof(pthread_t));
    if (thread_handle != NULL) {
		ret = pthread_create(thread_handle, NULL, thread_config->thread_func, thread_config->arg);
        pthread_detach(*thread_handle);

        *(pthread_t **)thread = thread_handle;
    }
#endif

    return ret;
}

static int32_t bcp_thread_destory(void **thread)
{   
    int ret = 0;

#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
	TaskHandle_t *thread_handle = *(TaskHandle_t **)thread;
    vTaskDelete(*thread_handle); 
#else
    pthread_t *thread_handle = *(pthread_t **)thread;
#endif
    free(thread_handle);
    thread_handle = NULL;

    return ret;
}

static void bcp_thread_exit(void **thread)
{   
#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
	TaskHandle_t *thread_handle = *(TaskHandle_t **)thread;
    vTaskDelete(NULL);
#else
    pthread_t *thread_handle = *(pthread_t **)thread;
    pthread_exit(NULL);
#endif

    free(thread_handle);
    thread_handle = NULL;
}


//---------------------------------------------------------------------
// critical section             
//---------------------------------------------------------------------
#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#endif

static int32_t bcp_create_critical(void **section)
{
    int32_t ret = 0;

#ifndef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
    pthread_mutex_t *x_mutext = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	if (x_mutext != NULL) {
		if (pthread_mutex_init(x_mutext, NULL) != 0) {
			free(x_mutext);
			x_mutext = NULL;
			ret = -1;
		} else {
			*(pthread_mutex_t **)section = x_mutext;
		}
	}
#endif

    return ret;
}


static void bcp_destory_critical(void **section)
{
#ifndef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
    pthread_mutex_t *x_mutext = *(pthread_mutex_t **)section;
	pthread_mutex_destroy(x_mutext);
	free(x_mutext);
	x_mutext = NULL;
#endif 
}
static void bcp_enter_critical(void **section)
{
#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
	taskENTER_CRITICAL(&mux);
#else
    struct timespec tout;
    clock_gettime(CLOCK_REALTIME, &tout);
    uint32_t diff_sec = 2;
    uint32_t diff_ms = 0;

    unsigned long long ns = tout.tv_nsec + diff_ms * 1000 * 1000;
    tout.tv_sec += diff_sec;
    tout.tv_sec += ns/1000000000;
    tout.tv_nsec = ns%1000000000;

    pthread_mutex_t *x_mutext = *(pthread_mutex_t **)section;
    ret = pthread_mutex_timedlock(x_mutext, &tout);
#endif
    
}

static void bcp_exit_critical(void **section)
{
#ifdef OSAL_PLATFORM_EMBEDDED_FREERTOS_ESP32
	taskEXIT_CRITICAL(&mux);
#else
    pthread_mutex_t *x_mutext = *(pthread_mutex_t **)section;
	pthread_mutex_unlock(x_mutext);
#endif
}


//---------------------------------------------------------------------
// crc             
//---------------------------------------------------------------------
static const uint16_t crc16tab[256]= {
    0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
    0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef,
    0x1231,0x0210,0x3273,0x2252,0x52b5,0x4294,0x72f7,0x62d6,
    0x9339,0x8318,0xb37b,0xa35a,0xd3bd,0xc39c,0xf3ff,0xe3de,
    0x2462,0x3443,0x0420,0x1401,0x64e6,0x74c7,0x44a4,0x5485,
    0xa56a,0xb54b,0x8528,0x9509,0xe5ee,0xf5cf,0xc5ac,0xd58d,
    0x3653,0x2672,0x1611,0x0630,0x76d7,0x66f6,0x5695,0x46b4,
    0xb75b,0xa77a,0x9719,0x8738,0xf7df,0xe7fe,0xd79d,0xc7bc,
    0x48c4,0x58e5,0x6886,0x78a7,0x0840,0x1861,0x2802,0x3823,
    0xc9cc,0xd9ed,0xe98e,0xf9af,0x8948,0x9969,0xa90a,0xb92b,
    0x5af5,0x4ad4,0x7ab7,0x6a96,0x1a71,0x0a50,0x3a33,0x2a12,
    0xdbfd,0xcbdc,0xfbbf,0xeb9e,0x9b79,0x8b58,0xbb3b,0xab1a,
    0x6ca6,0x7c87,0x4ce4,0x5cc5,0x2c22,0x3c03,0x0c60,0x1c41,
    0xedae,0xfd8f,0xcdec,0xddcd,0xad2a,0xbd0b,0x8d68,0x9d49,
    0x7e97,0x6eb6,0x5ed5,0x4ef4,0x3e13,0x2e32,0x1e51,0x0e70,
    0xff9f,0xefbe,0xdfdd,0xcffc,0xbf1b,0xaf3a,0x9f59,0x8f78,
    0x9188,0x81a9,0xb1ca,0xa1eb,0xd10c,0xc12d,0xf14e,0xe16f,
    0x1080,0x00a1,0x30c2,0x20e3,0x5004,0x4025,0x7046,0x6067,
    0x83b9,0x9398,0xa3fb,0xb3da,0xc33d,0xd31c,0xe37f,0xf35e,
    0x02b1,0x1290,0x22f3,0x32d2,0x4235,0x5214,0x6277,0x7256,
    0xb5ea,0xa5cb,0x95a8,0x8589,0xf56e,0xe54f,0xd52c,0xc50d,
    0x34e2,0x24c3,0x14a0,0x0481,0x7466,0x6447,0x5424,0x4405,
    0xa7db,0xb7fa,0x8799,0x97b8,0xe75f,0xf77e,0xc71d,0xd73c,
    0x26d3,0x36f2,0x0691,0x16b0,0x6657,0x7676,0x4615,0x5634,
    0xd94c,0xc96d,0xf90e,0xe92f,0x99c8,0x89e9,0xb98a,0xa9ab,
    0x5844,0x4865,0x7806,0x6827,0x18c0,0x08e1,0x3882,0x28a3,
    0xcb7d,0xdb5c,0xeb3f,0xfb1e,0x8bf9,0x9bd8,0xabbb,0xbb9a,
    0x4a75,0x5a54,0x6a37,0x7a16,0x0af1,0x1ad0,0x2ab3,0x3a92,
    0xfd2e,0xed0f,0xdd6c,0xcd4d,0xbdaa,0xad8b,0x9de8,0x8dc9,
    0x7c26,0x6c07,0x5c64,0x4c45,0x3ca2,0x2c83,0x1ce0,0x0cc1,
    0xef1f,0xff3e,0xcf5d,0xdf7c,0xaf9b,0xbfba,0x8fd9,0x9ff8,
    0x6e17,0x7e36,0x4e55,0x5e74,0x2e93,0x3eb2,0x0ed1,0x1ef0
};

static uint16_t crc16_calculate(const char *buf, int len) {
    int counter;
    uint16_t crc = 0;
    for (counter = 0; counter < len; counter++)
            crc = (crc<<8) ^ crc16tab[((crc>>8) ^ *buf++)&0x00FF];
    return crc;
}

//---------------------------------------------------------------------
// interface             
//---------------------------------------------------------------------
void bcp_pre_init(void)
{
    bcp_adapter_port_t bcp_adapter_port;

    bcp_adapter_port.bcp_thread.thread_create = bcp_thread_create;
    bcp_adapter_port.bcp_thread.thread_destory = bcp_thread_destory;
    bcp_adapter_port.bcp_thread.thread_exit = bcp_thread_exit;

    bcp_adapter_port.bcp_queue.queue_create = bcp_queue_create;
    bcp_adapter_port.bcp_queue.queue_send = bcp_queue_send;
    bcp_adapter_port.bcp_queue.queue_send_prior = bcp_queue_send_prior;
    bcp_adapter_port.bcp_queue.queue_recv = bcp_queue_recv;
    bcp_adapter_port.bcp_queue.queue_destory = bcp_queue_destory;

    bcp_adapter_port.bcp_time.delay_ms = bcp_delay_ms;
    bcp_adapter_port.bcp_time.get_ms = bcp_get_ms;

    bcp_adapter_port.bcp_timer.timer_create = bcp_timer_create;
    bcp_adapter_port.bcp_timer.timer_destory = bcp_timer_destory;
    bcp_adapter_port.bcp_timer.timer_start = bcp_timer_start;
    bcp_adapter_port.bcp_timer.timer_stop = bcp_timer_stop;

    bcp_adapter_port.bcp_mem.bcp_malloc = malloc;
    bcp_adapter_port.bcp_mem.bcp_free = free;

    bcp_adapter_port.bcp_crc.crc16_cal = crc16_calculate;

    bcp_adapter_port.bcp_critical.enter_critical_section = bcp_enter_critical;
    bcp_adapter_port.bcp_critical.leave_critical_section = bcp_exit_critical;
    bcp_adapter_port.bcp_critical.critical_section_create = bcp_create_critical;
    bcp_adapter_port.bcp_critical.critical_section_destory = bcp_destory_critical;
    
    bcp_adapter_port_init(&bcp_adapter_port);
}