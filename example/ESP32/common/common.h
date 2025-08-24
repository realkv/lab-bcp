/*
 * @Author: your name
 * @Date: 2021-10-12 11:57:26
 * @LastEditTime: 2025-08-15 15:50:48
 * @LastEditors: auto
 * @Description: In User Settings Edit
 * @FilePath: \esp32-new\sources\coap_test\main\common.h
 */

#ifndef __COMMON_H__
#define __COMMON_H__


#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_timer.h"


#include <stdint.h>
#undef uint8_t
#undef int8_t
#define uint8_t unsigned char
#define int8_t char

#undef uint16_t
#undef int16_t
#define uint16_t unsigned short
#define int16_t short

#undef uint32_t
#undef int32_t
#define uint32_t unsigned int
#define int32_t int


typedef void (*timer_cb)(void *arg);

void timer_start(esp_timer_handle_t timer, uint32_t period_ms);
void period_timer_init(esp_timer_handle_t *timer, timer_cb cb, const char *timer_name);


void *queue_create(uint32_t num, uint32_t element_size);
bool dequeue(void *queue, void *data, uint32_t timeout);
bool enqueue_back(void *queue, void *data, uint32_t timeout);
bool enqueue_front(void *queue, void *data, uint32_t timeout);
bool queue_reset(void *queue);


int32_t sem_new(void *sem, uint32_t max_count, uint32_t init_count);
void sem_give(void *sem);
int32_t sem_take(void *sem, uint32_t timeout);
void sem_free(void *sem);

#endif /* __COMMON_H__ */


