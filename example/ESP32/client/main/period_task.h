#ifndef __PERIOD_TASK_H__
#define __PERIOD_TASK_H__

#ifdef __cplusplus
extern "C" {
#endif

void period_task_create(uint32_t stack_size, uint8_t priority);

void period_task_register(void (*func)(void));


#ifdef __cplusplus
}
#endif

#endif /* __PERIOD_TASK_H__ */