# agents.md

## Purpose

This repository targets a gripper firmware built around `STM32F446RE`.
The main development flow is:

- build firmware with `PlatformIO`
- upload through `ST-Link`
- observe behavior through `CAN`
- validate sensors and storage with dedicated diagnostic firmware

This file summarizes the current working assumptions for future agent work.

## Available Tools

The agent can use the following capabilities in this environment.

- Local shell execution
  Commands such as `pio`, `candump`, `rg`, `sed`, `git`, and other normal Linux CLI tools are available.
- File editing
  Source, config, and documentation files in this repository can be modified directly.
- Local image inspection
  Existing local image files can be opened and inspected.
- Web access
  Available when needed, but most firmware work here should rely on the local repo and local hardware.

## Hardware Interaction Limits

The agent can interact with hardware only indirectly through software tooling.

- Can build and upload firmware if `ST-Link` is connected.
- Can read `CAN` traffic if the Linux CAN interface is up.
- Can inspect sensor and firmware state only through diagnostic firmware and observed outputs.

The agent cannot physically:

- move the motor by hand
- reposition magnets
- change wiring
- probe signals with instruments

Those actions must be done by the user.

## Current Hardware Setup

- MCU: `STM32F446RE`
- Motor input encoder: `AS5048A`
- Output encoder 1: `AS5600`
- Output encoder 2: `TMAG5170`
- External nonvolatile memory: `FM25CL64B-G` SPI FRAM
- Main external interface: `CAN`
- Upload/debug interface: `ST-Link`

## Sensor Roles

- `AS5048A`
  Reads the motor input shaft angle.
- `AS5600`
  Reads the output shaft angle.
- `TMAG5170`
  Measures 3-axis magnetic field and is being evaluated as an output-angle estimator.

Current working assumption for `TMAG5170`:

- the measured field contains a mixture of output-shaft `1x` content and input-shaft `8x` content
- despite that mixture, software-side LUT reconstruction is viable

## Bus / Peripheral Mapping

Known SPI chip-select assignments:

- `FRAM FM25CL64B-G`: `SPI1_nCS_2`
- `TMAG5170`: `SPI1_nCS_3`
- `AS5048A`: separate SPI chip-select defined in `board_config.h`

Other interface usage:

- `AS5600` uses `I2C`
- pin and board constants are defined in `include/board_config.h`

## Mechanical Context

- The actuator has an `8:1` reduction ratio.
- `AS5048A` reads the input shaft.
- `AS5600` and `TMAG5170` are both output-side encoders.
- `AS5600` and `TMAG5170` observe different magnets and may see opposite rotation directions depending on installation.

## Firmware Layout

Main product firmware:

- `custom_f446re`
- entry point: `src/main.cpp`

Main firmware behavior:

1. initialize SPI, CAN, motor driver, and motor control
2. load stored calibration from FRAM
3. initialize FOC if needed and persist results
4. reset multi-turn estimation from the current motor position
5. read `AS5600` and define output-side zero reference
6. run the control loop using sensor updates and CAN commands

Main firmware uses:

- `SimpleFOC`
- `AS5048A`
- `AS5600`
- `FM25CL64B` FRAM
- `CAN`

## Remaining Diagnostic Firmware

Only the following diagnostic environments are currently kept.

- `fram_test_f446re`
  Validates FRAM read/write behavior and power-cycle retention.
- `tmag_comm_test_f446re`
  Validates TMAG SPI communication and basic register access.
- `tmag_sensor_test_f446re`
  Validates that TMAG produces magnetic field measurement data.
- `tmag_xyz_live_test_f446re`
  Streams live `TMAG5170` `X/Y/Z` and raw values while the user manually moves the mechanism.
- `tmag_lut_angle_test_f446re`
  Evaluates output-angle estimation using `TMAG5170 XYZ -> LUT -> output angle`, referenced to `AS5600`.

Default PlatformIO environment:

- `pio run`
- resolves to `custom_f446re`

## Current TMAG Status

The previous TMAG SPI decode issue has already been fixed.

Current conclusions:

- TMAG raw and scaled data are now being decoded correctly.
- The TMAG internal angle-engine approach was not suitable for the current geometry.
- The software LUT approach is the current preferred direction.

Recent LUT-based validation outcome:

- calibration RMS: about `1.86 deg`
- validation RMS: about `1.39 deg`
- validation MAE: about `0.51 deg`

Working interpretation:

- direct magnetic geometry is imperfect
- but `TMAG5170` still carries enough repeatable information to estimate output angle in software

## Repository Hygiene Rules

- Keep `custom_f446re` building clean with `pio run`.
- Do not leave obsolete experimental test directories enabled in the main build.
- Prefer preserving only diagnostic firmware that still has a clear hardware-validation purpose.
- Treat generated logs and graphs as disposable artifacts unless explicitly requested for retention.

## Practical Workflow

When working on hardware-related tasks in this repo, prefer this order:

1. confirm which firmware environment is relevant
2. build it with `pio run -e <env>`
3. upload it if hardware is connected
4. inspect `CAN` output with `candump`
5. interpret the result before making additional firmware changes

