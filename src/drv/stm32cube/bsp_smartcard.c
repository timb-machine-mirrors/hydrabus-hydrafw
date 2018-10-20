/*
HydraBus/HydraNFC - Copyright (C) 2014-2015 Benjamin VERNOUX

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "bsp_smartcard.h"
#include "bsp_smartcard_conf.h"
#include "stm32f405xx.h"
#include "stm32f4xx_hal.h"
#include "bsp_gpio.h"

/*
Warning in order to use this driver all GPIOs peripherals shall be enabled.
*/
#define SMARTCARDx_TIMEOUT_MAX (100000) // About 10sec (see common/chconf.h/CH_CFG_ST_FREQUENCY) can be aborted by UBTN too
#define NB_SMARTCARD (BSP_DEV_SMARTCARD_END)

#define CLOCK_DIV8 (8)
#define CLOCK_DIV16 (16)

static SMARTCARD_HandleTypeDef smartcard_handle[NB_SMARTCARD];
static mode_config_proto_t* smartcard_mode_conf[NB_SMARTCARD];
static volatile uint16_t dummy_read;

/**
  * @brief  Init low level hardware: GPIO, CLOCK, NVIC...
  * @param  dev_num: SMARTCARD dev num
  * @retval None
  */
/*
  This function replaces HAL_SMARTCARD_MspInit() in order to manage multiple devices.
  HAL_SMARTCARD_MspInit() shall be empty/not defined
*/
static void smartcard_gpio_hw_init(bsp_dev_smartcard_t dev_num)
{
	(void)dev_num;
	GPIO_InitTypeDef GPIO_InitStructure;

	/* Enable the SMARTCARD peripheral */
	__USART1_CLK_ENABLE();

	GPIO_InitStructure.Mode = GPIO_MODE_AF_OD;
	GPIO_InitStructure.Pull  = GPIO_PULLUP;
	GPIO_InitStructure.Speed = BSP_SMARTCARD1_GPIO_SPEED;

	/* SMARTCARD1 TX pin configuration */
	GPIO_InitStructure.Alternate = BSP_SMARTCARD1_AF;
	GPIO_InitStructure.Pin = BSP_SMARTCARD1_TX_PIN;
	HAL_GPIO_Init(BSP_SMARTCARD1_TX_PORT, &GPIO_InitStructure);

	GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
	/* SMARTCARD1 RX pin configuration */
	GPIO_InitStructure.Pin = BSP_SMARTCARD1_CLK_PIN;
	HAL_GPIO_Init(BSP_SMARTCARD1_CLK_PORT, &GPIO_InitStructure);
}

/**
  * @brief  DeInit low level hardware: GPIO, CLOCK, NVIC...
  * @param  dev_num: SMARTCARD dev num
  * @retval None
  */
/*
  This function replaces HAL_SMARTCARD_MspDeInit() in order to manage multiple devices.
  HAL_SMARTCARD_MspDeInit() shall be empty/not defined
*/
static void smartcard_gpio_hw_deinit(bsp_dev_smartcard_t dev_num)
{
	(void)dev_num;
	/* Reset peripherals */
	__USART1_FORCE_RESET();
	__USART1_RELEASE_RESET();

	/* Disable peripherals GPIO */
	HAL_GPIO_DeInit(BSP_SMARTCARD1_TX_PORT, BSP_SMARTCARD1_TX_PIN);
	HAL_GPIO_DeInit(BSP_SMARTCARD1_CLK_PORT, BSP_SMARTCARD1_CLK_PIN);
}

/**
  * @brief  SMARTCARDx error treatment function.
  * @param  dev_num: SMARTCARD dev num
  * @retval None
  */
static void smartcard_error(bsp_dev_smartcard_t dev_num)
{
	if(bsp_smartcard_deinit(dev_num) == BSP_OK) {
		/* Re-Initialize the SMARTCARD comunication bus */
		bsp_smartcard_init(dev_num, smartcard_mode_conf[dev_num]);
	}
}

/**
  * @brief  Init SMARTCARD device.
  * @param  dev_num: SMARTCARD dev num.
  * @param  mode_conf: Mode config proto.
  * @retval status: status of the init.
  */
bsp_status_t bsp_smartcard_init(bsp_dev_smartcard_t dev_num, mode_config_proto_t* mode_conf)
{
	SMARTCARD_HandleTypeDef* hsmartcard;
	bsp_status_t status;

	smartcard_mode_conf[dev_num] = mode_conf;
	hsmartcard = &smartcard_handle[dev_num];

	smartcard_gpio_hw_init(dev_num);

	__HAL_SMARTCARD_RESET_HANDLE_STATE(hsmartcard);

	hsmartcard->Instance = BSP_SMARTCARD1;
	hsmartcard->Init.BaudRate = mode_conf->config.smartcard.dev_speed;

	switch(mode_conf->config.smartcard.dev_parity) {
	case 1: /* 8/even */
		hsmartcard->Init.Parity = SMARTCARD_PARITY_EVEN;
		hsmartcard->Init.WordLength = SMARTCARD_WORDLENGTH_9B;
		break;

	case 2: /* 8/odd */
		hsmartcard->Init.Parity = SMARTCARD_PARITY_ODD;
		hsmartcard->Init.WordLength = SMARTCARD_WORDLENGTH_9B;
		break;

	case 0: /* 8/none */
	default:
		hsmartcard->Init.Parity = SMARTCARD_PARITY_ODD;
		hsmartcard->Init.WordLength = SMARTCARD_WORDLENGTH_9B;
		break;
	}

	hsmartcard->Init.Mode = SMARTCARD_MODE_TX_RX;
	hsmartcard->Init.Prescaler = 12;
	hsmartcard->Init.GuardTime = 16;
	hsmartcard->Init.NACKState = SMARTCARD_NACK_ENABLE;
	hsmartcard->Init.CLKLastBit = SMARTCARD_LASTBIT_ENABLE;

	if(mode_conf->config.smartcard.dev_stop_bit == 1)
		hsmartcard->Init.StopBits   = SMARTCARD_STOPBITS_1_5;
	else
		hsmartcard->Init.StopBits   = SMARTCARD_STOPBITS_0_5;

	if(mode_conf->config.smartcard.dev_phase == 0)
		hsmartcard->Init.CLKPhase = SMARTCARD_PHASE_1EDGE;
	else
		hsmartcard->Init.CLKPhase = SMARTCARD_PHASE_2EDGE;

	if(mode_conf->config.smartcard.dev_polarity == 0)
		hsmartcard->Init.CLKPolarity = SMARTCARD_POLARITY_LOW;
	else
		hsmartcard->Init.CLKPolarity = SMARTCARD_POLARITY_HIGH;


	status = HAL_SMARTCARD_Init(hsmartcard);

	/* Dummy read to flush old character */
	dummy_read = hsmartcard->Instance->DR;

	return status;
}

/**
  * @brief  De-initialize the SMARTCARD comunication bus
  * @param  dev_num: SMARTCARD dev num.
  * @retval status: status of the deinit.
  */
bsp_status_t bsp_smartcard_deinit(bsp_dev_smartcard_t dev_num)
{
	SMARTCARD_HandleTypeDef* hsmartcard;
	bsp_status_t status;

	hsmartcard = &smartcard_handle[dev_num];

	/* De-initialize the SMARTCARD comunication bus */
	status = HAL_SMARTCARD_DeInit(hsmartcard);

	/* DeInit the low level hardware: GPIO, CLOCK, NVIC... */
	smartcard_gpio_hw_deinit(dev_num);

	return status;
}

/**
  * @brief  Sends a Byte in blocking mode and return the status.
  * @param  dev_num: SMARTCARD dev num.
  * @param  tx_data: data to send.
  * @param  nb_data: Number of data to send.
  * @retval status of the transfer.
  */
bsp_status_t bsp_smartcard_write_u8(bsp_dev_smartcard_t dev_num, uint8_t* tx_data, uint8_t nb_data)
{
	SMARTCARD_HandleTypeDef* hsmartcard;
	hsmartcard = &smartcard_handle[dev_num];

	bsp_status_t status;
	status = HAL_SMARTCARD_Transmit(hsmartcard, tx_data, nb_data, SMARTCARDx_TIMEOUT_MAX);
	if(status != BSP_OK) {
		smartcard_error(dev_num);
	}
	return status;
}

/**
  * @brief  Read a Byte in blocking mode and return the status.
  * @param  dev_num: SMARTCARD dev num.
  * @param  rx_data: Data to receive.
  * @param  nb_data: Number of data to receive.
  * @retval status of the transfer.
  */
bsp_status_t bsp_smartcard_read_u8(bsp_dev_smartcard_t dev_num, uint8_t* rx_data, uint8_t nb_data)
{
	SMARTCARD_HandleTypeDef* hsmartcard;
	hsmartcard = &smartcard_handle[dev_num];

	bsp_status_t status;
	status = HAL_SMARTCARD_Receive(hsmartcard, rx_data, nb_data, SMARTCARDx_TIMEOUT_MAX);
	if(status != BSP_OK) {
		smartcard_error(dev_num);
	}
	return status;
}

/**
  * @brief  Read bytes in blocking mode, with timeout
  * @param  dev_num: SMARTCARD dev num.
  * @param  rx_data: Data to receive.
  * @param  nb_data: Number of data to receive.
  * @param  timeout: Number of ticks to wait
  * @retval Number of bytes read
  */
bsp_status_t bsp_smartcard_read_u8_timeout(bsp_dev_smartcard_t dev_num, uint8_t* rx_data,
				      uint8_t nb_data, uint32_t timeout)
{
	SMARTCARD_HandleTypeDef* hsmartcard;
	hsmartcard = &smartcard_handle[dev_num];

	bsp_status_t status;
	status = HAL_SMARTCARD_Receive(hsmartcard, rx_data, nb_data, timeout);
	switch(status){
	case BSP_OK:
	case BSP_TIMEOUT:
	    return (nb_data-(hsmartcard->RxXferCount)-1);
	case BSP_ERROR:
	default:
		smartcard_error(dev_num);
		return 0;
	}
}

/**
  * @brief  Send a byte then Read a byte through the SMARTCARD interface.
  * @param  tx_data: Data to send.
  * @param  rx_data: Data to receive.
  * @param  nb_data: Number of data to send & receive.
  * @retval status of the transfer.
  */
bsp_status_t bsp_smartcard_write_read_u8(bsp_dev_smartcard_t dev_num, uint8_t* tx_data, uint8_t* rx_data, uint8_t nb_data)
{
	SMARTCARD_HandleTypeDef* hsmartcard;
	hsmartcard = &smartcard_handle[dev_num];

	bsp_status_t status;
	status = HAL_SMARTCARD_Transmit(hsmartcard, tx_data, nb_data, SMARTCARDx_TIMEOUT_MAX);
	if(status == BSP_OK) {
		status = HAL_SMARTCARD_Receive(hsmartcard, rx_data, nb_data, SMARTCARDx_TIMEOUT_MAX);
	} else {
		smartcard_error(dev_num);
	}
	return status;
}

/**
  * @brief  Checks if the SMARTCARD receive buffer is empty
  * @retval 0 if empty, 1 if data
  */
bsp_status_t bsp_smartcard_rxne(bsp_dev_smartcard_t dev_num)
{
	SMARTCARD_HandleTypeDef* hsmartcard;
	hsmartcard = &smartcard_handle[dev_num];

	return __HAL_SMARTCARD_GET_FLAG(hsmartcard, SMARTCARD_FLAG_RXNE);
}

/** \brief Return final baud rate configured for over8=0 or over8=1.
 *
 * \param dev_num bsp_dev_smartcard_t
 * \return uint32_t final baudrate configured
 *
 */
uint32_t bsp_smartcard_get_final_baudrate(bsp_dev_smartcard_t dev_num)
{
	float f_baudrate;
	float f_baudrate_frac;
	uint32_t final_baudrate;
	uint32_t clock;
	uint32_t brr;
	SMARTCARD_HandleTypeDef* hsmartcard;

	hsmartcard = &smartcard_handle[dev_num];
	brr = hsmartcard->Instance->BRR;

	if((hsmartcard->Instance == USART1) || (hsmartcard->Instance == USART6))
		clock = HAL_RCC_GetPCLK2Freq() / CLOCK_DIV16;
	else
		clock = HAL_RCC_GetPCLK1Freq() / CLOCK_DIV16;

	final_baudrate = brr >> 4;
	if(final_baudrate > 0) {
		f_baudrate_frac = (float)(brr & 0x0F) / 16.0f;
		f_baudrate = ((float)final_baudrate) + f_baudrate_frac;
		final_baudrate = (uint32_t)((float)clock / f_baudrate);
	}

	return final_baudrate;
}
