
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : TypeRush STM32G474RE Main Program
  ******************************************************************************
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* TIM3: PSC=169, ARR=999, PWM=1kHz (170MHz clock) */
#define TR_PWM_MAX          999
#define TR_BREATH_MS        10
#define TR_BREATH_STEP      10
#define TR_FLASH_HALF_MS   80
#define TR_DEBOUNCE_MS      35
#define TR_UART_LINE_MAX   48
#define TR_UART_QUIET_MS    5

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef hlpuart1;
TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */

/* 0 = Idle, 1 = Game running */
static uint8_t tr_running = 0;

/* 0 = Green, 1 = Yellow, 2 = Red */
static uint8_t tr_color = 0;

/* PWM breathing */
static uint16_t tr_brightness = 0;
static uint8_t tr_brightness_up = 1;
static uint32_t tr_breath_tick = 0;

/* Correct answer flash */
static uint8_t tr_flash_active = 0;
static uint8_t tr_flash_color = 0;
static uint32_t tr_flash_tick = 0;

/* START and STOP debounce */
static uint8_t tr_start_raw = 0;
static uint8_t tr_start_stable = 0;
static uint8_t tr_stop_raw = 0;
static uint8_t tr_stop_stable = 0;
static uint32_t tr_start_tick = 0;
static uint32_t tr_stop_tick = 0;

/* UART command buffer */
static char tr_uart_line[TR_UART_LINE_MAX];
static uint8_t tr_uart_length = 0;
static uint8_t tr_uart_discard = 0;
static uint32_t tr_uart_last_rx = 0;
static const char *tr_uart_reply = NULL;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_LPUART1_UART_Init(void);
static void MX_TIM3_Init(void);

/* USER CODE BEGIN PFP */
static void TR_SetPWM(uint16_t red, uint16_t yellow, uint16_t green);
static void TR_UpdateLED(uint32_t now);
static void TR_CheckButtons(uint32_t now);
static void TR_ReadUART(uint32_t now);
static void TR_HandleCommand(const char *cmd, uint32_t now);
static void TR_SendReply(uint32_t now);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Set the brightness of the three LEDs */
static void TR_SetPWM(uint16_t red, uint16_t yellow, uint16_t green)
{
  if (red > TR_PWM_MAX)
    red = TR_PWM_MAX;

  if (yellow > TR_PWM_MAX)
    yellow = TR_PWM_MAX;

  if (green > TR_PWM_MAX)
    green = TR_PWM_MAX;

  /* PC6 = Red */
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, red);

  /* PC9 = Yellow */
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, yellow);

  /* PC8 = Green */
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, green);
}

/* Update LED breathing and correct-answer flashing */
static void TR_UpdateLED(uint32_t now)
{
  /* Update brightness every 10 ms */
  if ((uint32_t)(now - tr_breath_tick) >= TR_BREATH_MS)
  {
    tr_breath_tick = now;

    if (tr_brightness_up != 0)
    {
      if (tr_brightness + TR_BREATH_STEP >= TR_PWM_MAX)
      {
        tr_brightness = TR_PWM_MAX;
        tr_brightness_up = 0;
      }
      else
      {
        tr_brightness += TR_BREATH_STEP;
      }
    }
    else
    {
      if (tr_brightness <= TR_BREATH_STEP)
      {
        tr_brightness = 0;
        tr_brightness_up = 1;
      }
      else
      {
        tr_brightness -= TR_BREATH_STEP;
      }
    }
  }

  /* Correct answer: flash twice */
  if (tr_flash_active != 0)
  {
    uint32_t elapsed = (uint32_t)(now - tr_flash_tick);

    if (elapsed < 4 * TR_FLASH_HALF_MS)
    {
      uint16_t value =
          ((elapsed / TR_FLASH_HALF_MS) % 2 == 0)
          ? TR_PWM_MAX : 0;

      TR_SetPWM(
          (tr_flash_color == 2) ? value : 0,
          (tr_flash_color == 1) ? value : 0,
          (tr_flash_color == 0) ? value : 0
      );

      return;
    }

    tr_flash_active = 0;
  }

  /* Idle: all three LEDs breathe */
  if (tr_running == 0)
  {
    TR_SetPWM(
        tr_brightness,
        tr_brightness,
        tr_brightness
    );
  }
  else
  {
    /* Running: only selected color breathes */
    TR_SetPWM(
        (tr_color == 2) ? tr_brightness : 0,
        (tr_color == 1) ? tr_brightness : 0,
        (tr_color == 0) ? tr_brightness : 0
    );
  }
}

/* Send a pending UART response */
static void TR_SendReply(uint32_t now)
{
  if (tr_uart_reply != NULL &&
      (uint32_t)(now - tr_uart_last_rx) >= TR_UART_QUIET_MS)
  {
    const char *reply = tr_uart_reply;
    tr_uart_reply = NULL;

    (void)HAL_UART_Transmit(
        &hlpuart1,
        (uint8_t *)reply,
        (uint16_t)strlen(reply),
        100
    );
  }
}

/* Process commands from the website */
static void TR_HandleCommand(const char *cmd, uint32_t now)
{
  /* Connection verification */
  if (strcmp(cmd, "HELLO") == 0 ||
      strcmp(cmd, "PING") == 0)
  {
    tr_uart_reply = "TYPERUSH READY\n";
  }

  /* Start game */
  else if (strcmp(cmd, "RUN") == 0)
  {
    tr_running = 1;
    tr_color = 0;
    tr_flash_active = 0;

    tr_uart_reply = "ACK RUN\n";
  }

  /* Stop game and return to idle */
  else if (strcmp(cmd, "IDLE") == 0 ||
           strcmp(cmd, "STOP") == 0)
  {
    tr_running = 0;
    tr_flash_active = 0;

    tr_uart_reply = "ACK IDLE\n";
  }

  /* Select green LED */
  else if (strcmp(cmd, "LEVEL GREEN") == 0)
  {
    tr_color = 0;
  }

  /* Select yellow LED */
  else if (strcmp(cmd, "LEVEL YELLOW") == 0)
  {
    tr_color = 1;
  }

  /* Select red LED */
  else if (strcmp(cmd, "LEVEL RED") == 0)
  {
    tr_color = 2;
  }

  /* Correct answer: flash green twice */
  else if (tr_running != 0 &&
           strcmp(cmd, "HIT GREEN") == 0)
  {
    tr_flash_color = 0;
    tr_flash_active = 1;
    tr_flash_tick = now;
  }

  /* Correct answer: flash yellow twice */
  else if (tr_running != 0 &&
           strcmp(cmd, "HIT YELLOW") == 0)
  {
    tr_flash_color = 1;
    tr_flash_active = 1;
    tr_flash_tick = now;
  }

  /* Correct answer: flash red twice */
  else if (tr_running != 0 &&
           strcmp(cmd, "HIT RED") == 0)
  {
    tr_flash_color = 2;
    tr_flash_active = 1;
    tr_flash_tick = now;
  }
}

/* Read UART commands using short-timeout polling */
static void TR_ReadUART(uint32_t now)
{
  uint8_t byte = 0;
  uint8_t n;

  for (n = 0; n < 32; n++)
  {
    HAL_StatusTypeDef status;

    status = HAL_UART_Receive(
        &hlpuart1,
        &byte,
        1,
        1
    );

    if (status != HAL_OK)
    {
      if (status == HAL_ERROR)
      {
        __HAL_UART_CLEAR_OREFLAG(&hlpuart1);

        tr_uart_length = 0;
        tr_uart_discard = 1;
      }

      break;
    }

    tr_uart_last_rx = now;

    /* Ignore carriage return */
    if (byte == '\r')
    {
      continue;
    }

    /* Newline means the command is complete */
    if (byte == '\n')
    {
      if (tr_uart_discard == 0 &&
          tr_uart_length > 0)
      {
        tr_uart_line[tr_uart_length] = '\0';

        TR_HandleCommand(tr_uart_line, now);
      }

      tr_uart_length = 0;
      tr_uart_discard = 0;
    }
    else if (tr_uart_discard == 0)
    {
      if (tr_uart_length < TR_UART_LINE_MAX - 1)
      {
        tr_uart_line[tr_uart_length] = (char)byte;
        tr_uart_length++;
      }
      else
      {
        tr_uart_discard = 1;
        tr_uart_length = 0;
      }
    }
  }
}

/* Detect START and STOP buttons */
static void TR_CheckButtons(uint32_t now)
{
  uint8_t start_pressed;
  uint8_t stop_pressed;

  start_pressed =
      (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_RESET)
      ? 1 : 0;

  stop_pressed =
      (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1) == GPIO_PIN_RESET)
      ? 1 : 0;

  /* START button debounce */
  if (start_pressed != tr_start_raw)
  {
    tr_start_raw = start_pressed;
    tr_start_tick = now;
  }

  if (start_pressed != tr_start_stable &&
      (uint32_t)(now - tr_start_tick) >= TR_DEBOUNCE_MS)
  {
    tr_start_stable = start_pressed;

    if (start_pressed != 0 && tr_running == 0)
    {
      tr_uart_reply = "EVT START\n";
    }
  }

  /* STOP button debounce */
  if (stop_pressed != tr_stop_raw)
  {
    tr_stop_raw = stop_pressed;
    tr_stop_tick = now;
  }

  if (stop_pressed != tr_stop_stable &&
      (uint32_t)(now - tr_stop_tick) >= TR_DEBOUNCE_MS)
  {
    tr_stop_stable = stop_pressed;

    if (stop_pressed != 0)
    {
      tr_running = 0;
      tr_flash_active = 0;

      tr_uart_reply = "EVT STOP\n";
    }
  }
}

/* USER CODE END 0 */

/**
  * @brief The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize configured peripherals */
  MX_GPIO_Init();
  MX_LPUART1_UART_Init();
  MX_TIM3_Init();

  /* USER CODE BEGIN 2 */

  /* Start red PWM */
  if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  /* Start green PWM */
  if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }

  /* Start yellow PWM */
  if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }

  tr_breath_tick = HAL_GetTick();
  tr_uart_last_rx = tr_breath_tick;

  TR_SetPWM(0, 0, 0);

  /* Startup UART message */
  tr_uart_reply = "TYPERUSH READY\n";

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    uint32_t now = HAL_GetTick();

    TR_ReadUART(now);
    TR_CheckButtons(now);
    TR_UpdateLED(now);
    TR_SendReply(now);

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

  HAL_PWREx_ControlVoltageScaling(
      PWR_REGULATOR_VOLTAGE_SCALE1_BOOST
  );

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue =
      RCC_HSICALIBRATION_DEFAULT;

  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType =
      RCC_CLOCKTYPE_HCLK |
      RCC_CLOCKTYPE_SYSCLK |
      RCC_CLOCKTYPE_PCLK1 |
      RCC_CLOCKTYPE_PCLK2;

  RCC_ClkInitStruct.SYSCLKSource =
      RCC_SYSCLKSOURCE_PLLCLK;

  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(
          &RCC_ClkInitStruct,
          FLASH_LATENCY_4
      ) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPUART1_UART_Init(void)
{
  /* USER CODE BEGIN LPUART1_Init 0 */
  /* USER CODE END LPUART1_Init 0 */

  /* USER CODE BEGIN LPUART1_Init 1 */
  /* USER CODE END LPUART1_Init 1 */

  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 115200;
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart1.Init.StopBits = UART_STOPBITS_1;
  hlpuart1.Init.Parity = UART_PARITY_NONE;
  hlpuart1.Init.Mode = UART_MODE_TX_RX;
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;

  hlpuart1.Init.OneBitSampling =
      UART_ONE_BIT_SAMPLE_DISABLE;

  hlpuart1.Init.ClockPrescaler =
      UART_PRESCALER_DIV1;

  hlpuart1.AdvancedInit.AdvFeatureInit =
      UART_ADVFEATURE_NO_INIT;

  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_SetTxFifoThreshold(
          &hlpuart1,
          UART_TXFIFO_THRESHOLD_1_8
      ) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_SetRxFifoThreshold(
          &hlpuart1,
          UART_RXFIFO_THRESHOLD_1_8
      ) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_DisableFifoMode(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN LPUART1_Init 2 */
  /* USER CODE END LPUART1_Init 2 */
}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{
  /* USER CODE BEGIN TIM3_Init 0 */
  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */
  /* USER CODE END TIM3_Init 1 */

  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 169;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 999;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;

  htim3.Init.AutoReloadPreload =
      TIM_AUTORELOAD_PRELOAD_DISABLE;

  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;

  if (HAL_TIM_ConfigClockSource(
          &htim3,
          &sClockSourceConfig
      ) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;

  sMasterConfig.MasterSlaveMode =
      TIM_MASTERSLAVEMODE_DISABLE;

  if (HAL_TIMEx_MasterConfigSynchronization(
          &htim3,
          &sMasterConfig
      ) != HAL_OK)
  {
    Error_Handler();
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

  /* Red LED - PC6 */
  if (HAL_TIM_PWM_ConfigChannel(
          &htim3,
          &sConfigOC,
          TIM_CHANNEL_1
      ) != HAL_OK)
  {
    Error_Handler();
  }

  /* Green LED - PC8 */
  if (HAL_TIM_PWM_ConfigChannel(
          &htim3,
          &sConfigOC,
          TIM_CHANNEL_3
      ) != HAL_OK)
  {
    Error_Handler();
  }

  /* Yellow LED - PC9 */
  if (HAL_TIM_PWM_ConfigChannel(
          &htim3,
          &sConfigOC,
          TIM_CHANNEL_4
      ) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN TIM3_Init 2 */
  /* USER CODE END TIM3_Init 2 */

  HAL_TIM_MspPostInit(&htim3);
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
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* Configure START PB0 and STOP PB1 */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;

  /* Falling edge: button connected to GND */
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;

  /* Internal pull-up */
  GPIO_InitStruct.Pull = GPIO_PULLUP;

  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI0 interrupt */
  HAL_NVIC_SetPriority(EXTI0_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  /* EXTI1 interrupt */
  HAL_NVIC_SetPriority(EXTI1_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief Error Handler
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

#ifdef USE_FULL_ASSERT

/**
  * @brief Assert failed
  * @param file: source file
  * @param line: source line
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}

#endif /* USE_FULL_ASSERT */
