/**
 * bms_hal.h — Hardware Abstraction Layer interface
 *
 * All platform-specific I/O goes through this interface.
 * Implementations: hal_stm32f4.c (real HW) and hal_mock.c (desktop test).
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifndef BMS_HAL_H
#define BMS_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include "bms_types.h"

/* ── I2C mux / module selection ─────────────────────────────────────── */

/**
 * Select the I2C mux channel for the given module.
 * On real HW, this controls the I2C mux to route to the correct module.
 * Must be called before hal_i2c_read/write for a specific module.
 * @param module_id  module index 0..(BMS_NUM_MODULES-1)
 */
void hal_i2c_select_module(uint8_t module_id);

/* ── I2C ───────────────────────────────────────────────────────────── */

/**
 * Write bytes to I2C device.
 * @param addr   7-bit I2C address
 * @param data   bytes to write
 * @param len    number of bytes
 * @return 0 on success, negative on error
 */
int32_t hal_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len);

/**
 * Read bytes from I2C device.
 * @param addr   7-bit I2C address
 * @param reg    register address to read from
 * @param buf    buffer to read into
 * @param len    number of bytes to read
 * @return 0 on success, negative on error
 */
int32_t hal_i2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len);

/* ── GPIO ──────────────────────────────────────────────────────────── */

typedef enum {
    GPIO_CONTACTOR_POS    = 0,  /* main positive contactor             */
    GPIO_CONTACTOR_NEG    = 1,  /* main negative contactor             */
    GPIO_PRECHARGE_RELAY  = 2,  /* pre-charge relay                    */
    GPIO_CONTACTOR_FB_POS = 3,  /* positive contactor feedback (input) */
    GPIO_CONTACTOR_FB_NEG = 4,  /* negative contactor feedback (input) */
    GPIO_FAULT_LED        = 5,
    GPIO_WARNING_LED      = 6,
    GPIO_FAULT_RELAY      = 7,  /* fault relay output (Table 14)       */
    GPIO_WARNING_RELAY    = 8,  /* warning relay output (Table 14)     */
    GPIO_PIN_COUNT        = 9
} bms_gpio_pin_t;

void hal_gpio_write(bms_gpio_pin_t pin, bool state);
bool hal_gpio_read(bms_gpio_pin_t pin);

/* ── ADC ───────────────────────────────────────────────────────────── */

typedef enum {
    ADC_BUS_VOLTAGE   = 0,     /* DC bus voltage sense                  */
    ADC_PACK_CURRENT  = 1,     /* hall-effect current sensor            */
    ADC_CONTACTOR_V   = 2,     /* voltage across contactor for feedback */
    ADC_CHANNEL_COUNT = 3
} bms_adc_channel_t;

/** Read ADC channel, return raw 12-bit value (0–4095). */
uint16_t hal_adc_read(bms_adc_channel_t channel);

/* ── CAN ───────────────────────────────────────────────────────────── */

/**
 * Transmit a CAN frame.
 * @return 0 on success, negative on error
 */
int32_t hal_can_transmit(const bms_can_frame_t *frame);

/**
 * Receive a CAN frame (non-blocking).
 * @param frame  output frame
 * @return 0 if frame received, 1 if no frame available, negative on error
 */
int32_t hal_can_receive(bms_can_frame_t *frame);

/* ── Timing ────────────────────────────────────────────────────────── */

/** Return monotonic tick count in milliseconds. */
uint32_t hal_tick_ms(void);

/** Delay for given milliseconds (blocking). */
void hal_delay_ms(uint32_t ms);

/* ── System ────────────────────────────────────────────────────────── */

/** Initialize all HAL peripherals. */
void hal_init(void);

/** Enter critical section (disable interrupts). */
void hal_critical_enter(void);

/** Exit critical section (restore interrupts). */
void hal_critical_exit(void);

/** Trigger system reset. */
void hal_system_reset(void);

#endif /* BMS_HAL_H */
