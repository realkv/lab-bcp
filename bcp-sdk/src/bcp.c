#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "bcp.h"


//---------------------------------------------------------------------
// queue node definition                                                         
//---------------------------------------------------------------------
typedef struct node_head {
	struct node_head *next, *prev;
} queue_node_t;

//---------------------------------------------------------------------
// queue operations                                                         
//---------------------------------------------------------------------
#define QUEUE_HEAD_INIT(name) { &(name), &(name) }
#define QUEUE_HEAD(name) \
	struct IQUEUEHEAD name = QUEUE_HEAD_INIT(name)

#define QUEUE_INIT(ptr) ( \
	(ptr)->next = (ptr), (ptr)->prev = (ptr))

#define OFFSETOF(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define CONTAINEROF(ptr, type, member) ( \
		(type*)( ((char*)((type*)ptr)) - OFFSETOF(type, member)) )

#define QUEUE_ENTRY(ptr, type, member) CONTAINEROF(ptr, type, member)

#define QUEUE_ADD(node, head) ( \
	(node)->prev = (head), (node)->next = (head)->next, \
	(head)->next->prev = (node), (head)->next = (node))

#define QUEUE_ADD_TAIL(node, head) ( \
	(node)->prev = (head)->prev, (node)->next = (head), \
	(head)->prev->next = (node), (head)->prev = (node))

#define QUEUE_DEL_BETWEEN(p, n) ((n)->prev = (p), (p)->next = (n))

#define QUEUE_DEL(entry) (\
	(entry)->next->prev = (entry)->prev, \
	(entry)->prev->next = (entry)->next, \
	(entry)->next = 0, (entry)->prev = 0)

#define QUEUE_DEL_INIT(entry) do { \
	QUEUE_DEL(entry); QUEUE_INIT(entry); } while (0)

#define QUEUE_IS_EMPTY(entry) ((entry) == (entry)->next)

#define queue_init		QUEUE_INIT
#define queue_entry	    QUEUE_ENTRY
#define queue_add		QUEUE_ADD
#define queue_add_tail	QUEUE_ADD_TAIL
#define queue_del		QUEUE_DEL
#define queue_del_init	QUEUE_DEL_INIT
#define queue_is_empty  QUEUE_IS_EMPTY

#define LIST_FOR_EACH_SAFE(item, next_item, list) \
    for ((item) = (list)->next, (next_item) = (item)->next; (item) != (list); \
            (item) = (next_item), (next_item) = (item)->next)

#define LIST_FOR_EACH_ENTRY(item, list, type, member) \
    for ((item) = queue_entry((list)->next, type, member); \
            &(item)->member != (list); \
            (item) = queue_entry((item)->member.next, type, member))

#define LIST_FOR_EACH_ENTRY_SAFE(item, next_item, list, type, member) \
    for ((item) = queue_entry((list)->next, type, member), \
            (next_item) = queue_entry((item)->member.next, type, member); \
            &((item)->member) != (list); \
            (item) = (next_item), (next_item) = queue_entry((item)->member.next, type, member))

//---------------------------------------------------------------------
// bcp structure             
//---------------------------------------------------------------------
#define BCP_MAGIC_HEAD                  0xBFCD

#define BCP_FRAME_DATA_COMPLETE         0x10
#define BCP_FRAME_DATA_START            0x11
#define BCP_FRAME_DATA_MIDDLE           0x12
#define BCP_FRAME_DATA_END              0x13
#define BCP_FRAME_DATA_ACK              0x14
#define BCP_FRAME_DATA_NACK             0x15
#define BCP_FRAME_SYNC_REQ              0x18
#define BCP_FRAME_SYNC_ACK              0x1C

typedef struct s_node_head {
	struct s_node_head *next;
} s_node_t;

typedef struct mem_pool {                        
    uint16_t block_size;
    uint16_t block_num;  
    uint8_t *head;
    s_node_t pool_list;      
} mem_pool_t;

typedef struct {                        
    s_node_t block_node;       
    mem_pool_t *mem_pool;     
    uint8_t *data;             
} mem_block_t;

typedef struct {
    queue_node_t node;                                                       
    uint16_t frame_len;
    uint16_t fsn;
    uint8_t frame_data[1];                     
} frame_t;

typedef struct {                                           
    uint16_t data_len;
    uint8_t data[1];                     
} mtu_t;

typedef enum {
    BCP_STOP = 0,
    BCP_HANDSHAKE,
    BCP_DONE,
} bcp_work_status_t;

struct _bcp_t {                      
    uint16_t mtu;                                
    uint16_t mfs;                               
	uint32_t mal;	
    
    uint8_t *mal_buf;
    uint8_t *mfs_buf;
    mem_pool_t frame_mem_pool; 
    mem_pool_t mtu_mem_pool;
    mem_pool_t snd_list_pool;
    queue_node_t ack_list;
  
    uint8_t snd_next;                              
    uint8_t rcv_next;   
    
    uint8_t exit_cmd;
    uint8_t exit_flag;

    uint8_t status;
    uint8_t recv_frame_flag;
    uint16_t recv_frame_offset;
    uint16_t recv_frame_len;
    uint16_t recv_app_data_offset;

    uint32_t sync_timeout_ms;

    void *queue;
    void *work_thread;
    void *timer;
    void *critical_section;
    void *owner;

    int32_t (*output)(const bcp_block_t *bcp_block, void *data, uint32_t len);
    void (*data_listener)(const bcp_block_t *bcp_block, void *data, uint32_t len);
    void (*opened_listener)(const bcp_block_t *bcp_block, bcp_open_status_t status);
};

typedef struct {
    uint16_t magic_head;
    uint8_t ctrl;
    uint8_t fsn;
    uint16_t len;
} bcp_frame_head_t;

typedef struct {
    uint32_t size;
    void *context;
    void (*event_handler)(bcp_t *bcp, const void *context);
} bcp_context_t;

static bcp_adapter_port_t bcp_adapter;

void bcp_adapter_port_init(const bcp_adapter_port_t *bcp_adapter_port)
{
    bcp_adapter.bcp_crc.crc16_cal = bcp_adapter_port->bcp_crc.crc16_cal;

    bcp_adapter.bcp_critical.critical_section_create = bcp_adapter_port->bcp_critical.critical_section_create;
    bcp_adapter.bcp_critical.critical_section_destory = bcp_adapter_port->bcp_critical.critical_section_destory;
    bcp_adapter.bcp_critical.enter_critical_section = bcp_adapter_port->bcp_critical.enter_critical_section;
    bcp_adapter.bcp_critical.leave_critical_section = bcp_adapter_port->bcp_critical.leave_critical_section;

    bcp_adapter.bcp_mem.bcp_malloc = bcp_adapter_port->bcp_mem.bcp_malloc;
    bcp_adapter.bcp_mem.bcp_free = bcp_adapter_port->bcp_mem.bcp_free;

    bcp_adapter.bcp_queue.queue_create = bcp_adapter_port->bcp_queue.queue_create;
    bcp_adapter.bcp_queue.queue_destory = bcp_adapter_port->bcp_queue.queue_destory;
    bcp_adapter.bcp_queue.queue_recv = bcp_adapter_port->bcp_queue.queue_recv;
    bcp_adapter.bcp_queue.queue_send = bcp_adapter_port->bcp_queue.queue_send;
    bcp_adapter.bcp_queue.queue_send_prior = bcp_adapter_port->bcp_queue.queue_send_prior;

    bcp_adapter.bcp_thread.thread_create = bcp_adapter_port->bcp_thread.thread_create;
    bcp_adapter.bcp_thread.thread_destory = bcp_adapter_port->bcp_thread.thread_destory;
    bcp_adapter.bcp_thread.thread_exit = bcp_adapter_port->bcp_thread.thread_exit;

    bcp_adapter.bcp_time.delay_ms = bcp_adapter_port->bcp_time.delay_ms;
    bcp_adapter.bcp_time.get_ms = bcp_adapter_port->bcp_time.get_ms;

    bcp_adapter.bcp_timer.timer_create = bcp_adapter_port->bcp_timer.timer_create;
    bcp_adapter.bcp_timer.timer_destory = bcp_adapter_port->bcp_timer.timer_destory;
    bcp_adapter.bcp_timer.timer_start = bcp_adapter_port->bcp_timer.timer_start;
    bcp_adapter.bcp_timer.timer_stop = bcp_adapter_port->bcp_timer.timer_stop;
}


static int8_t fsn_diff(uint8_t later, uint8_t earlier) {
    return ((int8_t)(later - earlier));
}

int32_t mem_pool_init(mem_pool_t *mem_pool, uint32_t block_size, uint32_t block_num)
{
    mem_pool->block_size = block_size;
    mem_pool->block_num = block_num;
    mem_pool->pool_list.next = NULL;
    mem_pool->head = NULL;

    uint32_t total_size = (sizeof(mem_block_t) + block_size) * block_num;
    mem_pool->head = (uint8_t *)bcp_adapter.bcp_mem.bcp_malloc(total_size);
    if (mem_pool->head == NULL) {
        return -1;
    }
   
    uint8_t *ptr = mem_pool->head;
    memset(ptr, 0, total_size);
    mem_block_t *mem_block;
    for (uint32_t i = 0; i < block_num; i++) {
        mem_block = (mem_block_t *)ptr;
        mem_block->block_node.next = mem_pool->pool_list.next;
        mem_pool->pool_list.next = &mem_block->block_node;
        mem_block->mem_pool = mem_pool;
        mem_block->data = ptr + sizeof(mem_block_t);
        ptr += block_size + sizeof(mem_block_t);
    }

    return 0;
}


int32_t mem_pool_deinit(mem_pool_t *mem_pool)
{
    bcp_adapter.bcp_mem.bcp_free(mem_pool->head);  
    mem_pool->head = NULL;
    mem_pool->block_size = 0;
    mem_pool->block_num = 0;
    mem_pool->pool_list.next = NULL;

    return 0;
}

void *mem_get_from_pool(bcp_t *bcp, mem_pool_t *mem_pool)
{
    void *ptr = NULL;

    if (bcp == NULL || mem_pool == NULL) {
        return ptr;
    }

    bcp_adapter.bcp_critical.enter_critical_section(&bcp->critical_section);

    if (mem_pool->pool_list.next != NULL) {
        mem_block_t *block = queue_entry(mem_pool->pool_list.next, mem_block_t, block_node);
        mem_pool->pool_list.next = block->block_node.next;
        block->block_node.next = NULL;
        ptr = block->data;
    }

    bcp_adapter.bcp_critical.leave_critical_section(&bcp->critical_section);

    return ptr;
}

void mem_free_to_pool(bcp_t *bcp, void *mem)
{
    bcp_adapter.bcp_critical.enter_critical_section(&bcp->critical_section);

    mem_block_t *block = (mem_block_t *)((uint8_t *)mem - sizeof(mem_block_t));
    mem_pool_t *mem_pool = block->mem_pool;
    block->block_node.next = mem_pool->pool_list.next;
    mem_pool->pool_list.next = &block->block_node;

    bcp_adapter.bcp_critical.leave_critical_section(&bcp->critical_section);
}

//---------------------------------------------------------------------
// log            
//---------------------------------------------------------------------
static void (*k_log_hook)(bcp_log_level_t, const char *) = NULL;
static bcp_log_level_t bcp_log_level = BCP_LOG_WARN;

void bcp_log_output_register(void (*log_output)(bcp_log_level_t level, const char *message)) 
{
    k_log_hook = log_output;
}

void bcp_log_level_set(bcp_log_level_t level) 
{
    bcp_log_level = level;
}

static void k_log(bcp_log_level_t level, const char *format, ...) 
{

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

static int32_t bcp_event_post(bcp_t *bcp,
                            void *context,
                            void (*context_handler)(bcp_t *bcp, const void *context)) 
{

    bcp_context_t bcp_context;
    bcp_context.context = context;
    bcp_context.event_handler = context_handler;

    return bcp_adapter.bcp_queue.queue_send(&bcp->queue, &bcp_context, sizeof(bcp_context), 0);  
}


static int32_t bcp_event_post_prior(bcp_t *bcp,
                            void *context,
                            void (*context_handler)(bcp_t *bcp, const void *context)) 
{
    bcp_context_t bcp_context;
    bcp_context.context = context;
    bcp_context.event_handler = context_handler;

    return bcp_adapter.bcp_queue.queue_send_prior(&bcp->queue, &bcp_context, sizeof(bcp_context), 0);  
}


static void bcp_thread_handler(void *arg)
{
    bcp_t *bcp = (bcp_t *)arg;

    while (1) {
        bcp_context_t bcp_context;
        if (bcp_adapter.bcp_queue.queue_recv(&bcp->queue, &bcp_context, sizeof(bcp_context_t), 0xffff) == 0) {
            if (bcp_context.event_handler) {
                bcp_context.event_handler(bcp, bcp_context.context);
            } 
        }

        if (bcp->exit_cmd != 0) {
            break;
        }
    }  

    bcp->exit_flag = 1;
    bcp_adapter.bcp_thread.thread_exit(&bcp->work_thread);
}



static void sync_frame_timeout_handle(bcp_t *bcp, const void *context)
{
    if (bcp->opened_listener) {
        bcp_block_t *bcp_block = (bcp_block_t *)bcp->owner;
        bcp->opened_listener(bcp_block, BCP_OPEND_ERROR_RSP_TIMEOUT);
    }
}

static void bcp_timer_tomeout_handler(void *arg)
{
    bcp_t *bcp = (bcp_t *)arg;
    bcp_adapter.bcp_timer.timer_stop(&bcp->timer);
    bcp_event_post_prior(bcp, NULL, sync_frame_timeout_handle);  
}

static void sync_frame_send_handle(bcp_t *bcp, const void *context)
{
    frame_t *sync_frame = (frame_t *)mem_get_from_pool(bcp, &bcp->mtu_mem_pool);
    if (sync_frame == NULL) {
        k_log(BCP_LOG_ERROR, "bcp sync send, sync mem get failed\n");

        if (bcp->opened_listener) {
            bcp_block_t *bcp_block = (bcp_block_t *)bcp->owner;
            bcp->opened_listener(bcp_block, BCP_OPEND_ERROR_MEM_FAIL);
        }
        return;
    }

    k_log(BCP_LOG_DEBUG, "bcp sync send, sync mem get ok\n");

    uint8_t *ptr = sync_frame->frame_data;
    bcp_frame_head_t frame_head;
    frame_head.magic_head = BCP_MAGIC_HEAD;
    frame_head.ctrl = BCP_FRAME_SYNC_REQ;
    frame_head.fsn = bcp->snd_next++;
    frame_head.len = sizeof(bcp->mfs);
    
    memcpy(ptr, &frame_head, sizeof(frame_head));
    ptr += sizeof(frame_head);
    memcpy(ptr, &bcp->mfs, sizeof(bcp->mfs));
    ptr += sizeof(bcp->mfs);

    uint16_t crc = bcp_adapter.bcp_crc.crc16_cal(sync_frame->frame_data, ptr - sync_frame->frame_data);
    *ptr++ = (uint8_t)crc;
    *ptr++ = (uint8_t)(crc >> 8);

    k_log(BCP_LOG_DEBUG, "bcp sync send, crc : %04x\n", crc);

    sync_frame->frame_len = ptr - sync_frame->frame_data;
    queue_init(&sync_frame->node);

    bcp_block_t *bcp_block = (bcp_block_t *)bcp->owner;
    if (bcp->output(bcp_block, sync_frame->frame_data, sync_frame->frame_len) != 0) {
        mem_free_to_pool(bcp, sync_frame);
        k_log(BCP_LOG_ERROR, "bcp sync send, send failed\n");
        if (bcp->opened_listener) {
            bcp->opened_listener(bcp_block, BCP_OPEND_ERROR_SEND_FAIL);
        }
        return;
    }
    
    queue_add_tail(&sync_frame->node, &bcp->ack_list);
    bcp->status = BCP_HANDSHAKE;
    bcp_adapter.bcp_timer.timer_start(&bcp->timer, bcp->sync_timeout_ms);
}

static void data_frame_pack(frame_t *frame, uint8_t *payload, uint32_t payload_len, uint32_t frame_type)
{
    k_log(BCP_LOG_DEBUG, "data_frame_pack, payload_len is %d, frame_type is %d\n", payload_len, frame_type);
    frame->frame_len = payload_len + 8;

    uint8_t *ptr = frame->frame_data;
    bcp_frame_head_t frame_head;
    frame_head.magic_head = BCP_MAGIC_HEAD;
    frame_head.ctrl = frame_type;
    frame_head.len = payload_len;
    
    memcpy(ptr, &frame_head, sizeof(frame_head));
    ptr += sizeof(frame_head);
    memcpy(ptr, payload, payload_len);
    ptr += payload_len;

    // temporary placeholder field
    ptr += 2;
}

static void data_frame_repack(bcp_t *bcp, frame_t *frame)
{
    uint8_t *ptr = frame->frame_data;
    ptr += 3;
    frame->fsn = bcp->snd_next++;
    *ptr = frame->fsn;

    uint16_t crc = bcp_adapter.bcp_crc.crc16_cal(frame->frame_data, frame->frame_len - 2);
    ptr = frame->frame_data;
    ptr[frame->frame_len - 2] = crc;
    ptr[frame->frame_len - 1] = crc >> 8;
}

static void data_frame_output(const bcp_t *bcp, const frame_t *frame)
{
    uint32_t count = (frame->frame_len + bcp->mtu - 1)/bcp->mtu;

    k_log(BCP_LOG_DEBUG, "data_frame_output, frame len is %d, frame sn is %d, slice count is %d\n", 
    frame->frame_len, frame->frame_data[3], count);

    uint8_t *data = (uint8_t *)frame->frame_data;
    uint16_t frame_len = frame->frame_len;
    bcp_block_t *bcp_block = (bcp_block_t *)bcp->owner;
    while(count > 0) {
        uint16_t len = frame_len > bcp->mtu ? bcp->mtu : frame_len;
        k_log(BCP_LOG_DEBUG, "bcp output, fsn is %d, len is %d, count is %d\n", frame->frame_data[3], len, count);
        if (bcp->output(bcp_block, data, len) != 0) {
            k_log(BCP_LOG_ERROR, "bcp output, output fail, frame_len : %d, fsn : %d\n", frame->frame_len, frame->fsn);
        }
        data += len;
        frame_len -= len;
        count--;
    }
}

static void bcp_send_handle(bcp_t *bcp, const void *context) 
{
    queue_node_t *snd_list = (queue_node_t *)context;

    frame_t *frame = NULL, *next_frame = NULL;
    LIST_FOR_EACH_ENTRY_SAFE(frame, next_frame, snd_list, frame_t, node) {
        queue_del(&frame->node);
        data_frame_repack(bcp, frame);
        data_frame_output(bcp, frame);

        queue_add_tail(&frame->node, &bcp->ack_list);
    }

    mem_free_to_pool(bcp, snd_list);
}

static void bcp_ack_nack_send(bcp_t *bcp, uint8_t frame_type, uint8_t ack_fsn)
{
    k_log(BCP_LOG_DEBUG, "bcp_ack_nack_send, frame_type : %d, ack_fsn : %d\n", frame_type, ack_fsn);
    uint8_t ack_frame[9];

    bcp_frame_head_t frame_head;
    frame_head.magic_head = BCP_MAGIC_HEAD;
    frame_head.ctrl = frame_type;
    frame_head.fsn = bcp->snd_next;  
    frame_head.len = 1;
    
    memcpy(ack_frame, &frame_head, sizeof(frame_head));
    ack_frame[6] = ack_fsn;

    uint16_t crc = bcp_adapter.bcp_crc.crc16_cal(ack_frame, 7);
    ack_frame[7] = crc;
    ack_frame[8] = crc >> 8;

    bcp_block_t *bcp_block = (bcp_block_t *)bcp->owner;
    if (bcp->output(bcp_block, ack_frame, 9) != 0) {
        k_log(BCP_LOG_ERROR, "bcp_ack_nack_send, output fail, ack_fsn : %d, fsn : %d\n", ack_fsn, frame_head.fsn);
    }

    k_log(BCP_LOG_DEBUG, "bcp_ack_nack_send, ack_fsn is %d, frame_type is %d\n", ack_fsn, frame_type);
}

static void app_data_notify(bcp_t *bcp, uint8_t *data, uint32_t len)
{
    bcp_ack_nack_send(bcp, BCP_FRAME_DATA_ACK, bcp->rcv_next);
    bcp->rcv_next++;

    if (bcp->mal_buf == NULL) {
        k_log(BCP_LOG_ERROR, "app_data_notify, mal_buf is empty\n");
        return;
    }

    uint16_t frame_payload_len = len - 8;
    if ((bcp->recv_app_data_offset + frame_payload_len) > bcp->mal) {
        k_log(BCP_LOG_ERROR, "app_data_notify, app data len is too long, len : %d\n", bcp->recv_app_data_offset + frame_payload_len);
        return;
    }

    memcpy(bcp->mal_buf + bcp->recv_app_data_offset, &data[6], frame_payload_len);
    bcp->recv_app_data_offset += frame_payload_len;
    uint8_t frame_type = data[2];
    k_log(BCP_LOG_DEBUG, "app_data_notify, frame_type : %d, frame_payload_len : %d\n", frame_type, frame_payload_len);
    if (frame_type == BCP_FRAME_DATA_COMPLETE || 
        frame_type == BCP_FRAME_DATA_END ) {
        
        bcp_block_t *bcp_block = (bcp_block_t *)bcp->owner;
        if (bcp->data_listener) {
            bcp->data_listener(bcp_block, bcp->mal_buf, bcp->recv_app_data_offset);
        }
        bcp->recv_app_data_offset = 0;
    } 
}

static void frame_completeness_check(bcp_t *bcp)
{
    k_log(BCP_LOG_DEBUG, "frame_completeness_check, recv_frame_offset : %d, recv_frame_len : %d\n", bcp->recv_frame_offset, bcp->recv_frame_len);
    if (bcp->recv_frame_offset >= bcp->recv_frame_len) {
        bcp->recv_frame_flag = 0;
        bcp->recv_frame_offset = 0;

        uint16_t cur_crc = bcp->mfs_buf[bcp->recv_frame_len - 1];
        cur_crc = cur_crc << 8 | bcp->mfs_buf[bcp->recv_frame_len - 2];
        uint16_t cal_crc = bcp_adapter.bcp_crc.crc16_cal(bcp->mfs_buf, bcp->recv_frame_len - 2);

        k_log(BCP_LOG_DEBUG, "frame_completeness_check, cur_crc : %04x, cal_crc : %04x\n", cur_crc, cal_crc);
        if (cal_crc == cur_crc) {
            app_data_notify(bcp, bcp->mfs_buf, bcp->recv_frame_len);
        } else {
            bcp_ack_nack_send(bcp, BCP_FRAME_DATA_NACK, bcp->rcv_next);
        }
        bcp->recv_frame_len = 0;
    }
}

static void slice_process(bcp_t *bcp, mtu_t *mtu_buf)
{
    k_log(BCP_LOG_DEBUG, "slice_process, data_len : %d\n", mtu_buf->data_len);

    uint8_t *p = bcp->mfs_buf;
    memcpy(p + bcp->recv_frame_offset, mtu_buf->data, mtu_buf->data_len);
    bcp->recv_frame_offset += mtu_buf->data_len;
    mem_free_to_pool(bcp, mtu_buf);

    frame_completeness_check(bcp);
}

static void first_slice_process(bcp_t *bcp, mtu_t *mtu_buf)
{
    uint8_t fsn = mtu_buf->data[3];
    k_log(BCP_LOG_DEBUG, "first_slice_process, fsn : %d, bcp->rcv_next : %d, data_len : %d\n", bcp->rcv_next, fsn, mtu_buf->data_len);
    if (fsn == bcp->rcv_next) {
        bcp->recv_frame_flag = 1;
        uint16_t payload_len = mtu_buf->data[5];
        payload_len = payload_len << 8 | mtu_buf->data[4];
        bcp->recv_frame_len = payload_len + 8;
        slice_process(bcp, mtu_buf);
    } else {
        mem_free_to_pool(bcp, mtu_buf);
        bcp_ack_nack_send(bcp, BCP_FRAME_DATA_NACK, bcp->rcv_next);
    }
}

static void bcp_input_data_process(bcp_t *bcp, const void *context) 
{
    mtu_t *mtu_buf = (mtu_t *)context;
    k_log(BCP_LOG_DEBUG, "bcp_input_data_process, recv_frame_flag : %d, data_len : %d\n", bcp->recv_frame_flag, mtu_buf->data_len);

    if (bcp->mfs_buf == NULL) {
        mem_free_to_pool(bcp, mtu_buf);
        k_log(BCP_LOG_ERROR, "bcp_input_data_process, mfs_buf is empty\n");
        return;
    }

    if (bcp->recv_frame_flag == 1) {
        slice_process(bcp, mtu_buf);
    } else {
        if (mtu_buf->data_len >= 8) {
            uint8_t frame_type = mtu_buf->data[2];
            if (frame_type == BCP_FRAME_DATA_COMPLETE ||
                frame_type == BCP_FRAME_DATA_START ||
                frame_type == BCP_FRAME_DATA_MIDDLE ||
                frame_type == BCP_FRAME_DATA_END) {
                    first_slice_process(bcp, mtu_buf);
            } else {
                mem_free_to_pool(bcp, mtu_buf);
            }
        } else {
            mem_free_to_pool(bcp, mtu_buf);
        }
    }

    // It's too late to release memory here
    // mem_free_to_pool(bcp, mtu_buf);
}

static void bcp_input_ack_process(bcp_t *bcp, const void *context) 
{
    mtu_t *mtu_buf = (mtu_t *)context;
    uint16_t cur_crc = mtu_buf->data[8];
    cur_crc = cur_crc << 8 | mtu_buf->data[7];
    uint16_t cal_crc = bcp_adapter.bcp_crc.crc16_cal(mtu_buf->data, 7);
    if (cal_crc != cur_crc) {
        k_log(BCP_LOG_ERROR, "bcp_input_ack_process, crc error, cal_crc : %d, cur_crc : %d\n", cal_crc, cur_crc);
        return;
    } 

    uint8_t ack_fsn = mtu_buf->data[6];
    mem_free_to_pool(bcp, mtu_buf);

    frame_t *frame = NULL, *next_frame = NULL;
    LIST_FOR_EACH_ENTRY_SAFE(frame, next_frame, &bcp->ack_list, frame_t, node) {

        if (fsn_diff(ack_fsn, frame->fsn) >= 0) {
            queue_del(&frame->node);
            mem_free_to_pool(bcp, frame);
        } 
        else {
            break;
        }
    }
}

static void bcp_input_nack_process(bcp_t *bcp, const void *context) 
{
    mtu_t *mtu_buf = (mtu_t *)context;
    uint16_t cur_crc = mtu_buf->data[8];
    cur_crc = cur_crc << 8 | mtu_buf->data[7];
    uint16_t cal_crc = bcp_adapter.bcp_crc.crc16_cal(mtu_buf->data, 7);
    if (cal_crc != cur_crc) {
        k_log(BCP_LOG_ERROR, "bcp_input_nack_process, crc error, cal_crc : %d, cur_crc : %d\n", cal_crc, cur_crc);
        return;
    } 

    uint8_t nack_fsn = mtu_buf->data[6];
    mem_free_to_pool(bcp, mtu_buf);

    frame_t *frame = NULL, *next_frame = NULL;
    LIST_FOR_EACH_ENTRY_SAFE(frame, next_frame, &bcp->ack_list, frame_t, node) {

        if (fsn_diff(nack_fsn, frame->fsn) <= 0) {
            data_frame_output(bcp, frame);
        } else {
            queue_del(&frame->node);
            mem_free_to_pool(bcp, frame);
        }
    }
}

static void bcp_sync_rsp_send(bcp_t *bcp, uint8_t fsn) 
{
    uint8_t sync_rsp_frame[8];

    bcp_frame_head_t frame_head;
    frame_head.magic_head = BCP_MAGIC_HEAD;
    frame_head.ctrl = BCP_FRAME_SYNC_ACK;
    frame_head.fsn = fsn;  
    frame_head.len = 0;
    
    memcpy(sync_rsp_frame, &frame_head, sizeof(frame_head));

    uint16_t crc = bcp_adapter.bcp_crc.crc16_cal(sync_rsp_frame, 6);
    sync_rsp_frame[6] = crc;
    sync_rsp_frame[7] = crc >> 8;

    bcp_block_t *bcp_block = (bcp_block_t *)bcp->owner;
    if (bcp->output(bcp_block, sync_rsp_frame, 8) != 0) {
        k_log(BCP_LOG_ERROR, "bcp_sync_rsp_send, output fail, fsn : %d\n", fsn);
    }
}
static void bcp_input_sync_req_process(bcp_t *bcp, const void *context) 
{
    mtu_t *mtu_buf = (mtu_t *)context;
    uint16_t cur_crc = mtu_buf->data[9];
    cur_crc = cur_crc << 8 | mtu_buf->data[8];
    uint16_t cal_crc = bcp_adapter.bcp_crc.crc16_cal(mtu_buf->data, 8);
    if (cal_crc != cur_crc) {
        k_log(BCP_LOG_ERROR, "bcp_input_sync_req_process, crc error, cal_crc : %04x, cur_crc : %04x\n", cal_crc, cur_crc);
        return;
    } 

    uint8_t first_fsn = mtu_buf->data[3];
    uint16_t peer_mfs = mtu_buf->data[7];
    peer_mfs = peer_mfs << 8 | mtu_buf->data[6];
    mem_free_to_pool(bcp, mtu_buf);
    
    // first clean
    if (bcp->mal_buf) {
        bcp_adapter.bcp_mem.bcp_free(bcp->mal_buf);
        bcp->mal_buf = NULL;
    }

    if (bcp->mfs_buf) {
        bcp_adapter.bcp_mem.bcp_free(bcp->mfs_buf);
        bcp->mfs_buf = NULL;
    }

    bcp->recv_app_data_offset = 0;
    bcp->recv_frame_flag = 0;
    bcp->recv_frame_offset = 0;
    bcp->recv_frame_len = 0;

    // resource init
    bcp->rcv_next = first_fsn + 1;

    bcp->mfs_buf = (uint8_t *)bcp_adapter.bcp_mem.bcp_malloc(peer_mfs);
    if (bcp->mfs_buf == NULL) {
        k_log(BCP_LOG_ERROR, "bcp input sync req, mfs buf get mem fail, peer_mfs : %d\n", peer_mfs);
        goto mfs_buf_init_fail;
    }

    bcp->mal_buf = (uint8_t *)bcp_adapter.bcp_mem.bcp_malloc(bcp->mal);
    if (bcp->mal_buf == NULL) {
        k_log(BCP_LOG_ERROR, "bcp input sync req, mal buf get mem fail, mal : %d\n", bcp->mal);
        goto mal_buf_init_fail;
    }

    bcp_sync_rsp_send(bcp, first_fsn);

    return;

mal_buf_init_fail:
    bcp_adapter.bcp_mem.bcp_free(bcp->mfs_buf);
    bcp->mfs_buf = NULL;
    
mfs_buf_init_fail:
    return;

}

static void bcp_input_sync_rsp_process(bcp_t *bcp, const void *context) 
{
    mtu_t *mtu_buf = (mtu_t *)context;
    uint16_t cur_crc = mtu_buf->data[7];
    cur_crc = cur_crc << 8 | mtu_buf->data[6];
    uint16_t cal_crc = bcp_adapter.bcp_crc.crc16_cal(mtu_buf->data, 6);
    if (cal_crc != cur_crc) {
        k_log(BCP_LOG_ERROR, "bcp_input_sync_rsp_process, crc error, cal_crc : %04x, cur_crc : %04x\n", cal_crc, cur_crc);
        return;
    } 
    mem_free_to_pool(bcp, mtu_buf);

    bcp_adapter.bcp_timer.timer_stop(&bcp->timer);

    frame_t *frame = NULL, *next_frame = NULL;
    LIST_FOR_EACH_ENTRY_SAFE(frame, next_frame, &bcp->ack_list, frame_t, node) {
        queue_del(&frame->node);
        mem_free_to_pool(bcp, frame);
    }

    bcp->status = BCP_DONE;

    if (bcp->opened_listener) {
        bcp_block_t *bcp_block = (bcp_block_t *)bcp->owner;
        bcp->opened_listener(bcp_block, BCP_OPEND_OK);
    }
}

int32_t bcp_input(bcp_block_t *bcp_block, void *data, uint32_t len)
{
    bcp_t *bcp = bcp_block->bcp;
    if (len > bcp->mtu) {
        k_log(BCP_LOG_ERROR, "bcp_input, input data len is too long, len : %d\n", len);
        return -1;
    }
    
    mtu_t *mtu_buf = (mtu_t *)mem_get_from_pool(bcp, &bcp->mtu_mem_pool);
    if (mtu_buf == NULL) {
        k_log(BCP_LOG_ERROR, "bcp_input, mtu buf mem get fail\n");
        return -2;
    }

    mtu_buf->data_len = len;
    memcpy(mtu_buf->data, data, len);

    int32_t ret = 0;
    if (len < 8) {
        ret = bcp_event_post(bcp, mtu_buf, bcp_input_data_process);
    } else {
        uint16_t magic_head = mtu_buf->data[1];
        magic_head = magic_head << 8 | mtu_buf->data[0];

        k_log(BCP_LOG_DEBUG, "bcp_input, magic_head : %04x\n", magic_head);

        if (magic_head == BCP_MAGIC_HEAD) {
            uint8_t frame_type = mtu_buf->data[2];
            k_log(BCP_LOG_DEBUG, "bcp_input, frame_type : %d\n", frame_type);
            if (frame_type == BCP_FRAME_DATA_ACK) {
                ret = bcp_event_post_prior(bcp, mtu_buf, bcp_input_ack_process);
            } else if (frame_type == BCP_FRAME_DATA_NACK) {
                ret = bcp_event_post_prior(bcp, mtu_buf, bcp_input_nack_process);
            } else if (frame_type == BCP_FRAME_SYNC_REQ) {
                ret = bcp_event_post_prior(bcp, mtu_buf, bcp_input_sync_req_process);
            } else if (frame_type == BCP_FRAME_SYNC_ACK) {
                ret = bcp_event_post_prior(bcp, mtu_buf, bcp_input_sync_rsp_process);
            } else {
                ret = bcp_event_post(bcp, mtu_buf, bcp_input_data_process);
            }
        } else {
            ret = bcp_event_post(bcp, mtu_buf, bcp_input_data_process);
        }
    }

    if (ret != 0) {
        mem_free_to_pool(bcp, mtu_buf);
        k_log(BCP_LOG_ERROR, "bcp_input, post fail\n");
        return -3;
    } else {
        k_log(BCP_LOG_TRACE, "bcp_input, post ok, data len : %d\n", len);
    }

    return 0;
}

bcp_block_t *bcp_create(const bcp_parm_t *bcp_parm, const bcp_interface_t *bcp_interface, const void *user_data)
{
    bcp_block_t *bcp_block = (bcp_block_t *)bcp_adapter.bcp_mem.bcp_malloc(sizeof(bcp_block_t));
    if (bcp_block == NULL) {
        k_log(BCP_LOG_ERROR, "bcp create, bcp_block get mem fail\n");
        return NULL;  
    }

    bcp_block->bcp = (bcp_t *)bcp_adapter.bcp_mem.bcp_malloc(sizeof(bcp_t));
    if (bcp_block->bcp == NULL) {
        k_log(BCP_LOG_ERROR, "bcp create, bcp get mem fail\n");
        goto bcp_mem_fail; 
    }

    bcp_t *bcp = bcp_block->bcp;

    if (bcp_adapter.bcp_critical.critical_section_create(&bcp->critical_section) != 0) {
        k_log(BCP_LOG_ERROR, "bcp create, bcp critical create failed\n");
        goto bcp_critical_create_fail;
    }

    bcp_block->user_data = (void *)user_data;
    bcp->owner = bcp_block;

    bcp->mal = bcp_parm->mal;
    bcp->mtu = bcp_parm->mtu;
    bcp->mfs = bcp_parm->mtu*bcp_parm->mfs_scale;

    // delay malloc after recv sync frame
    bcp->mal_buf = NULL;
    bcp->mfs_buf = NULL;

    if (mem_pool_init(&bcp->frame_mem_pool, bcp->mfs + sizeof(frame_t), (bcp->mal/bcp->mfs + 1) * 4) < 0) {
        k_log(BCP_LOG_ERROR, "bcp create, frame_mem_pool init failed\n");
        goto frame_mem_pool_init_fail;
    }

    if (mem_pool_init(&bcp->mtu_mem_pool, bcp->mtu + sizeof(mtu_t), bcp_parm->mfs_scale * 2) < 0) {
        k_log(BCP_LOG_ERROR, "bcp create, mtu_mem_pool init failed\n");
        goto mtu_mem_pool_init_fail;
    }

    if (mem_pool_init(&bcp->snd_list_pool, sizeof(queue_node_t), 3) < 0) {
        k_log(BCP_LOG_ERROR, "bcp create, snd_list_pool init failed\n");
        goto snd_list_pool_init_fail;
    }

    if (bcp_adapter.bcp_queue.queue_create(&bcp->queue, 5, sizeof(bcp_context_t)) != 0) {
        k_log(BCP_LOG_ERROR, "bcp create, queue create failed\n");
        goto bcp_queue_create_fail;
    }

    bcp_thread_config_t thread_config = {
        .thread_name = bcp_parm->work_thread_name,
        .thread_priority = bcp_parm->work_thread_priority,
        .thread_stack_size = bcp_parm->work_thread_stack_size,
        .thread_func = bcp_thread_handler,
        .arg = bcp,
    };
    if (bcp_adapter.bcp_thread.thread_create(&bcp->work_thread, &thread_config) != 0) {
        k_log(BCP_LOG_ERROR, "bcp create, work thread create failed\n");
        goto bcp_thread_create_fail;
    }

    if (bcp_adapter.bcp_timer.timer_create(&bcp->timer, bcp_timer_tomeout_handler, bcp) != 0) {
        k_log(BCP_LOG_ERROR, "bcp create, timer create failed\n");
        goto bcp_timer_create_fail;
    }

    queue_init(&bcp->ack_list);

    bcp->snd_next = 0;
    bcp->rcv_next = 0;
    bcp->status = BCP_STOP;
    bcp->exit_cmd = 0;
    bcp->exit_flag = 0;

    bcp->output = bcp_interface->output;
    bcp->data_listener = bcp_interface->data_listener;

    k_log(BCP_LOG_TRACE, "bcp create successful\n");
    
    return bcp_block;

bcp_timer_create_fail:
    bcp_adapter.bcp_thread.thread_destory(&bcp->work_thread);

bcp_thread_create_fail:
    bcp_adapter.bcp_queue.queue_destory(&bcp->queue);

bcp_queue_create_fail:
    mem_pool_deinit(&bcp->snd_list_pool);

snd_list_pool_init_fail:
    mem_pool_deinit(&bcp->mtu_mem_pool);

mtu_mem_pool_init_fail:
    mem_pool_deinit(&bcp->frame_mem_pool);

frame_mem_pool_init_fail:
    bcp_adapter.bcp_critical.critical_section_destory(&bcp->critical_section);
bcp_critical_create_fail:
    bcp_adapter.bcp_mem.bcp_free(bcp_block->bcp);
    bcp_block->bcp = NULL;
bcp_mem_fail:
    bcp_adapter.bcp_mem.bcp_free(bcp_block);
    bcp_block = NULL;

    return NULL;
}


static void bcp_exit_handle(bcp_t *bcp, const void *context) 
{
    bcp->exit_cmd = 1;
}

void bcp_destory(bcp_block_t *bcp_block)
{
    if (bcp_block == NULL) {
        k_log(BCP_LOG_INFO, "bcp_destory, bcp_block is null\n");
        return;
    }

    if (bcp_block->bcp == NULL) {
        bcp_adapter.bcp_mem.bcp_free(bcp_block);
        k_log(BCP_LOG_INFO, "bcp_destory, bcp is null\n");
        return;
    }

    bcp_t *bcp = bcp_block->bcp;
    // before clean res
    bcp_adapter.bcp_timer.timer_destory(&bcp->timer);

    if (bcp_event_post(bcp, NULL, bcp_exit_handle) != 0) {
        k_log(BCP_LOG_ERROR, "bcp_destory, post fail\n");
        bcp->exit_cmd = 1;
    }

    uint8_t count = 0;
    do {
        bcp_adapter.bcp_time.delay_ms(10);
    } while (count < 3 && bcp->exit_flag == 0);

    if (bcp->exit_flag == 0) {
        bcp_adapter.bcp_thread.thread_destory(&bcp->work_thread);
    }
    
    bcp_adapter.bcp_queue.queue_destory(&bcp->queue);
    mem_pool_deinit(&bcp->snd_list_pool);
    mem_pool_deinit(&bcp->mtu_mem_pool);
    mem_pool_deinit(&bcp->frame_mem_pool);
    bcp_adapter.bcp_critical.critical_section_destory(&bcp->critical_section);
    bcp_adapter.bcp_mem.bcp_free(bcp->mal_buf);
    bcp_adapter.bcp_mem.bcp_free(bcp->mfs_buf);
    bcp_adapter.bcp_mem.bcp_free(bcp);
    bcp_block->bcp = NULL;
    bcp_adapter.bcp_mem.bcp_free(bcp_block);

    k_log(BCP_LOG_INFO, "bcp_destory ok\n");
}

int32_t bcp_open(bcp_block_t *bcp_block, void (*opened_cb)(const bcp_block_t *bcp_block, bcp_open_status_t status), uint32_t timeout_ms)
{
    bcp_t *bcp = bcp_block->bcp;
    bcp->opened_listener = opened_cb;
    bcp->sync_timeout_ms = timeout_ms;
    return bcp_event_post_prior(bcp, NULL, sync_frame_send_handle);
}

// single thread used
int32_t bcp_send(bcp_block_t *bcp_block, void *data, uint32_t len)
{
    bcp_t *bcp = bcp_block->bcp;
    if (len > bcp->mal) {
        k_log(BCP_LOG_ERROR, "bcp_send, len is too loog, len : %d\n", len);
        return -1;
    }

    if (bcp_block == NULL || bcp_block->bcp == NULL) {
        k_log(BCP_LOG_ERROR, "bcp_send, bcp_block == NULL || bcp_block->bcp == NULL, len : %d\n", len);
        return -2;
    }

    if (bcp->status != BCP_DONE) {
        k_log(BCP_LOG_ERROR, "bcp_send, bcp is not ready, status : %d\n", bcp->status);
        return -2;
    }

    uint16_t max_payload = bcp->mfs - 8;
    uint16_t count = (len + max_payload - 1)/max_payload;

    k_log(BCP_LOG_DEBUG, "bcp_send, len is %d, divide count is %d, max_payload is %d\n", len, count, max_payload);

    int32_t ret = 0;
    queue_node_t *snd_list = (queue_node_t *)mem_get_from_pool(bcp, &bcp->snd_list_pool);
    if (snd_list == NULL) {
        k_log(BCP_LOG_ERROR, "bcp_send, snd list mem get fail\n");
        ret -= 3;
        goto snd_list_mem_fail;
    }
    queue_init(snd_list);

    for (uint32_t i = 0; i < count; i++) {
        frame_t *frame = (frame_t *)mem_get_from_pool(bcp, &bcp->frame_mem_pool);
        if (frame == NULL) {
            k_log(BCP_LOG_ERROR, "bcp_send, frame mem get fail\n");
            ret -= 4;
            goto frame_mem_fail;
        }

        frame->fsn = i;
        queue_init(&frame->node);
        queue_add_tail(&frame->node, snd_list);
    }

    if (count == 1) {
        frame_t *frame = queue_entry(snd_list->next, frame_t, node);
        data_frame_pack(frame, data, len, BCP_FRAME_DATA_COMPLETE);
    }
    else {
        uint8_t *start = (uint8_t *)data;
        uint32_t offset = 0;
        frame_t *frame = NULL;
        LIST_FOR_EACH_ENTRY(frame, snd_list, frame_t, node) {
            if (frame->fsn == 0) {
                data_frame_pack(frame, start + offset, max_payload - 8, BCP_FRAME_DATA_START);
                offset += max_payload - 8;
            } else if (frame->fsn < (count - 1)) {
                data_frame_pack(frame, start + offset, max_payload - 8, BCP_FRAME_DATA_MIDDLE);
                offset += max_payload - 8;
            } else {
                data_frame_pack(frame, start + offset, len - offset, BCP_FRAME_DATA_END);
            }
        }
    }

    if (bcp_event_post(bcp, snd_list, bcp_send_handle) != 0) {
        k_log(BCP_LOG_ERROR, "bcp_send, post fail\n");
        ret -= 5;
        goto send_post_fail;
    }

    return ret;

send_post_fail:
    frame_t *frame = NULL, *next_frame = NULL;
    LIST_FOR_EACH_ENTRY_SAFE(frame, next_frame, snd_list, frame_t, node) {
        queue_del(&frame->node);
        mem_free_to_pool(bcp, frame);
    }
    
frame_mem_fail:
    mem_free_to_pool(bcp, snd_list);

snd_list_mem_fail:
    return ret;
}
