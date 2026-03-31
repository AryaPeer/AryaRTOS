#ifndef INC_KERNEL_H_
#define INC_KERNEL_H_

#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "list.h"

#define SHPR2 *(volatile uint32_t*)0xE000ED1C
#define SHPR3 *(volatile uint32_t*)0xE000ED20
#define _ICSR *(volatile uint32_t*)0xE000ED04

#define SVC_RUN_FIRST_THREAD  0x00
#define SVC_YIELD             0x01

typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_SLEEPING,
    THREAD_TERMINATED
} thread_state_t;

typedef struct thread {
    uint32_t *sp;
    uint32_t *stack_base;
    uint32_t stack_size;
    uint8_t priority;
    thread_state_t state;
    uint32_t timeslice;
    uint32_t timeslice_remaining;
    uint32_t wake_tick;
    uint32_t tid;
    void (*entry)(void *);
    void *arg;
    void *blocked_on;
    list_t *blocked_queue;
    void *msg_buf;
    int32_t block_result;
    list_node_t ready_node;
    list_node_t wait_node;
    list_node_t sleep_node;
} thread_t;

typedef struct {
    thread_t *owner;
    list_t wait_queue;
    bool initialized;
} mutex_t;

typedef struct {
    int32_t count;
    int32_t max_count;
    list_t wait_queue;
    bool initialized;
} semaphore_t;

typedef struct {
    uint8_t *buffer;
    uint32_t msg_size;
    uint32_t capacity;
    uint32_t count;
    uint32_t head;
    uint32_t tail;
    list_t send_waiters;
    list_t recv_waiters;
    bool initialized;
} msgqueue_t;

extern thread_t *current_thread;
extern uint32_t os_tick_count;

void osKernelInitialize(void);
void osKernelStart(void);

uint32_t osCreateThread(void (*func)(void *), void *arg, uint8_t priority);
void osThreadTerminate(void);
void osYield(void);
void osSetPriority(uint32_t tid, uint8_t new_priority);
uint8_t osGetPriority(uint32_t tid);

void osSleep(uint32_t ms);
uint32_t osGetTick(void);

void osMutexInit(mutex_t *m);
int osMutexAcquire(mutex_t *m, uint32_t timeout_ms);
int osMutexRelease(mutex_t *m);

void osSemaphoreInit(semaphore_t *s, int32_t initial, int32_t max);
int osSemaphoreAcquire(semaphore_t *s, uint32_t timeout_ms);
int osSemaphoreRelease(semaphore_t *s);

void osMsgQueueInit(msgqueue_t *q, void *buf, uint32_t msg_size, uint32_t capacity);
int osMsgQueueSend(msgqueue_t *q, const void *msg, uint32_t timeout_ms);
int osMsgQueueRecv(msgqueue_t *q, void *msg, uint32_t timeout_ms);
uint32_t osMsgQueueCount(msgqueue_t *q);

void os_on_tick(void);
void osSched(void);

#endif /* INC_KERNEL_H_ */
