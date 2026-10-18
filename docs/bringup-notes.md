# DK-E8 bring-up notes

Problems hit while getting ExecuTorch, the display and the peripherals working on the
Alif Ensemble E8 DevKit (DK-E8), and the fix for each. Everything in `firmware/` already
includes these fixes.

## Board and tools

| Symptom | Cause | Fix |
|---|---|---|
| `app-write-mram` refuses to flash | Board is chip revision **A0**; SETOOLS 1.112 defaults to A1 | Set `"Revision": "A0"` in SETOOLS `utils/global-cfg.db` (check with `maintenance -opt getrevision`) |
| SETOOLS `-h` crashes | Non-UTF-8 locale | `export LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8` |
| After a replug the board sits in SEROM recovery (error 0x30, invalid TOC); `app-write-mram` prints "device is in RECOVERY MODE" | Interrupted or invalid MRAM image | `./maintenance` -> ROM -> MRAM Recovery, then flash again. It rewrites the 397 KB system package at 57600 baud: about **130 s**. Don't interrupt it (it is safe to rerun if you do) |
| J-Link: "Found SW-DP ... Failed to power up DAP" | The app stalled the bus, e.g. an OSPI XIP access the memory never answers | Flash a known-good app (`hello_rtt`) to get the debugger back. To find the step, print, then busy-wait a few seconds before the suspect access and read RTT during the wait |
| DWT cycle counts stop, delays never end | J-Link clears `DEMCR.TRCENA` when it detaches, so every `tools/rtt_dump.sh` call stops `CYCCNT` | Set `TRCENA` and `CYCCNTENA` again on every read (see `flash_test`) |
| `JLinkRTTLogger` can't attach | | `tools/rtt_dump.sh` reads the RTT buffer straight from memory |
| RTT buffer size setting ignored | The Arm tutorial's config uses the wrong macro name | Define `BUFFER_SIZE_UP`, not `SEGGER_RTT_BUFFER_SIZE_UP` |
| `CoreDebug` undeclared | Removed in CMSIS 6 | Use `DCB` |

## Display (MIPI DSI, ILI9806E 480x800)

- The DK-E8 routes the panel through a **MIPI/LVDS mux**. Call `board_gpios_config()` after
  `board_pins_config()` and before starting the CDC200, or the backlight comes on but the
  screen stays black.
- Panel variant 1 (the pack default) works. Pixel format RGB888, bytes in **B, G, R** order.
- The frame buffer lives in SRAM0, which is not powered by default (see below).
- Drawing straight into the scanned buffer tears. Draw into a back buffer in SRAM1, then
  `memcpy` and `SCB_CleanDCache()`.

## ExecuTorch on the Ethos-U85

Arm's MNIST tutorial runs but gives the same output for every input. Four fixes were needed:

1. **Power SRAM0 and SRAM1.** They are off at boot and the tutorial's power-up is a no-op, so
   CPU writes to the tensor arena are lost and the NPU reads zeros. Fix:
   `SERVICES_get_run_cfg`, then set `memory_blocks |= MRAM_MASK | SRAM0_MASK | SRAM1_MASK`,
   then `SERVICES_set_run_cfg`.
2. **Cache maintenance.** The Ethos-U driver's weak `ethosu_flush_dcache` and
   `ethosu_invalidate_dcache` are empty. Override them with `SCB_CleanDCache()` and
   `SCB_CleanInvalidateDCache()`.
3. **Address remap.** The NPU can't see the M55's DTCM through local addresses. Override
   `ethosu_address_remap()` to return `LocalToGlobal(address)`.
4. **int8 model IO.** The prebuilt bundle's CPU quantize and dequantize ops misbehave. Export
   with int8 inputs and outputs (`export_mnist.py --int8-io`) and do the (de)quantization in
   the app using the generated `*_io.h` scale and zero-point.

Fixes 2 and 3 are in `firmware/mnist_executorch/executorch_runner.cpp`. Fix 1 is in each
app's `platform_init()`.

## Peripherals

| Peripheral | Where | Notes |
|---|---|---|
| RGB LED0 / LED1 | GPIO12_3, GPIO7_4, GPIO12_0 / GPIO6_2, 6_4, 6_6 | LED0 green is on flex I/O |
| Joystick | LPGPIO15 pins 0..4 | Active low; the app reads the idle level at start |
| User pot | Blue knob **R79** beside the joystick, into ADC121 input 1 (0..1.8 V) | Single-shot conversion |
| IMUs | BMI323 and ICM-42670P on I3C, flex I/O P7_6/P7_7 | Needs `vdd_ioflex_3V3 = IOFLEX_LEVEL_1V8`. Only one IMU driver can be linked (duplicate symbols). RTE_Device.h and board_defs disagree on the INT pins, so poll instead of using interrupts |
| PDM mics | Channels 4/5 (PDM_D2_B, PDM_C2_A) | Clock `CLKEN_HFOSCx2`; filter settings from Alif's `demo_pdm.c`. Its gains (0x0D/0x13) are far too low: speech peaks near -50 dBFS. The gain register is linear (4x = +12 dB, max 0xFFF); 0x800/0xBB0 puts room noise near -53 dBFS |
| Touch | GT911 on I2C1, INT GPIO9_4, RST GPIO6_5 | Raw coordinates equal screen pixels. `GetState` only reports on a new interrupt, so treat 150 ms with no reports as lift-off |
| Camera | Two onsemi IAS flat-flex sockets: Camera 1 = J22 (top, right edge), Camera 2 = J16 (underside, left edge). ARX3A0 driver: I2C1 addr 0x36, power GPIO7_5, reset GPIO9_1, C1/C2 select GPIO14_3 | **Our kit has no camera module.** `cam_test` is ready for one: with nothing plugged in, only 0x1a answers on I2C1 and sensor init returns -1 |

- `Driver_ADC.h` and `Driver_PDM.h` don't compile as C++ (`typedef const struct`), so
  `hw_test` is plain C.
- The ADC needs `sys_ctrl_analog.c`, which no pack component pulls in. A copy is in
  `firmware/hw_test/vendor/`.
- If cbuild fails with "read ...: is a directory", the `firmware/tmp/` cache is corrupt.
  Delete `firmware/tmp` and `firmware/out/<project>`. The first build after that (or after adding a
  project) can fail the same way once; run it again.

## External memory (OSPI)

Tested with `flash_test` and `psram_test`. Both pass on our board (chip revision A0):

| Memory | What is actually fitted | Window | CPU read (D-cache on) | Notes |
|---|---|---|---|---|
| Flash, OSPI1 | ISSI octal NOR, **64 MB** (the pack says 32 MB) | XIP at `0xC0000000`, read-only | 74 MB/s sequential, 469 ns random | Pack 2.0.4 CMSIS driver (`OSPI Flash ISSI`) for erase/program: 4 KB erase 82 ms, program 1.1 MB/s, register-mode read 9 MB/s. Then `setup_flash_xip()` (`OSPI FLASH XIP:core`) for memory-mapped reads |
| RAM, OSPI0 | AP Memory **APS512XXN PSRAM, 64 MB**, not the ISSI HyperRAM the pack 2.0.4 board files describe | XIP at `0xA0000000`, read/write | 107 MB/s sequential, 113 MB/s write, 347 ns random | Needs the PSRAM driver from pack 2.2.1, copied to `firmware/psram_test/vendor/`. Runs x8 (octal) |
| MRAM (reference) | 5.5 MB on-chip | `0x80000000` | 201 MB/s | |

- **Flash reset is P15_7**, not P15_5: pack 2.0.4's `board_defs.h` gives LCD_RESET's pin (fixed in 2.2.x).
  The PSRAM reset is P15_6.
- **The flash must be reset before `setup_flash_xip()`** if the CMSIS driver ran first. The driver leaves
  the flash in octal DDR, but `setup_flash_xip()` reads the ID in single-wire SDR mode and fails otherwise.
- **Pack 2.0.4's HyperRAM path stalls the bus** at the first XIP access on this board, in every
  configuration (ISSI octal, S80K dual-octal; demo values or 2.2.1's RXDS delay 11; with 2.2.1's
  `FRF_DEFINED` transfer type). The RAM never answers HyperBus. Its ID register (MR2 = 0xDE) reads fine over
  Xccela register-mode transfers, which can't stall the bus.
- The PSRAM init is `aps512xxn_psram_init()` with the board settings of Alif's **AppKit-E8**
  (`BOARD_APS512XXN_PSRAM_PRESENT`), which wires it x8 on OSPI0 D0-D7. Our pins.h also muxes OSPI0
  D8-D15, but x16 dual-octal stalls at the first XIP access; not investigated further.
- Pack 2.2.1's driver needs `SOC_FEAT_AES_HAS_ADDR_CTRL_SHIM` and some `RTE_OSPI*` / `RTE_APS512XXN_*`
  values that 2.0.4's headers lack; `psram_test.cproject.yml` defines them. It replaces the pack's OSPI
  component, so don't link both in one project.
- Only the CPU path has been measured. Whether the Ethos-U85 can read weights through these windows is
  still to be tested.
