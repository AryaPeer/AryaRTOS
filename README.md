# AryaRTOS

A real-time operating system for the STM32F446RE.

## Features

- Priority-based preemptive scheduling with 8 priority levels
- Round-robin timeslicing for same-priority threads
- Mutexes with blocking and timeout support
- Counting semaphores
- Message queues with blocking send/receive
- Sleep, yield, and timeout support on all blocking operations

## How it works

Each thread gets its own stack and runs using the process stack pointer (PSP). The kernel keeps a ready queue per priority level and always picks the highest-priority runnable thread. Threads at the same priority take turns via round-robin timeslicing.

Context switching happens through PendSV. When SysTick fires every millisecond, it checks if a timeslice expired or a sleeping thread needs to wake up, then triggers PendSV to swap threads.

Message queues use a fixed-size circular buffer. If a thread tries to send to a full queue or receive from an empty one, it blocks until space opens up or a message arrives.

## Building

STM32CubeIDE project targeting the NUCLEO-F446RE. Import and build normally. Printf output goes over USART2 at 115200 baud.
