/* PSRAM test for the DK-E8: the external RAM on OSPI0 is an AP Memory APS512XXN (64 MB), not the ISSI
 * HyperRAM the pack 2.0.4 board files describe. Pack 2.0.4's HyperRAM XIP setup stalls the bus on it at the
 * first access, so this uses the OSPI/PSRAM driver from pack 2.2.1 (vendor/), as on Alif's AppKit-E8.
 *   1. read the mode registers over register-mode (polling) transfers, which cannot stall the bus, and
 *      check the ID;
 *   2. set up PSRAM XIP at 0xA0000000 and check integrity, size and CPU bandwidth.
 * Runs x8 (octal); x16 dual-octal (RTE_APS512XXN_PSRAM_DUAL_OCTAL_MODE_ENABLE 1) stalls at the first XIP
 * access on this board. Results go to RTT: tools/rtt_dump.sh firmware/out/psram_test/E8-HP/debug/psram_test.elf */

#include "RTE_Components.h"
#include CMSIS_device_header
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "Driver_IO.h"
#include "board_config.h"
#include "fault_handler.h"
#include "pinconf.h"
#include "vendor/APS512XXN_PSRAM.h"
#include "vendor/ospi.h"
#include "vendor/ospi_psram_xip.h"

#define RAM_BASE        0xA0000000u     /* OSPI0 XIP window */
#define RAM_PROBE_MAX   (128u << 20)
#define BW_BYTES        (4u << 20)
#define RESET_PORT      15
#define RAM_RESET_PIN   6               /* pins.h: P15_6 "OSPI0_RESET" */

#ifndef XIP_WAIT_CYCLES
#define XIP_WAIT_CYCLES RTE_APS512XXN_PSRAM_WAIT_CYCLES
#endif

extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(RESET_PORT);
static ARM_DRIVER_GPIO *const reset_gpio = &ARM_Driver_GPIO_(RESET_PORT);

static uint8_t buf[256 * 1024];
static volatile uint64_t sink;

/* ---------- timing (J-Link clears TRCENA on detach, so re-arm on use) ---------- */

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
    printf("[RAM] %-30s %4" PRIu32 ".%" PRIu32 " MB/s  (%" PRIu32 " KB in %" PRIu32 " us)\r\n", what,
           mbps10 / 10, mbps10 % 10, bytes / 1024, to_us(cyc));
}

/* ---------- ID probe ---------- */

static uint16_t mode_reg_read(OSPI_Type *ospi, uint32_t addr)
{
    ospi_transfer_t t = {0};
    uint32_t cmd_addr[2] = {APS512XXN_CMD_MODE_REGISTER_READ, addr};
    uint16_t val = 0;
    t.spi_frf      = SPI_FRF_OCTAL;
    t.ddr          = 1;
    t.inst_len     = SPI_INST_L_8_BIT;
    t.addr_len     = SPI_ADDR_L_32_BIT;
    t.dummy_cycle  = APS512XXN_INIT_REG_READ_WAIT_CYCLES - 1;
    t.tx_total_cnt = 2;
    t.tx_buff      = cmd_addr;
    t.rx_total_cnt = 1;
    t.rx_buff      = &val;
    ospi_transfer_polling(ospi, &t);
    return val;
}

/* Runs as ospi_psram_xip_init()'s ram_init hook, after clock, baud and RXDS delay are set up. */
static int32_t probe_then_init(OSPI_Type *ospi, AES_Type *aes)
{
    ospi_transfer_t t = {0};
    uint32_t reset_cmd = APS512XXN_CMD_GLOBAL_RESET;

    ospi_set_dfs(ospi, APS512XXN_OSPI_REG_DFS);
    t.spi_frf      = SPI_FRF_OCTAL;
    t.ddr          = 1;
    t.inst_len     = SPI_INST_L_8_BIT;
    t.addr_len     = SPI_ADDR_L_0_BIT;
    t.dummy_cycle  = APS512XXN_RESET_WAIT_CYCLES;
    t.tx_total_cnt = 1;
    t.tx_buff      = &reset_cmd;
    printf("[RAM] APS global reset...\r\n");
    ospi_send_polling(ospi, &t);
    delay_us(10);

    printf("[RAM] reading mode registers...\r\n");
    uint16_t mr0 = mode_reg_read(ospi, APS512XXN_MODE_REG0_ADDR);
    uint16_t mr1 = mode_reg_read(ospi, APS512XXN_MODE_REG1_ADDR);
    uint16_t mr2 = mode_reg_read(ospi, APS512XXN_MODE_REG2_ADDR);
    printf("[RAM] MR0 %04x  MR1 %04x  MR2 %04x  (APS512XXN ID 0x%02x expected in MR2 high byte)\r\n",
           mr0, mr1, mr2, APS512XXN_ID);
    if ((uint8_t)(mr2 >> 8) != APS512XXN_ID) {
        printf("[RAM] not an APS512XXN\r\n");
        return -1;
    }
    printf("[RAM] APS512XXN found; configuring latency, burst and %s mode\r\n",
           RTE_APS512XXN_PSRAM_DUAL_OCTAL_MODE_ENABLE ? "x16" : "x8");
    return aps512xxn_psram_init(ospi, aes);
}

static ospi_psram_xip_config ram_cfg = {
    .instance       = OSPI_INSTANCE_0,
    .bus_speed      = 100000000,
    .ram_init       = probe_then_init,
    .ddr_drive_edge = 1,
    .rxds_delay     = 11,
    .wait_cycles    = XIP_WAIT_CYCLES,
    .slave_select   = 0,
    .dfs            = 32,
    .spi_frf        = RTE_APS512XXN_PSRAM_DUAL_OCTAL_MODE_ENABLE ? OSPI_SPI_FRF_DUAL_OCTAL : OSPI_SPI_FRF_OCTAL,
    .ram_type       = RAM_TYPE_PSRAM,
    .config_mode    = CONFIG_MODE_USER_PARAMETERS,
    .signal_delay   = 22,
};

/* ---------- RAM tests ---------- */

static uint32_t pattern(uint32_t i, uint32_t seed)
{
    uint32_t x = (i + 1u) * 2654435761u ^ seed;
    return x ^ (x >> 15);
}

static uint32_t find_size(void)
{
    volatile uint32_t *p = (volatile uint32_t *)RAM_BASE;
    const uint32_t marker = 0x600DF00Du;
    p[0] = marker;
    for (uint32_t s = 1u << 20; s < RAM_PROBE_MAX; s <<= 1) {
        p[s / 4] = 0xA5000000u | (s >> 20);
        SCB_CleanInvalidateDCache();
        if (p[0] != marker) return s;     /* the write at offset s wrapped round to offset 0 */
    }
    return RAM_PROBE_MAX;
}

static uint32_t pattern_pass(uint32_t size, uint32_t seed, uint32_t *wr_cyc, uint32_t *rd_cyc)
{
    volatile uint32_t *p = (volatile uint32_t *)RAM_BASE;
    uint32_t words = size / 4, errors = 0;

    uint32_t t0 = cycles();
    for (uint32_t i = 0; i < words; i++) p[i] = pattern(i, seed);
    SCB_CleanInvalidateDCache();
    *wr_cyc = cycles() - t0;

    t0 = cycles();
    for (uint32_t i = 0; i < words; i++) {
        uint32_t got = p[i], want = pattern(i, seed);
        if (got != want && errors++ < 5) {
            printf("[RAM]   error at +0x%08" PRIx32 ": got %08" PRIx32 " want %08" PRIx32 "\r\n", i * 4, got, want);
        }
    }
    *rd_cyc = cycles() - t0;
    return errors;
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

static void test_ram(void)
{
    volatile uint32_t *p = (volatile uint32_t *)RAM_BASE;
    SCB_CleanInvalidateDCache();
    printf("[RAM] first word: %08" PRIx32 "\r\n", p[0]);
    p[0] = 0x12345678u;
    p[1] = 0x9ABCDEF0u;
    SCB_CleanInvalidateDCache();
    printf("[RAM] wrote 12345678 9abcdef0, read %08" PRIx32 " %08" PRIx32 "\r\n", p[0], p[1]);

    uint32_t size = find_size();
    printf("[RAM] detected size: %" PRIu32 " MB%s\r\n", size >> 20,
           size == RAM_PROBE_MAX ? " (no wrap seen up to the probe limit)" : "");

    uint32_t wr, rd, e1, e2;
    e1 = pattern_pass(size, 0x00000000u, &wr, &rd);
    printf("[RAM] pattern pass 1 (%" PRIu32 " MB): %" PRIu32 " errors\r\n", size >> 20, e1);
    print_bw("pattern write (CPU)", size, wr);
    print_bw("pattern verify (CPU)", size, rd);
    e2 = pattern_pass(size, 0xFFFFFFFFu, &wr, &rd);
    printf("[RAM] pattern pass 2 (inverted): %" PRIu32 " errors\r\n", e2);
    printf("[RAM] RESULT: %s\r\n", (e1 | e2) ? "FAIL" : "PASS");

    print_bw("sequential read", BW_BYTES, read_cycles(RAM_BASE, BW_BYTES));
    uint32_t t0 = cycles();
    memcpy(buf, (const void *)RAM_BASE, sizeof(buf));
    print_bw("RAM -> DTCM memcpy", sizeof(buf), cycles() - t0);
    t0 = cycles();
    memcpy((void *)(RAM_BASE + (1u << 20)), buf, sizeof(buf));
    SCB_CleanDCache();
    print_bw("DTCM -> RAM memcpy", sizeof(buf), cycles() - t0);
    printf("[RAM] random 4-byte read: %" PRIu32 " ns\r\n", random_read_ns(RAM_BASE, size, 20000));
}

int main(void)
{
    if (board_pins_config() != 0) while (1) {}
    fault_dump_enable(true);
    (void)cycles();
    printf("\r\n[RAM] OSPI0 PSRAM test (pack 2.2.1 driver), XIP wait cycles %d, %s\r\n", XIP_WAIT_CYCLES,
           RTE_APS512XXN_PSRAM_DUAL_OCTAL_MODE_ENABLE ? "dual octal x16" : "octal x8");

    if (pinconf_set(RESET_PORT, RAM_RESET_PIN, PINMUX_ALTERNATE_FUNCTION_0, 0) ||
        reset_gpio->Initialize(RAM_RESET_PIN, NULL) != ARM_DRIVER_OK ||
        reset_gpio->PowerControl(RAM_RESET_PIN, ARM_POWER_FULL) != ARM_DRIVER_OK ||
        reset_gpio->SetDirection(RAM_RESET_PIN, GPIO_PIN_DIRECTION_OUTPUT) != ARM_DRIVER_OK) {
        printf("[RAM] FAIL: reset GPIO\r\n");
        while (1) __WFE();
    }
    reset_gpio->SetValue(RAM_RESET_PIN, GPIO_PIN_OUTPUT_STATE_LOW);
    delay_us(10);
    reset_gpio->SetValue(RAM_RESET_PIN, GPIO_PIN_OUTPUT_STATE_HIGH);
    delay_us(500);

    if (ospi_psram_xip_init(&ram_cfg) < 0) {
        printf("[RAM] FAIL: ospi_psram_xip_init\r\n");
    } else {
        printf("[RAM] XIP init ok\r\n");
        test_ram();
    }
    printf("[RAM] done\r\n");
    while (1) __WFE();
}
