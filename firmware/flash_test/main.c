/* OSPI flash test for the DK-E8: the 64 MB ISSI octal NOR flash on OSPI1.
 * Erases, programs and verifies two 4 KB sectors with the CMSIS flash driver (one at 31 MB, one 32 MB above
 * it, which also tells a 32 MB part from a 64 MB one), then switches the flash to memory-mapped XIP and
 * checks the same data and the CPU read bandwidth there, with MRAM as the reference.
 * Results go to RTT: tools/rtt_dump.sh firmware/out/flash_test/E8-HP/debug/flash_test.elf
 *
 * Nothing outside the two test sectors is erased or written. */

#include "RTE_Components.h"
#include CMSIS_device_header
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "Driver_Flash.h"
#include "Driver_IO.h"
#include "board_config.h"
#include "fault_handler.h"
#include "pinconf.h"
#include "setup_flash_xip.h"

#define FLASH_XIP_BASE  0xC0000000u     /* OSPI1 XIP window (MPU: normal, read-only, write-through) */
#define MRAM_BASE       0x80000000u
#define FLASH_LO_ADDR   0x01F00000u
#define FLASH_HI_ADDR   (FLASH_LO_ADDR + (32u << 20))
#define SECTOR_BYTES    4096u
#define BW_BYTES        (4u << 20)      /* bandwidth runs: well above the D-cache */

/* pins.h: P15_7 is "OSPI1_RESET". Pack 2.0.4's board_defs.h says P15_5, which is LCD_RESET
 * (fixed in pack 2.2.x). */
#define RESET_PORT      15
#define FLASH_RESET_PIN 7

extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(RESET_PORT);
static ARM_DRIVER_GPIO *const reset_gpio = &ARM_Driver_GPIO_(RESET_PORT);

extern ARM_DRIVER_FLASH ARM_Driver_Flash_(1);
static ARM_DRIVER_FLASH *const flash = &ARM_Driver_Flash_(1);

static uint8_t buf[256 * 1024];         /* DTCM scratch */
static uint8_t expect_lo[SECTOR_BYTES];
static uint8_t expect_hi[SECTOR_BYTES];
static volatile uint64_t sink;

/* ---------- timing ---------- */

/* J-Link clears TRCENA when it detaches (every tools/rtt_dump.sh call), which stops CYCCNT; re-arm on use. */
static inline uint32_t cycles(void)
{
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    return DWT->CYCCNT;
}

static void delay_us(uint32_t us)
{
    uint32_t t0 = cycles(), n = us * (SystemCoreClock / 1000000u);
    while (cycles() - t0 < n) {}
}

static uint32_t to_us(uint32_t cyc) { return (uint32_t)((uint64_t)cyc * 1000000u / SystemCoreClock); }

static void print_bw(const char *what, uint32_t bytes, uint32_t cyc)
{
    if (cyc == 0) cyc = 1;
    uint32_t mbps10 = (uint32_t)((uint64_t)bytes * 10u * SystemCoreClock / cyc / 1000000u);
    printf("[FLASH] %-32s %4" PRIu32 ".%" PRIu32 " MB/s  (%" PRIu32 " KB in %" PRIu32 " us)\r\n", what,
           mbps10 / 10, mbps10 % 10, bytes / 1024, to_us(cyc));
}

/* ---------- helpers ---------- */

static uint32_t pattern(uint32_t i, uint32_t seed)
{
    uint32_t x = (i + 1u) * 2654435761u ^ seed;
    return x ^ (x >> 15);
}

static uint32_t count_diff(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < n; i++) c += a[i] != b[i];
    return c;
}

static uint32_t read_cycles(uintptr_t base, uint32_t bytes)
{
    const volatile uint64_t *p = (const volatile uint64_t *)base;
    uint64_t acc = 0;
    SCB_CleanInvalidateDCache();
    uint32_t t0 = cycles();
    for (uint32_t i = 0; i < bytes / 8; i += 4) acc += p[i] ^ p[i + 1] ^ p[i + 2] ^ p[i + 3];
    uint32_t t = cycles() - t0;
    sink ^= acc;
    return t;
}

/* Average time for independent random 4-byte reads that miss the cache. */
static uint32_t random_read_ns(uintptr_t base, uint32_t span, uint32_t n)
{
    const volatile uint32_t *p = (const volatile uint32_t *)base;
    uint32_t x = 12345u, acc = 0;
    SCB_CleanInvalidateDCache();
    uint32_t t0 = cycles();
    for (uint32_t i = 0; i < n; i++) {
        x = x * 1664525u + 1013904223u;
        acc += p[(x % span) / 4];
    }
    uint32_t t = cycles() - t0;
    sink ^= acc;
    return (uint32_t)((uint64_t)t * 1000000000u / SystemCoreClock / n);
}

static int pulse_reset(void)
{
    if (pinconf_set(RESET_PORT, FLASH_RESET_PIN, PINMUX_ALTERNATE_FUNCTION_0, 0) ||
        reset_gpio->Initialize(FLASH_RESET_PIN, NULL) != ARM_DRIVER_OK ||
        reset_gpio->PowerControl(FLASH_RESET_PIN, ARM_POWER_FULL) != ARM_DRIVER_OK ||
        reset_gpio->SetDirection(FLASH_RESET_PIN, GPIO_PIN_DIRECTION_OUTPUT) != ARM_DRIVER_OK ||
        reset_gpio->SetValue(FLASH_RESET_PIN, GPIO_PIN_OUTPUT_STATE_LOW) != ARM_DRIVER_OK) {
        return -1;
    }
    delay_us(10);
    if (reset_gpio->SetValue(FLASH_RESET_PIN, GPIO_PIN_OUTPUT_STATE_HIGH) != ARM_DRIVER_OK) return -1;
    delay_us(500);
    return 0;
}

/* Erase, program and read back one sector; returns the number of bad bytes, or -1 on a driver error. */
static int32_t sector_test(uint32_t addr, const uint8_t *data, uint32_t width, int report)
{
    uint32_t t0 = cycles();
    if (flash->EraseSector(addr) != ARM_DRIVER_OK) return -1;
    uint32_t erase_cyc = cycles() - t0;
    t0 = cycles();
    if (flash->ProgramData(addr, data, SECTOR_BYTES / width) != (int32_t)(SECTOR_BYTES / width)) return -1;
    uint32_t prog_cyc = cycles() - t0;
    memset(buf, 0, SECTOR_BYTES);
    t0 = cycles();
    flash->ReadData(addr, buf, SECTOR_BYTES / width);
    uint32_t rd_cyc = cycles() - t0;
    if (report) {
        printf("[FLASH] 4 KB sector: erase %" PRIu32 " us, program %" PRIu32 " us, read %" PRIu32 " us\r\n",
               to_us(erase_cyc), to_us(prog_cyc), to_us(rd_cyc));
    }
    return (int32_t)count_diff(buf, data, SECTOR_BYTES);
}

/* ---------- test ---------- */

static void test_flash(void)
{
    if (pulse_reset()) { printf("[FLASH] FAIL: reset GPIO\r\n"); return; }
    if (flash->Initialize(NULL) != ARM_DRIVER_OK || flash->PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK) {
        printf("[FLASH] FAIL: driver init\r\n");
        return;
    }
    ARM_FLASH_INFO *info = flash->GetInfo();
    uint32_t width = 1u << flash->GetCapabilities().data_width;   /* bytes per driver data item */
    printf("[FLASH] driver config: %" PRIu32 " sectors x %" PRIu32 " B = %" PRIu32 " MB\r\n",
           info->sector_count, info->sector_size, info->sector_count * info->sector_size >> 20);

    for (uint32_t i = 0; i < SECTOR_BYTES / 4; i++) {
        ((uint32_t *)expect_lo)[i] = pattern(i, 0x5EC70Fu);
        ((uint32_t *)expect_hi)[i] = pattern(i, 0xB16B00Bu);
    }
    int32_t bad_lo = sector_test(FLASH_LO_ADDR, expect_lo, width, 1);
    int32_t bad_hi = sector_test(FLASH_HI_ADDR, expect_hi, width, 0);
    /* A 32 MB part wraps the high address onto the low sector and overwrites it. */
    flash->ReadData(FLASH_LO_ADDR, buf, SECTOR_BYTES / width);
    uint32_t lo_after = count_diff(buf, expect_lo, SECTOR_BYTES);
    printf("[FLASH] bad bytes: low sector %" PRId32 ", high sector %" PRId32 ", low after high write %" PRIu32
           " -> %s\r\n", bad_lo, bad_hi, lo_after, lo_after ? "32 MB part (wrapped)" : "64 MB part");

    uint32_t t0 = cycles();
    flash->ReadData(0, buf, sizeof(buf) / width);
    print_bw("read (driver, register mode)", sizeof(buf), cycles() - t0);
    printf("[FLASH] RESULT (driver): %s\r\n", (bad_lo == 0 && bad_hi == 0 && lo_after == 0) ? "PASS" : "FAIL");

    /* Memory-mapped mode: how the CPU or NPU would read weights in place. setup_flash_xip() reads the ID in
     * single-wire SDR, but the driver left the flash in octal DDR, so reset it first. */
    flash->PowerControl(ARM_POWER_OFF);
    flash->Uninitialize();
    if (pulse_reset() || setup_flash_xip() != 0) { printf("[FLASH] FAIL: XIP setup\r\n"); return; }

    SCB_CleanInvalidateDCache();
    uint32_t xbad_lo = count_diff((const uint8_t *)(FLASH_XIP_BASE + FLASH_LO_ADDR), expect_lo, SECTOR_BYTES);
    uint32_t xbad_hi = count_diff((const uint8_t *)(FLASH_XIP_BASE + FLASH_HI_ADDR), expect_hi, SECTOR_BYTES);
    printf("[FLASH] XIP read back: %" PRIu32 " / %" PRIu32 " bad bytes (low / high sector)\r\n", xbad_lo, xbad_hi);
    printf("[FLASH] RESULT (XIP): %s\r\n", (xbad_lo | xbad_hi) ? "FAIL" : "PASS");
    print_bw("XIP sequential read", BW_BYTES, read_cycles(FLASH_XIP_BASE, BW_BYTES));
    printf("[FLASH] XIP random 4-byte read: %" PRIu32 " ns\r\n", random_read_ns(FLASH_XIP_BASE, 64u << 20, 20000));
}

int main(void)
{
    if (board_pins_config() != 0) while (1) {}
    fault_dump_enable(true);
    (void)cycles();

    printf("\r\n[FLASH] OSPI1 flash test, core clock %" PRIu32 " MHz, D-cache %s\r\n", SystemCoreClock / 1000000u,
           (SCB->CCR & SCB_CCR_DC_Msk) ? "on" : "off");
    print_bw("MRAM sequential read (reference)", 2u << 20, read_cycles(MRAM_BASE, 2u << 20));
    test_flash();
    printf("[FLASH] done\r\n");
    while (1) __WFE();
}
