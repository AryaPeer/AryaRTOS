#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "main.h"
#include "kernel.h"

extern void runFirstThread(void);

static thread_t thread_pool[MAX_THREADS];
static uint8_t  stack_memory[MAX_STACK_SIZE] __attribute__((aligned(8)));
static uint32_t stack_offset  = 0;
static uint32_t thread_count  = 0;

thread_t *current_thread = NULL;
uint32_t  os_tick_count  = 0;

static thread_t *idle_thread = NULL;

static list_t   ready_queues[MAX_PRIORITY];
static uint8_t  ready_bitmap = 0;
static list_t   sleep_list;

static void idle_thread_func(void *arg);
static void thread_trampoline(void);
static void sleep_list_insert(thread_t *t);
static void wait_queue_insert(list_t *queue, thread_t *t);

static uint32_t *alloc_stack(uint32_t size)
{
    size = (size + 7u) & ~7u;
    if (stack_offset + size > MAX_STACK_SIZE) {
        return NULL;
    }
    stack_offset += size;
    return (uint32_t *)&stack_memory[stack_offset];
}

static void sleep_list_insert(thread_t *t)
{
    list_node_t *pos = sleep_list.sentinel.next;
    while (pos != &sleep_list.sentinel) {
        thread_t *other = LIST_ENTRY(pos, thread_t, sleep_node);
        if ((int32_t)(other->wake_tick - t->wake_tick) > 0) break;
        pos = pos->next;
    }
    list_insert_before(pos, &t->sleep_node);
}

static void wait_queue_insert(list_t *queue, thread_t *t)
{
    list_node_t *pos = queue->sentinel.next;
    while (pos != &queue->sentinel) {
        thread_t *other = LIST_ENTRY(pos, thread_t, wait_node);
        if (other->priority > t->priority) break;
        pos = pos->next;
    }
    list_insert_before(pos, &t->wait_node);
}

static void sched_add_ready(thread_t *t)
{
    if (t->state == THREAD_BLOCKED || t->state == THREAD_SLEEPING) {
        t->timeslice_remaining = t->timeslice;
    }
    t->state = THREAD_READY;
    list_push_back(&ready_queues[t->priority], &t->ready_node);
    ready_bitmap |= (1u << t->priority);
}

static thread_t *sched_pick_next(void)
{
    if (ready_bitmap == 0) return NULL;
    uint8_t highest = (uint8_t)__builtin_ctz(ready_bitmap);
    list_node_t *node = list_pop_front(&ready_queues[highest]);
    if (node == NULL) return NULL;
    if (list_is_empty(&ready_queues[highest])) {
        ready_bitmap &= ~(1u << highest);
    }
    return LIST_ENTRY(node, thread_t, ready_node);
}

static void sched_remove(thread_t *t)
{
    list_remove(&t->ready_node);
    if (list_is_empty(&ready_queues[t->priority])) {
        ready_bitmap &= ~(1u << t->priority);
    }
}

static inline void sched_maybe_preempt(thread_t *newly_ready)
{
    if (newly_ready->priority < current_thread->priority) {
        _ICSR |= 1 << 28;
        __asm("isb");
    }
}

__attribute__((optimize("O0")))
void SVC_Handler_Main(unsigned int *svc_args)
{
    unsigned int svc_number = ((char *)svc_args[6])[-2];

    switch (svc_number) {
    case SVC_RUN_FIRST_THREAD: {
        thread_t *first = sched_pick_next();
        if (first == NULL) {
            first = idle_thread;
        }
        first->state   = THREAD_RUNNING;
        current_thread = first;
        __set_PSP((uint32_t)first->sp);
        runFirstThread();
        break;
    }

    case SVC_YIELD:
        _ICSR |= 1 << 28;
        __asm("isb");
        break;

    default:
        break;
    }
}

void osSched(void)
{
    current_thread->sp = (uint32_t *)__get_PSP();

    if (current_thread->state == THREAD_RUNNING) {
        sched_add_ready(current_thread);
    }

    thread_t *next = sched_pick_next();
    if (next == NULL) {
        next = idle_thread;
    }

    if (current_thread->state != THREAD_TERMINATED &&
        *current_thread->stack_base != STACK_CANARY_VALUE) {
        printf("\r\n!!! STACK OVERFLOW: thread %lu !!!\r\n", current_thread->tid);
        while (1) { }
    }

    next->state    = THREAD_RUNNING;
    current_thread = next;
    __set_PSP((uint32_t)next->sp);
}

static void thread_trampoline(void)
{
    osThreadTerminate();
}

uint32_t osCreateThread(void (*func)(void *), void *arg, uint8_t priority)
{
    if (func == NULL) {
        return UINT32_MAX;
    }
    if (priority >= MAX_PRIORITY) {
        priority = MAX_PRIORITY - 1;
    }

    __disable_irq();

    if (thread_count >= MAX_THREADS) {
        __enable_irq();
        return UINT32_MAX;
    }

    uint32_t *stack_top = alloc_stack(DEFAULT_STACK_SIZE);
    if (stack_top == NULL) {
        __enable_irq();
        return UINT32_MAX;
    }

    uint32_t *stack_base = (uint32_t *)((uint8_t *)stack_top - DEFAULT_STACK_SIZE);

    memset(stack_base, 0xCC, DEFAULT_STACK_SIZE);

    *stack_base = STACK_CANARY_VALUE;

    uint32_t *sp = stack_top;
    *(--sp) = 1u << 24;                         // xPSR (Thumb bit)
    *(--sp) = (uint32_t)func;                    // PC
    *(--sp) = (uint32_t)thread_trampoline;       // LR
    *(--sp) = 0;                                 // R12
    *(--sp) = 0;                                 // R3
    *(--sp) = 0;                                 // R2
    *(--sp) = 0;                                 // R1
    *(--sp) = (uint32_t)arg;                     // R0

    *(--sp) = 0xFFFFFFFDu;                       // LR (EXC_RETURN)
    for (int i = 0; i < 8; i++) {
        *(--sp) = 0;                             // R11...R4
    }

    thread_t *t = &thread_pool[thread_count];
    memset(t, 0, sizeof(thread_t));

    t->sp                  = sp;
    t->stack_base          = stack_base;
    t->stack_size          = DEFAULT_STACK_SIZE;
    t->priority            = priority;
    t->state               = THREAD_READY;
    t->timeslice           = DEFAULT_TIMESLICE;
    t->timeslice_remaining = DEFAULT_TIMESLICE;
    t->tid                 = thread_count;
    t->entry               = func;
    t->arg                 = arg;

    t->ready_node.next = &t->ready_node;
    t->ready_node.prev = &t->ready_node;
    t->wait_node.next  = &t->wait_node;
    t->wait_node.prev  = &t->wait_node;
    t->sleep_node.next = &t->sleep_node;
    t->sleep_node.prev = &t->sleep_node;

    sched_add_ready(t);

    uint32_t tid = thread_count;
    thread_count++;

    if (current_thread != NULL) {
        sched_maybe_preempt(t);
    }

    __enable_irq();
    return tid;
}

void osYield(void)
{
    __asm("SVC #1");
}

void osThreadTerminate(void)
{
    __disable_irq();
    current_thread->state = THREAD_TERMINATED;
    list_remove(&current_thread->ready_node);
    list_remove(&current_thread->wait_node);
    list_remove(&current_thread->sleep_node);

    _ICSR |= 1 << 28;
    __asm("isb");
    __enable_irq();
    while (1) { }
}

void osSetPriority(uint32_t tid, uint8_t new_priority)
{
    if (tid >= thread_count || new_priority >= MAX_PRIORITY) return;

    __disable_irq();

    thread_t *t = &thread_pool[tid];

    if (t->state == THREAD_READY) {
        sched_remove(t);
        t->priority = new_priority;
        sched_add_ready(t);
        sched_maybe_preempt(t);
    } else if (t == current_thread) {
        uint8_t old_pri = t->priority;
        t->priority = new_priority;
        if (new_priority > old_pri && ready_bitmap != 0) {
            if ((uint8_t)__builtin_ctz(ready_bitmap) < new_priority) {
                _ICSR |= 1 << 28;
                __asm("isb");
            }
        }
    } else if (t->state == THREAD_BLOCKED && t->blocked_queue != NULL) {
        list_remove(&t->wait_node);
        t->priority = new_priority;
        wait_queue_insert(t->blocked_queue, t);
    } else {
        t->priority = new_priority;
    }

    __enable_irq();
}

uint8_t osGetPriority(uint32_t tid)
{
    if (tid >= thread_count) return MAX_PRIORITY;
    return thread_pool[tid].priority;
}

void osSleep(uint32_t ms)
{
    if (ms == 0) {
        osYield();
        return;
    }

    __disable_irq();
    current_thread->state     = THREAD_SLEEPING;
    current_thread->wake_tick = os_tick_count + ms;
    sleep_list_insert(current_thread);
    _ICSR |= 1 << 28;
    __asm("isb");
    __enable_irq();
}

uint32_t osGetTick(void)
{
    return os_tick_count;
}

void os_on_tick(void)
{
    os_tick_count++;

    if (current_thread == NULL) return;

    while (!list_is_empty(&sleep_list)) {
        list_node_t *head = sleep_list.sentinel.next;
        thread_t *t = LIST_ENTRY(head, thread_t, sleep_node);
        if ((int32_t)(t->wake_tick - os_tick_count) > 0) break;

        list_remove(head);

        if (t->state == THREAD_BLOCKED) {
            list_remove(&t->wait_node);
            t->block_result  = -1;
            t->blocked_on    = NULL;
            t->blocked_queue = NULL;
            t->msg_buf       = NULL;
        }

        sched_add_ready(t);
        if (t->priority < current_thread->priority) {
            _ICSR |= 1 << 28;
            __asm("isb");
        }
    }

    if (current_thread->timeslice_remaining > 0) {
        current_thread->timeslice_remaining--;
        if (current_thread->timeslice_remaining == 0) {
            current_thread->timeslice_remaining = current_thread->timeslice;
            _ICSR |= 1 << 28;
            __asm("isb");
        }
    }
}

void osMutexInit(mutex_t *m)
{
    m->owner       = NULL;
    list_init(&m->wait_queue);
    m->initialized = true;
}

int osMutexAcquire(mutex_t *m, uint32_t timeout_ms)
{
    if (!m->initialized) return -1;

    __disable_irq();

    if (m->owner == NULL) {
        m->owner = current_thread;
        __enable_irq();
        return 0;
    }

    if (m->owner == current_thread) {
        __enable_irq();
        return -1;
    }

    if (timeout_ms == 0) {
        __enable_irq();
        return -1;
    }

    current_thread->state        = THREAD_BLOCKED;
    current_thread->blocked_on   = m;
    current_thread->blocked_queue = &m->wait_queue;
    current_thread->block_result = 0;
    wait_queue_insert(&m->wait_queue, current_thread);

    if (timeout_ms != UINT32_MAX) {
        current_thread->wake_tick = os_tick_count + timeout_ms;
        sleep_list_insert(current_thread);
    }

    _ICSR |= 1 << 28;
    __asm("isb");
    __enable_irq();

    return current_thread->block_result;
}

int osMutexRelease(mutex_t *m)
{
    if (!m->initialized) return -1;

    __disable_irq();

    if (m->owner != current_thread) {
        __enable_irq();
        return -1;
    }

    if (!list_is_empty(&m->wait_queue)) {
        list_node_t *node = list_pop_front(&m->wait_queue);
        thread_t *waiter  = LIST_ENTRY(node, thread_t, wait_node);
        list_remove(&waiter->sleep_node);

        m->owner              = waiter;
        waiter->blocked_on    = NULL;
        waiter->blocked_queue = NULL;
        waiter->block_result  = 0;
        sched_add_ready(waiter);
        sched_maybe_preempt(waiter);
    } else {
        m->owner = NULL;
    }

    __enable_irq();
    return 0;
}

void osSemaphoreInit(semaphore_t *s, int32_t initial, int32_t max)
{
    s->count       = initial;
    s->max_count   = max;
    list_init(&s->wait_queue);
    s->initialized = true;
}

int osSemaphoreAcquire(semaphore_t *s, uint32_t timeout_ms)
{
    if (!s->initialized) return -1;

    __disable_irq();

    if (s->count > 0) {
        s->count--;
        __enable_irq();
        return 0;
    }

    if (timeout_ms == 0) {
        __enable_irq();
        return -1;
    }

    current_thread->state        = THREAD_BLOCKED;
    current_thread->blocked_on   = s;
    current_thread->blocked_queue = &s->wait_queue;
    current_thread->block_result = 0;
    wait_queue_insert(&s->wait_queue, current_thread);

    if (timeout_ms != UINT32_MAX) {
        current_thread->wake_tick = os_tick_count + timeout_ms;
        sleep_list_insert(current_thread);
    }

    _ICSR |= 1 << 28;
    __asm("isb");
    __enable_irq();

    return current_thread->block_result;
}

int osSemaphoreRelease(semaphore_t *s)
{
    if (!s->initialized) return -1;

    __disable_irq();

    if (!list_is_empty(&s->wait_queue)) {
        list_node_t *node = list_pop_front(&s->wait_queue);
        thread_t *waiter  = LIST_ENTRY(node, thread_t, wait_node);
        list_remove(&waiter->sleep_node);

        waiter->blocked_on    = NULL;
        waiter->blocked_queue = NULL;
        waiter->block_result  = 0;
        sched_add_ready(waiter);
        sched_maybe_preempt(waiter);
    } else if (s->count < s->max_count) {
        s->count++;
    }

    __enable_irq();
    return 0;
}

void osMsgQueueInit(msgqueue_t *q, void *buf, uint32_t msg_size, uint32_t capacity)
{
    q->buffer      = (uint8_t *)buf;
    q->msg_size    = msg_size;
    q->capacity    = capacity;
    q->count       = 0;
    q->head        = 0;
    q->tail        = 0;
    list_init(&q->send_waiters);
    list_init(&q->recv_waiters);
    q->initialized = true;
}

int osMsgQueueSend(msgqueue_t *q, const void *msg, uint32_t timeout_ms)
{
    if (!q->initialized) return -1;

    __disable_irq();

    if (!list_is_empty(&q->recv_waiters)) {
        list_node_t *node   = list_pop_front(&q->recv_waiters);
        thread_t *receiver  = LIST_ENTRY(node, thread_t, wait_node);
        list_remove(&receiver->sleep_node);

        memcpy(receiver->msg_buf, msg, q->msg_size);
        receiver->blocked_on    = NULL;
        receiver->blocked_queue = NULL;
        receiver->msg_buf       = NULL;
        receiver->block_result  = 0;
        sched_add_ready(receiver);
        sched_maybe_preempt(receiver);
        __enable_irq();
        return 0;
    }

    if (q->count < q->capacity) {
        memcpy(&q->buffer[q->tail * q->msg_size], msg, q->msg_size);
        q->tail = (q->tail + 1) % q->capacity;
        q->count++;
        __enable_irq();
        return 0;
    }

    if (timeout_ms == 0) {
        __enable_irq();
        return -1;
    }

    current_thread->state        = THREAD_BLOCKED;
    current_thread->blocked_on   = q;
    current_thread->blocked_queue = &q->send_waiters;
    current_thread->msg_buf      = (void *)msg;
    current_thread->block_result = 0;
    wait_queue_insert(&q->send_waiters, current_thread);

    if (timeout_ms != UINT32_MAX) {
        current_thread->wake_tick = os_tick_count + timeout_ms;
        sleep_list_insert(current_thread);
    }

    _ICSR |= 1 << 28;
    __asm("isb");
    __enable_irq();

    return current_thread->block_result;
}

int osMsgQueueRecv(msgqueue_t *q, void *msg, uint32_t timeout_ms)
{
    if (!q->initialized) return -1;

    __disable_irq();

    if (q->count > 0) {
        memcpy(msg, &q->buffer[q->head * q->msg_size], q->msg_size);
        q->head = (q->head + 1) % q->capacity;
        q->count--;

        if (!list_is_empty(&q->send_waiters)) {
            list_node_t *node = list_pop_front(&q->send_waiters);
            thread_t *sender  = LIST_ENTRY(node, thread_t, wait_node);
            list_remove(&sender->sleep_node);

            memcpy(&q->buffer[q->tail * q->msg_size],
                   sender->msg_buf, q->msg_size);
            q->tail = (q->tail + 1) % q->capacity;
            q->count++;

            sender->blocked_on    = NULL;
            sender->blocked_queue = NULL;
            sender->msg_buf       = NULL;
            sender->block_result  = 0;
            sched_add_ready(sender);
            sched_maybe_preempt(sender);
        }

        __enable_irq();
        return 0;
    }

    if (timeout_ms == 0) {
        __enable_irq();
        return -1;
    }

    current_thread->state        = THREAD_BLOCKED;
    current_thread->blocked_on   = q;
    current_thread->blocked_queue = &q->recv_waiters;
    current_thread->msg_buf      = msg;
    current_thread->block_result = 0;
    wait_queue_insert(&q->recv_waiters, current_thread);

    if (timeout_ms != UINT32_MAX) {
        current_thread->wake_tick = os_tick_count + timeout_ms;
        sleep_list_insert(current_thread);
    }

    _ICSR |= 1 << 28;
    __asm("isb");
    __enable_irq();

    return current_thread->block_result;
}

uint32_t osMsgQueueCount(msgqueue_t *q)
{
    if (!q->initialized) return 0;
    return q->count;
}

static void idle_thread_func(void *arg)
{
    (void)arg;
    while (1) {
        __WFI();
    }
}

void osKernelInitialize(void)
{
    SHPR3 = (SHPR3 & ~(0xFFu << 16) & ~(0xFFu << 24))
          | (0xFEu << 16)
          | (0xFEu << 24);
    SHPR2 = (SHPR2 & ~(0xFFu << 24)) | (0xFDu << 24);

    for (int i = 0; i < MAX_PRIORITY; i++) {
        list_init(&ready_queues[i]);
    }
    list_init(&sleep_list);

    osCreateThread(idle_thread_func, NULL, IDLE_THREAD_PRIORITY);
    idle_thread = &thread_pool[0];
}

void osKernelStart(void)
{
    __asm("SVC #0");
}
