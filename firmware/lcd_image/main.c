/* Show a static image on the DK-E8 MIPI LCD (ILI9806E, 480x800).
 *
 * Based on the Alif pack's demo_cdc200.c. Status on the RGB LED:
 *   green = display running, red = a setup step failed.
 */
#include <string.h>

#include "RTE_Components.h"
#include CMSIS_device_header

#include "Driver_IO.h"
#include "Driver_CDC200.h"
#include "board_config.h"
#include "se_services_port.h"

#define _GET_DRIVER_REF(ref, peri, chan) \
    extern ARM_DRIVER_##peri Driver_##peri##chan; \
    static ARM_DRIVER_##peri * ref = &Driver_##peri##chan;
#define GET_DRIVER_REF(ref, peri, chan) _GET_DRIVER_REF(ref, peri, chan)

#if defined(BOARD_RGB_LED_INSTANCE) && (BOARD_RGB_LED_INSTANCE == 0)
    GET_DRIVER_REF(gpio_g, GPIO, BOARD_LEDRGB0_G_GPIO_PORT);
    GET_DRIVER_REF(gpio_r, GPIO, BOARD_LEDRGB0_R_GPIO_PORT);
    #define BOARD_LEDRGB_G_GPIO_PIN BOARD_LEDRGB0_G_GPIO_PIN
    #define BOARD_LEDRGB_R_GPIO_PIN BOARD_LEDRGB0_R_GPIO_PIN
#else
    GET_DRIVER_REF(gpio_g, GPIO, BOARD_LEDRGB1_G_GPIO_PORT);
    GET_DRIVER_REF(gpio_r, GPIO, BOARD_LEDRGB1_R_GPIO_PORT);
    #define BOARD_LEDRGB_G_GPIO_PIN BOARD_LEDRGB1_G_GPIO_PIN
    #define BOARD_LEDRGB_R_GPIO_PIN BOARD_LEDRGB1_R_GPIO_PIN
#endif

#define DIMAGE_X (RTE_PANEL_HACTIVE_TIME)
#define DIMAGE_Y (RTE_PANEL_VACTIVE_LINE)
#if RTE_CDC200_PIXEL_FORMAT == 1
#define PIXEL_BYTES (3)   /* RGB888 */
#else
#error "splash_img.c is RGB888; set RTE_CDC200_PIXEL_FORMAT to 1"
#endif

extern const uint8_t splash_img[DIMAGE_Y * DIMAGE_X * PIXEL_BYTES];

static uint8_t lcd_image[DIMAGE_Y][DIMAGE_X][PIXEL_BYTES]
    __attribute__((section(".bss.lcd_frame_buf")));

extern ARM_DRIVER_CDC200 Driver_CDC200;
static ARM_DRIVER_CDC200 *CDCdrv = &Driver_CDC200;

static void led_init(ARM_DRIVER_GPIO *gpio, uint8_t pin)
{
    gpio->Initialize(pin, NULL);
    gpio->PowerControl(pin, ARM_POWER_FULL);
    gpio->SetDirection(pin, GPIO_PIN_DIRECTION_OUTPUT);
    gpio->SetValue(pin, GPIO_PIN_OUTPUT_STATE_LOW);
}

static void fail(void)
{
    gpio_r->SetValue(BOARD_LEDRGB_R_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_HIGH);
    while (1) __WFI();
}

static void display_callback(uint32_t event)
{
    if (event & ARM_CDC_DSI_ERROR_EVENT) {
        fail();
    }
}

int main(void)
{
    uint32_t service_error_code;
    run_profile_t runp = {0};

    if (board_pins_config() != 0) {
        while (1);
    }
#if BOARD_CONFIGURE_LVDS_MUX
    /* Default GPIO states, including the MIPI/LVDS mux select.
     * Must run before the panel driver drives its reset pin. */
    if (board_gpios_config() != 0) {
        while (1);
    }
#endif
    led_init(gpio_r, BOARD_LEDRGB_R_GPIO_PIN);
    led_init(gpio_g, BOARD_LEDRGB_G_GPIO_PIN);

    se_services_port_init();

    /* MIPI clocks */
    if (SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_CLK_100M, true,
                                     &service_error_code) != SERVICES_REQ_SUCCESS) {
        fail();
    }
    if (SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_HFOSC, true,
                                     &service_error_code) != SERVICES_REQ_SUCCESS) {
        fail();
    }

    /* Power MRAM, SRAM0 (frame buffer) and the MIPI PHYs */
    if (SERVICES_get_run_cfg(se_services_s_handle, &runp, &service_error_code)) {
        fail();
    }
    runp.memory_blocks  = MRAM_MASK | SRAM0_MASK;
    runp.phy_pwr_gating = MIPI_PLL_DPHY_MASK | MIPI_TX_DPHY_MASK | MIPI_RX_DPHY_MASK | LDO_PHY_MASK;
    if (SERVICES_set_run_cfg(se_services_s_handle, &runp, &service_error_code)) {
        fail();
    }

    memcpy(lcd_image, splash_img, sizeof(lcd_image));

    if (CDCdrv->Initialize(display_callback) != ARM_DRIVER_OK ||
        CDCdrv->PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK ||
        CDCdrv->Control(CDC200_CONFIGURE_DISPLAY, (uint32_t) lcd_image) != ARM_DRIVER_OK ||
        CDCdrv->Start() != ARM_DRIVER_OK) {
        fail();
    }

    gpio_g->SetValue(BOARD_LEDRGB_G_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_HIGH);
    while (1) __WFI();
}

// Stubs to suppress missing stdio definitions for nosys
#define TRAP_RET_ZERO  {__BKPT(0); return 0;}
int _close(int val) TRAP_RET_ZERO
int _lseek(int val0, int val1, int val2) TRAP_RET_ZERO
int _read(int val0, char * val1, int val2) TRAP_RET_ZERO
int _write(int val0, char * val1, int val2) TRAP_RET_ZERO
