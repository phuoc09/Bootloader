/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "flash.h"
#include "boot_sim.h" 
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
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#define ADDR_OTA_INFO    0x08002000 // Format: [Ver A, Stat A, Try A, Ver B, Stat B, Try B]
#define ADDR_APP_1       0x08003000 // App 1 (Slot A) Address
#define ADDR_APP_2       0x08005000 // App 2 (Slot B) Address
#define APP_SIZE         0x2000     // 8KB per partition

// OTA States Definition
#define OTA_STATE_VALID   0x01
#define OTA_STATE_PENDING 0x02
#define OTA_STATE_DOWNLOADING 0x03 
#define OTA_STATE_EMPTY   0xFF

typedef void (*pFunction)(void);

uint8_t active_slot = 'A'; 
uint8_t app_version = 1;
uint8_t skip_timeout = 0;

// Erase specific application slot dynamically (1KB pages)
void flash_erase_slot(uint32_t start_addr, uint32_t size)
{
    for (uint32_t offset = 0; offset < size; offset += 1024)
    {
        flash_erase(start_addr + offset);
    }
}

// ---------------------------------------------------------
// CORE ROLLBACK & BOOT LOGIC
// ---------------------------------------------------------
// ---------------------------------------------------------
// CORE ROLLBACK & BOOT LOGIC
// ---------------------------------------------------------
void check_and_prepare_boot()
{
    uint8_t verA   = *(__IO uint8_t*)(ADDR_OTA_INFO);
    uint8_t stateA = *(__IO uint8_t*)(ADDR_OTA_INFO + 1);
    uint8_t tryA   = *(__IO uint8_t*)(ADDR_OTA_INFO + 2);
    
    uint8_t verB   = *(__IO uint8_t*)(ADDR_OTA_INFO + 3);
    uint8_t stateB = *(__IO uint8_t*)(ADDR_OTA_INFO + 4);
    uint8_t tryB   = *(__IO uint8_t*)(ADDR_OTA_INFO + 5);
    
    uint8_t need_update = 0;

    // 1. Initial Format Check
    if (stateA == 0xFF && stateB == 0xFF) {
        verA = 1; stateA = OTA_STATE_VALID; tryA = 0;
        verB = 0; stateB = OTA_STATE_EMPTY; tryB = 0;
        need_update = 1;
    }

		// 2. ROLLBACK & DOWNLOADING CHECK:
    // - N?u PENDING và try >= 1 -> App b? crash khi ch?y th?!
    // - N?u DOWNLOADING -> B? m?t di?n khi dang n?p OTA d? dang!
    if ((stateA == OTA_STATE_PENDING && tryA >= 1) || stateA == OTA_STATE_DOWNLOADING) {
        stateA = OTA_STATE_EMPTY; verA = 0; tryA = 0; // Ðánh d?u là tr?ng
        flash_unlock();
        flash_erase_slot(ADDR_APP_1, APP_SIZE);       // Xóa ngay vùng App A b? l?i/n?p thi?u
        flash_lock();
        need_update = 1;
    }
    if ((stateB == OTA_STATE_PENDING && tryB >= 1) || stateB == OTA_STATE_DOWNLOADING) {
        stateB = OTA_STATE_EMPTY; verB = 0; tryB = 0; // Ðánh d?u là tr?ng
        flash_unlock();
        flash_erase_slot(ADDR_APP_2, APP_SIZE);       // Xóa ngay vùng App B b? l?i/n?p thi?u
        flash_lock();
        need_update = 1;
    }

    // 3. Select Active Slot
    if (stateA == OTA_STATE_PENDING) {
        active_slot = 'A'; app_version = verA;
    } else if (stateB == OTA_STATE_PENDING) {
        active_slot = 'B'; app_version = verB;
    } else {
        active_slot = 'A'; app_version = verA;
        if (stateA == OTA_STATE_EMPTY && stateB != OTA_STATE_EMPTY) {
            active_slot = 'B'; app_version = verB;
        } else if (stateB != OTA_STATE_EMPTY && verB > verA) {
            active_slot = 'B'; app_version = verB;
        }
    }

    // 4. Set try_to_boot = 1 for the selected slot AND trigger skip_timeout
    if (active_slot == 'A' && stateA == OTA_STATE_PENDING && tryA == 0) {
        tryA = 1;
        need_update = 1;
        skip_timeout = 1; // <--- B?T C? B? QUA 10s
    } else if (active_slot == 'B' && stateB == OTA_STATE_PENDING && tryB == 0) {
        tryB = 1;
        need_update = 1;
        skip_timeout = 1; // <--- B?T C? B? QUA 10s
    }

    // 5. Save OTA info to flash
    if (need_update) {
        flash_unlock();
        flash_erase(ADDR_OTA_INFO);
        uint8_t info_buf[6] = {verA, stateA, tryA, verB, stateB, tryB};
        flash_write(ADDR_OTA_INFO, info_buf, 6);
        flash_lock();
    }
}

// Dynamic boot to the active application
void enter_to_application()
{
    uint32_t boot_addr = (active_slot == 'B') ? ADDR_APP_2 : ADDR_APP_1;
    
    HAL_RCC_DeInit();
    HAL_DeInit();

    SCB->SHCSR &= ~(SCB_SHCSR_USGFAULTENA_Msk |
                    SCB_SHCSR_BUSFAULTENA_Msk |
                    SCB_SHCSR_MEMFAULTENA_Msk);

    __set_MSP(*(__IO uint32_t *)boot_addr);

    pFunction app_entry = (pFunction)(*(__IO uint32_t *)(boot_addr + 4));
    app_entry();
}

#define START_BYTE 0xAA
#define ACK 0x79
#define NACK 0x1F
#define BLOCK_SIZE 256

uint16_t simpleCRC(uint8_t *data, uint16_t len)
{
    uint16_t crc = 0;
    for (uint16_t i = 0; i < len; i++) { crc += data[i]; }
    return crc;
}

void send_byte(uint8_t byte) { HAL_UART_Transmit(&huart1, &byte, 1, HAL_MAX_DELAY); }
uint8_t receive_byte(uint8_t *byte, uint32_t timeout) { return HAL_UART_Receive(&huart1, byte, 1, timeout); }

uint8_t header[3];
uint8_t data[BLOCK_SIZE];
uint8_t crc_buf[2];
uint16_t crc_calc;
uint16_t crc_recv;

void bootloader_loop()
{
    uint32_t app_address = 0;
    uint16_t len;
    uint32_t start_time = HAL_GetTick(); 
    uint8_t ota_active = 0;              

    for (int b = 0; b < 3; b++) { header[b] = 0; }
    
    // Process Rollback & Select Slot
    check_and_prepare_boot();

    // ==========================================
    // N?U V?A N?P XONG (T?c là m?i tang try t? 0 lên 1) -> B? QUA CH? 10S
    // ==========================================
    if (skip_timeout == 1) {
        skip_timeout = 0; // Reset l?i c? (dù sau dó nh?y vào app luôn)
        return;           // Thoát th?ng kh?i bootloader_loop d? vào App ngay l?p t?c
    }

    // Turn ON LED (PA0) for 10s wait
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET);

    while (1)
    {
        // 1. TIMEOUT LOGIC
        if (!ota_active)
        {
            if (HAL_GetTick() - start_time >= 10000) break; // Timeout -> Jump to App
            if (receive_byte(&header[0], 100) != HAL_OK || header[0] != START_BYTE) continue;
            ota_active = 1;
        }
        else
        {
            if (receive_byte(&header[0], 5000) != HAL_OK || header[0] != START_BYTE) continue;
        }

        // 2. PACKET PARSING
        if (HAL_UART_Receive(&huart1, &header[1], 2, 1000) != HAL_OK) { send_byte(NACK); continue; }
        len = (header[1] << 8) | header[2];
        if (len > BLOCK_SIZE) { send_byte(NACK); continue; }
        
        // K?T THÚC NH?N FILE (Len == 0) -> RESET NGAY L?P T?C
				if (len == 0) { 
            // 1. Ð?c l?i 6 byte thông tin OTA hi?n t?i t? Flash
            uint8_t verA   = *(__IO uint8_t*)(ADDR_OTA_INFO);
            uint8_t stateA = *(__IO uint8_t*)(ADDR_OTA_INFO + 1);
            uint8_t tryA   = *(__IO uint8_t*)(ADDR_OTA_INFO + 2);
            uint8_t verB   = *(__IO uint8_t*)(ADDR_OTA_INFO + 3);
            uint8_t stateB = *(__IO uint8_t*)(ADDR_OTA_INFO + 4);
            uint8_t tryB   = *(__IO uint8_t*)(ADDR_OTA_INFO + 5);

            uint8_t need_update = 0;

            // 2. Tìm xem vùng nào dang DOWNLOADING thì ch?t thành PENDING
            if (stateA == OTA_STATE_DOWNLOADING) {
                stateA = OTA_STATE_PENDING;
                need_update = 1;
            } else if (stateB == OTA_STATE_DOWNLOADING) {
                stateB = OTA_STATE_PENDING;
                need_update = 1;
            }

            // 3. Ghi dè tr?ng thái PENDING chính th?c vào Flash
            if (need_update) {
                flash_unlock();
                flash_erase(ADDR_OTA_INFO);
                uint8_t info_buf[6] = {verA, stateA, tryA, verB, stateB, tryB};
                flash_write(ADDR_OTA_INFO, info_buf, 6);
                flash_lock();
            }

            // 4. Báo cáo hoàn t?t và Reset ph?n c?ng
            send_byte(ACK); 
            HAL_Delay(10);        
            NVIC_SystemReset();   
        }
        
        if (HAL_UART_Receive(&huart1, data, len, 1000) != HAL_OK) { send_byte(NACK); continue; }
        if (HAL_UART_Receive(&huart1, crc_buf, 2, 500) != HAL_OK) { send_byte(NACK); continue; }

        crc_recv = (crc_buf[0] << 8) | crc_buf[1];
        crc_calc = simpleCRC(data, len);
        if (crc_recv != crc_calc) { send_byte(NACK); continue; }

        // COMMAND PROCESSING
        if (len == 1 && data[0] == 0x01) 
        {
            send_byte(active_slot);
            continue;
        }
				else if (len == 2) 
        {
            uint8_t target_slot = data[0];
            uint8_t version = data[1];

            if (target_slot == 'A') app_address = ADDR_APP_1;
            else if (target_slot == 'B') app_address = ADDR_APP_2;
            else app_address = ADDR_APP_1; 

            // Read current 6 bytes
            uint8_t verA   = *(__IO uint8_t*)(ADDR_OTA_INFO);
            uint8_t stateA = *(__IO uint8_t*)(ADDR_OTA_INFO + 1);
            uint8_t tryA   = *(__IO uint8_t*)(ADDR_OTA_INFO + 2);
            uint8_t verB   = *(__IO uint8_t*)(ADDR_OTA_INFO + 3);
            uint8_t stateB = *(__IO uint8_t*)(ADDR_OTA_INFO + 4);
            uint8_t tryB   = *(__IO uint8_t*)(ADDR_OTA_INFO + 5);

            // BU?C M?I: Ðánh d?u Firmware là DOWNLOADING trong lúc ch? n?p các block
            if (target_slot == 'A') {
                verA = version; stateA = OTA_STATE_DOWNLOADING; tryA = 0; // <-- Ð?i thành DOWNLOADING
            } else {
                verB = version; stateB = OTA_STATE_DOWNLOADING; tryB = 0; // <-- Ð?i thành DOWNLOADING
            }

            flash_unlock();
            flash_erase_slot(app_address, APP_SIZE);
            flash_erase(ADDR_OTA_INFO);
            uint8_t info_buf[6] = {verA, stateA, tryA, verB, stateB, tryB};
            flash_write(ADDR_OTA_INFO, info_buf, 6);
            
            active_slot = target_slot; 
            app_version = version;
            
            send_byte(ACK);
            continue;
        }
        else 
        {
            if (app_address != 0) 
            {
                flash_write(app_address, data, len);
                app_address += len;
            }
            send_byte(ACK);
        }
    }

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);
    flash_lock();
}

uint8_t mData[10] ={9,8,7,6,5,4,3,2,1,0};
void writeData(){
  flash_unlock();
  flash_erase(0x0801FC00);
  flash_write(0x0801FC00,mData,10);
  flash_lock();
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
  MX_USART1_UART_Init();
  
  /* USER CODE BEGIN 2 */
  
  // Read memory state initially
  //read_ota_info();
	//flash_unlock();
  //flash_erase_slot(0x8002000, 0x10000);
	//flash_lock();
  send_byte(ACK);
  
  // Loop handles 10s wait, LED toggle, and OTA process
  bootloader_loop();
  
  // Enter the correct application (A or B)
  enter_to_application();
  
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
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

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
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
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
