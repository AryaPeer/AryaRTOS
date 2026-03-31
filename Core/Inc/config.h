#ifndef INC_CONFIG_H_
#define INC_CONFIG_H_

#define MAX_THREADS         32
#define MAX_PRIORITY        8       // 0 = highest, 7 = lowest
#define DEFAULT_STACK_SIZE  1024
#define MAX_STACK_SIZE      0xA000
#define DEFAULT_TIMESLICE   5
#define IDLE_THREAD_PRIORITY 7
#define STACK_CANARY_VALUE  0xDEADBEEF

#endif /* INC_CONFIG_H_ */
