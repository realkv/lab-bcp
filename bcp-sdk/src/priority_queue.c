
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "priority_queue.h"
#include "bcp_port.h"

typedef struct {
    queue_node node;
    uint8_t priority;
    uint16_t size;
    uint8_t data[1];

} priority_elem_t;

static b_sem_t sem_op = {NULL, NULL, NULL, NULL};
static b_mutex_t mutex_op = {NULL, NULL, NULL, NULL};

static void* (*malloc_hook)(size_t) = NULL;
static void (*free_hook)(void *) = NULL;

// internal malloc
void *k_malloc(size_t size) {
	if (malloc_hook) 
		return malloc_hook(size);
	return malloc(size);
}

// internal free
void k_free(void *ptr) {
	if (free_hook) {
		free_hook(ptr);
	}	else {
		free(ptr);
	}
}

// redefine allocator
void k_allocator(void *(*new_malloc)(size_t), void (*new_free)(void*)) {
	malloc_hook = new_malloc;
	free_hook = new_free;
}


void sem_func_register(const b_sem_t *b_sem)
{
    sem_op.sem_create = b_sem->sem_create;
    sem_op.sem_give = b_sem->sem_give;
    sem_op.sem_take = b_sem->sem_take;
    sem_op.sem_free = b_sem->sem_free;
}

void mutex_func_register(const b_mutex_t *b_mutex)
{
    mutex_op.mutex_create = b_mutex->mutex_create;
    mutex_op.mutex_unlock = b_mutex->mutex_unlock;
    mutex_op.mutex_lock = b_mutex->mutex_lock;
    mutex_op.mutex_free = b_mutex->mutex_free;
}

bool priority_queue_reset(priority_queue_t *queue) {
    if (queue == NULL || queue->lock == NULL) {
        return false;
    }

    if (mutex_op.mutex_lock == NULL || mutex_op.mutex_lock(&queue->lock, MUTEX_MAX_TIMEOUT) != 0) {
        return false;
    }

    for (uint32_t i = 0; i <= queue->max_priority; i++) {
        while (!queue_is_empty(&queue->priority_map[i])) {
            priority_elem_t *elem = queue_entry(queue->priority_map[i].next, priority_elem_t, node);
            queue_del_init(&elem->node);
            k_free(elem);     
        }
    }

    mutex_op.mutex_unlock(&queue->lock);

    return true;
}

bool priority_queue_deinit(priority_queue_t *queue) {
    if (priority_queue_reset(queue) != true) {
        return false;
    }
    
    k_free(queue->priority_map);

    sem_op.sem_free(&queue->enqueue_sem);
    sem_op.sem_free(&queue->dequeue_sem);
    mutex_op.mutex_free(&queue->lock);

    queue->capacity = 0;
    queue->max_priority = 0;
    queue->ready_priority = 0;

    return true;
}

bool priority_queue_init(priority_queue_t *queue, uint8_t max_priority, uint32_t capacity) {
    assert(queue != NULL);

    if (sem_op.sem_create == NULL || mutex_op.mutex_create == NULL) {
        return false;
    }

    int32_t res = 0;
    queue->priority_map = (queue_node *)k_malloc(sizeof(queue_node)*(max_priority + 1));
    if (queue->priority_map == NULL) {
        res = -1;
    }

    if (res == 0) {
        queue->max_priority = max_priority;

        for (uint32_t i = 0; i <= max_priority; i++) {
            queue_init(&queue->priority_map[i]);
        }

        if (sem_op.sem_create(&queue->enqueue_sem, capacity, capacity) != 0) {
            res = -2;
        }
    }

    if (res == 0) {
        if (sem_op.sem_create(&queue->dequeue_sem, capacity, 0) != 0) {
            res = -3;
        }
    }

    if (res == 0) {
        if (mutex_op.mutex_create(&queue->lock) != 0) {
            res = -4;
        }
    }

    switch (res) {
        case -4 :
            sem_op.sem_free(&queue->dequeue_sem);
        case -3 :
            sem_op.sem_free(&queue->enqueue_sem);
        case -2 :
            k_free(queue->priority_map);
        case -1 :
            return false;

        case 0 :  
        break;
    }

    queue->capacity = capacity;
    queue->ready_priority = 0;
  
    return true;
}

bool priority_queue_enqueue(priority_queue_t *queue, uint8_t priority, void *data, uint32_t size, uint32_t timeout) {

    assert(queue != NULL);
    assert(data != NULL);

    if (sem_op.sem_take == NULL || queue->enqueue_sem == NULL) {
        return false;
    }

    if (priority > queue->max_priority) {
        return false;
    }

    if (sem_op.sem_take(&queue->enqueue_sem, timeout) != 0) {
        return false;
    } 

    priority_elem_t *new_elem = (priority_elem_t *)k_malloc(sizeof(priority_elem_t) + size);
    if (new_elem == NULL) {
        sem_op.sem_give(&queue->enqueue_sem);
        return false;
    }

    queue_init(&new_elem->node);
    new_elem->priority = priority;
    new_elem->size = size;
    memcpy(new_elem->data, data, size);

    if (mutex_op.mutex_lock(&queue->lock, MUTEX_MAX_TIMEOUT) != 0) {
        sem_op.sem_give(&queue->enqueue_sem);
        k_free(new_elem);
        return false;
    }
    queue_add_tail(&new_elem->node, &queue->priority_map[priority]);
    if (priority > queue->ready_priority) {
        queue->ready_priority = priority;
    }
    mutex_op.mutex_unlock(&queue->lock);

    sem_op.sem_give(&queue->dequeue_sem);

    return true;
}

bool priority_queue_dequeue(priority_queue_t *queue, void *data, uint32_t timeout) {
    assert(queue != NULL);
    assert(data != NULL);

    if (sem_op.sem_take == NULL || queue->dequeue_sem == NULL) {
        return false;
    }

    if (sem_op.sem_take(&queue->dequeue_sem, timeout) != 0) {
        return false;
    }

    if (mutex_op.mutex_lock(&queue->lock, MUTEX_MAX_TIMEOUT) != 0) {
        sem_op.sem_give(&queue->dequeue_sem);
        return false;
    }

    bool status = true;  
    if (!queue_is_empty(&queue->priority_map[queue->ready_priority])) {    
        priority_elem_t *elem =  queue_entry(queue->priority_map[queue->ready_priority].next, priority_elem_t, node);
        queue_del_init(&elem->node);
        memcpy(data, elem->data, elem->size);
        k_free(elem);  
    }
    else {
        status = false;
    }

    int32_t i = queue->ready_priority;
    for (; i > 0; i--) {
        if (!queue_is_empty(&queue->priority_map[i])) {
            break;
        }
    }
    queue->ready_priority = i;
    
    mutex_op.mutex_unlock(&queue->lock);
    sem_op.sem_give(&queue->enqueue_sem);

    return status;
}

