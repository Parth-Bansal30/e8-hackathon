/* Sound Sense board support (see ss_board.h). The bring-up fixes come from docs/bringup-notes.md. */
#include "ss_board.h"

#include <stdio.h>

#include "Driver_ADC.h"
#include "Driver_IO.h"
#include "board_config.h"
#include "gpio.h"
#include "se_services_port.h"

static volatile uint32_t g_ms;
/* A J-Link detach (the RTT logger, every host command) clears TRCENA and stops the cycle counter until
 * ss_cycles() re-arms it. Re-arm it every 1 ms here, so a detach during a 50-125 ms full redraw costs at
 * most 1 ms of cycle time instead of the whole draw (IMU times went back by up to 108 ms). */
void SysTick_Handler(void)
{
    g_ms++;
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
uint32_t ss_ms(void) { return g_ms; }

void ss_halt(const char *why)
{
    printf("ERROR: %s\r\n", why);
    while (1) __WFI();
}

void ss_board_init(void)
{
    uint32_t err;
    run_profile_t runp = {0};

    if (board_pins_config() != 0 || board_gpios_config() != 0) {
        ss_halt("board pin/gpio config");   /* gpios also sets the MIPI/LVDS mux */
    }
    if (SysTick_Config(SystemCoreClock / 1000u) != 0) ss_halt("SysTick");
    (void)ss_cycles();

    se_services_port_init();
    if (SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_CLK_100M, true, &err) ||
        SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_HFOSC, true, &err)) {
        ss_halt("MIPI clocks");
    }
    if (SERVICES_get_run_cfg(se_services_s_handle, &runp, &err)) ss_halt("get_run_cfg");
    runp.memory_blocks  |= MRAM_MASK | SRAM0_MASK | SRAM1_MASK;
    runp.phy_pwr_gating |= MIPI_PLL_DPHY_MASK | MIPI_TX_DPHY_MASK | MIPI_RX_DPHY_MASK | LDO_PHY_MASK;
    runp.vdd_ioflex_3V3  = IOFLEX_LEVEL_1V8;   /* I3C (IMUs) on flex I/O P7_6/P7_7 */
    if (SERVICES_set_run_cfg(se_services_s_handle, &runp, &err)) ss_halt("set_run_cfg");
}

/* ---------- joystick ---------- */

extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_JOY_SW_A_GPIO_PORT);
static ARM_DRIVER_GPIO *const joy_drv = &ARM_Driver_GPIO_(BOARD_JOY_SW_A_GPIO_PORT);
static const uint8_t joy_pin[5] = {BOARD_JOY_SW_A_GPIO_PIN, BOARD_JOY_SW_B_GPIO_PIN, BOARD_JOY_SW_C_GPIO_PIN,
                                   BOARD_JOY_SW_D_GPIO_PIN, BOARD_JOY_SW_CENTER_GPIO_PIN};
static uint32_t joy_idle[5], joy_prev;

int ss_joy_init(void)
{
    for (int i = 0; i < 5; i++) {
        if (joy_drv->Initialize(joy_pin[i], NULL) || joy_drv->PowerControl(joy_pin[i], ARM_POWER_FULL) ||
            joy_drv->SetDirection(joy_pin[i], GPIO_PIN_DIRECTION_INPUT)) {
            return -1;
        }
        joy_drv->GetValue(joy_pin[i], &joy_idle[i]);
    }
    joy_prev = 0;
    return 0;
}

/* One read of the port for all five switches: each LPGPIO read crosses into the slow low-power domain
 * (about 130 us, measured by PC sampling), so five driver calls cost five times that. */
uint32_t ss_joy_state(void)
{
    const uint32_t port = ((GPIO_Type *)LPGPIO_BASE)->GPIO_EXT_PORTA;
    uint32_t m = 0;
    for (int i = 0; i < 5; i++) {
        const uint32_t v = (port >> joy_pin[i]) & 1u ? GPIO_PIN_STATE_HIGH : GPIO_PIN_STATE_LOW;
        if (v != joy_idle[i]) m |= 1u << i;
    }
    return m;
}

uint32_t ss_joy_pressed(void)
{
    const uint32_t now = ss_joy_state(), edges = now & ~joy_prev;
    joy_prev = now;
    return edges;
}

/* ---------- LEDs ---------- */

extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_LEDRGB0_R_GPIO_PORT);
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_LEDRGB0_G_GPIO_PORT);
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_LEDRGB0_B_GPIO_PORT);
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_LEDRGB1_R_GPIO_PORT);
static ARM_DRIVER_GPIO *const led_drv[2][3] = {
    {&ARM_Driver_GPIO_(BOARD_LEDRGB0_R_GPIO_PORT), &ARM_Driver_GPIO_(BOARD_LEDRGB0_G_GPIO_PORT),
     &ARM_Driver_GPIO_(BOARD_LEDRGB0_B_GPIO_PORT)},
    {&ARM_Driver_GPIO_(BOARD_LEDRGB1_R_GPIO_PORT), &ARM_Driver_GPIO_(BOARD_LEDRGB1_G_GPIO_PORT),
     &ARM_Driver_GPIO_(BOARD_LEDRGB1_B_GPIO_PORT)},
};
static const uint8_t led_pin[2][3] = {
    {BOARD_LEDRGB0_R_GPIO_PIN, BOARD_LEDRGB0_G_GPIO_PIN, BOARD_LEDRGB0_B_GPIO_PIN},
    {BOARD_LEDRGB1_R_GPIO_PIN, BOARD_LEDRGB1_G_GPIO_PIN, BOARD_LEDRGB1_B_GPIO_PIN},
};
static bool leds_ok;

int ss_led_init(void)
{
    for (int l = 0; l < 2; l++) {
        for (int c = 0; c < 3; c++) {
            ARM_DRIVER_GPIO *d = led_drv[l][c];
            if (d->Initialize(led_pin[l][c], NULL) || d->PowerControl(led_pin[l][c], ARM_POWER_FULL) ||
                d->SetDirection(led_pin[l][c], GPIO_PIN_DIRECTION_OUTPUT)) {
                return -1;
            }
        }
    }
    leds_ok = true;
    ss_led_set(0, 0);
    ss_led_set(1, 0);
    return 0;
}

extern ARM_DRIVER_ADC ARM_Driver_ADC12(BOARD_POTENTIOMETER_ADC12_INSTANCE);
static ARM_DRIVER_ADC *const pot_adc = &ARM_Driver_ADC12(BOARD_POTENTIOMETER_ADC12_INSTANCE);
static volatile int32_t pot_val = -1;
static volatile bool pot_busy;
static bool pot_ok;

static void pot_cb(uint32_t event, uint8_t channel, uint32_t value)
{
    (void)channel;
    if (event & ARM_ADC_EVENT_CONVERSION_COMPLETE) {
        pot_val = (int32_t)value;
        pot_busy = false;
    }
}

int ss_pot_init(void)
{
    if (pot_adc->Initialize(pot_cb) || pot_adc->PowerControl(ARM_POWER_FULL) ||
        pot_adc->Control(ARM_ADC_CONVERSION_MODE_CTRL, ARM_ADC_SINGLE_SHOT_CH_CONV) ||
        pot_adc->Control(ARM_ADC_CHANNEL_INIT_VAL, BOARD_POTENTIOMETER_ADC12_INPUT)) {
        return -1;
    }
    pot_ok = true;
    return 0;
}

int32_t ss_pot_read(void)
{
    if (!pot_ok) return -1;
    if (!pot_busy) {
        pot_adc->Stop();
        pot_busy = true;
        if (pot_adc->Start() != ARM_DRIVER_OK) pot_busy = false;
    }
    return pot_val;
}

extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_LCD_BACKLIGHT_GPIO_PORT);

void ss_backlight(int on)
{
    ARM_Driver_GPIO_(BOARD_LCD_BACKLIGHT_GPIO_PORT).SetValue(BOARD_LCD_BACKLIGHT_GPIO_PIN, on ? GPIO_PIN_OUTPUT_STATE_HIGH
                                                                                         : GPIO_PIN_OUTPUT_STATE_LOW);
}

void ss_led_set(int led, uint32_t colour)
{
    if (!leds_ok || led < 0 || led > 1) return;
    for (int c = 0; c < 3; c++) {
        led_drv[led][c]->SetValue(led_pin[led][c], (colour >> c) & 1u ? GPIO_PIN_OUTPUT_STATE_HIGH
                                                                       : GPIO_PIN_OUTPUT_STATE_LOW);
    }
}
