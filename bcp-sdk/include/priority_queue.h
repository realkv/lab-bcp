
#ifndef __PRIORITY_QUEUE_H__
#define __PRIORITY_QUEUE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

//---------------------------------------------------------------------
// Queue Node Definition                                                         
//---------------------------------------------------------------------
typedef struct node_head 
{
	struct node_head *next, *prev;
} queue_node;

//---------------------------------------------------------------------
// Queue Operations                                                         
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



#define SEM_MAX_TIMEOUT         0xffffffffUL
#define MUTEX_MAX_TIMEOUT       0xffffffffUL

//---------------------------------------------------------------------
// Priority Queue Definition                
//---------------------------------------------------------------------
typedef struct priority_queue_t {

    queue_node *priority_map;
    void *enqueue_sem;
    void *dequeue_sem;
    void *lock;
    uint32_t capacity;
    uint8_t max_priority;
    uint8_t ready_priority;

} priority_queue_t;


/**
 * @description: The priority queue definition resets the priority queue to its initial state, but does not release the queue resources.
 * @param {priority_queue_t} *queue Point to @priority_queue_t Defined objects.
 * @return {*}  Returns true on success and false on failure.
 */
bool priority_queue_reset(priority_queue_t *queue);

/**
 * @description: De initialization of priority queue will release all requested resources.
 * @param {priority_queue_t} *queue  Point to @priority_queue_t Defined objects.
 * @return {*}  Returns true on success and false on failure.
 */
bool priority_queue_deinit(priority_queue_t *queue);

// 优先级队列初始化，申请队列需要的资源，max_priority 从 0 开始，数值越大，优先级越高，capacity 为队列的深度
/**
 * @description: Initialize the priority queue and request the resources needed by the queue.
 * @param {priority_queue_t} *queue Point to @priority_queue_t Defined objects.
 * @param {uint8_t} max_priority The maximum priority of the queue, starting from 0. The higher the value, the higher the priority.
 * @param {uint32_t} capacity  Depth of queue.
 * @return {*} Returns true on success and false on failure.
 */
bool priority_queue_init(priority_queue_t *queue, uint8_t max_priority, uint32_t capacity);

/**
 * @description: Enqueue by copying data.
 * @param {priority_queue_t} *queue  Point to @priority_queue_t Defined objects.
 * @param {uint8_t} priority  The priority of the current data. The lowest priority is 0, 
 * and the highest priority cannot exceed the highest priority of the queue.
 * @param {void} *data   Point to data.
 * @param {uint32_t} size   Size of data
 * @param {uint32_t} timeout  The timeout of queue waiting, which is blocked during the waiting time.
 * @return {*}  Returns true if enqueue successfully, false if enqueue fails or timeout occurs.
 */
bool priority_queue_enqueue(priority_queue_t *queue, uint8_t priority, void *data, uint32_t size, uint32_t timeout);

/**
 * @description: Dequeue by copying data.
 * @param {priority_queue_t} *queue Point to @priority_queue_t Defined objects.
 * @param {void} *data  Point to data.
 * @param {uint32_t} timeout  The timeout of queue waiting, which is blocked during the waiting time.
 * @return {*}  Returns true if dequeue successfully, false if dequeue fails or timeout occurs.
 */
bool priority_queue_dequeue(priority_queue_t *queue, void *data, uint32_t timeout);

#ifdef __cplusplus
}
#endif


#endif /* __PRIORITY_QUEUE_H__ */
