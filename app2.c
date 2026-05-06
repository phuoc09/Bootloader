/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Smooth LED breathing effect on PA0
  *                   - Software PWM ~ 1 kHz (no hardware timer used)
  *                   - DWT cycle counter for microsecond-precision delay
  *                   - Gamma-corrected lookup table for natural-looking breath
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* PWM configuration:
 *   PWM_PERIOD_US = 1000 us  -> PWM frequency = 1 kHz (flicker-free)
 *   PWM_LEVELS    = 100      -> duty resolution: 1% steps, 10 us per tick
 */
#define PWM_PERIOD_US     1000U
#define PWM_LEVELS        100U
#define PWM_TICK_US       (PWM_PERIOD_US / PWM_LEVELS)   /* 10 us */

/* Breath timing:
 *   PWM_CYCLES_PER_STEP cycles are emitted at each duty value before stepping.
 *   Total breath cycle = 2 * BREATH_TABLE_SIZE * PWM_CYCLES_PER_STEP * PWM_PERIOD_US
 *   With table size 128 and 12 cycles per step: 2*128*12*1ms = ~3.07 s per full breath.
 */
#define PWM_CYCLES_PER_STEP   12U
#define BREATH_TABLE_SIZE     128U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* Gamma-corrected breath table (index 0..127 -> duty 0..PWM_LEVELS).
 * Built from a half-sine raised to gamma 2.2 to match human eye perception:
 *   duty[i] = round( PWM_LEVELS * ( sin(pi * i / N)^2.2 ) )    for i in [0, N)
 *
 * Eye sees this as a soft, natural inhale/exhale.
 */
static const uint8_t breath_lut[BREATH_TABLE_SIZE] = {
      0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   1,   1,   1,   1,   2,   2,
      2,   3,   3,   4,   4,   5,   5,   6,
      6,   7,   8,   9,   9,  10,  11,  12,
     13,  14,  15,  16,  17,  18,  20,  21,
     22,  24,  25,  27,  28,  30,  31,  33,
     35,  36,  38,  40,  42,  44,  45,  47,
     49,  51,  53,  55,  57,  59,  61,  63,
     65,  67,  69,  71,  73,  75,  77,  79,
     81,  82,  84,  86,  88,  89,  91,  92,
     93,  95,  96,  97,  98,  99, 100, 100,
    100, 100, 100, 100, 100, 100, 100, 100,
    100, 100, 100, 100, 100, 100, 100, 100,
    100, 100, 100, 100, 100, 100, 100, 100,
    100, 100, 100, 100, 100, 100, 100, 100,
    100, 100, 100, 100, 100, 100, 100, 100,
};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
/* USER CODE BEGIN PFP */
static void DWT_Init(void);
static inline void DWT_Delay_us(uint32_t us);
static void Breath_PWM_Cycle(uint8_t duty);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief Enable the DWT cycle counter (Cortex-M3+).
  *        Used as a high-resolution time base for microsecond delays.
  */
static void DWT_Init(void)
{
    /* Unlock DWT (some debug tools lock it) */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    /* Reset and enable cycle counter */
    DWT->CYCCNT = 0U;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

/**
  * @brief Busy-wait for `us` microseconds using DWT cycle counter.
  *        Resolution = 1 CPU cycle (~13.9 ns @ 72 MHz).
  *        Handles 32-bit wrap-around correctly via unsigned subtraction.
  */
static inline void DWT_Delay_us(uint32_t us)
{
    uint32_t start  = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < cycles) { /* spin */ }
}

/**
  * @brief Emit one PWM cycle on PA0 for the given duty (0..PWM_LEVELS).
  *        Cycle length = PWM_PERIOD_US (1 ms by default -> 1 kHz).
  */
static void Breath_PWM_Cycle(uint8_t duty)
{
    if (duty > PWM_LEVELS) duty = PWM_LEVELS;

    uint32_t on_us  = (uint32_t)duty * PWM_TICK_US;
    uint32_t off_us = PWM_PERIOD_US - on_us;

    if (on_us > 0U)
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET);
        DWT_Delay_us(on_us);
    }

    if (off_us > 0U)
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);
        DWT_Delay_us(off_us);
    }
}

#define ADDR_OTA_INFO 0x08002000

void confirm_app_valid(char my_slot) {
    uint8_t verA   = *(__IO uint8_t*)(ADDR_OTA_INFO);
    uint8_t stateA = *(__IO uint8_t*)(ADDR_OTA_INFO + 1);
    uint8_t tryA   = *(__IO uint8_t*)(ADDR_OTA_INFO + 2);
    
    uint8_t verB   = *(__IO uint8_t*)(ADDR_OTA_INFO + 3);
    uint8_t stateB = *(__IO uint8_t*)(ADDR_OTA_INFO + 4);
    uint8_t tryB   = *(__IO uint8_t*)(ADDR_OTA_INFO + 5);

    uint8_t need_update = 0;

    // Check if an update is needed (0x01 is OTA_STATE_VALID)
    if (my_slot == 'A' && stateA != 0x01) {
        stateA = 0x01; 
        tryA = 0; // Reset try counter
        need_update = 1;
    } else if (my_slot == 'B' && stateB != 0x01) {
        stateB = 0x01; 
        tryB = 0; // Reset try counter
        need_update = 1;
    }

    if (need_update) {
        flash_unlock();
        
        // Erase the ENTIRE flash page containing the OTA info
        // Make sure your flash_erase function clears the whole 1KB/2KB page
        flash_erase(ADDR_OTA_INFO); 
        
        // Write the 6 bytes of updated information
        uint8_t new_info[6] = {verA, stateA, tryA, verB, stateB, tryB};
        flash_write(ADDR_OTA_INFO, new_info, 6);
        
        flash_lock();
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
	DWT_Init();
  /* USER CODE BEGIN 2 */
	SCB->VTOR = 0x0800C800;
  confirm_app_valid('B');
  /* Bring up DWT for us-precision delays */
  

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* Inhale: walk the LUT 0 -> N-1 */
    for (uint32_t i = 0; i < BREATH_TABLE_SIZE; ++i)
    {
        uint8_t duty = breath_lut[i];
        for (uint32_t c = 0; c < PWM_CYCLES_PER_STEP; ++c)
        {
            Breath_PWM_Cycle(duty);
        }
    }

    /* Exhale: walk the LUT N-1 -> 0 */
    for (int32_t i = (int32_t)BREATH_TABLE_SIZE - 1; i >= 0; --i)
    {
        uint8_t duty = breath_lut[i];
        for (uint32_t c = 0; c < PWM_CYCLES_PER_STEP; ++c)
        {
            Breath_PWM_Cycle(duty);
        }
    }
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
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
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  /* HIGH speed -> clean PWM edges at 1 kHz */
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

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
#ifdef USE_FULL_ASSERT
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