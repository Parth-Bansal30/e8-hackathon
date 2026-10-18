/* DK-E8 camera bring-up: ARX3A0 over MIPI CSI-2 -> CPI -> RAM -> LCD preview.
 *
 * Bare-metal port of Alif's FreeRTOS ARX3A0 demo. Each init step is shown on the LCD
 * and logged over RTT, so a failure says where it stopped. Once running it captures
 * frames in a loop, keeps the exposure sane with a software gain loop (the ARX3A0
 * driver has no auto-exposure), and shows the centre 480x480 of the 560x560 frame.
 * The log also prints the mean of each 2x2 Bayer position: if they differ on a
 * coloured scene the sensor is colour (RGGB), otherwise mono.
 */
#include "RTE_Components.h"
#include CMSIS_device_header

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Driver_CDC200.h"
#include "Driver_CPI.h"
#include "Driver_I2C.h"
#include "Driver_IO.h"
#include "board_config.h"
#include "se_services_port.h"
#include "sys_ctrl_cpi.h"

#include "assets/fonts.h"

#define LCD_W (RTE_PANEL_HACTIVE_TIME)
#define LCD_H (RTE_PANEL_VACTIVE_LINE)
#define CAM_W (RTE_ARX3A0_CAMERA_SENSOR_FRAME_WIDTH)
#define CAM_H (RTE_ARX3A0_CAMERA_SENSOR_FRAME_HEIGHT)
#define VIEW  480                    /* centre crop shown on the LCD */
#define VIEW_Y 64

static uint8_t fb[LCD_H][LCD_W][3] __attribute__((section(".bss.lcd_frame_buf"), aligned(16)));
static uint8_t back[LCD_H][LCD_W][3] __attribute__((section(".bss.at_sram1"), aligned(16)));
/* RAW8 frame written by the CPI's DMA; 32-byte aligned for cache maintenance */
static uint8_t cam[CAM_H][CAM_W] __attribute__((section(".bss.camera_frame_buf"), aligned(32)));

extern ARM_DRIVER_CDC200 Driver_CDC200;
extern ARM_DRIVER_CPI Driver_CPI;

typedef struct { uint8_t r, g, b; } rgb;
static const rgb kBg     = {16, 20, 32};
static const rgb kHeader = {32, 40, 64};
static const rgb kText   = {235, 238, 245};
static const rgb kMuted  = {140, 150, 170};
static const rgb kGood   = {60, 200, 110};
static const rgb kBad    = {235, 80, 70};

/* ---------- time + drawing ---------- */

static volatile uint32_t g_ms;
void SysTick_Handler(void) { g_ms++; }

static void fill_rect(int x, int y, int w, int h, rgb c)
{
    for (int j = (y < 0 ? 0 : y); j < y + h && j < LCD_H; j++) {
        for (int i = (x < 0 ? 0 : x); i < x + w && i < LCD_W; i++) {
            back[j][i][0] = c.b;
            back[j][i][1] = c.g;
            back[j][i][2] = c.r;
        }
    }
}

static void draw_text(int x, int y, const char *s, rgb c)
{
    const font_t *f = &font_small;
    for (; *s; s++, x += f->w) {
        if (*s < f->first || *s > f->last) continue;
        const uint8_t *g = f->data + (size_t)(*s - f->first) * f->w * f->h;
        for (int j = 0; j < f->h; j++) {
            int py = y + j;
            if (py < 0 || py >= LCD_H) continue;
            for (int i = 0; i < f->w; i++) {
                int px = x + i;
                unsigned a = g[j * f->w + i];
                if (!a || px < 0 || px >= LCD_W) continue;
                uint8_t *d = back[py][px];
                d[0] = (uint8_t)(d[0] + ((int)c.b - d[0]) * (int)a / 255);
                d[1] = (uint8_t)(d[1] + ((int)c.g - d[1]) * (int)a / 255);
                d[2] = (uint8_t)(d[2] + ((int)c.r - d[2]) * (int)a / 255);
            }
        }
    }
}

static void present(void)
{
    memcpy(fb, back, sizeof(fb));
    SCB_CleanDCache();
}

/* Init progress, one line per step, so a hang or failure shows where it stopped. */
static int step_y = 90;
static void step(const char *what, bool ok)
{
    printf("[CAM] %-28s %s\r\n", what, ok ? "ok" : "FAILED");
    draw_text(20, step_y, what, kText);
    draw_text(380, step_y, ok ? "ok" : "FAIL", ok ? kGood : kBad);
    step_y += 34;
    present();
}

static void halt(const char *why)
{
    printf("ERROR: %s\r\n", why);
    while (1) __WFI();
}

/* ---------- platform ---------- */

static void display_callback(uint32_t event)
{
    if (event & ARM_CDC_DSI_ERROR_EVENT) halt("DSI error");
}

static void platform_init(void)
{
    uint32_t err;
    run_profile_t runp = {0};

    if (board_pins_config() != 0 || board_gpios_config() != 0) halt("board pin/gpio config");
    if (SysTick_Config(SystemCoreClock / 1000U) != 0) halt("SysTick");
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    se_services_port_init();
    if (SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_CLK_100M, true, &err) ||
        SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_HFOSC, true, &err)) {
        halt("MIPI clocks");
    }
    if (SERVICES_get_run_cfg(se_services_s_handle, &runp, &err)) halt("get_run_cfg");
    runp.memory_blocks  |= MRAM_MASK | SRAM0_MASK | SRAM1_MASK;
    /* TX D-PHY drives the LCD, RX D-PHY receives the camera */
    runp.phy_pwr_gating |= MIPI_PLL_DPHY_MASK | MIPI_TX_DPHY_MASK | MIPI_RX_DPHY_MASK | LDO_PHY_MASK;
    runp.vdd_ioflex_3V3  = IOFLEX_LEVEL_1V8;   /* camera power enable GPIO7_5 is on flex I/O */
    if (SERVICES_set_run_cfg(se_services_s_handle, &runp, &err)) halt("set_run_cfg");

    fill_rect(0, 0, LCD_W, LCD_H, kBg);
    fill_rect(0, 0, LCD_W, 64, kHeader);
    draw_text(20, 18, "Camera bring-up (ARX3A0)", kText);
    present();

    ARM_DRIVER_CDC200 *cdc = &Driver_CDC200;
    if (cdc->Initialize(display_callback) != ARM_DRIVER_OK || cdc->PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK ||
        cdc->Control(CDC200_CONFIGURE_DISPLAY, (uint32_t)fb) != ARM_DRIVER_OK || cdc->Start() != ARM_DRIVER_OK) {
        halt("display start");
    }
}

/* ---------- I2C probe (diagnostic) ---------- */

/* Powers the sensor the way the ARX3A0 driver does, then scans I2C1 with the camera
 * selector (GPIO14_3) low and high, reading the 16-bit ID at register 0x3000 from every
 * device that answers. Leaves I2C1 and the GPIOs uninitialized for the real driver. */
extern ARM_DRIVER_I2C ARM_Driver_I2C_(BOARD_CAMERA_I2C_INSTANCE);
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(RTE_ARX3A0_CAMERA_SENSOR_POWER_GPIO_PORT);
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(RTE_ARX3A0_CAMERA_SENSOR_RESET_GPIO_PORT);
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_CAMERA_I2C_C1_C2_GPIO_PORT);

static volatile uint32_t i2c_ev;
static void i2c_cb(uint32_t event) { i2c_ev |= event; }

static bool i2c_wait(void)
{
    const uint32_t t0 = g_ms;
    while (!(i2c_ev & ARM_I2C_EVENT_TRANSFER_DONE) && g_ms - t0 < 20) {
    }
    return (i2c_ev & ARM_I2C_EVENT_TRANSFER_DONE) && !(i2c_ev & (ARM_I2C_EVENT_ADDRESS_NACK |
                                                                 ARM_I2C_EVENT_TRANSFER_INCOMPLETE |
                                                                 ARM_I2C_EVENT_BUS_ERROR));
}

static void out_pin(ARM_DRIVER_GPIO *d, uint8_t pin, bool high)
{
    d->Initialize(pin, NULL);
    d->PowerControl(pin, ARM_POWER_FULL);
    d->SetDirection(pin, GPIO_PIN_DIRECTION_OUTPUT);
    d->SetValue(pin, high ? GPIO_PIN_OUTPUT_STATE_HIGH : GPIO_PIN_OUTPUT_STATE_LOW);
}

static void delay_ms(uint32_t ms)
{
    const uint32_t t0 = g_ms;
    while (g_ms - t0 < ms) {
    }
}

static void i2c_probe(void)
{
    ARM_DRIVER_I2C *i2c = &ARM_Driver_I2C_(BOARD_CAMERA_I2C_INSTANCE);
    ARM_DRIVER_GPIO *pwr = &ARM_Driver_GPIO_(RTE_ARX3A0_CAMERA_SENSOR_POWER_GPIO_PORT);
    ARM_DRIVER_GPIO *rst = &ARM_Driver_GPIO_(RTE_ARX3A0_CAMERA_SENSOR_RESET_GPIO_PORT);
    ARM_DRIVER_GPIO *sel = &ARM_Driver_GPIO_(BOARD_CAMERA_I2C_C1_C2_GPIO_PORT);

    set_cpi_pixel_clk(CPI_PIX_CLKSEL_400MZ, RTE_ARX3A0_CAMERA_SENSOR_CSI_CLK_SCR_DIV);  /* sensor XVCLK */
    out_pin(rst, RTE_ARX3A0_CAMERA_SENSOR_RESET_PIN_NO, false);
    out_pin(pwr, RTE_ARX3A0_CAMERA_SENSOR_POWER_PIN_NO, true);
    delay_ms(5);
    out_pin(rst, RTE_ARX3A0_CAMERA_SENSOR_RESET_PIN_NO, true);
    delay_ms(20);

    i2c->Initialize(i2c_cb);
    i2c->PowerControl(ARM_POWER_FULL);
    i2c->Control(ARM_I2C_BUS_SPEED, ARM_I2C_BUS_SPEED_STANDARD);
    char line[64];
    for (int s = 0; s < 2; s++) {
        out_pin(sel, BOARD_CAMERA_I2C_C1_C2_GPIO_PIN, s);
        delay_ms(2);
        int n = snprintf(line, sizeof(line), "I2C sel=%d:", s);
        for (uint32_t a = 0x08; a < 0x78; a++) {
            uint8_t reg[2] = {0x30, 0x00}, id[2] = {0};
            /* Same sequence as Alif's Camera_Sensor_i2c.c: write the register address with STOP,
             * then read. This driver reports TRANSFER_DONE even when a write is NACKed, so only a
             * completed read counts as a device being there. */
            i2c_ev = 0;
            if (i2c->MasterTransmit(a, reg, 2, false) != ARM_DRIVER_OK) continue;
            i2c_wait();
            i2c_ev = 0;
            if (i2c->MasterReceive(a, id, 2, false) != ARM_DRIVER_OK || !i2c_wait()) continue;
            printf("[CAM] sel=%d addr 0x%02lx: reg 0x3000 = 0x%02x%02x\r\n", s, (unsigned long)a, id[0], id[1]);
            if (n < (int)sizeof(line) - 6) n += snprintf(line + n, sizeof(line) - n, " %02lx", (unsigned long)a);
        }
        printf("[CAM] %s\r\n", line);
        draw_text(20, step_y, line, kMuted);
        step_y += 34;
        present();
    }
    out_pin(sel, BOARD_CAMERA_I2C_C1_C2_GPIO_PIN, false);   /* back to the default camera */
    i2c->PowerControl(ARM_POWER_OFF);
    i2c->Uninitialize();
    pwr->Uninitialize(RTE_ARX3A0_CAMERA_SENSOR_POWER_PIN_NO);
    rst->Uninitialize(RTE_ARX3A0_CAMERA_SENSOR_RESET_PIN_NO);
}

/* ---------- camera ---------- */

static volatile uint32_t cam_events;
static void camera_callback(uint32_t event) { cam_events |= event; }

#define CAM_ERRORS (ARM_CPI_EVENT_ERR_CAMERA_INPUT_FIFO_OVERRUN | ARM_CPI_EVENT_ERR_CAMERA_OUTPUT_FIFO_OVERRUN | \
                    ARM_CPI_EVENT_ERR_HARDWARE | ARM_CPI_EVENT_MIPI_CSI2_ERROR)

static bool check(const char *what, int32_t ret)
{
    if (ret != ARM_DRIVER_OK) printf("[CAM] %s returned %ld\r\n", what, (long)ret);
    step(what, ret == ARM_DRIVER_OK);
    return ret == ARM_DRIVER_OK;
}

static bool camera_init(void)
{
    ARM_DRIVER_CPI *cpi = &Driver_CPI;
    return check("CPI driver init", cpi->Initialize(camera_callback)) &&
           /* sensor power/reset GPIOs, XVCLK, I2C1, chip ID check */
           check("sensor power + ID", cpi->PowerControl(ARM_POWER_FULL)) &&
           check("CPI + CSI-2 configure", cpi->Control(CPI_CONFIGURE, 0)) &&
           /* sensor register tables */
           check("ARX3A0 configure", cpi->Control(CPI_CAMERA_SENSOR_CONFIGURE, 0)) &&
           check("CPI events", cpi->Control(CPI_EVENTS_CONFIGURE, ARM_CPI_EVENT_CAMERA_CAPTURE_STOPPED | CAM_ERRORS));
}

/* Capture one frame into cam[]; returns the elapsed ms, or -1 on error/timeout. */
static int capture(uint32_t *events_out)
{
    ARM_DRIVER_CPI *cpi = &Driver_CPI;
    const uint32_t t0 = g_ms;
    cam_events = 0;
    if (cpi->CaptureFrame(cam) != ARM_DRIVER_OK) return -1;
    while (!(cam_events & (ARM_CPI_EVENT_CAMERA_CAPTURE_STOPPED | CAM_ERRORS)) && g_ms - t0 < 1000) {
    }
    cpi->Stop();
    *events_out = cam_events;
    if (!(cam_events & ARM_CPI_EVENT_CAMERA_CAPTURE_STOPPED)) return -1;
    /* the CPI wrote RAM behind the D-cache */
    SCB_InvalidateDCache_by_Addr(cam, sizeof(cam));
    return (int)(g_ms - t0);
}

/* gain is 16.16 fixed point; below 1.0 the driver shortens the integration time instead */
static uint32_t gain = 0x20000;

int main(void)
{
    platform_init();
    printf("[CAM] start, frame %dx%d at %p\r\n", CAM_W, CAM_H, (void *)cam);

    i2c_probe();
    if (!camera_init()) {
        draw_text(20, step_y + 20, "Camera init failed - see RTT log", kBad);
        present();
        halt("camera init");
    }
    Driver_CPI.Control(CPI_CAMERA_SENSOR_GAIN, gain);

    uint32_t frames = 0, errors = 0, last_log = 0, last_fps_t = g_ms, fps_frames = 0;
    unsigned fps10 = 0;
    for (;;) {
        uint32_t ev;
        const int ms = capture(&ev);
        if (ms < 0) {
            errors++;
            printf("[CAM] capture failed, events 0x%lx (errors %lu)\r\n", (unsigned long)ev, (unsigned long)errors);
            continue;
        }
        frames++;
        fps_frames++;

        /* stats: overall mean and the four 2x2 Bayer positions (subsampled) */
        uint32_t sum = 0, bayer[4] = {0}, n4 = 0;
        uint8_t lo = 255, hi = 0;
        for (int y = 0; y < CAM_H; y += 8) {
            for (int x = 0; x < CAM_W; x += 8) {
                const uint8_t *p = &cam[y][x];
                bayer[0] += p[0]; bayer[1] += p[1]; bayer[2] += p[CAM_W]; bayer[3] += p[CAM_W + 1];
                n4++;
                if (p[0] < lo) lo = p[0];
                if (p[0] > hi) hi = p[0];
            }
        }
        sum = bayer[0] + bayer[1] + bayer[2] + bayer[3];
        const uint32_t mean = sum / (4 * n4);

        /* software auto-exposure toward mid-grey */
        if (mean < 90 && gain < 0x400000) {
            gain = gain * 5 / 4;
            Driver_CPI.Control(CPI_CAMERA_SENSOR_GAIN, gain);
        } else if (mean > 150 && gain > 0x1000) {
            gain = gain * 4 / 5;
            Driver_CPI.Control(CPI_CAMERA_SENSOR_GAIN, gain);
        }

        /* centre crop -> grey RGB888 */
        const int ox = (CAM_W - VIEW) / 2, oy = (CAM_H - VIEW) / 2, lx = (LCD_W - VIEW) / 2;
        for (int y = 0; y < VIEW; y++) {
            const uint8_t *src = &cam[oy + y][ox];
            uint8_t *dst = back[VIEW_Y + y][lx];
            for (int x = 0; x < VIEW; x++, dst += 3) dst[0] = dst[1] = dst[2] = src[x];
        }

        if (g_ms - last_fps_t >= 1000) {
            fps10 = fps_frames * 10000U / (g_ms - last_fps_t);
            fps_frames = 0;
            last_fps_t = g_ms;
        }
        char buf[64];
        fill_rect(0, VIEW_Y + VIEW, LCD_W, LCD_H - VIEW_Y - VIEW, kBg);
        snprintf(buf, sizeof(buf), "%u.%u fps   capture %d ms", fps10 / 10, fps10 % 10, ms);
        draw_text(20, VIEW_Y + VIEW + 20, buf, kText);
        snprintf(buf, sizeof(buf), "mean %lu  range %u..%u", (unsigned long)mean, lo, hi);
        draw_text(20, VIEW_Y + VIEW + 56, buf, kMuted);
        snprintf(buf, sizeof(buf), "gain %lu.%02lux  frames %lu  err %lu", (unsigned long)(gain >> 16),
                 (unsigned long)((gain & 0xFFFF) * 100 >> 16), (unsigned long)frames, (unsigned long)errors);
        draw_text(20, VIEW_Y + VIEW + 92, buf, kMuted);
        present();

        if (g_ms - last_log >= 2000) {
            printf("[CAM] t=%lus frames=%lu err=%lu fps=%u.%u cap=%dms mean=%lu range=%u..%u gain=0x%lx "
                   "bayer=%lu/%lu/%lu/%lu\r\n",
                   (unsigned long)(g_ms / 1000), (unsigned long)frames, (unsigned long)errors, fps10 / 10,
                   fps10 % 10, ms, (unsigned long)mean, lo, hi, (unsigned long)gain, (unsigned long)(bayer[0] / n4),
                   (unsigned long)(bayer[1] / n4), (unsigned long)(bayer[2] / n4), (unsigned long)(bayer[3] / n4));
            last_log = g_ms;
        }
    }
}
