/* Sound Sense board support: power-up, timing, joystick and the two RGB LEDs.
 * Plain C, shared by the product app and the test benches (recorder, imu_bench). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "RTE_Components.h"
#include CMSIS_device_header

#ifdef __cplusplus
extern "C" {
#endif

/* Pins and the GPIO mux, SysTick (1 ms), and through the Secure Enclave: the MIPI clocks and the
 * power of SRAM0/1, the MIPI PHYs and the 1.8 V flex I/O the IMUs' I3C bus needs. Halts on failure. */
void ss_board_init(void);

/* Print the reason to RTT and stop. */
void ss_halt(const char *why);

/* DWT cycle counter (400 MHz). J-Link clears TRCENA and CYCCNTENA when it detaches (every
 * tools/rtt_dump.sh call), so every read sets them again. */
static inline uint32_t ss_cycles(void)
{
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    return DWT->CYCCNT;
}
static inline uint32_t ss_cyc_to_us(uint32_t c) { return c / (SystemCoreClock / 1000000u); }

/* Milliseconds since ss_board_init (SysTick). */
uint32_t ss_ms(void);

/* Joystick: 5 switches on LPGPIO15, active low (the idle level is read at init).
 * The bit order follows the board files' names A, B, C, D, centre. */
enum { SS_JOY_A = 1u, SS_JOY_B = 2u, SS_JOY_C = 4u, SS_JOY_D = 8u, SS_JOY_CENTER = 16u };
/* Directions as seen by someone reading the screen (checked at the board, 2026-10-18). The switches
 * are not debounced: one press can register twice. */
enum { SS_JOY_UP = SS_JOY_D, SS_JOY_DOWN = SS_JOY_A, SS_JOY_LEFT = SS_JOY_B, SS_JOY_RIGHT = SS_JOY_C };
int ss_joy_init(void);
uint32_t ss_joy_state(void);     /* switches held now */
uint32_t ss_joy_pressed(void);   /* switches newly pressed since the previous call */

/* Two RGB LEDs, each colour on or off. */
enum { SS_LED_RED = 1u, SS_LED_GREEN = 2u, SS_LED_BLUE = 4u, SS_LED_WHITE = 7u };
int ss_led_init(void);
void ss_led_set(int led, uint32_t colour);   /* led 0 or 1; colour = SS_LED_* bits, 0 = off */

#ifdef __cplusplus
}
#endif
