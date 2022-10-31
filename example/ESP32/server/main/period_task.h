/*
 * @Author: your name
 * @Date: 2021-10-12 11:52:40
 * @LastEditTime: 2022-10-21 15:19:49
 * @LastEditors: auto
 * @Description: In User Settings Edit
 * @FilePath: \new-bcp-sdk\server\main\period_task.h
 */

#ifndef __PERIOD_TASK_H__
#define __PERIOD_TASK_H__

#ifdef __cplusplus
extern "C" {
#endif

void period_task_create(uint32_t stack_size, uint8_t priority);

void period_exec_cb_register(void (*period_exec_cb)(void));

#ifdef __cplusplus
}
#endif




#endif /* __PERIOD_TASK_H__ */