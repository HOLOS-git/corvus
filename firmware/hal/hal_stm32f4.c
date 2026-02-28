/**
 * hal_stm32f4.c — Real STM32F4 HAL implementation (stubs)
 *
 * Target: STM32F446 (Cortex-M4F)
 * Peripherals: I2C1, GPIOB/C/D, CAN1, ADC1, SysTick
 *
 * These are skeletal implementations that compile but require
 * the full STM32 HAL library and BSP to link.
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifdef STM32_BUILD

#include "bms_hal.h"

/* STM32 HAL headers would go here:
 * #include "stm32f4xx_hal.h"
 * #include "stm32f4xx_hal_i2c.h"
 * #include "stm32f4xx_hal_can.h"
 * #include "stm32f4xx_hal_gpio.h"
 * #include "stm32f4xx_hal_adc.h"
 */

/* Placeholder handles */
static volatile uint32_t s_tick_ms = 0U;

void hal_init(void)
{
    /* HAL_Init();
     * SystemClock_Config();
     * MX_I2C1_Init();
     * MX_CAN1_Init();
     * MX_ADC1_Init();
     * MX_GPIO_Init(); */
}

void hal_i2c_select_module(uint8_t module_id)
{
    (void)module_id;
    /* Select I2C mux channel for the given module:
     * e.g. TCA9548A mux write to select channel module_id */
}

int32_t hal_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len)
{
    (void)addr; (void)data; (void)len;
    /* HAL_I2C_Master_Transmit(&hi2c1, addr << 1, data, len, 100); */
    return 0;
}

int32_t hal_i2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)addr; (void)reg; (void)buf; (void)len;
    /* HAL_I2C_Mem_Read(&hi2c1, addr << 1, reg, I2C_MEMADD_SIZE_8BIT, buf, len, 100); */
    return 0;
}

void hal_gpio_write(bms_gpio_pin_t pin, bool state)
{
    (void)pin; (void)state;
    /* HAL_GPIO_WritePin(port, pin_mask, state ? GPIO_PIN_SET : GPIO_PIN_RESET); */
}

bool hal_gpio_read(bms_gpio_pin_t pin)
{
    (void)pin;
    /* return HAL_GPIO_ReadPin(port, pin_mask) == GPIO_PIN_SET; */
    return false;
}

uint16_t hal_adc_read(bms_adc_channel_t channel)
{
    (void)channel;
    /* HAL_ADC_Start(&hadc1); HAL_ADC_PollForConversion(&hadc1, 10); */
    return 0U;
}

int32_t hal_can_transmit(const bms_can_frame_t *frame)
{
    (void)frame;
    /* CAN_TxHeaderTypeDef header; ... HAL_CAN_AddTxMessage(...); */
    return 0;
}

int32_t hal_can_receive(bms_can_frame_t *frame)
{
    (void)frame;
    /* if (HAL_CAN_GetRxFifoFillLevel(...)) ... */
    return 1; /* no frame available */
}

uint32_t hal_tick_ms(void)
{
    return s_tick_ms;
    /* return HAL_GetTick(); */
}

void hal_delay_ms(uint32_t ms)
{
    (void)ms;
    /* HAL_Delay(ms); */
}

void hal_critical_enter(void)
{
    /* __disable_irq(); */
}

void hal_critical_exit(void)
{
    /* __enable_irq(); */
}

void hal_system_reset(void)
{
    /* NVIC_SystemReset(); */
}

#endif /* STM32_BUILD */
