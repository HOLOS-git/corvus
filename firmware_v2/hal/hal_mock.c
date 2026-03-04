/**
 * @file hal_mock.c
 * @brief Mock HAL for desktop testing
 *
 * Street Smart Edition.
 * Provides controllable mock implementations of all HAL functions
 * for unit testing without hardware.
 */

#ifdef DESKTOP_BUILD

#include "bms_hal.h"
#include "bms_config.h"
#include <string.h>

/* ── Mock state ────────────────────────────────────────────────────── */

static uint32_t s_tick = 0U;
static bool     s_gpio_state[GPIO_PIN_COUNT];
static uint16_t s_adc_values[ADC_CHANNEL_COUNT];
static bool     s_iwdg_reset = false;
static uint32_t s_iwdg_feed_count = 0U;

/* Mock I2C data store */
#define MOCK_I2C_SIZE 4096U
static uint8_t s_i2c_data[MOCK_I2C_SIZE];
static int32_t s_i2c_fail_result = 0;  /* 0=success, -1=fail */
static uint8_t s_selected_module = 0U;

/* Mock NVM */
#define MOCK_NVM_SIZE 4096U
static uint8_t s_mock_nvm[MOCK_NVM_SIZE];

/* Mock CAN */
#define MOCK_CAN_RX_SIZE 16U
static bms_can_frame_t s_can_rx_buf[MOCK_CAN_RX_SIZE];
static uint8_t s_can_rx_head = 0U;
static uint8_t s_can_rx_tail = 0U;

static bms_can_frame_t s_can_tx_buf[MOCK_CAN_RX_SIZE];
static uint8_t s_can_tx_count = 0U;

/* ── Mock control API (for tests) ──────────────────────────────────── */

void mock_reset_all(void)
{
    s_tick = 0U;
    memset(s_gpio_state, 0, sizeof(s_gpio_state));
    memset(s_adc_values, 0, sizeof(s_adc_values));
    memset(s_i2c_data, 0, sizeof(s_i2c_data));
    memset(s_mock_nvm, 0, sizeof(s_mock_nvm));
    s_i2c_fail_result = 0;
    s_iwdg_reset = false;
    s_iwdg_feed_count = 0U;
    s_can_rx_head = 0U;
    s_can_rx_tail = 0U;
    s_can_tx_count = 0U;
}

void mock_set_tick(uint32_t tick_ms) { s_tick = tick_ms; }
void mock_advance_tick(uint32_t ms) { s_tick += ms; }
void mock_set_gpio(bms_gpio_pin_t pin, bool state) { s_gpio_state[pin] = state; }
void mock_set_adc(bms_adc_channel_t ch, uint16_t val) { s_adc_values[ch] = val; }
void mock_set_i2c_fail(int32_t result) { s_i2c_fail_result = result; }
void mock_set_iwdg_reset(bool was_reset) { s_iwdg_reset = was_reset; }
uint32_t mock_get_iwdg_feed_count(void) { return s_iwdg_feed_count; }

void mock_inject_can_frame(const bms_can_frame_t *frame)
{
    uint8_t next = (s_can_rx_head + 1U) % MOCK_CAN_RX_SIZE;
    if (next != s_can_rx_tail) {
        s_can_rx_buf[s_can_rx_head] = *frame;
        s_can_rx_head = next;
    }
}

uint8_t mock_get_can_tx_count(void) { return s_can_tx_count; }

const bms_can_frame_t *mock_get_can_tx(uint8_t idx)
{
    if (idx < s_can_tx_count) { return &s_can_tx_buf[idx]; }
    return NULL;
}

/* Store I2C data for read-back (indexed by module << 8 | reg) */
void mock_set_i2c_reg16(uint8_t module, uint8_t reg, uint16_t val)
{
    uint16_t addr = ((uint16_t)module << 8U) | reg;
    if (addr + 1U < MOCK_I2C_SIZE) {
        s_i2c_data[addr] = (uint8_t)(val & 0xFFU);       /* LSB first */
        s_i2c_data[addr + 1U] = (uint8_t)((val >> 8U) & 0xFFU);
    }
}

void mock_set_i2c_reg8(uint8_t module, uint8_t reg, uint8_t val)
{
    uint16_t addr = ((uint16_t)module << 8U) | reg;
    if (addr < MOCK_I2C_SIZE) {
        s_i2c_data[addr] = val;
    }
}

/* ── HAL implementations ───────────────────────────────────────────── */

void hal_i2c_select_module(uint8_t module_id) { s_selected_module = module_id; }

int32_t hal_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len)
{
    (void)addr; (void)data; (void)len;
    return s_i2c_fail_result;
}

int32_t hal_i2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)addr;
    if (s_i2c_fail_result != 0) { return s_i2c_fail_result; }

    uint16_t base = ((uint16_t)s_selected_module << 8U) | reg;
    uint16_t i;
    for (i = 0U; i < len && (base + i) < MOCK_I2C_SIZE; i++) {
        buf[i] = s_i2c_data[base + i];
    }
    return 0;
}

int32_t hal_i2c_bus_recovery(void) { return 0; }

void hal_gpio_write(bms_gpio_pin_t pin, bool state)
{
    if ((uint8_t)pin < GPIO_PIN_COUNT) { s_gpio_state[pin] = state; }
}

bool hal_gpio_read(bms_gpio_pin_t pin)
{
    if ((uint8_t)pin < GPIO_PIN_COUNT) { return s_gpio_state[pin]; }
    return false;
}

uint16_t hal_adc_read(bms_adc_channel_t channel)
{
    if ((uint8_t)channel < ADC_CHANNEL_COUNT) { return s_adc_values[channel]; }
    return 0U;
}

/* P3-03: Fan tachometer mock */
static uint16_t s_mock_fan_rpm = 1000U;
void mock_set_fan_rpm(uint16_t rpm) { s_mock_fan_rpm = rpm; }
uint16_t hal_fan_tach_read_rpm(void) { return s_mock_fan_rpm; }

int32_t hal_can_transmit(const bms_can_frame_t *frame)
{
    if (s_can_tx_count < MOCK_CAN_RX_SIZE) {
        s_can_tx_buf[s_can_tx_count++] = *frame;
    }
    return 0;
}

int32_t hal_can_receive(bms_can_frame_t *frame)
{
    if (s_can_rx_tail == s_can_rx_head) { return 1; }
    *frame = s_can_rx_buf[s_can_rx_tail];
    s_can_rx_tail = (s_can_rx_tail + 1U) % MOCK_CAN_RX_SIZE;
    return 0;
}

void hal_can_set_filter(uint32_t id1, uint32_t id2) { (void)id1; (void)id2; }

uint32_t hal_tick_ms(void) { return s_tick; }
void hal_delay_ms(uint32_t ms) { s_tick += ms; }

void hal_init(void) { mock_reset_all(); }
void hal_critical_enter(void) { }
void hal_critical_exit(void) { }
void hal_system_reset(void) { }

void hal_iwdg_init(uint32_t timeout_ms) { (void)timeout_ms; }
void hal_iwdg_feed(void) { s_iwdg_feed_count++; }
bool hal_iwdg_was_reset(void) { return s_iwdg_reset; }

void bms_hal_nvm_write(uint32_t addr, const void *data, uint16_t len)
{
    if (addr + len <= MOCK_NVM_SIZE) {
        memcpy(&s_mock_nvm[addr], data, len);
    }
}

void bms_hal_nvm_read(uint32_t addr, void *data, uint16_t len)
{
    if (addr + len <= MOCK_NVM_SIZE) {
        memcpy(data, &s_mock_nvm[addr], len);
    } else {
        memset(data, 0, len);
    }
}

static uint16_t s_balance_mask[BMS_NUM_MODULES];

void bms_hal_bq76952_set_balance(uint8_t module_id, uint16_t cell_mask)
{
    if (module_id < BMS_NUM_MODULES) { s_balance_mask[module_id] = cell_mask; }
}

uint16_t mock_get_balance_mask(uint8_t module_id)
{
    return (module_id < BMS_NUM_MODULES) ? s_balance_mask[module_id] : 0U;
}

#endif /* DESKTOP_BUILD */
