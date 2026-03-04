/**
 * @file bms_hal.h
 * @brief Hardware Abstraction Layer — expanded for safety I/O
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P1-02: IWDG init/feed (Henrik, Yara)
 *   P1-03: Gas detector GPIO/ADC inputs (Catherine, Mikael, Henrik, Priya)
 *   P1-04: Ventilation status GPIO (Henrik, Priya)
 *   P1-05: Fire detection/suppression GPIO (Henrik, Priya)
 *   P1-06: IMD alarm GPIO (Dave, Catherine, Mikael, Henrik)
 *   P0-02: I2C bus recovery (Dave, Catherine)
 */

#ifndef BMS_HAL_H
#define BMS_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include "bms_types.h"

/* ── I2C ───────────────────────────────────────────────────────────── */

void    hal_i2c_select_module(uint8_t module_id);
int32_t hal_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len);
int32_t hal_i2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len);

/* P0-02: I2C bus recovery — clock toggle to unstick SDA */
int32_t hal_i2c_bus_recovery(void);

/* ── GPIO — expanded for safety I/O ────────────────────────────────── */

typedef enum {
    GPIO_CONTACTOR_POS     = 0,
    GPIO_CONTACTOR_NEG     = 1,
    GPIO_PRECHARGE_RELAY   = 2,
    GPIO_CONTACTOR_FB_POS  = 3,   /* input */
    GPIO_CONTACTOR_FB_NEG  = 4,   /* input */
    GPIO_FAULT_LED         = 5,
    GPIO_WARNING_LED       = 6,
    GPIO_FAULT_RELAY       = 7,
    GPIO_WARNING_RELAY     = 8,
    /* P1-03: Gas detection */
    GPIO_GAS_ALARM_LOW     = 9,   /* input: gas low alarm */
    GPIO_GAS_ALARM_HIGH    = 10,  /* input: gas high alarm */
    /* P1-04: Ventilation */
    GPIO_VENT_STATUS       = 11,  /* input: vent running */
    GPIO_VENT_CMD          = 12,  /* output: emergency vent command */
    /* P1-05: Fire */
    GPIO_FIRE_DETECT       = 13,  /* input: fire detection */
    GPIO_FIRE_SUPPRESS_IN  = 14,  /* input: suppression activated */
    GPIO_FIRE_RELAY_OUT    = 15,  /* output: thermal fault to fire panel */
    /* P1-06: IMD */
    GPIO_IMD_ALARM         = 16,  /* input: insulation alarm */
    /* P3-03: Fan tachometer (Priya — cooling failure detection) */
    GPIO_FAN_TACH          = 17,  /* input: fan tachometer pulse */
    GPIO_PIN_COUNT         = 18
} bms_gpio_pin_t;

void hal_gpio_write(bms_gpio_pin_t pin, bool state);
bool hal_gpio_read(bms_gpio_pin_t pin);

/* ── ADC — expanded ────────────────────────────────────────────────── */

typedef enum {
    ADC_BUS_VOLTAGE    = 0,   /* P0-03: DC bus voltage sense */
    ADC_PACK_CURRENT   = 1,
    ADC_CONTACTOR_V    = 2,
    ADC_GAS_ANALOG     = 3,   /* P1-03: analog gas concentration */
    ADC_IMD_RESISTANCE = 4,   /* P1-06: IMD resistance analog output */
    ADC_CHANNEL_COUNT  = 5
} bms_adc_channel_t;

uint16_t hal_adc_read(bms_adc_channel_t channel);

/* ── CAN ───────────────────────────────────────────────────────────── */

int32_t hal_can_transmit(const bms_can_frame_t *frame);
int32_t hal_can_receive(bms_can_frame_t *frame);

/* P2-08: CAN hardware filter — accept only expected IDs */
void hal_can_set_filter(uint32_t id1, uint32_t id2);

/* ── Timing ────────────────────────────────────────────────────────── */

uint32_t hal_tick_ms(void);
void     hal_delay_ms(uint32_t ms);

/* ── System ────────────────────────────────────────────────────────── */

void hal_init(void);
void hal_critical_enter(void);
void hal_critical_exit(void);
void hal_system_reset(void);

/* P3-05: Critical section macros (Dave — RTOS shared data protection)
 * Maps to __disable_irq()/__enable_irq() on bare metal,
 * taskENTER_CRITICAL()/taskEXIT_CRITICAL() on FreeRTOS. */
#ifdef USE_FREERTOS
  #include "FreeRTOS.h"
  #include "task.h"
  #define BMS_ENTER_CRITICAL()  taskENTER_CRITICAL()
  #define BMS_EXIT_CRITICAL()   taskEXIT_CRITICAL()
#else
  #define BMS_ENTER_CRITICAL()  hal_critical_enter()
  #define BMS_EXIT_CRITICAL()   hal_critical_exit()
#endif

/* ── P1-02: Independent Watchdog (Henrik, Yara) ────────────────────── */

/** Initialize IWDG with timeout ≤100ms. Called once at boot. */
void hal_iwdg_init(uint32_t timeout_ms);

/** Feed (reload) the watchdog. Must be called from protection loop. */
void hal_iwdg_feed(void);

/** Check if last reset was caused by IWDG. */
bool hal_iwdg_was_reset(void);

/* ── P3-03: Fan Tachometer (Priya — cooling failure detection) ──────── */

/** Read fan RPM from tachometer input (GPIO pulse counting or timer capture).
 *  Returns 0 if no pulses detected. */
uint16_t hal_fan_tach_read_rpm(void);

/* ── NVM ───────────────────────────────────────────────────────────── */

void bms_hal_nvm_write(uint32_t addr, const void *data, uint16_t len);
void bms_hal_nvm_read(uint32_t addr, void *data, uint16_t len);

/* ── Balance HAL ───────────────────────────────────────────────────── */

void bms_hal_bq76952_set_balance(uint8_t module_id, uint16_t cell_mask);

#endif /* BMS_HAL_H */
