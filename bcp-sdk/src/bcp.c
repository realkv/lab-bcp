
#include <stdbool.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>

#include "priority_queue.h"
#include "bcp_port.h"


//---------------------------------------------------------------------
// bcp structure             
//---------------------------------------------------------------------

#define BCP_HEAD           0xA5A5

typedef enum {
    COMPLETE_FRAME = 0,
    START_FRAME,
    PROCESS_FRAME,
    END_FRAME,
} frame_type_t;

typedef enum {
    PART_RETRANS = 0,
    SELECT_RETRANS,
} retrans_type_t;

typedef struct msg_t {
    queue_node node;
    uint8_t event;
    void *context;
} msg_t;

typedef enum {
    FRAME_WAITING_ACK = 0,
    FRAME_WAITING_TRANS,
} frame_status_t;

typedef struct {
    queue_node node;
    uint32_t retrans_timeout;                  
    uint32_t timeout_ms;                       
    uint32_t rto;                              
    uint8_t trans_count;                       
    uint8_t lost_ack_count;                    
    uint16_t frame_len;
    uint16_t frame_offset;
    uint8_t bcp_id; 
    uint8_t frame_status;
    uint32_t trans_time;
    uint8_t frame_data[1];                     
    
} frame_t;

typedef struct {
    uint8_t is_ack;                            
    uint8_t fsn;
    uint8_t una;                                
    uint8_t bcp_id; 
} ack_t;

typedef struct {             
    uint32_t sync_timeout;
    uint8_t sync_count;
    uint16_t frame_len;
    uint8_t frame_data[1];
} sync_frame_t;

typedef enum {
    BCP_STOP = 0,
    BCP_HANDSHAKE,
    BCP_DONE,
} bcp_work_status_t;

typedef struct {
    uint8_t need_ack;                          
    uint8_t mtu;                                
    uint16_t mfs;                               
	uint32_t mal;								
    queue_node snd_buf;
    queue_node rcv_queue;
    queue_node rcv_buf;
    queue_node ack_list;

    uint8_t snd_next;                              
    uint8_t rcv_next;                            
    uint8_t snd_status;
    uint8_t rcv_status;

    frame_t *expected_frame;
    uint8_t *app_buf;
    sync_frame_t *sync_frame;
    uint32_t last_sync_id;

    uint8_t bcp_id;
    uint16_t ack_snd_buf_offset;
    uint8_t *ack_snd_buf;

    priority_queue_t msg_priority_queue;

    int32_t (*output)(int32_t bcp_id, void *data, uint32_t len);
    void (*data_received)(int32_t bcp_id, void *data, uint32_t len);
    uint16_t (*crc16_cal)(void *data, uint32_t len);
} bcp_t;


//---------------------------------------------------------------------
// log            
//---------------------------------------------------------------------
static void (*k_log_hook)(bcp_log_level_t, const char *) = NULL;
static bcp_log_level_t bcp_log_level = BCP_LOG_WARN;

void bcp_log_output_register(void (*log_output)(bcp_log_level_t level, const char *message)) {
    k_log_hook = log_output;
}

void bcp_log_level_set(bcp_log_level_t level) {
    bcp_log_level = level;
}

static void k_log(bcp_log_level_t level, const char *format, ...) {

    if (level > bcp_log_level) {
        return;
    }
    
    char buf[100];

    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, 100, format, ap);
    va_end(ap);
    
    if (k_log_hook) {
        k_log_hook(level, buf);
    } else {
        printf("bcp log level : %d", level);
        printf("%s\n", buf);
    }
}

//---------------------------------------------------------------------
// mem           
//---------------------------------------------------------------------
static void* bcp_malloc(size_t size) {
	return k_malloc(size);
}

static void bcp_free(void *ptr) {
	k_free(ptr);
}

void bcp_allocator(void* (*new_malloc)(size_t), void (*new_free)(void*)) {
    k_allocator(new_malloc, new_free);
}


static int8_t fsn_diff(uint8_t later, uint8_t earlier) {
    return ((int8_t)(later - earlier));
}

static int32_t time_diff(uint32_t later, uint32_t earlier) {
    return ((int32_t)(later - earlier));
}


static uint32_t (*bcp_gettime_ms)(void) = NULL;

void op_get_ms_register(uint32_t (*get_current_ms)(void)) {
    // get_ms_op = get_current_ms;
    bcp_gettime_ms = get_current_ms;
}


static uint32_t get_ms(void) {
    if (bcp_gettime_ms != NULL) {
        return bcp_gettime_ms();
    } 

    return 0;
}


//---------------------------------------------------------------------
// event priority            
//---------------------------------------------------------------------
// Higher number, higher priority
#define APP_TX_EVENT        0
#define APP_RX_EVENT        1
#define RETRANS_EVENT       3
#define ACK_TX_EVENT        3
#define NACK_TX_EVENT       3
#define ACK_RX_EVENT        3
#define NACK_RX_EVENT       3
#define TIMER_CHECK_EVENT   4
#define BCP_CLEAN_EVENT     4
#define BCP_CREATE_EVENT    0
#define SYNC_TX_EVENT       4
#define SYNC_RX_EVENT       4

//---------------------------------------------------------------------
// bcp table                  
//---------------------------------------------------------------------

static bcp_t *bcp_table[MAX_BCP_NUM] = { NULL, NULL, NULL, NULL };

static int32_t get_idle_bcp_id(void) {
    int32_t id = -1;
    for (uint8_t i = 0; i < MAX_BCP_NUM; i++) {
        if (bcp_table[i] == NULL) {
            id = i;
            break;
        }
    }

    return id; 
}

static int32_t add_bcp_by_id(int32_t bcp_id, bcp_t *bcp) {
    if (bcp_id < 0 || 
    bcp_id >= MAX_BCP_NUM || 
    bcp == NULL) {
        return -1;
    }

    if (bcp_table[bcp_id] != NULL) {
        return -2;
    }

    bcp_table[bcp_id] = bcp;

    return 0;
}

static int32_t remove_bcp_by_id(int32_t bcp_id) {
    if (bcp_id < 0 || 
    bcp_id >= MAX_BCP_NUM) {
        return -1;
    }

    if (bcp_table[bcp_id] == NULL) {
        return 0;
    }

    bcp_table[bcp_id] = NULL;

    return 0;
}

static bcp_t *get_bcp_by_id(int32_t bcp_id) {
    if (bcp_id < 0 || bcp_id >= MAX_BCP_NUM) {
        return NULL;
    }

    return bcp_table[bcp_id];
}

//---------------------------------------------------------------------
// event run                  
//---------------------------------------------------------------------

typedef struct {
    void *context;
    void (*context_handler)(bcp_t *bcp, void *context);
    void (*context_free)(void *context);
} bcp_event_item_t;

static void msg_priority_queue_reset(int32_t bcp_id) {
    k_log(BCP_LOG_DEBUG, "bcp msg priority_queue reset\n");   
    bcp_t *bcp = get_bcp_by_id(bcp_id);
    if (bcp == NULL) {
        k_log(BCP_LOG_ERROR, "get bcp failed, bcp id : %d\n", bcp_id);
        return;
    }

    bcp_event_item_t event_item = { 0 };
    while (priority_queue_dequeue(&bcp->msg_priority_queue, &event_item, 0) == true) {
        k_log(BCP_LOG_INFO, "bcp msg priority_queue reset\n");  
        if (event_item.context_free) {
            event_item.context_free(event_item.context);
        }
    }
}

static void msg_priority_queue_deinit(int32_t bcp_id) {
    k_log(BCP_LOG_DEBUG, "bcp msg priority_queue deinit\n");   
    bcp_t *bcp = get_bcp_by_id(bcp_id);
    if (bcp == NULL) {
        k_log(BCP_LOG_ERROR, "get bcp failed, bcp id : %d\n", bcp_id);
        return;
    }

    msg_priority_queue_reset(bcp_id);
    priority_queue_deinit(&bcp->msg_priority_queue);
}

static bool bcp_event_post( int32_t bcp_id,
                            uint32_t priority,
                            void *context,
                            void (*context_handler)(bcp_t *bcp, void *context), 
                            void (*context_free)(void *context) ) {
    bcp_t *bcp = get_bcp_by_id(bcp_id);
    if (bcp == NULL) {
        k_log(BCP_LOG_ERROR, "event post, get bcp failed, bcp id : %d\n", bcp_id);
        return false;
    }

    bcp_event_item_t event_item = { context,
                                    context_handler,
                                    context_free };

    return priority_queue_enqueue(&bcp->msg_priority_queue, priority, 
    &event_item, sizeof(bcp_event_item_t), 0);    
}

int32_t bcp_task_run(int32_t bcp_id, uint32_t timeout_ms) {

    bcp_t *bcp = get_bcp_by_id(bcp_id);
    if (bcp == NULL) {
        // k_log(BCP_LOG_ERROR, "get bcp failed, bcp id : %d\n", bcp_id);
        return -1;
    }

    bcp_event_item_t event_item = { 0 };
    if (priority_queue_dequeue(&bcp->msg_priority_queue, &event_item, timeout_ms) == true) {
        if (event_item.context_handler) {
            event_item.context_handler(bcp, event_item.context);
        }

        if (event_item.context_free) {
            event_item.context_free(event_item.context);
        }
    }

    return 0;
}


//---------------------------------------------------------------------
// init and deinit                 
//---------------------------------------------------------------------
#define SYNC_REQ_CMD        0x01
#define SYNC_RSP_CMD        0x02

static void sync_frame_send(bcp_t *bcp, uint8_t cmd);

typedef struct {
    void (*result_notify)(int32_t result);
} bcp_release_parm_t;

static void bcp_clean_handler(bcp_t *bcp, void *context) {
    bcp_release_parm_t *bcp_release_parm = (bcp_release_parm_t *)context;
    if (bcp == NULL) {
        k_log(BCP_LOG_ERROR, "bcp clean, bcp is NULL\n");
        if (bcp_release_parm != NULL && bcp_release_parm->result_notify != NULL) {
            bcp_release_parm->result_notify(-1);
        }
        return;
    }

    bcp->snd_status = BCP_STOP;
    bcp->rcv_status = BCP_STOP;

    while (!queue_is_empty(&bcp->snd_buf)) {
        frame_t *frame = queue_entry(bcp->snd_buf.next, frame_t, node);
        k_log(BCP_LOG_DEBUG, "bcp clean snd buf, frame is %d\r\n", frame->frame_data[3]);
        queue_del(&frame->node);
        bcp_free(frame);  
    }

    while (!queue_is_empty(&bcp->rcv_queue)) {
        frame_t *frame = queue_entry(bcp->rcv_queue.next, frame_t, node);
        queue_del(&frame->node);
        bcp_free(frame);
    }

    while (!queue_is_empty(&bcp->rcv_buf)) {
        frame_t *frame = queue_entry(bcp->rcv_buf.next, frame_t, node);
        queue_del(&frame->node);
        bcp_free(frame);
    }

    if (bcp->need_ack == 1) {
        while (!queue_is_empty(&bcp->ack_list)) {
            frame_t *frame = queue_entry(bcp->ack_list.next, frame_t, node);
            queue_del(&frame->node);
            bcp_free(frame);
        }

        bcp->ack_snd_buf_offset = 0;
        if (bcp->ack_snd_buf != NULL) {
            bcp_free(bcp->ack_snd_buf);
        }
    }

    if (bcp->expected_frame != NULL) {
        bcp_free(bcp->expected_frame);
    }

    if (bcp->app_buf != NULL) {
        bcp_free(bcp->app_buf);
    }

    if (bcp->sync_frame != NULL) {
        bcp_free(bcp->sync_frame);
    }

    msg_priority_queue_deinit(bcp->bcp_id);

    remove_bcp_by_id(bcp->bcp_id);
    bcp_free(bcp);
    
    
    if (bcp_release_parm != NULL && bcp_release_parm->result_notify != NULL) {
        bcp_release_parm->result_notify(0);
    }

    k_log(BCP_LOG_INFO, "bcp clean over\r\n");
}


int32_t bcp_release(int32_t bcp_id, void (*result_notify)(int32_t result)) {
    bcp_t *bcp = get_bcp_by_id(bcp_id);
    if (bcp == NULL) {
        k_log(BCP_LOG_ERROR, "bcp release, bcp id is error\n");
        return -1;
    }

    bcp_release_parm_t *bcp_release_parm = (bcp_release_parm_t *)bcp_malloc(sizeof(bcp_release_parm_t));
    if (bcp_release_parm == NULL) {
        k_log(BCP_LOG_ERROR, "bcp release, get mem failed\n");
        return -2;
    }

    bcp_release_parm->result_notify = result_notify;

    k_log(BCP_LOG_TRACE, "bcp release, bcp_id : %d\n", bcp_id);
    if (bcp_event_post(bcp_id, BCP_CLEAN_EVENT, bcp_release_parm, bcp_clean_handler, bcp_free) != true) {
        bcp_free(bcp_release_parm);
        bcp_release_parm = NULL;
        k_log(BCP_LOG_ERROR, "bcp release, event enqueue failed\n");
        return -3;
    }

    return 0;
}


static int32_t bcp_init(bcp_t *bcp, const bcp_parm_t *bcp_parm, const bcp_interface_t *bcp_interface) {
    bcp->need_ack = bcp_parm->need_ack;
    bcp->mal = bcp_parm->mal;

    bcp->app_buf = (uint8_t *) bcp_malloc(bcp->mal);
    if (bcp->app_buf == NULL) {
        k_log(BCP_LOG_ERROR, "bcp init, bcp_malloc failed\n");
        return -1;
    }

    uint16_t ack_snd_buf_size = 0;
    if (bcp->need_ack == 1) {
        ack_snd_buf_size = 512;
        bcp->ack_snd_buf = (uint8_t *) bcp_malloc(ack_snd_buf_size);
        if (bcp->ack_snd_buf == NULL) {
            k_log(BCP_LOG_ERROR, "bcp init, bcp_malloc failed\n");
            bcp_free(bcp->app_buf);
            return -2;
        }

        bcp->ack_snd_buf_offset = 0;
    }

    if (priority_queue_init(&bcp->msg_priority_queue, 4, 50) != true) {
        if (bcp->need_ack == 1 && bcp->ack_snd_buf != NULL) {
            bcp_free(bcp->ack_snd_buf);
            bcp->ack_snd_buf = NULL;
        }
        bcp_free(bcp->app_buf);
        k_log(BCP_LOG_ERROR, "bcp init, bcp msg queue init failed\n");
        return -3;
    }

    bcp->need_ack = bcp_parm->need_ack;                     
    bcp->mtu = bcp_parm->mtu;
    bcp->mfs = bcp_parm->mtu * bcp_parm->check_multiple;

    queue_init(&bcp->snd_buf);
    queue_init(&bcp->rcv_queue);
    queue_init(&bcp->rcv_buf);

    if (bcp->need_ack == 1) {
        queue_init(&bcp->ack_list);
    }

    bcp->expected_frame = NULL;
    bcp->sync_frame = NULL;
    
    bcp->snd_next = 0;
    bcp->rcv_next = 0;

    bcp->snd_status = BCP_HANDSHAKE;
    bcp->rcv_status = BCP_HANDSHAKE;

    bcp->output = bcp_interface->output;
    bcp->data_received = bcp_interface->data_received;
    bcp->crc16_cal = bcp_interface->crc16_cal;

    return 0;
}

static void sync_send_handler(bcp_t *bcp, void *context) {
    sync_frame_send(bcp, SYNC_REQ_CMD);
}

int32_t bcp_create(const bcp_parm_t *bcp_parm, const bcp_interface_t *bcp_interface) {

    int32_t bcp_id = get_idle_bcp_id();
    if (bcp_id < 0) {
        k_log(BCP_LOG_ERROR, "bcp create, The number of bcps has reached the upper limit\n");
        return -1;
    }

    bcp_t *bcp = get_bcp_by_id(bcp_id);
    if (bcp != NULL) {
        k_log(BCP_LOG_ERROR, "bcp create, bcp is already occupied\n");
        return -2;
    }

    bcp = (bcp_t *)bcp_malloc(sizeof(bcp_t));
    if (bcp == NULL) {
        k_log(BCP_LOG_ERROR, "bcp create, get mem failed\n");
        return -3;
    }

    if (bcp_init(bcp, bcp_parm, bcp_interface) != 0) {
        bcp_free(bcp);
        bcp = NULL;
        k_log(BCP_LOG_ERROR, "bcp create, bcp init failed\n");
        return -4;
    }

    bcp->bcp_id = bcp_id;
    if (add_bcp_by_id(bcp_id, bcp) != 0) {
        bcp_free(bcp);
        bcp = NULL;
        k_log(BCP_LOG_ERROR, "bcp create, bcp add failed\n");
        return -5;
    }

    bool ret = bcp_event_post(bcp_id, SYNC_TX_EVENT, NULL, sync_send_handler, bcp_free);
    if (ret != true) {
        bcp_free(bcp);
        bcp = NULL;
        k_log(BCP_LOG_ERROR, "bcp create, post sync event failed\n");
        return -6;
    }

    k_log(BCP_LOG_TRACE, "bcp create ok\n");

    return bcp_id;
}


//---------------------------------------------------------------------
// app send                
//---------------------------------------------------------------------
static void frame_send_now(bcp_t *bcp, frame_t *frame) {
    uint32_t count = (frame->frame_len + bcp->mtu - 1)/bcp->mtu;

    k_log(BCP_LOG_DEBUG, "frame_send_now, frame len is %d, frame sn is %d, seg count is %d\n", 
    frame->frame_len, frame->frame_data[3], count);

    frame->trans_time = get_ms();

	uint8_t *data = frame->frame_data;
	uint16_t frame_len = frame->frame_len;
	while(count > 0) {
		uint16_t len = frame_len > bcp->mtu ? bcp->mtu : frame_len;
        k_log(BCP_LOG_DEBUG, "bcp output, fsn is %d, len is %d, count is %d\n", frame->frame_data[3], len, count);
        bcp->output(bcp->bcp_id, data, len); 
		data += len;
		frame_len -= len;
		count--;
	}

    frame->frame_status = FRAME_WAITING_ACK;
    frame->trans_count++;

    frame->timeout_ms = get_ms() + frame->rto;
    frame->retrans_timeout = get_ms();
    
    k_log(BCP_LOG_DEBUG, "frame is new frame, fsn is %d, rto is %d, timeout_ms is %d\n", frame->frame_data[3], frame->rto, frame->timeout_ms);
    
}

static void frame_send(bcp_t *bcp, frame_t *frame) {
    if (bcp->output == NULL) {
        k_log(BCP_LOG_ERROR, "bcp output interface is NULL\n");
        return;
    }

    if (bcp->need_ack == 0) {
        frame_send_now(bcp, frame);
    }
}

static void frame_repack(bcp_t *bcp, uint8_t *frame_head, uint16_t len) {
    if (frame_head == NULL || len == 0) {
        k_log(BCP_LOG_ERROR, "frame pack error, frame head is NULL or len is zero\n");
        return;
    }
        
    uint16_t crc = bcp->crc16_cal(frame_head, len-2);

    uint8_t *p = &frame_head[len-2];
    *p++ = crc;
    *p++ = crc >> 8;
}

static void data_send_handler(bcp_t *bcp, void *context) {
    frame_t *frame = (frame_t *)context;
    queue_init(&frame->node);

    frame->frame_status = FRAME_WAITING_TRANS;
    frame->frame_data[3] = bcp->snd_next++;
    frame_repack(bcp, frame->frame_data, frame->frame_len);
    queue_add_tail(&frame->node, &bcp->snd_buf);

    if (bcp->snd_status != BCP_DONE) {
        k_log(BCP_LOG_DEBUG, "waitting trans ... ... ... \n");
        return;
    }

    k_log(BCP_LOG_DEBUG, "trans_data_send, frame len is %d, frame sn is %d\n", 
    frame->frame_len, frame->frame_data[3]);

    frame_send(bcp, frame);
}

static void trans_frame_pack(bcp_t *bcp, uint8_t *frame_head, uint8_t *data, uint16_t len, frame_type_t type) { 
    uint8_t *p = frame_head;
    uint8_t ctrl = 0xA0;
    if (bcp->need_ack == 1) {
        ctrl |= 0x10;
    }
    ctrl |= type;

    *p++ = 0xA5;
    *p++ = 0xA5;
    *p++ = ctrl;
    *p++ = bcp->snd_next;                              
    *p++ = (uint8_t)len;
    *p++ = (uint8_t)(len >> 8);

    memcpy(p, data, len);
    p += len;

    uint16_t crc = bcp->crc16_cal(frame_head, len + 6);

    *p++ = crc;
    *p++ = crc >> 8;
}

static int32_t trans_frame_enqueue(bcp_t *bcp, uint8_t *data, uint16_t len, frame_type_t type, uint16_t live_time) {
    if (bcp == NULL) {
        k_log(BCP_LOG_ERROR, "trans frame enqueue, bcp is NULL\n");
        return -1;
    }

    frame_t *frame = (frame_t *)bcp_malloc(sizeof(frame_t) + len + 8);
    if (frame == NULL) {
        k_log(BCP_LOG_ERROR, "frame enqueue error, get mem failed\n");
        return -2;
    }

    frame->rto = MAX_INTERVAL_RTO*((bcp->mfs + bcp->mtu - 1)/bcp->mtu)*live_time; 
       
    frame->trans_count = 0;
    frame->lost_ack_count = 0;
    frame->frame_len = len + 8;
 
    trans_frame_pack(bcp, frame->frame_data, data, len, type);

    k_log(BCP_LOG_DEBUG, "trans_frame_enqueue, %d frame packed ok, rto is %d\n", frame->frame_data[3], frame->rto);

    return bcp_event_post(bcp->bcp_id, APP_TX_EVENT, frame, data_send_handler, NULL) == true ? 0 : -3;  
}

static int32_t data_divide(bcp_t *bcp, uint8_t *data, uint16_t len) {
    if (bcp == NULL) {
        k_log(BCP_LOG_ERROR, "bcp divide, bcp is NULL\n");
        return -1;
    }

    if (bcp->snd_status != BCP_DONE) {
        k_log(BCP_LOG_ERROR, "bcp divide, bcp->snd_status is not done, bcp->snd_status is %d\n", bcp->snd_status);
        return -2;
    }
        
    uint16_t count = 0;
    uint16_t max_payload = bcp->mfs - 8;

    count = (len + max_payload - 1)/max_payload;

    k_log(BCP_LOG_DEBUG, "len is %d, divide count is %d, max_payload is %d\n", len, count, max_payload);

    int32_t ret = 0;
    frame_type_t type;
    if (count == 1) {
        type = COMPLETE_FRAME;
        ret = trans_frame_enqueue(bcp, data, len, type, count);
    }
    else {
        uint16_t size = 0;
        type = START_FRAME;
        ret = trans_frame_enqueue(bcp, data, max_payload, type, count);
        if (ret != 0) {
            k_log(BCP_LOG_ERROR, "data divide error, enqueue error\n");
            return ret;
        }

        data += max_payload;
        size += max_payload;

        uint32_t i = 1;
        for (i = 1; i < count - 1; i++) {
            type = PROCESS_FRAME;
            ret = trans_frame_enqueue(bcp, data, max_payload, type, count);
            if (ret != 0) {
                k_log(BCP_LOG_ERROR, "data divide error, enqueue error\n");
                return ret;
            }
            data += max_payload;
            size += max_payload;
        } 

        type = END_FRAME;
        ret = trans_frame_enqueue(bcp, data, len - size, type, count);
        if (ret != 0) {
            k_log(BCP_LOG_ERROR, "data divide error, enqueue error\n");
            return ret;
        }
    }

    return ret;
}

int32_t bcp_send(int32_t bcp_id, void *data, uint32_t len) {
    bcp_t *bcp = get_bcp_by_id(bcp_id);
    if (bcp == NULL) {
        k_log(BCP_LOG_ERROR, "bcp send, get bcp falied, bcp_id is %d\n", bcp_id);
        return -1;
    }
    k_log(BCP_LOG_DEBUG, "bcp send, bcp_id is %d, data len is %d\n", bcp_id, len);
    
    return data_divide(bcp, data, len);
}


//---------------------------------------------------------------------
// retrans               
//---------------------------------------------------------------------

typedef struct {
    uint8_t bcp_id;
    uint8_t need_update_fsn;
} retrans_parm_t;

static void retrans_send_handler(bcp_t *bcp, void *context) {
    retrans_parm_t *parm = (retrans_parm_t *)context;

    k_log(BCP_LOG_INFO, "retrans_send_handler, bcp_id : %d, need_update_fsn : %d\n", parm->bcp_id, parm->need_update_fsn);

    queue_node *p, *next;
    if (parm->need_update_fsn == 1) {
        bcp->snd_next = 0; 

        for (p = bcp->snd_buf.next; p != &bcp->snd_buf; p = next) {
            frame_t *temp_frame = queue_entry(p, frame_t, node);
            next = p->next;
            temp_frame->frame_data[3] = bcp->snd_next++;
            frame_repack(bcp, temp_frame->frame_data, temp_frame->frame_len);
        }
    }

    for (p = bcp->snd_buf.next; p != &bcp->snd_buf; p = next) {
        frame_t *temp_frame = queue_entry(p, frame_t, node);
        next = p->next;

        if (bcp->snd_status != BCP_DONE) {
            break;
        }

        if (temp_frame->frame_status == FRAME_WAITING_TRANS) {

            // uint32_t cur_time = get_ms();
            // if (time_diff(cur_time, temp_frame->trans_time) >= 100) {
                k_log(BCP_LOG_DEBUG, "retrans send ------ frame len is %d, frame sn is %d\n", 
                temp_frame->frame_len, temp_frame->frame_data[3]);
                frame_send(bcp, temp_frame);
        
            // }
            // else {
            //     k_log(BCP_LOG_DEBUG, "retrans time too short ------ frame len is %d, frame sn is %d\n", 
            //     temp_frame->frame_len, temp_frame->frame_data[3]);
            // } 
        }
    }
}

static void retrans_send(uint8_t bcp_id, uint8_t need_update_fsn) {
    retrans_parm_t *retrans_parm = (retrans_parm_t *)bcp_malloc(sizeof(retrans_parm_t));
    if (retrans_parm == NULL) {
        k_log(BCP_LOG_ERROR, "retrans_event_enqueue, parm get mem failed\n");
        return;
    }

    retrans_parm->bcp_id = bcp_id;
    retrans_parm->need_update_fsn = need_update_fsn;

    bcp_event_post(bcp_id, RETRANS_EVENT, retrans_parm, retrans_send_handler, bcp_free);  
}


//---------------------------------------------------------------------
// ack/nack send             
//---------------------------------------------------------------------
typedef struct {
    uint8_t is_ack;                            
    uint8_t fsn;
    uint8_t una;                                
    uint8_t bcp_id; 
} ack_ctx_t;

static void ack_or_nack_pack(bcp_t *bcp, uint8_t *data, uint8_t is_ack, uint8_t fsn, uint8_t una) {
    uint8_t *p = data;

    uint8_t ctrl = 0xA0;
    if (is_ack == 1) {
        ctrl |= 0x18;
    } else {
        ctrl |= 0x04;
    }

    uint8_t len = 1;
        
    *p++ = 0xA5;
    *p++ = 0xA5;
    *p++ = ctrl;
    *p++ = fsn;                            
    *p++ = (uint8_t)len;
    *p++ = (uint8_t)(len >> 8);
    *p++ = una;

    uint16_t crc = bcp->crc16_cal(data, 6 + len);

    *p++ = crc;
    *p++ = crc >> 8;
}


static int32_t ack_or_nack_send(bcp_t *bcp, uint8_t is_ack, uint8_t fsn, uint8_t una) {

    if (is_ack == 0) {
        k_log(BCP_LOG_DEBUG, "nack send, is_ack : %d, fsn : %d, una : %d\n", 
        is_ack, fsn, una);
    }
    else {
        k_log(BCP_LOG_DEBUG, "ack send, is_ack : %d, fsn : %d, una : %d\n", 
        is_ack, fsn, una);
    }
    
    int32_t ret = 0;
    uint8_t len = 1;
    if (is_ack == 0) {
        uint8_t frame_data[8 + 1];
        ack_or_nack_pack(bcp, frame_data, 0, fsn, una);

        ret = bcp->output(bcp->bcp_id, frame_data, len + 8);
    }
    else {
        frame_t *ack_frame = (frame_t *)bcp_malloc(sizeof(frame_t) + 8 + len);
        if (ack_frame == NULL) {
            k_log(BCP_LOG_ERROR, "ack_or_nack_send, ack_frame get mem failed\n");
            return -1;
        }

        ack_or_nack_pack(bcp, ack_frame->frame_data, is_ack , fsn, una);
        ack_frame->frame_len = len + 8;
        ack_frame->bcp_id = bcp->bcp_id;

        queue_add_tail(&ack_frame->node, &bcp->ack_list);
    }

    return ret;
}


//---------------------------------------------------------------------
// low level receive               
//---------------------------------------------------------------------

static void uplayer_pick_notify(bcp_t *bcp) {

    static uint16_t app_data_len = 0;
    queue_node *p, *next;
    for (p = bcp->rcv_queue.next; p != &bcp->rcv_queue; p = next) {
        frame_t *frame = queue_entry(p, frame_t, node);
        next = p->next;
        queue_del(&frame->node);

        frame_type_t type = frame->frame_data[2] & 0x03;
        uint16_t len = frame->frame_data[5] << 8 | frame->frame_data[4];
        app_data_len = app_data_len > bcp->mal ? bcp->mal : app_data_len;
        len = len > (bcp->mal - app_data_len) ? bcp->mal - app_data_len : len;

        memcpy(bcp->app_buf, &frame->frame_data[6], len);
        app_data_len += len;

        bcp_free(frame);
        
        if (type == END_FRAME || type == COMPLETE_FRAME || app_data_len >= bcp->mal) {
            if (bcp->data_received) {
                bcp->data_received(bcp->bcp_id, bcp->app_buf, app_data_len);
            }
            app_data_len = 0;
            break;
        }
    }    
}

static int32_t frame_integrity_check(bcp_t *bcp, uint8_t *data, uint16_t len) {
    uint16_t crc = data[len-1] << 8 | data[len-2]; 
    if (bcp->crc16_cal(data, len - 2) != crc) {
        k_log(BCP_LOG_ERROR, "crc error, rcv frame crc is %x\n", crc);
        return -1;
    }

    return 0;
}

static int32_t ack_mode_frame_process(bcp_t *bcp, frame_t *frame) {
    uint8_t fsn = frame->frame_data[3];

    // The current frame number is less than rcv_next indicates that the frame has been received and processed, drop it.
    if (fsn_diff(fsn, bcp->rcv_next) >= 0) {

        queue_node *p;
        uint8_t flag = 0;
        for (p = bcp->rcv_buf.prev; p != &bcp->rcv_buf; p = p->prev) {
            frame_t *frame = queue_entry(p, frame_t, node);
            uint8_t frame_fsn = frame->frame_data[3];
            if (frame_fsn == fsn) {
                // The current frame is a duplicate frame, drop it.
                flag = 1;
                break;
            }

            if (fsn_diff(fsn, frame_fsn) > 0)
                break;
        }

        if (flag == 0) {
            k_log(BCP_LOG_DEBUG, "recv new frame\n");
            if (frame != NULL) {
                queue_add(&frame->node, p);
            }

            return 0;
        } else {
            return -1;
        }
    } 

    return -2;
}

static int32_t nack_mode_frame_process(bcp_t *bcp, frame_t *frame) {
    uint8_t fsn = frame->frame_data[3];

    // Nack mode, save only consecutive frames.
    if (fsn != bcp->rcv_next) {
        if (fsn_diff(fsn, bcp->rcv_next) > 0) {
            k_log(BCP_LOG_WARN, "recv_frame_handle, fsn != bcp.rcv_next, fsn : %d, una : %d\n", fsn, bcp->rcv_next);
            // The frames are discontinuous, and nack response needs to be triggered.
            ack_or_nack_send(bcp, 0, fsn, bcp->rcv_next);
        }
        else {
            k_log(BCP_LOG_WARN, "recv_frame_handle, fsn < bcp.rcv_next, free, fsn : %d, una : %d\n", fsn, bcp->rcv_next);  
        }
        
        return -1;
    }
    
    queue_add_tail(&frame->node, &bcp->rcv_buf);
    return 0;
}

static int32_t frame_pickup(bcp_t *bcp, uint8_t cur_fsn) {
    uint8_t app_rcv_flag = 0;
    int32_t ret = 0;

    // update rcv_next
    queue_node *p, *next;
    for (p = bcp->rcv_buf.next; p != &bcp->rcv_buf; p = next) {
        frame_t *frame = queue_entry(p, frame_t, node);
        next = p->next;
        uint8_t fsn = frame->frame_data[3];
        if (fsn_diff(fsn, bcp->rcv_next) > 0) {
            k_log(BCP_LOG_WARN, "frame discontinuity, break, frame_fsn is %d, rcv_next is %d \n", fsn, bcp->rcv_next);
            break;
        }
        else if (fsn == bcp->rcv_next) {
            bcp->rcv_next++;
            queue_del(&frame->node);
            queue_add_tail(&frame->node, &bcp->rcv_queue);

            k_log(BCP_LOG_DEBUG, "recv_frame_handle, add rcv_queue \n");

            frame_type_t type = frame->frame_data[2] & 0x03;
            // If it is the end frame, notify the application layer to pick.
            if (type == END_FRAME || type == COMPLETE_FRAME) {
                app_rcv_flag = 1;
                k_log(BCP_LOG_DEBUG, "recv end or complete frame, frame_fsn is %d\n", fsn); 
            }
        }   
    }

    if (bcp->need_ack == 1) {
        ret = ack_or_nack_send(bcp, 1, cur_fsn, bcp->rcv_next);
    }
    
    if (app_rcv_flag != 0) {
        uplayer_pick_notify(bcp);
    }

    return ret;
}


static void full_frame_process(bcp_t *bcp, void *context) {

    frame_t *frame = (frame_t *)context;

    uint8_t fsn = frame->frame_data[3];
    if (frame_integrity_check(bcp, frame->frame_data, frame->frame_len) != 0) {
        // Check error. The nack is added to the corresponding queue and 
        // the transmission response event is triggered.
        if (bcp->need_ack == 0) {
            // Send nack response. The frame number is the latest consecutive frame number received.
            ack_or_nack_send(bcp, 0, fsn, bcp->rcv_next);
        }

        bcp_free(frame);
        return;
    }

    int32_t ret = 0;
    if (bcp->need_ack == 1) {      
        ret = ack_mode_frame_process(bcp, frame);   
    }
    else {
        // Nack mode, save only consecutive frames.
        ret = nack_mode_frame_process(bcp, frame); 
    }

    if (ret != 0) {
        bcp_free(frame);
    }

    frame_pickup(bcp, fsn);
}

static int32_t full_frame_recv(bcp_t *bcp, frame_t *frame) {
    return bcp_event_post(bcp->bcp_id, APP_RX_EVENT, frame, full_frame_process, NULL) == true ? 0 : -1; 
}


static int32_t frame_seg_recv(bcp_t *bcp, uint8_t *data, uint16_t len) {

	uint8_t *p = bcp->expected_frame->frame_data;
    if (bcp->expected_frame->frame_offset + len <= bcp->expected_frame->frame_len) {    
        memcpy(p + bcp->expected_frame->frame_offset, data, len);
        bcp->expected_frame->frame_offset += len; 
    }
    else {
        uint16_t remaind = bcp->expected_frame->frame_len - bcp->expected_frame->frame_offset;
        memcpy(p + bcp->expected_frame->frame_offset, data, remaind);
        bcp->expected_frame->frame_offset += remaind; 
    }

    int32_t ret = 0;
    if (bcp->expected_frame->frame_offset >= bcp->expected_frame->frame_len) {
        k_log(BCP_LOG_DEBUG, "recv all slice, frame_len is %d\n", bcp->expected_frame->frame_len); 

        ret = full_frame_recv(bcp, bcp->expected_frame);
        bcp->expected_frame = NULL;
    }
	else {
        k_log(BCP_LOG_DEBUG, "recv seg, frame_offset is %d, frame_len is %d\n", 
        bcp->expected_frame->frame_offset, bcp->expected_frame->frame_len); 
    }

    return ret; 
}


static frame_t *expected_frame_create(uint32_t frame_len) {
    frame_t *frame = (frame_t *)bcp_malloc(sizeof(frame_t) + frame_len);
    if (frame == NULL) {
        k_log(BCP_LOG_ERROR, "blank frame get mem failed\n");
        return NULL;
    } 

    memset(frame, 0, sizeof(frame_t) + frame_len);
    frame->frame_len = frame_len;
    return frame;
}

static int32_t frame_first_seg_recv(bcp_t *bcp, uint8_t *data, uint32_t len) {

    if (len > bcp->mfs)
        len = bcp->mfs;

    uint16_t payload_len = data[5] << 8 | data[4]; 
    uint16_t frame_len = payload_len + 8;

    if (bcp->expected_frame != NULL) {
        bcp_free(bcp->expected_frame);
        bcp->expected_frame = NULL;
    }

    bcp->expected_frame = expected_frame_create(frame_len);
    if (bcp->expected_frame == NULL) {
        k_log(BCP_LOG_ERROR, "frame_first_seg_recv, expected_frame create failed\n");
        return -11;
    }

    return frame_seg_recv(bcp, data, len);  
}


static int32_t frame_other_seg_recv(bcp_t *bcp, uint8_t *data, uint16_t len) {
    if (bcp->expected_frame == NULL) {
        k_log(BCP_LOG_ERROR, "frame_other_seg_recv, expected_frame is null\n");
        return -10;
    }
    return frame_seg_recv(bcp, data, len);
}


//---------------------------------------------------------------------
// ack/nack receive              
//---------------------------------------------------------------------

static void retrans_part_handle(bcp_t *bcp, void *context) {
    ack_t *ack = (ack_t *)context;

    if (bcp->snd_status != BCP_DONE) {
        k_log(BCP_LOG_ERROR, "retrans_part_handle, bcp snd status is not done\n");
        return;
    }

    k_log(BCP_LOG_INFO, "recv ack/nack, is_ack is %d, fsn is %d, una is %d\n", ack->is_ack, 
    ack->fsn, ack->una); 

    queue_node *p, *next;
    p = bcp->snd_buf.next;

    if (p != &bcp->snd_buf) {
        frame_t *frame = queue_entry(p, frame_t, node);
        // next = p->next;
        uint8_t fsn = frame->frame_data[3];

        k_log(BCP_LOG_DEBUG, "retrans_part_handle, first fsn is %d\n", fsn);
        
        if (fsn_diff(fsn, ack->una) > 0) {
            k_log(BCP_LOG_INFO, "retrans_part_handle, sync frame req start\n");
            sync_frame_send(bcp, SYNC_REQ_CMD);
        }
        else {
            // All frame numbers greater than or equal to ack ->una are retransmitted
            for (; p != &bcp->snd_buf; p = next) {
                frame = queue_entry(p, frame_t, node);
                next = p->next;
                uint8_t fsn = frame->frame_data[3];

                if (fsn_diff(fsn, ack->una) >= 0) {
                    frame->frame_status = FRAME_WAITING_TRANS;
                }
                else {
                    queue_del(&frame->node);
                    bcp_free(frame);
                }
            }

            retrans_send(bcp->bcp_id, 0);
        }
    }
    else {
        k_log(BCP_LOG_INFO, "retrans_part_handle, sync frame req start\n");
        sync_frame_send(bcp, SYNC_REQ_CMD);
    }

}

static void retrans_select_handle(bcp_t *bcp, void *context) {
    ack_t *ack = (ack_t *)context;

    k_log(BCP_LOG_DEBUG, "recv ack, is_ack is %d, fsn is %d, una is %d\n", ack->is_ack, 
    ack->fsn, ack->una); 

    queue_node *p, *next;
    // Clear frames smaller than una from the snd_buf
    for (p = bcp->snd_buf.next; p != &bcp->snd_buf; p = next) {
        frame_t *frame = queue_entry(p, frame_t, node);   
        next = p->next;    
        uint8_t fsn = frame->frame_data[3];
        if (fsn_diff(fsn, ack->una) < 0 || fsn == ack->fsn) {
            k_log(BCP_LOG_INFO, "recv ack, del frame sn is %d, timeout is %d, ack->una is %d\n", fsn, frame->timeout_ms, ack->una); 
            queue_del(&frame->node);
            bcp_free(frame);   
        }

        if (fsn_diff(fsn, ack->fsn) < 0) {
            frame->lost_ack_count++;

            if (frame->lost_ack_count >= MAX_ACK_LOST_NUM || time_diff(get_ms(), frame->retrans_timeout) >= 0) {
                k_log(BCP_LOG_DEBUG, "add frame to retrans, frame sn is %d, timeout is %d\n", fsn, frame->timeout_ms); 
                frame->lost_ack_count = 0;
                frame->frame_status = FRAME_WAITING_TRANS;
            }
        }
        else {
            break;
        }  
    }
}

static void ack_or_nack_recv_handler(bcp_t *bcp, void *context) {
    ack_ctx_t *ack_ctx = (ack_ctx_t *)context;
    if (ack_ctx->is_ack == 1) {
        retrans_select_handle(bcp, ack_ctx);
    }
    else {
        retrans_part_handle(bcp, ack_ctx);
    }
}

static int32_t ack_or_nack_recv(bcp_t *bcp, uint8_t is_ack, uint8_t *data, uint16_t len) {

    ack_ctx_t *ack_ctx = (ack_ctx_t *)bcp_malloc(sizeof(ack_ctx_t));
    if (ack_ctx == NULL) {
        k_log(BCP_LOG_ERROR, "ack context get mem failed\n"); 
        return -1;
    }

    ack_ctx->is_ack = is_ack;
    ack_ctx->fsn = data[3];
    ack_ctx->una = data[6];

    return bcp_event_post(bcp->bcp_id, ACK_RX_EVENT, ack_ctx, ack_or_nack_recv_handler, bcp_free) == true ? 0 : -1; 
}


//---------------------------------------------------------------------
// sync frame receive & send              
//---------------------------------------------------------------------
typedef struct {
    uint8_t cmd;
    uint32_t id;
} sync_info_t;


static int32_t sync_id_gnerate(void) {
    int ret = 0;
    uint8_t *p = (uint8_t *)&ret;
    uint8_t len = sizeof(ret);

    while ( len-- ) {
        *p++ = rand() & 0xFF;
    }
        
    return ret;
}

static void sync_frame_pack(bcp_t *bcp, uint8_t *frame_data, uint8_t cmd) {
    uint8_t *p = frame_data;
    uint8_t *head = p;

    uint8_t ctrl = 0xA0;
    if (bcp->need_ack == 1) {
        ctrl |= 0x1C;
    } else {
        ctrl |= 0x0C;
    }
        
    *p++ = 0xA5;
    *p++ = 0xA5;
    *p++ = ctrl;
    *p++ = 0;                              
    *p++ = 0x05;          
    *p++ = 0;              
    *p++ = cmd;           
    int32_t id = sync_id_gnerate();
    *p++ = (uint8_t)(id);
    *p++ = (uint8_t)(id >> 8);
    *p++ = (uint8_t)(id >> 16);
    *p++ = (uint8_t)(id >> 24);
    
    uint32_t len = p - head;
    k_log(BCP_LOG_INFO, "sync frame pack, len is %d\n", len);
    uint16_t crc = bcp->crc16_cal(frame_data, len);

    *p++ = crc;
    *p++ = crc >> 8;
}

static void sync_frame_send(bcp_t *bcp, uint8_t cmd) {

    sync_frame_t *sync_frame = NULL;
    if (cmd == SYNC_REQ_CMD) {
        bcp->snd_status = BCP_HANDSHAKE;

        if (bcp->sync_frame != NULL) {
            if (bcp->output) {
                k_log(BCP_LOG_INFO, "sync_frame_send, bcp->bcp_id : %d, cmd : %d, sync_count : %d\n", 
                bcp->bcp_id, cmd, bcp->sync_frame->sync_count);
                bcp->sync_frame->sync_timeout = get_ms() + 2000;
                bcp->sync_frame->sync_count++;
                if (bcp->output(bcp->bcp_id, bcp->sync_frame->frame_data, bcp->sync_frame->frame_len) < 0) {
                    k_log(BCP_LOG_ERROR, "sync frame send failed\n");
                }
                
            }
            return;
        }
    }

    sync_frame = bcp_malloc(sizeof(sync_frame_t) + 13);
    if (sync_frame == NULL) {
        k_log(BCP_LOG_ERROR, "sync frame send, get mem failed\n");
        return;
    }
    memset(sync_frame, 0, sizeof(sync_frame_t) + 13);
    sync_frame_pack(bcp, sync_frame->frame_data, cmd);
    sync_frame->frame_len = 13;
    sync_frame->sync_timeout = get_ms() + 2000;
    sync_frame->sync_count++;

    k_log(BCP_LOG_INFO, "sync_frame_send, bcp->bcp_id : %d, cmd : %d, sync_count : %d, sync_timeout : %d\n", 
    bcp->bcp_id, cmd, sync_frame->sync_count, sync_frame->sync_timeout);

    if (bcp->output) {
        if (bcp->output(bcp->bcp_id, sync_frame->frame_data, sync_frame->frame_len) < 0) {
            k_log(BCP_LOG_ERROR, "sync frame send failed\n");
        }
    }

    if (cmd == SYNC_REQ_CMD) {
        bcp->sync_frame = sync_frame;
    } else {
        bcp_free(sync_frame);
        sync_frame = NULL;
    }

    k_log(BCP_LOG_INFO, "sync frame send ok\n");
}

static void sync_frame_rcv_req_handle(bcp_t *bcp, uint32_t sync_id) {

    if (bcp->last_sync_id == sync_id) {
        k_log(BCP_LOG_INFO, "receive duplicate sync req frame, sync_id is %d\n", sync_id);
        return;
    }

    bcp->last_sync_id = sync_id;

    while (!queue_is_empty(&bcp->rcv_queue)) {
        frame_t *frame = queue_entry(bcp->rcv_queue.next, frame_t, node);
        queue_del(&frame->node);
        bcp_free(frame);
    }

    while (!queue_is_empty(&bcp->rcv_buf)) {
        frame_t *frame = queue_entry(bcp->rcv_buf.next, frame_t, node);
        queue_del(&frame->node);
        bcp_free(frame);
    }

    if (bcp->expected_frame != NULL) {
        bcp_free(bcp->expected_frame);
        bcp->expected_frame = NULL;
    }

    bcp->rcv_next = 0;
    bcp->rcv_status = BCP_DONE;

    k_log(BCP_LOG_INFO, "receive sync req frame, process over\n");
}


static void sync_frame_rcv_rsp_handle(bcp_t *bcp, uint32_t sync_id) {

    if (bcp->sync_frame == NULL) {
        k_log(BCP_LOG_INFO, "receive duplicate sync rsp frame\n");
        return;
    }

    if (bcp->sync_frame != NULL) {
        bcp_free(bcp->sync_frame);
        bcp->sync_frame = NULL;
    }

    while (!queue_is_empty(&bcp->snd_buf)) {
        frame_t *frame = queue_entry(bcp->snd_buf.next, frame_t, node);
        k_log(BCP_LOG_DEBUG, "bcp clean snd buf, frame is %d\r\n", frame->frame_data[3]);
        queue_del(&frame->node);
        bcp_free(frame);  
    }

    bcp->snd_next = 0;
    bcp->snd_status = BCP_DONE;

    k_log(BCP_LOG_INFO, "receive sync rsp frame, process over\n");
}


static void sync_frame_recv_handler(bcp_t *bcp, void *context) {

    sync_info_t *sync_info = (sync_info_t *)context;

    k_log(BCP_LOG_INFO, "sync_frame_handler, bcp_id : %d, cmd : %d\n", bcp->bcp_id, sync_info->cmd);
    
    if (sync_info->cmd == SYNC_REQ_CMD) {
        sync_frame_rcv_req_handle(bcp, sync_info->id);

        sync_info->cmd = SYNC_RSP_CMD;
        sync_frame_send(bcp, sync_info->cmd);
    }      
    else if (sync_info->cmd == SYNC_RSP_CMD) {
        sync_frame_rcv_rsp_handle(bcp, sync_info->id);
    }
}

static int32_t sync_frame_recv(bcp_t *bcp, uint8_t *data, uint16_t len) {
 
    if (len < 13) {
        k_log(BCP_LOG_ERROR, "sync frame recv, len is error, len : %d\n", len);
        return -1;
    }

    sync_info_t *sync_info = (sync_info_t *)bcp_malloc(sizeof(sync_info_t));
    if (sync_info == NULL) {
        k_log(BCP_LOG_ERROR, "sync frame recv, get mem failed, bcp_id : %d, cmd : %d\n", bcp->bcp_id, data[6]);
        return -2;
    }
    sync_info->cmd = data[6];
    sync_info->id = data[7] | (data[8] << 8) | (data[9] << 16) | (data[10] << 24);

    bool ret = bcp_event_post(bcp->bcp_id, SYNC_RX_EVENT, sync_info, sync_frame_recv_handler, bcp_free);
    if (ret != true) {
        k_log(BCP_LOG_ERROR, "sync frame recv, post failed, bcp_id : %d, cmd : %d\n", bcp->bcp_id, sync_info->cmd);
    }
    k_log(BCP_LOG_DEBUG, "sync frame recv, post ret is %d, bcp_id : %d, cmd : %d\n", ret, bcp->bcp_id, sync_info->cmd);

    return ret == true ? 0 : -3;
}

//---------------------------------------------------------------------
// low level receive               
//---------------------------------------------------------------------

static int32_t frame_verify(bcp_t *bcp, uint8_t *data, uint32_t len) {

    int32_t ret = 0;
    if (len >= 8) {

        uint16_t head = data[1] << 8 | data[0]; 
        uint8_t ctrl = data[2];

        if (head == BCP_HEAD && (ctrl & 0xE0) == 0xA0) {
            
            uint8_t data_type = ctrl & 0x0C;
            switch (data_type) {
                // data package
                case 0x00 : {
                    if (bcp->rcv_status != BCP_DONE) {
                        k_log(BCP_LOG_ERROR, "wait sync, drop package, bcp_id is %d\n", bcp->bcp_id);
                        return -3;
                    }
                    k_log(BCP_LOG_TRACE, "recv first seg, len is %d\n", len);
                    ret = frame_first_seg_recv(bcp, data, len);
                }
                break;

                // nack package
                case 0x04 : {
                    k_log(BCP_LOG_TRACE, "recv nack, bcp_id is %d\n", bcp->bcp_id);
                    ret = ack_or_nack_recv(bcp, 0, data, len);
                }
                break;

                // ack package
                case 0x08 : {
                    k_log(BCP_LOG_TRACE, "recv ack, bcp_id is %d\n", bcp->bcp_id);
                    ret = ack_or_nack_recv(bcp, 1, data, len);
                }
                break;

                // sync package
                case 0x0C : {
                    k_log(BCP_LOG_TRACE, "recv sync package, bcp_id is %d \n", bcp->bcp_id);
                    ret = sync_frame_recv(bcp, data, len);
                }
                break;

                default:
                break;
            }

            return ret;
        }
    }

    if (bcp->rcv_status != BCP_DONE) {
        k_log(BCP_LOG_ERROR, "wait sync, drop package, bcp_id is %d\n", bcp->bcp_id);
        return 0;
    }

    k_log(BCP_LOG_TRACE, "recv other seg, len is %d\n", len);
    ret = frame_other_seg_recv(bcp, data, len);

    return ret;
}



int32_t bcp_input(int32_t bcp_id, void *data, uint32_t len) {
    bcp_t *bcp = get_bcp_by_id(bcp_id);
    if (bcp == NULL) {
        k_log(BCP_LOG_ERROR, "bcp input, get bcp falied, bcp_id is %d\n", bcp_id);
        return -1;
    }
    
    if (bcp->rcv_status == BCP_STOP) {
        k_log(BCP_LOG_ERROR, "bcp input, bcp status is stop, bcp_id is %d\n", bcp_id); 
        return -2;
    }
    k_log(BCP_LOG_TRACE, "bcp input, bcp_id is %d, len is %d\n", bcp_id, len); 

    int32_t ret = 0;
    if (bcp->need_ack == 0) {
        ret = frame_verify(bcp, (uint8_t *)data, len);
    }
    else {
        // ack mode
        uint8_t *p = (uint8_t *)data;
        uint8_t *head = p;
        uint8_t *tail = head;
        uint8_t *end = p + len - 1;

        uint16_t size = 0;
        if (*p++ == 0xA5 && *p++ == 0xA5 && (*p & 0xE0) == 0xA0) {
            
            size = head[5] << 8 | head[4];
            tail =  head + size + 8 - 1;

            k_log(BCP_LOG_TRACE, "bcp input, size is %d, len is %d\n", size, len); 

            while (tail < end) {
                k_log(BCP_LOG_TRACE, "bcp input, tail < end, verify len is %d\n", size + 8); 
                ret = frame_verify(bcp, head, size + 8);
                if (ret != 0) {
                    k_log(BCP_LOG_ERROR, "bcp input, frame verify error, bcp_id is %d, len is %d, ret is %d\n", 
                    bcp_id, size + 8, ret); 
                    break;
                }

                head += size + 8;
                size = head[5] << 8 | head[4]; 
                tail =  head + size + 8 -1;
            }

            if (tail == end) {
                k_log(BCP_LOG_TRACE, "bcp input, tail == end, verify len is %d\n", size + 8); 
                ret = frame_verify(bcp, head, size + 8);
            } else if (tail > end) {
                k_log(BCP_LOG_TRACE, "bcp input, tail > end, verify len is %d\n", len); 
                ret = frame_verify(bcp, (uint8_t *)data, len);
            }
        }
        else {
            ret = frame_verify(bcp, (uint8_t *)data, len);
        }
    }

    return ret;
}


//---------------------------------------------------------------------
// period check               
//---------------------------------------------------------------------

static void ack_mode_data_send(bcp_t *bcp, frame_t *frame) {

    if (bcp->ack_snd_buf_offset >= bcp->mtu) {
        bcp->output(bcp->bcp_id, bcp->ack_snd_buf, bcp->mtu); 
        bcp->ack_snd_buf_offset = 0;
    }
    
    uint8_t *p = &bcp->ack_snd_buf[bcp->ack_snd_buf_offset];
    uint32_t count = (frame->frame_len + bcp->mtu - 1)/bcp->mtu;

    k_log(BCP_LOG_DEBUG, "ack_mode_data_send, frame len is %d, frame sn is %d, bcp_id is %d, seg count is %d\n", 
    frame->frame_len, frame->frame_data[3], bcp->bcp_id, count);

    frame->trans_time = get_ms();

    uint8_t *data = frame->frame_data;
    uint16_t frame_len = frame->frame_len;
    while(count > 0) {

        uint16_t len = frame_len > bcp->mtu ? bcp->mtu : frame_len;

        if ((bcp->ack_snd_buf_offset + len) > bcp->mtu) {
            bcp->output(bcp->bcp_id, bcp->ack_snd_buf, bcp->ack_snd_buf_offset); 
            bcp->ack_snd_buf_offset = 0;
            p = bcp->ack_snd_buf;
        }
        memcpy(p, data, len);
        p += len;
        bcp->ack_snd_buf_offset += len;
    
        data += len;
        frame_len -= len;

        count--;
    }

    frame->frame_status = FRAME_WAITING_ACK;
    frame->trans_count++;

    frame->timeout_ms = get_ms() + frame->rto;
    frame->retrans_timeout = get_ms() + frame->rto;
}

static void bcp_snd_buf_check(bcp_t *bcp, uint32_t cur_ms) {

    queue_node *p, *next;
    if (bcp->need_ack == 0) {
        // nack mode
        for (p = bcp->snd_buf.next; p != &bcp->snd_buf; p = next) {
            frame_t *frame = queue_entry(p, frame_t, node);  
            next = p->next;

            if (frame->frame_status == FRAME_WAITING_ACK) {
                if (time_diff(cur_ms, frame->timeout_ms) >= 0) {  
                    queue_del(&frame->node);
                    bcp_free(frame); 
                    k_log(BCP_LOG_TRACE, "nack mode, delete frame : %d \n", frame->frame_data[3]);  
                }
            }  
        }
    }
    else {
        // ack mode

        // first send ack
        for (p = bcp->ack_list.next; p != &bcp->ack_list; p = next) {
            frame_t *frame = queue_entry(p, frame_t, node);  
            next = p->next;

            queue_del(&frame->node);
            ack_mode_data_send(bcp, frame);
            bcp_free(frame);
        }

        for (p = bcp->snd_buf.next; p != &bcp->snd_buf; p = next) {
            frame_t *frame = queue_entry(p, frame_t, node);  
            next = p->next;

            if (frame->frame_status == FRAME_WAITING_ACK) {
                if (time_diff(cur_ms, frame->timeout_ms) >= 0) {  
                    ack_mode_data_send(bcp, frame);
                }
            } 
            else {
                ack_mode_data_send(bcp, frame);
            }
        }

        if (bcp->ack_snd_buf_offset > 0) {
            bcp->output(bcp->bcp_id, bcp->ack_snd_buf, bcp->ack_snd_buf_offset); 
            bcp->ack_snd_buf_offset = 0;
        }
    }
}

static void bcp_check_handler(bcp_t *bcp, void *context)
{
    if (bcp == NULL) {
        k_log(BCP_LOG_INFO, "check, bcp is null\n");
        return;
    }

    if (bcp->snd_status == BCP_STOP) {
        k_log(BCP_LOG_INFO, "check, snd_status is not done\n");
        return;
    }

    uint32_t cur_ms = get_ms();
    if (bcp->snd_status == BCP_HANDSHAKE) {
        if (bcp->sync_frame != NULL) {
            if (time_diff(cur_ms, bcp->sync_frame->sync_timeout) >= 0) {
                k_log(BCP_LOG_INFO, "check, retrans sync frame\n");
                sync_frame_send(bcp, SYNC_REQ_CMD);
            }
        } else {
            k_log(BCP_LOG_INFO, "check, retrans sync frame\n");
            sync_frame_send(bcp, SYNC_REQ_CMD);
        }
        
        return;
    }

    bcp_snd_buf_check(bcp, cur_ms);
}

void bcp_check(int32_t bcp_id) {   

    bcp_t *bcp = get_bcp_by_id(bcp_id);
    if (bcp == NULL) {
        return;
    }

    if (bcp_event_post(bcp_id, TIMER_CHECK_EVENT, NULL, bcp_check_handler, NULL) != true) {
        k_log(BCP_LOG_INFO, "check, enqueue error\n");
    } 
}

