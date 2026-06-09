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
#include "i2c.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "i2c_lcd.h"
#include "ow.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TEMP_MIN 36.0f  // Encender relé
#define TEMP_MAX 39.0f  // Apagar relé
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
I2C_LCD_HandleTypeDef lcd1;
ow_t ds18;
char buf[16];
uint8_t scratchpad[9];
float temperatura = 0.0f;

// Variables de control para el motor paso a paso
uint8_t direccion_motor = 1; // 1 = un sentido, 0 = sentido contrario

// Callback para que la librería procese los tiempos del Timer
void mi_ow_callback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM1) {
        ow_callback(&ds18);
    }
}
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void DWT_Init(void);
void delay_us(uint32_t us);
void mover_motor(int pasos, uint8_t dir, uint32_t t_us);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void DWT_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // Habilita DWT
    DWT->CYCCNT = 0;                                // Limpia contador
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;            // Habilita contador
}

void delay_us(uint32_t us) {
    uint32_t cycles = (SystemCoreClock / 1000000L) * us;
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles);
}

// Nueva función auxiliar para mover el motor de forma controlada (MUDADO A PB4)
void mover_motor(int pasos, uint8_t dir, uint32_t t_us) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, dir ? GPIO_PIN_SET : GPIO_PIN_RESET); // PIN_5 = DIR
    for(int p = 0; p < pasos; p++){
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);                    // PIN_4 = STEP (Antes era 6)
        delay_us(t_us);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
        delay_us(t_us);
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
  MX_I2C1_Init();
  MX_TIM1_Init();
  /* USER CODE BEGIN 2 */
  // --- CONFIGURACIÓN MANUAL PA2 (RELÉ) ---
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE(); // Asegurar reloj de GPIOA activo

    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET); // Empezar apagado

  // --- CONFIGURACIÓN MANUAL PB5 Y PB4 (MOTOR PASO A PASO) ---
    __HAL_RCC_GPIOB_CLK_ENABLE(); // Asegurar reloj de GPIOB activo

    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5 | GPIO_PIN_4, GPIO_PIN_RESET); // Pines a cero

    // Inicializar el contador de microsegundos
    DWT_Init();

  // 1. Configurar e Inicializar LCD
    lcd1.hi2c = &hi2c1;
    lcd1.address = 0x4E; // Cambiar a 0x7E si no hay imagen
    lcd_init(&lcd1);
    lcd_clear(&lcd1);
    lcd_puts(&lcd1, "Iniciando...");

    // 2. Configurar OneWire en PA0
    ow_init_t ow_config = {
        .tim_handle = &htim1,
        .tim_cb = mi_ow_callback,
        .gpio = GPIOA,
        .pin = GPIO_PIN_0,
        .rom_id_filter = 0
    };
    ow_init(&ds18, &ow_config);

    // 3. Iniciar búsqueda de sensores
    HAL_TIM_Base_Start_IT(&htim1);

    ow_update_rom_id(&ds18);
    while(ow_is_busy(&ds18));

    lcd_clear(&lcd1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // 1. Iniciar conversión de temperatura
    if (ow_xfer(&ds18, 0x44, NULL, 0, 0) == OW_ERR_NONE)
    {
        while(ow_is_busy(&ds18));

        // ¡El motor gira durante los 750ms que el sensor necesita para procesar!
        mover_motor(750, direccion_motor, 400);

        // 2. Leer datos del sensor
        if (ow_xfer(&ds18, 0xBE, NULL, 0, 9) == OW_ERR_NONE)
        {
            while(ow_is_busy(&ds18));
            ow_read_resp(&ds18, scratchpad, 9);

            // 3. Validar integridad de datos (CRC)
            if (ow_crc(scratchpad, 8) == scratchpad[8])
            {
                int16_t raw = (scratchpad[1] << 8) | scratchpad[0];
                temperatura = raw / 16.0f;

                // --- LÓGICA DE CONTROL (HISTERESIS) ---
                if (temperatura <= TEMP_MIN) {
                    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET); // ON (Calor)
                }
                else if (temperatura >= TEMP_MAX) {
                    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET); // OFF
                }

                // --- ACTUALIZAR LCD ---
                lcd_gotoxy(&lcd1, 0, 0);
                snprintf(buf, sizeof(buf), "Temp: %.2f C  ", temperatura);
                lcd_puts(&lcd1, buf);

                lcd_gotoxy(&lcd1, 0, 1);
                if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_2)) {
                    lcd_puts(&lcd1, "Status: HEATING ");
                } else {
                    lcd_puts(&lcd1, "Status: OK      ");
                }
            }
            else {
                // ERROR DE CRC
                HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
                lcd_gotoxy(&lcd1, 0, 1);
                lcd_puts(&lcd1, "ERR: CRC CHECK  ");
            }
        }
    }
    else {
        // ERROR DE CONEXIÓN
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
        lcd_gotoxy(&lcd1, 0, 1);
        lcd_puts(&lcd1, "ERR: NO SENSOR  ");
    }
  /* USER CODE END 3 */
  }
  /* USER CODE BEGIN WHILE */
}
/* USER CODE END WHILE */

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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
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

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  * where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
