/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "kernel.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static volatile uint32_t pass_count = 0;
static volatile uint32_t fail_count = 0;

#define TEST_ASSERT(cond, name) do { \
    if (cond) { printf("  PASS: %s\r\n", name); pass_count++; } \
    else      { printf("  FAIL: %s\r\n", name); fail_count++; } \
} while(0)

static volatile uint32_t shared_counter = 0;
static volatile uint32_t thread_order[8];
static volatile uint32_t order_idx = 0;
static mutex_t test_mutex;
static semaphore_t test_sem;
static volatile uint32_t sleep_start_tick = 0;
static volatile uint32_t sleep_end_tick = 0;
static volatile uint32_t mq_received_val = 0;
static uint8_t mq_buf[4 * sizeof(uint32_t)];
static msgqueue_t test_mq;
static volatile uint32_t mq_blocking_send_ok = 0;
static volatile int term_thread_ran = 0;
static volatile uint32_t rr_counter_a = 0;
static volatile uint32_t rr_counter_b = 0;
static volatile uint32_t rr_interleave_count = 0;
static volatile uint32_t pri_change_observed = 0;
static volatile float fpu_result_a = 0.0f;
static volatile float fpu_result_b = 0.0f;

static void thread_increment(void *arg)
{
    (void)arg;
    for (int i = 0; i < 5; i++) {
        shared_counter++;
        osYield();
    }
}

static void thread_record_order(void *arg)
{
    uint32_t id = (uint32_t)(uintptr_t)arg;
    if (order_idx < 8)
        thread_order[order_idx++] = id;
}

static void thread_mutex_holder(void *arg)
{
    (void)arg;
    osMutexAcquire(&test_mutex, UINT32_MAX);
    shared_counter = 1;
    osSleep(100);
    osMutexRelease(&test_mutex);
}

static void thread_mutex_waiter(void *arg)
{
    (void)arg;
    osSleep(20);
    osMutexAcquire(&test_mutex, UINT32_MAX);
    shared_counter = 2;
    osMutexRelease(&test_mutex);
}

static void thread_sem_producer(void *arg)
{
    (void)arg;
    osSleep(50);
    osSemaphoreRelease(&test_sem);
}

static void thread_sem_consumer(void *arg)
{
    (void)arg;
    int rc = osSemaphoreAcquire(&test_sem, 2000);
    shared_counter = (rc == 0) ? 1 : 0;
}

static void thread_mq_receiver(void *arg)
{
    (void)arg;
    uint32_t val = 0;
    int rc = osMsgQueueRecv(&test_mq, &val, 2000);
    if (rc == 0) mq_received_val = val;
}

static void thread_mq_sender(void *arg)
{
    (void)arg;
    osSleep(50);
    uint32_t val = 0xBEEF;
    osMsgQueueSend(&test_mq, &val, UINT32_MAX);
}

static void thread_sleep_test(void *arg)
{
    (void)arg;
    sleep_start_tick = osGetTick();
    osSleep(200);
    sleep_end_tick = osGetTick();
}

static void thread_terminates_early(void *arg)
{
    (void)arg;
    term_thread_ran = 1;
    osThreadTerminate();
}

static void thread_rr_a(void *arg)
{
    (void)arg;
    uint32_t last_b = 0;
    while (rr_counter_a < 500000) {
        rr_counter_a++;
        if (rr_counter_b != last_b) {
            rr_interleave_count++;
            last_b = rr_counter_b;
        }
    }
}

static void thread_rr_b(void *arg)
{
    (void)arg;
    while (rr_counter_b < 500000)
        rr_counter_b++;
}

static void thread_check_priority(void *arg)
{
    (void)arg;
    osSleep(50);
    pri_change_observed = osGetPriority(current_thread->tid);
}

static void thread_mutex_hog(void *arg)
{
    (void)arg;
    osMutexAcquire(&test_mutex, UINT32_MAX);
    osSleep(500);
    osMutexRelease(&test_mutex);
}

static void thread_mq_blocking_sender(void *arg)
{
    (void)arg;
    uint32_t val = 99;
    int rc = osMsgQueueSend(&test_mq, &val, 2000);
    mq_blocking_send_ok = (rc == 0) ? 1 : 0;
}

static void thread_fpu_a(void *arg)
{
    (void)arg;
    volatile float acc = 1.0f;
    for (int i = 0; i < 10000; i++)
        acc = acc * 1.0001f;
    fpu_result_a = acc;
}

static void thread_fpu_b(void *arg)
{
    (void)arg;
    volatile float acc = 2.0f;
    for (int i = 0; i < 10000; i++)
        acc = acc * 0.9999f;
    fpu_result_b = acc;
}

static void test_runner(void *arg)
{
    (void)arg;
    int rc;

    printf("\r\n1. Thread creation & execution\r\n");
    shared_counter = 0;
    osCreateThread(thread_increment, NULL, 3);
    osSleep(200);
    TEST_ASSERT(shared_counter == 5, "thread ran and incremented 5x");

    printf("2. Thread argument passing\r\n");
    order_idx = 0;
    memset((void *)thread_order, 0, sizeof(thread_order));
    osCreateThread(thread_record_order, (void *)(uintptr_t)42, 3);
    osSleep(100);
    TEST_ASSERT(order_idx == 1, "thread ran exactly once");
    TEST_ASSERT(thread_order[0] == 42, "received argument 42");

    printf("3. Priority scheduling\r\n");
    order_idx = 0;
    memset((void *)thread_order, 0, sizeof(thread_order));
    osCreateThread(thread_record_order, (void *)(uintptr_t)2, 5);
    osCreateThread(thread_record_order, (void *)(uintptr_t)1, 2);
    osSleep(200);
    TEST_ASSERT(order_idx == 2, "both threads ran");
    TEST_ASSERT(thread_order[0] == 1, "high-pri ran first");
    TEST_ASSERT(thread_order[1] == 2, "low-pri ran second");

    printf("4. Round-robin timeslicing\r\n");
    rr_counter_a = 0;
    rr_counter_b = 0;
    rr_interleave_count = 0;
    osCreateThread(thread_rr_a, NULL, 4);
    osCreateThread(thread_rr_b, NULL, 4);
    osSleep(2000);
    TEST_ASSERT(rr_counter_a == 500000, "RR thread A completed");
    TEST_ASSERT(rr_counter_b == 500000, "RR thread B completed");
    TEST_ASSERT(rr_interleave_count >= 3,
                "threads actually interleaved (timeslicing worked)");
    printf("         interleaves detected: %lu\r\n", rr_interleave_count);

    printf("5. Sleep timing\r\n");
    sleep_start_tick = 0;
    sleep_end_tick = 0;
    osCreateThread(thread_sleep_test, NULL, 3);
    osSleep(400);
    uint32_t elapsed = sleep_end_tick - sleep_start_tick;
    TEST_ASSERT(elapsed >= 199 && elapsed <= 210, "osSleep(200) ~200 ticks");
    if (elapsed < 199 || elapsed > 210)
        printf("         actual: %lu ticks\r\n", elapsed);

    printf("6. Yield\r\n");
    shared_counter = 0;
    osCreateThread(thread_increment, NULL, 3);
    osCreateThread(thread_increment, NULL, 3);
    osSleep(300);
    TEST_ASSERT(shared_counter == 10, "2 threads x 5 increments via yield");

    printf("7. Mutex basic lock/unlock\r\n");
    osMutexInit(&test_mutex);
    rc = osMutexAcquire(&test_mutex, 0);
    TEST_ASSERT(rc == 0, "acquire uncontested");
    rc = osMutexAcquire(&test_mutex, 0);
    TEST_ASSERT(rc == -1, "re-acquire fails (no recursion)");
    rc = osMutexRelease(&test_mutex);
    TEST_ASSERT(rc == 0, "release OK");

    printf("8. Mutex contention\r\n");
    osMutexInit(&test_mutex);
    shared_counter = 0;
    osCreateThread(thread_mutex_holder, NULL, 3);
    osCreateThread(thread_mutex_waiter, NULL, 3);
    osSleep(300);
    TEST_ASSERT(shared_counter == 2, "both acquired in sequence");

    printf("9. Mutex timeout\r\n");
    osMutexInit(&test_mutex);
    osCreateThread(thread_mutex_hog, NULL, 3);
    osSleep(20);
    rc = osMutexAcquire(&test_mutex, 50);
    TEST_ASSERT(rc == -1, "timed out as expected");
    osSleep(500);

    printf("10. Semaphore basic\r\n");
    osSemaphoreInit(&test_sem, 1, 5);
    rc = osSemaphoreAcquire(&test_sem, 0);
    TEST_ASSERT(rc == 0, "acquire (count=1)");
    rc = osSemaphoreAcquire(&test_sem, 0);
    TEST_ASSERT(rc == -1, "acquire fails (count=0)");
    osSemaphoreRelease(&test_sem);
    rc = osSemaphoreAcquire(&test_sem, 0);
    TEST_ASSERT(rc == 0, "acquire after release");
    osSemaphoreRelease(&test_sem);

    printf("11. Semaphore max count\r\n");
    osSemaphoreInit(&test_sem, 0, 2);
    osSemaphoreRelease(&test_sem);
    osSemaphoreRelease(&test_sem);
    osSemaphoreRelease(&test_sem);
    rc = osSemaphoreAcquire(&test_sem, 0);
    int r2 = osSemaphoreAcquire(&test_sem, 0);
    int r3 = osSemaphoreAcquire(&test_sem, 0);
    TEST_ASSERT(rc == 0 && r2 == 0, "acquired 2 (max)");
    TEST_ASSERT(r3 == -1, "3rd acquire fails (capped at max=2)");

    printf("12. Semaphore blocking\r\n");
    osSemaphoreInit(&test_sem, 0, 1);
    shared_counter = 0;
    osCreateThread(thread_sem_consumer, NULL, 3);
    osCreateThread(thread_sem_producer, NULL, 3);
    osSleep(300);
    TEST_ASSERT(shared_counter == 1, "consumer unblocked by producer");

    printf("13. Semaphore timeout\r\n");
    osSemaphoreInit(&test_sem, 0, 1);
    rc = osSemaphoreAcquire(&test_sem, 50);
    TEST_ASSERT(rc == -1, "timed out");

    printf("14. Message queue send/recv\r\n");
    osMsgQueueInit(&test_mq, mq_buf, sizeof(uint32_t), 4);
    uint32_t sv = 0x1234, rv = 0;
    rc = osMsgQueueSend(&test_mq, &sv, 0);
    TEST_ASSERT(rc == 0, "send OK");
    TEST_ASSERT(osMsgQueueCount(&test_mq) == 1, "count=1");
    rc = osMsgQueueRecv(&test_mq, &rv, 0);
    TEST_ASSERT(rc == 0 && rv == 0x1234, "recv correct value");
    TEST_ASSERT(osMsgQueueCount(&test_mq) == 0, "count=0");

    printf("15. MQ blocking recv\r\n");
    osMsgQueueInit(&test_mq, mq_buf, sizeof(uint32_t), 4);
    mq_received_val = 0;
    osCreateThread(thread_mq_receiver, NULL, 3);
    osCreateThread(thread_mq_sender, NULL, 3);
    osSleep(300);
    TEST_ASSERT(mq_received_val == 0xBEEF, "receiver got 0xBEEF");

    printf("16. MQ full + blocking send\r\n");
    osMsgQueueInit(&test_mq, mq_buf, sizeof(uint32_t), 4);
    uint32_t v;
    for (int i = 0; i < 4; i++) {
        v = (uint32_t)(i + 1);
        osMsgQueueSend(&test_mq, &v, 0);
    }
    TEST_ASSERT(osMsgQueueCount(&test_mq) == 4, "queue full (4/4)");
    v = 0;
    rc = osMsgQueueSend(&test_mq, &v, 0);
    TEST_ASSERT(rc == -1, "send fails when full (timeout=0)");
    mq_blocking_send_ok = 0;
    osCreateThread(thread_mq_blocking_sender, NULL, 3);
    osSleep(50);
    osMsgQueueRecv(&test_mq, &v, 0);
    osSleep(100);
    TEST_ASSERT(mq_blocking_send_ok == 1, "blocking send unblocked after recv");

    printf("17. MQ recv timeout\r\n");
    osMsgQueueInit(&test_mq, mq_buf, sizeof(uint32_t), 4);
    uint32_t dummy = 0;
    rc = osMsgQueueRecv(&test_mq, &dummy, 50);
    TEST_ASSERT(rc == -1, "recv timed out on empty queue");

    printf("18. osSetPriority\r\n");
    pri_change_observed = 0;
    uint32_t tid = osCreateThread(thread_check_priority, NULL, 5);
    osSleep(10);
    osSetPriority(tid, 2);
    osSleep(100);
    TEST_ASSERT(pri_change_observed == 2, "observed new priority 2");

    printf("19. Thread self-termination\r\n");
    term_thread_ran = 0;
    osCreateThread(thread_terminates_early, NULL, 3);
    osSleep(100);
    TEST_ASSERT(term_thread_ran == 1, "thread ran then terminated");

    printf("20. FPU context switching\r\n");
    fpu_result_a = 0.0f;
    fpu_result_b = 0.0f;
    osCreateThread(thread_fpu_a, NULL, 4);
    osCreateThread(thread_fpu_b, NULL, 4);
    osSleep(2000);

    float expect_a = 1.0f;
    for (int i = 0; i < 10000; i++) expect_a *= 1.0001f;
    float expect_b = 2.0f;
    for (int i = 0; i < 10000; i++) expect_b *= 0.9999f;
    float err_a = fpu_result_a - expect_a;
    float err_b = fpu_result_b - expect_b;
    if (err_a < 0) err_a = -err_a;
    if (err_b < 0) err_b = -err_b;
    TEST_ASSERT(err_a < 0.001f, "FPU thread A result correct");
    TEST_ASSERT(err_b < 0.001f, "FPU thread B result correct");
    if (err_a >= 0.001f || err_b >= 0.001f) {
        printf("         A: %ld/1000  B: %ld/1000\r\n",
               (int32_t)(fpu_result_a * 1000),
               (int32_t)(fpu_result_b * 1000));
    }

    printf("21. Tick counter\r\n");
    uint32_t t1 = osGetTick();
    osSleep(100);
    uint32_t t2 = osGetTick();
    uint32_t diff = t2 - t1;
    TEST_ASSERT(diff >= 99 && diff <= 110, "100ms advanced ~100 ticks");

    printf("\r\n RESULTS: %lu passed, %lu failed\r\n", pass_count, fail_count);
    if (fail_count == 0)
        printf(" ALL TESTS PASSED\r\n");

    while (1) {
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
        osSleep(fail_count == 0 ? 200 : 1000);
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  osKernelInitialize();
  osCreateThread(test_runner, NULL, 1);
  osKernelStart();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
int __io_putchar(int ch)
{
  uint8_t c = (uint8_t)ch;
  HAL_UART_Transmit(&huart2, &c, 1, HAL_MAX_DELAY);
  return ch;
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
