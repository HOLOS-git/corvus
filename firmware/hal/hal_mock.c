/**
 * hal_mock.c — Desktop mock HAL for testing
 *
 * Implements all HAL functions with injectable state:
 * - Fake I2C responses (cell voltages, temperatures, safety regs)
 * - Capturable GPIO writes (contactor commands)
 * - Simulated CAN TX/RX queues
 * - Controllable tick counter
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifdef DESKTOP_BUILD

#include "bms_hal.h"
#include "bms_bq76952.h"
#include "bms_config.h"
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════
 * Mock state — all publicly accessible for test injection
 * ═══════════════════════════════════════════════════════════════════════ */

/* Simulated cell voltages per module (what BQ76952 would return) */
static uint16_t mock_cell_mv[BMS_NUM_MODULES][BMS_CELLS_PER_BQ76952];

/* Simulated temperatures per module in 0.1K (BQ76952 raw format) */
static uint16_t mock_temp_raw[BMS_NUM_MODULES][BMS_TEMPS_PER_MODULE];

/* Simulated safety registers per module */
static uint8_t mock_safety_a[BMS_NUM_MODULES];
static uint8_t mock_safety_b[BMS_NUM_MODULES];
static uint8_t mock_safety_c[BMS_NUM_MODULES];

/* Simulated current per module (CC2, signed 16-bit in mA) */
static int16_t mock_current_ma[BMS_NUM_MODULES];

/* GPIO pin states */
static bool mock_gpio_out[GPIO_PIN_COUNT];
static bool mock_gpio_in[GPIO_PIN_COUNT];

/* ADC values */
static uint16_t mock_adc[ADC_CHANNEL_COUNT];

/* CAN TX capture queue */
#define MOCK_CAN_QUEUE_SIZE 32U
static bms_can_frame_t mock_can_tx_queue[MOCK_CAN_QUEUE_SIZE];
static uint16_t mock_can_tx_head;
static uint16_t mock_can_tx_count;

/* CAN RX injection queue */
static bms_can_frame_t mock_can_rx_queue[MOCK_CAN_QUEUE_SIZE];
static uint16_t mock_can_rx_head;
static uint16_t mock_can_rx_tail;
static uint16_t mock_can_rx_count;

/* Tick counter */
static uint32_t mock_tick;

/* I2C error injection */
static bool mock_i2c_fail;

/* Active module (set by hal_i2c_select_module) */
static uint8_t mock_active_module;

/* Device number response (for init verification) */
static uint16_t mock_device_number;

/* Last subcmd written */
static uint16_t mock_last_subcmd;

/* ═══════════════════════════════════════════════════════════════════════
 * Public mock control API (called from tests)
 * ═══════════════════════════════════════════════════════════════════════ */

void mock_hal_reset(void)
{
    uint8_t mod;
    uint8_t cell;
    uint8_t sens;

    memset(mock_cell_mv, 0, sizeof(mock_cell_mv));
    memset(mock_temp_raw, 0, sizeof(mock_temp_raw));
    memset(mock_safety_a, 0, sizeof(mock_safety_a));
    memset(mock_safety_b, 0, sizeof(mock_safety_b));
    memset(mock_safety_c, 0, sizeof(mock_safety_c));
    memset(mock_current_ma, 0, sizeof(mock_current_ma));
    memset(mock_gpio_out, 0, sizeof(mock_gpio_out));
    memset(mock_gpio_in, 0, sizeof(mock_gpio_in));
    memset(mock_adc, 0, sizeof(mock_adc));
    memset(mock_can_tx_queue, 0, sizeof(mock_can_tx_queue));
    memset(mock_can_rx_queue, 0, sizeof(mock_can_rx_queue));

    mock_can_tx_head = 0U;
    mock_can_tx_count = 0U;
    mock_can_rx_head = 0U;
    mock_can_rx_tail = 0U;
    mock_can_rx_count = 0U;
    mock_tick = 0U;
    mock_active_module = 0U;
    mock_i2c_fail = false;
    mock_device_number = 0x7695U; /* valid BQ76952 */
    mock_last_subcmd = 0U;

    /* Set default cell voltages to 3675 mV (mid-SoC) */
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        for (cell = 0U; cell < BMS_CELLS_PER_BQ76952; cell++) {
            mock_cell_mv[mod][cell] = 3675U;
        }
        /* Default temps: 25°C = 298.15K = 2982 in 0.1K */
        for (sens = 0U; sens < BMS_TEMPS_PER_MODULE; sens++) {
            mock_temp_raw[mod][sens] = 2982U;
        }
    }
}

void mock_set_cell_voltage(uint8_t module_id, uint8_t cell_idx, uint16_t mv)
{
    if (module_id < BMS_NUM_MODULES && cell_idx < BMS_CELLS_PER_BQ76952) {
        mock_cell_mv[module_id][cell_idx] = mv;
    }
}

void mock_set_all_cell_voltages(uint16_t mv)
{
    uint8_t mod, cell;
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        for (cell = 0U; cell < BMS_CELLS_PER_BQ76952; cell++) {
            mock_cell_mv[mod][cell] = mv;
        }
    }
}

void mock_set_temperature(uint8_t module_id, uint8_t sensor_idx, int16_t deci_c)
{
    if (module_id < BMS_NUM_MODULES && sensor_idx < BMS_TEMPS_PER_MODULE) {
        /* Convert 0.1°C to 0.1K: add 2731 */
        mock_temp_raw[module_id][sensor_idx] = (uint16_t)((int32_t)deci_c + 2731);
    }
}

void mock_set_all_temperatures(int16_t deci_c)
{
    uint8_t mod, sens;
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        for (sens = 0U; sens < BMS_TEMPS_PER_MODULE; sens++) {
            mock_temp_raw[mod][sens] = (uint16_t)((int32_t)deci_c + 2731);
        }
    }
}

void mock_set_safety_a(uint8_t module_id, uint8_t flags)
{
    if (module_id < BMS_NUM_MODULES) { mock_safety_a[module_id] = flags; }
}

void mock_set_safety_b(uint8_t module_id, uint8_t flags)
{
    if (module_id < BMS_NUM_MODULES) { mock_safety_b[module_id] = flags; }
}

void mock_set_i2c_fail(bool fail) { mock_i2c_fail = fail; }

void mock_set_gpio_input(bms_gpio_pin_t pin, bool state)
{
    if ((uint8_t)pin < GPIO_PIN_COUNT) { mock_gpio_in[pin] = state; }
}

bool mock_get_gpio_output(bms_gpio_pin_t pin)
{
    if ((uint8_t)pin < GPIO_PIN_COUNT) { return mock_gpio_out[pin]; }
    return false;
}

void mock_set_adc(bms_adc_channel_t ch, uint16_t val)
{
    if ((uint8_t)ch < ADC_CHANNEL_COUNT) { mock_adc[ch] = val; }
}

void mock_set_tick(uint32_t ms) { mock_tick = ms; }
void mock_advance_tick(uint32_t ms) { mock_tick += ms; }

void mock_inject_can_rx(const bms_can_frame_t *frame)
{
    if (mock_can_rx_count < MOCK_CAN_QUEUE_SIZE) {
        mock_can_rx_queue[mock_can_rx_tail] = *frame;
        mock_can_rx_tail = (mock_can_rx_tail + 1U) % MOCK_CAN_QUEUE_SIZE;
        mock_can_rx_count++;
    }
}

uint16_t mock_get_can_tx_count(void) { return mock_can_tx_count; }

bool mock_get_can_tx_frame(uint16_t idx, bms_can_frame_t *frame)
{
    if (idx < mock_can_tx_count) {
        *frame = mock_can_tx_queue[idx];
        return true;
    }
    return false;
}

void mock_clear_can_tx(void)
{
    mock_can_tx_head = 0U;
    mock_can_tx_count = 0U;
}

/* ═══════════════════════════════════════════════════════════════════════
 * HAL implementation
 * ═══════════════════════════════════════════════════════════════════════ */

void hal_init(void)
{
    mock_hal_reset();
}

int32_t hal_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len)
{
    (void)addr;
    if (mock_i2c_fail) { return -1; }

    /* Detect subcommand writes to 0x3E */
    if (len >= 3U && data[0] == BQ76952_REG_SUBCMD_LOW) {
        mock_last_subcmd = (uint16_t)((uint16_t)data[2] << 8U) | (uint16_t)data[1];
    }

    return 0;
}

void hal_i2c_select_module(uint8_t module_id)
{
    if (module_id < BMS_NUM_MODULES) {
        mock_active_module = module_id;
    }
}

int32_t hal_i2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    uint8_t m = mock_active_module;
    (void)addr;
    if (mock_i2c_fail) { return -1; }

    /* Route reads based on register address, using active module */

    /* Cell voltage registers: 0x14–0x32 */
    if (reg >= BQ76952_REG_CELL1_VOLTAGE && reg <= 0x32U && len == 2U) {
        uint8_t cell_idx = (uint8_t)((reg - BQ76952_REG_CELL1_VOLTAGE) / 2U);
        uint16_t mv = mock_cell_mv[m][cell_idx];
        buf[0] = (uint8_t)(mv & 0xFFU);
        buf[1] = (uint8_t)((mv >> 8U) & 0xFFU);
        return 0;
    }

    /* Safety registers */
    if (reg == BQ76952_REG_SAFETY_ALERT_A && len == 1U) { buf[0] = mock_safety_a[m]; return 0; }
    if (reg == BQ76952_REG_SAFETY_STATUS_A && len == 1U) { buf[0] = mock_safety_a[m]; return 0; }
    if (reg == BQ76952_REG_SAFETY_ALERT_B && len == 1U) { buf[0] = mock_safety_b[m]; return 0; }
    if (reg == BQ76952_REG_SAFETY_STATUS_B && len == 1U) { buf[0] = mock_safety_b[m]; return 0; }
    if (reg == BQ76952_REG_SAFETY_ALERT_C && len == 1U) { buf[0] = mock_safety_c[m]; return 0; }

    /* Temperature registers */
    if (reg == BQ76952_REG_TS1_TEMP && len == 2U) {
        uint16_t v = mock_temp_raw[m][0];
        buf[0] = (uint8_t)(v & 0xFFU); buf[1] = (uint8_t)((v >> 8U) & 0xFFU);
        return 0;
    }
    if (reg == BQ76952_REG_TS2_TEMP && len == 2U) {
        uint16_t v = mock_temp_raw[m][1];
        buf[0] = (uint8_t)(v & 0xFFU); buf[1] = (uint8_t)((v >> 8U) & 0xFFU);
        return 0;
    }
    if (reg == BQ76952_REG_TS3_TEMP && len == 2U) {
        uint16_t v = mock_temp_raw[m][2];
        buf[0] = (uint8_t)(v & 0xFFU); buf[1] = (uint8_t)((v >> 8U) & 0xFFU);
        return 0;
    }

    /* CC2 current */
    if (reg == BQ76952_REG_CC2_CURRENT && len == 2U) {
        int16_t c = mock_current_ma[m];
        buf[0] = (uint8_t)((uint16_t)c & 0xFFU);
        buf[1] = (uint8_t)(((uint16_t)c >> 8U) & 0xFFU);
        return 0;
    }

    /* Stack voltage */
    if (reg == BQ76952_REG_STACK_VOLTAGE && len == 2U) {
        /* Sum cells 0..13, return in units of 10mV */
        uint32_t sum = 0U;
        uint8_t ci;
        for (ci = 0U; ci < BMS_SE_PER_MODULE; ci++) {
            sum += mock_cell_mv[m][ci];
        }
        uint16_t val10 = (uint16_t)(sum / 10U);
        buf[0] = (uint8_t)(val10 & 0xFFU);
        buf[1] = (uint8_t)((val10 >> 8U) & 0xFFU);
        return 0;
    }

    /* Subcmd data buffer (0x40) — return device number if last subcmd was DEVICE_NUMBER */
    if (reg == BQ76952_REG_SUBCMD_DATA && len == 2U) {
        if (mock_last_subcmd == BQ76952_SUBCMD_DEVICE_NUMBER) {
            buf[0] = (uint8_t)(mock_device_number & 0xFFU);
            buf[1] = (uint8_t)((mock_device_number >> 8U) & 0xFFU);
            return 0;
        }
    }

    /* Default: zero fill */
    memset(buf, 0, len);
    return 0;
}

void hal_gpio_write(bms_gpio_pin_t pin, bool state)
{
    if ((uint8_t)pin < GPIO_PIN_COUNT) {
        mock_gpio_out[pin] = state;
    }
}

bool hal_gpio_read(bms_gpio_pin_t pin)
{
    if ((uint8_t)pin < GPIO_PIN_COUNT) {
        return mock_gpio_in[pin];
    }
    return false;
}

uint16_t hal_adc_read(bms_adc_channel_t channel)
{
    if ((uint8_t)channel < ADC_CHANNEL_COUNT) {
        return mock_adc[channel];
    }
    return 0U;
}

int32_t hal_can_transmit(const bms_can_frame_t *frame)
{
    if (mock_can_tx_count < MOCK_CAN_QUEUE_SIZE) {
        mock_can_tx_queue[mock_can_tx_head] = *frame;
        mock_can_tx_head = (mock_can_tx_head + 1U) % MOCK_CAN_QUEUE_SIZE;
        mock_can_tx_count++;
    }
    return 0;
}

int32_t hal_can_receive(bms_can_frame_t *frame)
{
    if (mock_can_rx_count == 0U) {
        return 1; /* no frame */
    }
    *frame = mock_can_rx_queue[mock_can_rx_head];
    mock_can_rx_head = (mock_can_rx_head + 1U) % MOCK_CAN_QUEUE_SIZE;
    mock_can_rx_count--;
    return 0;
}

uint32_t hal_tick_ms(void)
{
    return mock_tick;
}

void hal_delay_ms(uint32_t ms)
{
    mock_tick += ms;
}

void hal_critical_enter(void) { /* no-op on desktop */ }
void hal_critical_exit(void)  { /* no-op on desktop */ }
void hal_system_reset(void)   { /* no-op on desktop */ }

#endif /* DESKTOP_BUILD */
