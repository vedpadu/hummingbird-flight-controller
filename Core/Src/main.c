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
#include "dma.h"
#include "spi.h"
#include "tim.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "imu.h"
#include "expresslrs.h"
#include "esc.h"
#include "kalman.h"
#include "button_handler.h"
#include "com_debugging.h"
#include "control.h"
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

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// pass through buffers for sending data between peripherals
uint8_t radio_dma_buffer[8] = {0x00};

uint16_t rc_data[8] = {0,0,0,0,0,0,0,0};

uint16_t mot_buf[4] = {0};

uint32_t last_kalman_tick = 0;

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	if (htim == htim_esc_kalman) {
		if(!motors_armed){
			arm_ESC();
		}else{
			float32_t currTick = micros();
			  float32_t deltTime = (float32_t)(getDeltaTime(currTick, last_kalman_tick)) * 0.000001;
			  last_kalman_tick = currTick;
			  updateKalman(gyro, accel, deltTime);
			  controlsOuterUpdate(estimate, accel);
		}
	  }else if(htim == htim_elrs){
		  // keep the internal clock in phase
		  setLastClockTime(micros());
		  clockPhaseUpdate(0);
		  doFhssIrq();
	  }else if(htim == htim_control_loop){
		  if(bmi270_ready && kalman_initialized && !isDisconnected()){
			  // fast loop, gets inputs and does rate control
			  //TODO: inputs do not have to be here
			  expressLrsSetRcDataFromPayload(rc_data);
			  controlsInnerLoop(rc_data, mot_buf, gyro, accel, estimate);
			  set_esc_outputs(mot_buf);
		  }
	  }else if(htim == htim_imu){
		  if(bmi270_ready && kalman_initialized){
			  readIMUData();
		  }
	  }else if(htim == htim_debug_loop){
		  if(bmi270_ready && kalman_initialized){
				if(!isBindingMode()){
					HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_15);
				}
				HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_14);
		  }

	  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
   if (GPIO_Pin == GPIO_PIN_13) {
	  // process radio packet on interrupt
	  setLastPacketTime(micros());
	  sx1280_read_interrupt(radio_dma_buffer);
	  processRFPacket((uint8_t *)radio_dma_buffer, micros());
  } else if(GPIO_Pin == GPIO_PIN_2){
	  // add erase flash sector functionality on multiple presses as well as clearing bind mode
	  char* data4 = "BUTTON PRESS\n";
	  CDC_Transmit_FS((uint8_t *)data4, strlen(data4));
	  processButtonPress(micros());
  } else {
      __NOP();
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
  MX_DMA_Init();
  MX_TIM4_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM5_Init();
  MX_USB_DEVICE_Init();
  MX_SPI1_Init();
  MX_SPI3_Init();
  MX_TIM9_Init();
  MX_TIM10_Init();
  MX_TIM11_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_Base_Start_IT(htim_debug_loop); // debug loop and LEDs

  // initialize peripherals
  init_ESC();
  initExpressLRS();
  IMUInit();
  controlsInit();
  quaternion_t initEst = {1.0, {0.0, 0.0, 0.0}};
  last_kalman_tick = micros();
  initKalman(initEst, ESTIMATE_COV, GYRO_COV, GYRO_BIAS_COV, ACCEL_PROC_COV, ACCEL_BIAS_COV, ACCEL_OBS_COV);

  // start the microsecond clock
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= 1;
  DWT->CYCCNT = 0;
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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 4;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
uint32_t micros()
{
	return (DWT->CYCCNT/48);
}

uint32_t getDeltaTime(uint32_t greater, uint32_t lesser)
{
	if(greater < lesser){
		greater += (0xFFFFFFFF/48);
	}
	return greater - lesser;
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
