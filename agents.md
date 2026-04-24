# agents.md

## Purpose

This repository started as a gripper firmware for `STM32F446RE`, but the current refactor target is broader:

- a reusable BLDC actuator firmware core
- board-specific pin mapping decided at compile time
- actuator-specific configuration and calibration stored in external FRAM
- output-shaft control as the main coordinate system
- support for different output encoder implementations (`AS5600` or `TMAG LUT`)

The working flow remains:

- build firmware with `PlatformIO`
- upload through `ST-Link`
- observe behavior through `CAN`
- validate sensors and storage with dedicated diagnostic firmware

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

## Agent Workflow Expectations

- After each meaningful implementation step, update this `agents.md` file with:
  - what was just completed
  - what the next highest-priority task is
- Unless a real user decision is required, do not stop at a checkpoint.
  Continue directly to the next highest-priority task.
- If `CAN` observation is needed and the Linux interface is down, first try to bring up `can0` at `1 Mbps`.
  If privilege is insufficient, record that exact blocker and continue with non-blocked work.

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
  Reads the output shaft angle directly.
  Depending on the actuator profile, it can be:
  - the primary output encoder at runtime
  - the reference encoder used to calibrate `TMAG LUT`
- `TMAG5170`
  Measures 3-axis magnetic field.
  Depending on the actuator profile, it can be:
  - a calibration/validation sensor
  - the primary output encoder through LUT-based software angle reconstruction

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

Main firmware behavior before the refactor was:

1. initialize SPI, CAN, motor driver, and motor control
2. load stored calibration from FRAM
3. initialize FOC if needed and persist results
4. reset multi-turn estimation from the current motor position
5. read `AS5600` and define output-side zero reference
6. run the control loop using sensor updates and CAN commands

Main firmware currently uses:

- `SimpleFOC`
- `AS5048A`
- `AS5600`
- `FM25CL64B` FRAM
- `CAN`

## Refactor Direction

The current architecture direction decided in chat is:

- control reference is `output shaft`
- official CAN targets are `output angle` and `velocity`
- if the actuator profile is `VelocityOnly`, output-axis-referenced velocity control may run without an output absolute encoder
- if the actuator profile is `As5600` or `TmagLut`, output-angle motion requires a valid output encoder/calibration
- board details stay compile-time
- actuator configuration and calibration live in FRAM
- output encoder is selected by actuator profile
- app-specific logic should sit above a reusable actuator core

Current intended split:

- reusable core:
  - motor drive / FOC
  - actuator state machine
  - CAN mode handling
  - output encoder abstraction
  - configuration and calibration persistence
- product-specific app layer:
  - gripper `% open`
  - product-specific limits and semantics

Current output-encoder operating modes clarified by the user:

1. no output absolute encoder, using only output-axis-referenced velocity control
2. `AS5600` as the runtime output encoder
3. `TMAG LUT` as the runtime output encoder

Additional user-confirmed calibration rule:

- `TMAG LUT` runtime mode requires calibration first
- that calibration flow uses `AS5600` as the reference encoder

User-confirmed profile policy:

- the stored runtime mode should be one of:
  - `VelocityOnly`
  - `As5600`
  - `TmagLut`
- `TMAG` calibration success must not auto-promote the runtime profile
  Promotion to `TmagLut` should happen only when the stored profile is explicitly changed
- `AS5600` is required for `TMAG` calibration, but not for `TMAG` runtime after calibration

New direction requested by the user on `2026-04-22`:

- this firmware should no longer be treated as a gripper-specific firmware
- application-facing command/status units should move away from `% open`
- output-angle commands should be represented in `deg`
- output-velocity commands should be represented in `deg/s`
- `VelocityOnly` should remain the "no output encoder, velocity only" mode
- a separate "no output encoder but angle-capable" path is needed for `gear_ratio == 1:1`
  because in that case the input encoder `AS5048A` is also the output encoder
- runtime mode/profile changes should no longer require reflashing helper firmware
- final intended firmware set should be only:
  - one `TMAG` calibration firmware
  - one main firmware

Additional user decisions captured after that request:

- the firmware should become fully actuator-generic rather than gripper-centric
- command/status units should move to:
  - angle: `deg`
  - velocity: `deg/s`
- "gripper compatibility" is not a design priority
- actuator travel should be configured generically through:
  - `output_min_deg`
  - `output_max_deg`
- for a gripper use-case, the user can simply set those min/max limits to the gripper travel range
- for runtime mode changes over CAN, the implementation may choose the internal apply/save behavior pragmatically
- additional clarification:
  - when no separate output encoder is mounted but `gear_ratio == 1:1`,
    the stored zero reference can still be persistent across reboots
    because `AS5048A` is then also the output-angle sensor
  - however this only removes the encoder-hardware problem; multi-turn absolute ambiguity after power loss still depends on travel range and system assumptions

Implementation progress after those decisions:

- the old gripper-centric runtime API has been replaced with an actuator-generic API:
  - new file set:
    - [include/actuator_api.h](/home/gyungminnoh/projects/NoFW/NoFW/include/actuator_api.h)
    - [src/actuator_api.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/actuator_api.cpp)
  - the main command/state units are now:
    - output angle in `deg`
    - output velocity in `deg/s`
- `ActuatorConfig` now stores generic travel limits in:
  - `output_min_deg`
  - `output_max_deg`
- legacy config/calibration bundle loading now migrates older stored records into the new config model
- a new `DirectInput` profile has been added:
  - enum value: `OutputEncoderType::DirectInput`
  - this is the angle-capable `gear_ratio == 1:1` path without a separate output encoder
  - it uses `AS5048A` directly as the output-angle sensor
- new persistent zero storage for the direct-input path has been added:
  - `DirectInputCalibrationData`
  - stored inside `ConfigStore::CalibrationBundle`
- a new runtime output encoder implementation has been added:
  - [include/sensors/output_encoder_direct_input.h](/home/gyungminnoh/projects/NoFW/NoFW/include/sensors/output_encoder_direct_input.h)
  - [src/sensors/output_encoder_direct_input.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/sensors/output_encoder_direct_input.cpp)
- the default legacy profile selection has changed:
  - `gear_ratio <= 1` now defaults to `DirectInput`
  - it no longer defaults to `VelocityOnly`
- CAN command/status payloads are now actuator-generic:
  - output angle command/status:
    - ID base `0x200 / 0x400`
    - payload: `int32` little-endian `mdeg`
  - output velocity command/status:
    - ID base `0x210 / 0x410`
    - payload: `int32` little-endian `mdeg/s`
- main firmware control-mode behavior is now command-driven instead of profile-overloaded:
  - receiving an angle command switches runtime control to `OutputAngle`
  - receiving a velocity command switches runtime control to `OutputVelocity`
  - timeout now:
    - holds current angle in angle mode
    - drives target velocity to zero in velocity mode
- CAN-based persistent profile switching has been added to the main firmware:
  - new command ID base:
    - `0x220 + node_id`
  - payload:
    - `data[0] = OutputEncoderType`
  - the main firmware now:
    - receives the profile command
    - applies the requested profile at runtime
    - saves it into `FRAM`
    - reflects the result on the existing runtime diagnostic frame `0x5F0 + node_id`
- live hardware validation already completed for this new slice:
  - `pio run -e custom_f446re` succeeded
  - `pio run -e custom_f446re -t upload` succeeded
  - `0x407` now emits 4-byte output-angle status in `mdeg`
  - `0x417` now emits 4-byte output-velocity status in `mdeg/s`
  - test command `0x207#50C30000` (`50.000 deg`) changed angle status upward
  - test command `0x217#A0860100` (`100.000 deg/s`) changed velocity status
  - runtime profile switching over CAN was verified on hardware:
    - `0x227#00` switched runtime/stored profile to `VelocityOnly`
    - diagnostic frame changed to `FB 00 00 02 01 00 00 00`
    - `0x227#02` switched runtime/stored profile back to `TmagLut`
    - diagnostic frame returned to `FB 02 02 01 01 01 00 01`
- Documentation has now been rewritten to match the new actuator-generic protocol/model:
  - [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)
  - [docs/can_arch.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_arch.md)
  - [docs/firmware_user_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/firmware_user_guide.md)
  - docs now describe:
    - `deg` / `deg/s` payload units
    - `DirectInput` profile
    - CAN-based runtime profile switching on `0x220 + node_id`
    - the fact that reflashing helper firmware is no longer the intended profile-switch workflow

## Current Refactor Status

The following structural work is already done in the repository.

- Output encoder abstraction added:
  - [include/sensors/output_encoder.h](/home/gyungminnoh/projects/NoFW/NoFW/include/sensors/output_encoder.h)
- `AS5600` runtime implementation added:
  - [include/sensors/output_encoder_as5600.h](/home/gyungminnoh/projects/NoFW/NoFW/include/sensors/output_encoder_as5600.h)
  - [src/sensors/output_encoder_as5600.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/sensors/output_encoder_as5600.cpp)
- `TMAG LUT` runtime implementation skeleton added:
  - [include/sensors/output_encoder_tmag_lut.h](/home/gyungminnoh/projects/NoFW/NoFW/include/sensors/output_encoder_tmag_lut.h)
  - [src/sensors/output_encoder_tmag_lut.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/sensors/output_encoder_tmag_lut.cpp)
- Common `TMAG LUT` estimator extracted from the test firmware:
  - [include/sensors/tmag_lut_estimator.h](/home/gyungminnoh/projects/NoFW/NoFW/include/sensors/tmag_lut_estimator.h)
  - [src/sensors/tmag_lut_estimator.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/sensors/tmag_lut_estimator.cpp)
- Common `TMAG LUT` calibration builder extracted from the test firmware:
  - [include/sensors/tmag_calibration_builder.h](/home/gyungminnoh/projects/NoFW/NoFW/include/sensors/tmag_calibration_builder.h)
  - [src/sensors/tmag_calibration_builder.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/sensors/tmag_calibration_builder.cpp)
- Output encoder selection/ownership moved out of `main.cpp` into a manager:
  - [include/sensors/output_encoder_manager.h](/home/gyungminnoh/projects/NoFW/NoFW/include/sensors/output_encoder_manager.h)
  - [src/sensors/output_encoder_manager.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/sensors/output_encoder_manager.cpp)
- New persistent config/calibration types added:
  - [include/config/actuator_types.h](/home/gyungminnoh/projects/NoFW/NoFW/include/config/actuator_types.h)
  - [include/config/actuator_config.h](/home/gyungminnoh/projects/NoFW/NoFW/include/config/actuator_config.h)
  - [include/config/calibration_data.h](/home/gyungminnoh/projects/NoFW/NoFW/include/config/calibration_data.h)
- New FRAM-backed config store added:
  - [include/storage/config_store.h](/home/gyungminnoh/projects/NoFW/NoFW/include/storage/config_store.h)
  - [src/storage/config_store.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/storage/config_store.cpp)
- Dedicated `TMAG` calibration runner added for hardware-side LUT learning and FRAM persistence:
  - [src/tmag_calibration_runner/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/tmag_calibration_runner/main.cpp)
  - PlatformIO environment: `tmag_calibration_runner_f446re`
- Legacy default actuator-profile helper added:
  - [include/config/actuator_defaults.h](/home/gyungminnoh/projects/NoFW/NoFW/include/config/actuator_defaults.h)
  - [src/config/actuator_defaults.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/config/actuator_defaults.cpp)
- Shared calibration magic/constants extracted:
  - [include/config/calibration_constants.h](/home/gyungminnoh/projects/NoFW/NoFW/include/config/calibration_constants.h)
- `main.cpp` no longer calls `AS5600` low-level read helpers directly.
  It now goes through the output encoder manager.
- Old `bootstrapOutputOffset()` helper was removed because it became redundant.
- Legacy calibration compatibility load/save was pushed down into `ConfigStore`.
  The product firmware no longer keeps a local `CalData` compatibility shim in `main.cpp`.
- Runtime now uses stored `ActuatorConfig` values for more than just persistence.
  `GripperAPI` and `CAN` node-id selection now consume the stored config at runtime.
- Verified builds after this refactor step:
  - `pio run -e custom_f446re`
  - `pio run -e tmag_calibration_runner_f446re`

Important current behavior:

- `custom_f446re` still behaves like the old `AS5600`-based product firmware by default.
- `ActuatorConfig` is now loaded from FRAM if present; otherwise a legacy default config is created and stored.
- Calibration loading now prefers the new bundle format and falls back to the old legacy record at address `0`.
- The legacy calibration record is still written in parallel for compatibility.
- `TMAG LUT` runtime evaluation code and calibration runner now exist in the main firmware tree, but the main product config still defaults to `AS5600`.
- `TMAG` calibration can now be produced into `TmagCalibrationData` bundle format, but it still needs real hardware validation and runtime promotion policy.
- Live hardware validation has now started:
  - `can0` was brought up successfully at `1 Mbps`
  - `tmag_calibration_runner_f446re` was uploaded successfully through `ST-Link`
  - `candump` confirmed the runner is alive on `0x787/0x797/0x7A7/0x7B7`
  - sample budget was increased to `3072`
  - runner-side TMAG calibration ratio was separated from the app travel ratio and pinned to the validated `8:1` hardware path
  - phase transition now uses absolute output-angle magnitude so opposite encoder installation direction does not block calibration
  - a logic bug in `TmagCalibrationBuilder::build()` was fixed so its internal validation pass no longer rejects every calibration attempt
  - the runner now completes `BootDelay(0) -> Calibrating(1) -> Validating(2) -> Done(3)` on real hardware
  - live hardware result from the successful run:
    - calibration sample count about `1490`
    - valid LUT bins `256`
    - calibration RMS about `1.09 deg`
    - validation RMS about `1.12 deg`
    - validation MAE about `0.48 deg`
- FRAM persistence is now verified on hardware:
  - a follow-up `fram_test_f446re` run loaded the stored bundle successfully
  - CAN `0x5D7` reported `bundle_loaded=1`, `tmag.valid=1`, `valid_bin_count=256`
- Runtime encoder selection is now more conservative:
  - if the stored profile requests `TmagLut` but valid `TMAG` calibration is missing, runtime falls back to `AS5600`
  - this keeps the current product firmware on a safe path until explicit promotion policy is finished
- Product firmware now recognizes the three user-selected output mode families in config:
  - `VelocityOnly`
  - `As5600`
  - `TmagLut`
- Legacy/default profile construction is now mode-aware:
  - `GEAR_RATIO <= 1:1` defaults to `VelocityOnly`
  - higher-ratio legacy profiles still default to `As5600`
- Main firmware calibration requirements are now mode-aware:
  - `VelocityOnly` requires only valid `FOC` calibration
  - `As5600` requires valid `FOC` plus valid `AS5600` calibration
  - `TmagLut` requires valid `FOC` plus valid `TMAG` calibration
- Main firmware boot reference handling is now mode-aware:
  - encoder-backed modes align boot reference from the active output encoder
  - `VelocityOnly` profiles fall back to a motor-based boot reference and do not require an output absolute read
- Manual zero handling now skips encoder-zero writes when the active profile does not use an output absolute encoder
- Main firmware now has an explicit runtime profile-switch path in the existing button maintenance flow:
  - enter maintenance/manual-zero mode with the existing long press
  - while in that mode, medium press cycles the stored runtime profile across selectable modes
  - profile switching is explicit and persisted in `ActuatorConfig`
  - `TMAG` is only selectable when valid `TMAG` calibration is already stored
  - long press while already in maintenance mode exits that mode
- `VelocityOnly` control semantics are now wired into the product firmware:
  - the existing CAN command frame is interpreted as signed percent in `VelocityOnly`
  - timeout policy for `VelocityOnly` now commands zero speed instead of hold-position
  - the existing CAN status frame reports signed output-velocity percent in `VelocityOnly`
  - encoder-backed profiles keep the previous `% open` semantics
- Added a tiny FRAM-backed actuator-config utility firmware for hardware-side profile changes without button interaction:
  - PlatformIO environments:
    - `actuator_config_velocityonly_f446re`
    - `actuator_config_as5600_f446re`
    - `actuator_config_tmag_f446re`
  - entry point:
    - [src/actuator_config_tool/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/actuator_config_tool/main.cpp)
  - it writes the selected runtime profile into `ActuatorConfig` and reports before/after state on CAN `0x5E0 + node_id`
- Verified `VelocityOnly` profile write on hardware:
  - uploaded `actuator_config_velocityonly_f446re`
  - observed CAN `0x5E7` frames like `FA 01 01 01 00 02 01 00`
  - this confirmed:
    - stored config was loaded successfully
    - config save succeeded
    - previous profile was `As5600(1)`
    - new profile is `VelocityOnly(0)`
    - default control mode became `OutputVelocity(2)`
- Fixed a build regression after adding the config utility:
  - `custom_f446re` initially started linking `src/actuator_config_tool/main.cpp`
  - this caused duplicate `setup()` / `loop()` definitions
  - `platformio.ini` now excludes `actuator_config_tool/` from the main firmware build filter
- Hardware-validated initial `VelocityOnly` runtime semantics in the main firmware:
  - uploaded `custom_f446re` with the stored `VelocityOnly` profile
  - observed `0x407` status frames carrying signed values rather than clamped `0..100%` position
  - controlled CAN test on `0x207` showed interval-average status shifting by command polarity:
    - idle average about `-0.72%`
    - `+50%` command window average about `+4.06%`
    - `-50%` command window average about `-5.47%`
    - stop window average returned near zero
- Added dedicated velocity CAN IDs while preserving the legacy mirror for compatibility:
  - command: `0x210 + node_id`
  - status: `0x410 + node_id`
  - in `VelocityOnly`, firmware now accepts both:
    - legacy gripper command ID `0x200 + node_id`
    - dedicated velocity command ID `0x210 + node_id`
  - in `VelocityOnly`, firmware now emits both:
    - legacy mirror on `0x400 + node_id`
    - dedicated velocity status on `0x410 + node_id`
  - encoder-backed profiles keep using only the gripper IDs
- Verified dedicated velocity CAN IDs on hardware:
  - sent commands on `0x217`
  - observed periodic signed velocity reports on `0x417`
  - controlled test interval averages were:
    - idle about `+0.45%`
    - `+50%` command window about `+4.87%`
    - `-50%` command window about `-2.88%`
    - stop window about `+0.06%`
  - also confirmed the legacy mirror remains active:
    - `0x407` and `0x417` were emitting matching payloads during `VelocityOnly`
- Verified explicit `TmagLut` profile selection on hardware without using the button flow:
  - added and uploaded `actuator_config_tmag_f446re`
  - observed CAN `0x5E7` frames like `FA 01 01 00 02 01 01 01`
  - this confirmed:
    - previous profile was `VelocityOnly(0)`
    - new profile is `TmagLut(2)`
    - default control mode returned to `OutputAngle(1)`
    - both output-angle and velocity capabilities are enabled in the stored config
- Verified main firmware boot behavior after storing `TmagLut`:
  - uploaded `custom_f446re`
  - observed periodic `0x407` traffic again with `0x0000` payload at boot
  - observed no `0x417` traffic
  - this is consistent with leaving `VelocityOnly` and returning to encoder-backed `% open` reporting semantics
- Added a runtime diagnostic frame to the main firmware on CAN `0x5F0 + node_id`:
  - payload marker: `0xFB`
  - reports:
    - stored `output_encoder_type`
    - actual active encoder type after fallback selection
    - default control mode
    - enabled capability bits
    - `g_need_calibration`
    - whether an output encoder is required
- Verified active runtime encoder selection on hardware in `TmagLut` mode:
  - observed `0x5F7` frames like `FB 02 02 01 01 01 00 01`
  - this confirmed:
    - stored profile is `TmagLut(2)`
    - active runtime encoder is also `TmagLut(2)`
    - no runtime fallback to `As5600` occurred in this boot
    - calibration was not currently blocking motion
- Verified encoder-backed position semantics while `TmagLut` was active:
  - with `0x207` commanding `50.00%`, `0x407` moved off boot zero
  - controlled capture showed:
    - idle average about `0.00%`
    - command window average about `5.12%`
    - peak observed about `9.30%`
  - after returning command to `0%`, `0x407` began decaying back toward the open end
- Updated repo documentation to match current firmware behavior:
  - [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)
  - [docs/can_arch.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_arch.md)
  - docs now describe:
    - encoder-backed vs `VelocityOnly` CAN semantics
    - dedicated velocity IDs `0x210/0x410 + node_id`
    - legacy mirror behavior in `VelocityOnly`
    - runtime diagnostic frame `0x5F0 + node_id`
- Added a first end-user-facing manual for people unfamiliar with the firmware:
  - [docs/firmware_user_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/firmware_user_guide.md)
  - it covers:
    - what the firmware does
    - the three output-encoder profiles
    - recommended upload / CAN validation workflow
    - profile switching by config firmware and by button
    - `TMAG LUT` calibration flow
    - practical CAN examples and troubleshooting
- Verified builds after the latest runtime-selection change:
  - `pio run -e custom_f446re`
  - `pio run -e fram_test_f446re`
  - `pio run -e tmag_calibration_runner_f446re`
  - `pio run -e actuator_config_velocityonly_f446re`
  - `pio run -e actuator_config_as5600_f446re`
  - `pio run -e actuator_config_tmag_f446re`
  - `pio run -e custom_f446re -t upload`

## Current TMAG Status

The previous TMAG SPI decode issue has already been fixed.

Current conclusions from prior hardware work:

- TMAG raw and scaled data are decoded correctly.
- The TMAG internal angle-engine approach was not suitable for the current geometry.
- The software LUT approach is the preferred direction.
- The field likely contains mixed output-shaft `1x` and input-shaft harmonic content.
- Despite that mixture, the LUT-based estimator was accurate enough in testing to justify integration work.

Recent validated LUT result from the dedicated test firmware:

- RMS error about `0.59 deg`
- MAE about `0.50 deg`
- max absolute error about `1.24 deg`
- the previous branch-jump outlier was removed by restricting candidates using the input-shaft angle and gear ratio

Latest implementation step completed:

- removed the reflashing-based profile-change helper workflow from the repo:
  - deleted the obsolete helper source:
    - [src/actuator_config_tool/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/actuator_config_tool/main.cpp)
  - removed the obsolete PlatformIO environments:
    - `actuator_config_velocityonly_f446re`
    - `actuator_config_as5600_f446re`
    - `actuator_config_tmag_f446re`
- cleaned the remaining `TMAG` calibration runner comment that still referenced old gripper semantics
- updated the `TMAG` output-encoder report so it now reflects:
  - the actuator-generic firmware direction
  - CAN/FRAM-based runtime profile switching
  - the added `DirectInput` path for `gear_ratio == 1:1`
- verified the final intended firmware pair still builds clean after that cleanup:
  - `pio run -e custom_f446re`
  - `pio run -e tmag_calibration_runner_f446re`
- re-uploaded `custom_f446re` to hardware and re-verified the cleaned runtime path:
  - `can0` confirmed `UP` at `1 Mbps`
  - upload through `ST-Link` succeeded
  - periodic runtime frames were present after boot:
    - `0x5F7` diag: `FB02020101010001`
    - `0x407` output-angle status
    - `0x417` output-velocity status
  - the board is currently still running stored/active `TmagLut(2)`
- issued small runtime commands on the new actuator-generic channels while `TmagLut` was active:
  - angle command:
    - `cansend can0 207#30750000` = `30.000 deg`
  - velocity command:
    - `cansend can0 217#60EA0000` = `60.000 deg/s`
  - stop:
    - `cansend can0 217#00000000`
  - observed response summary:
    - idle: angle about `0.681 deg`, velocity about `-0.097 deg/s`
    - after `30 deg` angle command: angle status rose to about `1.131 deg`
    - during `60 deg/s` velocity command: velocity status shifted positive and angle continued rising to about `1.413 deg`
    - after stop: velocity returned near zero while angle settled around `1.43 deg`
  - this is not yet a wide-range tracking validation, but it confirms the `deg` / `deg/s` runtime channels are still alive on hardware after the repo cleanup
- found and fixed a larger architectural gap in the main runtime path:
  - angle-mode outer-loop feedback and CAN `0x407/0x417` status had still been derived from motor-shaft multi-turn estimation,
    not from the active output encoder
  - added `OutputEncoderManager::read(OutputAngleSample&)`
  - updated runtime control/status to use active output-encoder feedback when available
  - `TMAG LUT` and `DirectInput` runtime `read()` paths now apply stored `zero_offset_rad`
  - `readAbsoluteAngleRad()` remains the raw-absolute path for boot-reference and zero-capture flows
- verified the output-encoder-feedback patch on hardware:
  - `pio run -e custom_f446re` succeeded
  - `pio run -e custom_f446re -t upload` succeeded
  - after upload, `0x407` no longer behaved like the previous motor-derived near-static output estimate
  - it now reports full absolute-angle movement/wrap consistent with active output-encoder reads
- new hardware finding after that fix:
  - with stored/active profile still `TmagLut(2)`, idle `0x407` output-angle status continuously swept through a large part of the `0..360 deg` range
    while `0x417` stayed near zero most of the time
  - this suggests the current `TMAG LUT` runtime estimator is not stable at rest in the main firmware path,
    and that previous "quiet" status frames had been masking the problem because they were motor-derived
- attempted to compare against `As5600` by sending profile-switch command `0x227#01`,
  but the runtime diagnostic frame remained `FB02020101010001`
  - likely interpretation:
    - `As5600` is not currently selectable on this stored board state
    - the main firmware stayed in `TmagLut`

## Remaining Work

The highest-priority remaining tasks are now:

Latest implementation step in progress:

- user requested a control-policy change:
  - output-shaft encoders should be used only at boot / zero-capture time to establish the `0 deg` reference
  - runtime FOC control and CAN angle/velocity status should go back to motor-side multi-turn estimation
  - in other words:
    - boot reference: output encoder may be used
    - manual zero / calibration capture: output encoder may be used
    - runtime closed-loop control: use input-encoder multi-turn path
- code has been updated accordingly:
  - removed the just-added runtime `OutputEncoderManager::read(...)` path from the main control/status flow
  - `CanService::poll(...)` now again derives angle/velocity status from motor multi-turn state
  - `main.cpp` again uses motor multi-turn output estimation for the position outer loop
  - output encoders remain in the boot-reference and zero-offset capture paths only
- verified this reverted runtime policy:
  - `pio run -e custom_f446re` succeeded
  - `pio run -e custom_f446re -t upload` succeeded
  - runtime diagnostic frame remained:
    - `0x5F7`: `FB02020101010001`
  - idle CAN capture no longer showed the previous output-encoder-driven full-circle sweep
  - `0x407` and `0x417` are again derived from motor-side multi-turn estimation
  - observed idle sample range after upload:
    - `0x407` around `43.7 deg -> 42.6 deg` over the short capture window
    - `0x417` remained near zero with small noise only

The highest-priority remaining tasks are now:

Latest implementation step completed:

- updated the user-facing docs to match the clarified control policy:
  - output encoders are used only for boot zero alignment and explicit zero capture
  - runtime FOC control and CAN `0x407/0x417` status are based on motor-side `AS5048A` multi-turn estimation
- updated:
  - [docs/firmware_user_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/firmware_user_guide.md)
  - [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)
  - [docs/can_arch.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_arch.md)
- ran direct hardware validation on the clarified runtime policy and wrote a report:
  - report:
    - [docs/runtime_validation_report_2026-04-22.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/runtime_validation_report_2026-04-22.md)
  - identified and fixed two issues during validation:
    - boot was auto-commanding `0 deg` instead of holding the current aligned position
    - `0x417` velocity status was under-reported because the internal velocity sample state was updated even when `millis()` had not advanced
  - final validation scope:
    - boot consistency across two uploads after settle time
    - idle stability
    - streamed angle command response at `20 Hz`
    - streamed velocity command response at `20 Hz`
  - final key results:
    - boot 1 and boot 2 angle status matched at about `38.672 deg`
    - idle angle stayed stable within about `0.001 deg`
    - angle command direction followed the streamed command sequence correctly
    - streamed `60 deg/s` velocity command produced about `3.5 .. 3.6 deg/s` measured output velocity
    - reported `0x417` velocity now matches angle-derived velocity closely
  - final assessment in the report:
    - runtime behavior now passes for correctness under the clarified policy
    - remaining issue is performance / tuning rather than protocol correctness

The highest-priority remaining tasks are now:

1. Tune motor-side performance limits and gains so commanded output speed is closer to the requested setpoint.

Latest implementation step completed:

- user clarified the intended policy:
  - velocity control should not care about angle limits
  - only the angle-control path should use additional braking near configured output-angle limits
- main firmware was updated accordingly:
  - velocity mode no longer applies the software angle-limit clamp
  - angle mode now owns the edge-braking behavior
- after a subsequent regression attempt, the user reported another bench PSU OCP event that likely happened during an overly aggressive stop
- in response, the angle-mode edge braking was softened further:
  - [include/board_config.h](/home/gyungminnoh/projects/NoFW/NoFW/include/board_config.h)
  - [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp)
  - new dedicated constant:
    - `ACTUATOR_OUTPUT_EDGE_BRAKE_DEG_S2 = 60.0f`
  - this replaces the earlier edge-braking behavior that implicitly reused a much larger outer-loop acceleration budget
- uploaded the softer angle-braking firmware successfully:
  - `pio run -e custom_f446re -t upload`
  - programming finished
  - verify OK
  - target reset completed
- documented this follow-up change in:
  - [docs/runtime_validation_report_2026-04-22_ratio_corrected.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/runtime_validation_report_2026-04-22_ratio_corrected.md)

- tuned the main firmware motor velocity loop modestly:
  - [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp)
  - updated:
    - `motor.PID_velocity.P = 0.12`
    - `motor.PID_velocity.I = 0.4`
- reran a settled-window velocity sweep after the status-sampling fix and gain update:
  - positive direction:
    - `30 deg/s` settled average: about `30.125 deg/s`
    - `40 deg/s` settled average: about `40.048 deg/s`
    - `60 deg/s` settled average: about `59.985 deg/s`
    - `100 deg/s` settled average: about `99.583 deg/s`
  - `+150 / +200 deg/s` initially looked bad, but that run was taken while already near the positive travel edge
    - angle stopped near `2305 deg`
    - velocity collapsed toward zero
    - interpretation:
      - this was travel-limit clamp behavior, not immediate evidence of high-speed loop failure
  - negative direction from the high-angle region:
    - `-150 deg/s` settled average: about `-149.134 deg/s`
    - `-200 deg/s` settled average: about `-197.477 deg/s`
  - conclusion:
    - with enough travel margin, the current firmware tracks accurately through at least `|200 deg/s|`
- explicitly characterized positive edge-clamp behavior with a long `+200 deg/s` run:
  - mid command window:
    - velocity average about `197.827 deg/s`
  - late command window:
    - velocity average dropped to about `117.340 deg/s`
    - last reported velocity in that window dropped to about `32.466 deg/s`
  - post window:
    - angle settled near `2412 deg`
    - velocity decayed near zero
  - interpretation:
    - the soft travel clamp is working
    - but the current implementation ramps commanded velocity down toward zero through the slew limiter rather than braking sharply
    - so extra overshoot near the travel edge is expected
- updated the corrected-ratio runtime report with:
  - the gain change
  - settled-window sweep results
  - positive edge-clamp behavior
  - explicit note that the earlier positive `150 / 200 deg/s` collapse was travel-limit clamp contamination
  - file:
    - [docs/runtime_validation_report_2026-04-22_ratio_corrected.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/runtime_validation_report_2026-04-22_ratio_corrected.md)

- improved runtime velocity-status quality in [src/can_service.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/can_service.cpp):
  - `0x417` velocity is now derived at the CAN status transmit interval
  - it is no longer based on very short loop-to-loop deltas
- verified after upload with a longer `10 deg/s` controlled test:
  - command-window full velocity average: about `7.845 deg/s`
  - settled tail velocity average: about `9.410 deg/s`
  - settled tail range: about `7.471 .. 10.767 deg/s`
  - command-window angle increased from about `13.348 deg` to about `42.374 deg`
  - this is a substantial improvement over the earlier noisy velocity-status result
- updated the corrected-ratio runtime report with the new result:
  - [docs/runtime_validation_report_2026-04-22_ratio_corrected.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/runtime_validation_report_2026-04-22_ratio_corrected.md)

- fixed a newly discovered arm-time reference bug:
  - after the first boot-safe arm/disarm implementation, simply sending `arm` still shifted the reported output angle by about `12 deg`
  - root cause:
    - [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp) re-ran `refreshBootReference()` inside `armPowerStage()`
  - fix:
    - removed that re-alignment from the arm path
  - verified after upload:
    - pre-arm angle average: about `63.974 deg`
    - armed angle average: about `63.977 deg`
    - post-disarm angle average: about `63.975 deg`
    - arm/disarm no longer shifts output reference
- re-ran controlled low-risk motion checks with the corrected `8:1` firmware:
  - small angle-step test:
    - initial status: `63.979 deg`
    - commanded target: `73.979 deg`
    - post-window average: about `74.190 deg`
    - result:
      - angle command sign and magnitude now look consistent with the corrected ratio
  - conservative velocity test:
    - streamed `10 deg/s`
    - angle increased from about `74.188 deg` to about `80.184 deg` during the command window
    - reported velocity average during the command window: about `4.331 deg/s`
    - result:
      - positive velocity command now produces positive motion
      - but achieved speed is still below the requested setpoint and velocity status remains noisy
- widened the controlled validation sweep after the velocity-status fix:
  - `+20 deg` angle-step test:
    - initial status: `45.948 deg`
    - commanded target: `65.948 deg`
    - post-window average: about `66.350 deg`
  - `20 deg/s` velocity test:
    - settled tail velocity average: about `19.347 deg/s`
    - settled tail angle increased from about `85.529 deg` to about `127.183 deg`
  - result:
    - the corrected-ratio firmware now looks coherent not only at the smallest setpoints but also at the current moderate validation point
- wrote a corrected-ratio runtime validation report:
  - [docs/runtime_validation_report_2026-04-22_ratio_corrected.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/runtime_validation_report_2026-04-22_ratio_corrected.md)

- added a conservative slew limit for CAN `OutputVelocity` commands:
  - [include/board_config.h](/home/gyungminnoh/projects/NoFW/NoFW/include/board_config.h)
  - [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp)
  - new default:
    - `ACTUATOR_OUTPUT_VELOCITY_SLEW_DEG_S2 = 90.0f`
  - effect:
    - large velocity step commands are now ramped on the output side before conversion into motor-side velocity demand
    - this reduces the risk of repeating the previous high-current step event when velocity mode is armed and commanded
- extended runtime diagnostic reporting so CAN now exposes power-stage armed state:
  - `0x5F0 + node_id`, byte `data[7]`
    - bit0 = output feedback required
    - bit1 = power stage armed
- updated docs for:
  - power-stage arm/disarm command
  - runtime diagnostic bit layout
  - velocity-command slew behavior
  - files:
    - [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)
    - [docs/firmware_user_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/firmware_user_guide.md)
    - [docs/can_arch.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_arch.md)
    - [docs/power_stage_boot_safety_report_2026-04-22.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/power_stage_boot_safety_report_2026-04-22.md)
- verified after these changes:
  - `pio run -e custom_f446re` succeeded
  - `pio run -e custom_f446re -t upload` succeeded
  - post-upload CAN observation without any arm command showed:
    - `0x5F7 = FB02020101010001`
    - interpreted `data[7] = 0x01`
    - therefore:
      - output feedback required = `1`
      - power stage armed = `0`
  - this is the first direct CAN-side confirmation that the uploaded firmware is not auto-arming the power stage at boot
  - explicit arm/disarm sequence was also verified over CAN:
    - sent:
      - `cansend can0 237#01`
      - `cansend can0 237#00`
    - observed runtime diag sequence:
      - `FB02020101010001` -> armed `0`
      - `FB02020101010003` -> armed `1`
      - `FB02020101010001` -> armed `0`
    - this confirms the new power-stage arm state transition is working as designed
  - recorded the result in:
    - [docs/power_stage_boot_safety_report_2026-04-22.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/power_stage_boot_safety_report_2026-04-22.md)

- successfully uploaded the new boot-safe main firmware after target power was restored:
  - command:
    - `pio run -e custom_f446re -t upload`
  - result:
    - programming finished
    - verify OK
    - target reset completed
- current expected behavior of the uploaded firmware:
  - board boot initializes config/sensors/CAN
  - power stage remains `disarmed` by default
  - `PIN_EN_GATE` should stay low until explicit CAN arm command `0x230 + node_id`
  - default node `7`:
    - arm: `cansend can0 237#01`
    - disarm: `cansend can0 237#00`

- identified the likely reason the same over-current behavior could still appear on simple board power-up:
  - the firmware documentation already said "`CAN enable` is required"
  - but the actual main firmware still enabled `PIN_EN_GATE` and ran `initFOC()` automatically during boot
  - so board power-up could still raise the power stage even before any explicit user command
- implemented a boot-safe power-stage arming path in the main firmware:
  - [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp)
  - [src/can_service.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/can_service.cpp)
  - [src/can_protocol.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/can_protocol.cpp)
  - [include/can_service.h](/home/gyungminnoh/projects/NoFW/NoFW/include/can_service.h)
  - [include/can_protocol.h](/home/gyungminnoh/projects/NoFW/NoFW/include/can_protocol.h)
  - [include/board_config.h](/home/gyungminnoh/projects/NoFW/NoFW/include/board_config.h)
- new behavior:
  - boot initializes sensors, FRAM, CAN, config, and output-reference state
  - boot leaves the power stage `disarmed`
  - `PIN_EN_GATE` stays low by default
  - `motor.init()` / `initFOC()` run only after an explicit CAN power-stage arm command
  - new arm/disarm CAN command:
    - `0x230 + node_id`
    - default node `7` -> `0x237`
    - `0x237#01` = arm
    - `0x237#00` = disarm
- updated docs for the new arm/disarm requirement:
  - [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)
  - [docs/firmware_user_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/firmware_user_guide.md)
  - [docs/can_arch.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_arch.md)
- verified after the change:
  - `pio run -e custom_f446re` succeeded
- attempted to upload the new boot-safe firmware, but the upload was blocked by target power state rather than build correctness:
  - `pio run -e custom_f446re -t upload`
  - failed with:
    - `Error: target voltage may be too low for reliable debugging`
    - `Error: init mode failed (unable to connect to the target)`

- re-uploaded the main firmware only, per the current hardware safety constraint:
  - command run:
    - `pio run -e custom_f446re -t upload`
  - result:
    - programming finished
    - verify OK
    - target reset completed
- this upload-only session was intentionally done without runtime motion validation because the user reported the previous firmware state could trip the power supply OCP on board power-up
- current safety constraint for the next step:
  - board can be powered for flashing
  - motor three-phase leads remain disconnected during this session
  - no motion/FOC behavior should be interpreted until the hardware is reconnected for a controlled validation run

- corrected the live gear-ratio source of truth in firmware:
  - [include/board_config.h](/home/gyungminnoh/projects/NoFW/NoFW/include/board_config.h) now sets `GEAR_RATIO = 8.0f`
  - removed the stale `CONFIRMED` wording that had been attached to the wrong `240.0f` value
- added a targeted migration path for stale stored defaults:
  - [include/config/actuator_defaults.h](/home/gyungminnoh/projects/NoFW/NoFW/include/config/actuator_defaults.h)
  - [src/config/actuator_defaults.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/config/actuator_defaults.cpp)
  - if FRAM still contains the known stale default fingerprint
    - `gear_ratio = 240.0f`
    - `output_min_deg = 0.0f`
    - `output_max_deg = 72.0f`
  - firmware now migrates it to the current build defaults instead of continuing to run with the stale ratio
- wired that migration into both firmware entry paths that load actuator config:
  - main firmware:
    - [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp)
  - TMAG calibration runner:
    - [src/tmag_calibration_runner/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/tmag_calibration_runner/main.cpp)
- marked the existing runtime validation report as ratio-contaminated historical data:
  - [docs/runtime_validation_report_2026-04-22.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/runtime_validation_report_2026-04-22.md)
  - absolute `deg` / `deg/s` magnitudes from that run are now explicitly documented as not trustworthy because the firmware still used the stale ratio during capture
- verified after these fixes:
  - `pio run -e custom_f446re` succeeded

- audited the repo for additional code/documentation conflicts after the user corrected the real hardware ratio to `8:1`
- additional conflicts found:
  - stale "confirmed" label in [include/board_config.h](/home/gyungminnoh/projects/NoFW/NoFW/include/board_config.h):
    - it still says `Gear ratio (CONFIRMED)` while the value is the stale `240.0f`
  - wrong derived travel range source:
    - `ACTUATOR_OUTPUT_MAX_RAD` and `ACTUATOR_OUTPUT_MAX_DEG` are derived directly from the stale compile-time ratio
    - so any default travel based on those values is also wrong until the ratio source is fixed
  - stale validation report interpretation:
    - [docs/runtime_validation_report_2026-04-22.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/runtime_validation_report_2026-04-22.md) used firmware output values produced under the stale ratio
    - directionality / protocol-path observations may still be useful
    - absolute output angle and output speed magnitudes from that report are not trustworthy as real-world `deg` / `deg/s` conclusions
  - stale historical notes inside this file:
    - some older `agents.md` entries still mention `% open`, gripper semantics, or `VelocityOnly` percent semantics that no longer match the current code path
    - one old note says `GEAR_RATIO <= 1:1` defaults to `VelocityOnly`, but current code in [src/config/actuator_defaults.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/config/actuator_defaults.cpp) defaults `gear_ratio <= 1` to `DirectInput`
    - some old notes mention legacy mirror CAN behavior in `VelocityOnly`, but current code in [src/can_service.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/can_service.cpp) and [src/can_protocol.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/can_protocol.cpp) now handles only the actuator-generic `deg` / `deg/s` command/status IDs
- no new contradiction was found in the current primary documentation set for:
  - CAN bitrate / node / IDs
  - current `deg` / `deg/s` payload units
  - timeout behavior
  - output-encoder usage policy
  - runtime status being based on motor-side multi-turn estimation
- conclusion from the audit:
  - the main live conflict is still the stale ratio source of truth
  - the next most important cleanup is to mark the existing validation report as ratio-contaminated and regenerate it after the ratio fix

- discovered a hard contradiction in stored project assumptions:
  - [include/board_config.h](/home/gyungminnoh/projects/NoFW/NoFW/include/board_config.h) still defines `GEAR_RATIO = 240.0f`
  - this is what the firmware build currently uses for command/status scaling and limit conversion
  - but the user has now explicitly clarified the real hardware ratio is `8:1`
  - [agents.md](/home/gyungminnoh/projects/NoFW/NoFW/agents.md) itself also already described the actuator as `8:1`
- this means the current firmware math, recent speed-limit reasoning, and parts of the validation interpretation are using the wrong mechanical ratio
- root cause of the mistake:
  - the implementation path trusted the compile-time constant in `board_config.h`
  - the existing contradiction between code and project notes was not reconciled before continuing with tuning and hardware validation
- this mismatch must be corrected before any further runtime tuning conclusions are trusted

- started the first performance-tuning pass by raising the motor-side motion limits and wiring them through all three clamp points:
  - [include/board_config.h](/home/gyungminnoh/projects/NoFW/NoFW/include/board_config.h)
  - [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp)
  - [src/actuator_api.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/actuator_api.cpp)
- introduced:
  - `ACTUATOR_MOTOR_VELOCITY_LIMIT_RAD_S = 320.0f`
  - `ACTUATOR_MOTOR_ACCEL_LIMIT_RAD_S2 = 2000.0f`
- applied those limits consistently to:
  - `motor.velocity_limit`
  - outer position-loop `pvc.vel_limit`
  - outer position-loop `pvc.accel_limit`
  - output-velocity CAN clamp conversion inside `ActuatorAPI`
- during the first high-speed streamed velocity validation, the bench power supply entered over-current protection and the session was stopped before a full log was captured
- initial suspicion was that velocity mode travel protection was missing, but the user clarified there is no mechanical output end-stop in the current setup
- the corrected most likely root cause is now:
  - the first tuning pass raised the motor-side velocity limit from `20 rad/s` to `320 rad/s`
  - with `GEAR_RATIO = 240`, a commanded `60 deg/s` output velocity becomes about `251 rad/s` on the motor side
  - `OutputVelocity` mode applies that step directly, without any slew limiting
  - the motor is running `MotionControlType::velocity` with `TorqueControlType::voltage`
  - so the velocity PID can immediately demand a large phase voltage at low speed / near stall
  - because there is no current limiting in the present configuration, that transient can draw enough supply current to trip the bench power supply OCP even without any mechanical end-stop
- added a firmware-side safety fix in [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp):
  - velocity mode now also clamps outward motion at `output_min_deg/output_max_deg`
  - once the current estimated output angle is already at or beyond a travel edge, any further command in the outward direction is forced to `0 deg/s`
- verified after this fix:
  - `pio run -e custom_f446re` succeeded

The highest-priority remaining tasks are now:

1. Re-validate the softened angle-mode edge braking with a controlled edge-approach test before doing any more aggressive regression runs.
2. Keep the boot-safe arm/disarm behavior, corrected ratio, arm-no-jump behavior, stabilized `0x417` status path, settled-window sweep, and edge-clamp characterization as regression checks.
3. Only resume further performance tuning if operation beyond the currently validated `|200 deg/s|` range is required or if different edge behavior is desired.

Latest implementation step completed:

- rechecked the current main-loop policy in [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp):
  - `OutputVelocity` mode now bypasses output-angle-limit braking entirely
  - output encoders are used only for boot/alignment and explicit zero capture
  - runtime FOC and CAN status continue to use motor-side `AS5048A` multi-turn estimation
  - angle-control mode alone applies the softer edge-braking cap:
    - `ACTUATOR_OUTPUT_EDGE_BRAKE_DEG_S2 = 60.0f`
- recorded the latest conservative post-softening validation result in
  [docs/runtime_validation_report_2026-04-22_ratio_corrected.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/runtime_validation_report_2026-04-22_ratio_corrected.md):
  - start angle before test: about `82.260 deg`
  - commanded target: `0 deg`
  - command-window angle moved from about `82.241 deg` to about `-0.497 deg`
  - post-window average settled around `-0.060 deg`
  - command-window average velocity was about `-48.670 deg/s`
  - no new PSU OCP event was observed during this conservative run
- cleaned up the corrected-ratio runtime report so it now distinguishes:
  - historical positive-edge clamp behavior from an earlier revision
  - current firmware policy where velocity mode ignores angle-limit braking
  - the still-open need for broader angle-mode edge-braking validation

The highest-priority remaining tasks are now:

1. Re-validate the softened angle-mode edge braking from multiple starting distances near both configured travel limits, starting conservatively.
2. If OCP can still be reproduced in angle mode, replace or further limit the current braking strategy rather than increasing aggressiveness.
3. Keep the current boot-safe arm/disarm behavior, corrected ratio, arm-no-jump behavior, stabilized `0x417` status path, and settled-window velocity sweep as regression checks.

Latest implementation step completed:

- added one more safety layer to the current `angle mode` stopping path:
  - [include/board_config.h](/home/gyungminnoh/projects/NoFW/NoFW/include/board_config.h)
  - [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp)
  - new constant:
    - `ACTUATOR_OUTPUT_ANGLE_MODE_SLEW_DEG_S2 = 60.0f`
- reason for the change:
  - the outer position loop already has an acceleration limit
  - but the later edge-braking cap could still reduce the final velocity command abruptly near the travel edges
  - that abrupt final-stage deceleration could create strong regenerative braking and contribute to PSU OCP
- implementation effect:
  - `OutputVelocity` mode is unchanged and still uses only its own velocity-command slew path
  - `OutputAngle` mode now applies:
    - outer position-loop shaping
    - edge-braking speed cap
    - final angle-mode velocity slew limiting before `motor.move(...)`
  - this reduces sudden command reversals and deceleration spikes in the angle-control path
- verification:
  - `pio run -e custom_f446re` succeeded after the change
- documentation updated:
  - [docs/firmware_user_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/firmware_user_guide.md)
  - [docs/can_arch.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_arch.md)

The highest-priority remaining tasks are now:

1. Upload this safer angle-mode braking revision and re-validate near both configured travel limits, starting with conservative angle-to-zero checks.
2. If OCP can still be reproduced in angle mode, lower or redesign the final angle-mode braking/slew strategy instead of making the stop sharper.
3. Keep the current boot-safe arm/disarm behavior, corrected ratio, arm-no-jump behavior, stabilized `0x417` status path, and settled-window velocity sweep as regression checks.

Latest implementation step completed:

- uploaded the safer angle-mode braking revision successfully:
  - `pio run -e custom_f446re -t upload`
  - programming finished
  - verify OK
  - target reset completed
- confirmed the board came back on CAN after upload:
  - `can0` is up at `1 Mbps`
  - observed runtime diag:
    - `0x5F7 = FB 02 02 01 01 01 00 01`
  - interpretation:
    - stored profile = `2` (`TmagLut`)
    - active profile = `2` (`TmagLut`)
    - velocity mode enabled = `1`
    - angle mode enabled = `1`
    - calibration required = `0`
    - power stage armed bit = `0`
- this confirms the new revision is running on the board and still boots in the expected disarmed state after reset

The highest-priority remaining tasks are now:

1. Re-validate the uploaded safer angle-mode braking revision near both configured travel limits, starting with conservative angle-to-zero checks.
2. If OCP can still be reproduced in angle mode, lower or redesign the final angle-mode braking/slew strategy instead of making the stop sharper.
3. Keep the current boot-safe arm/disarm behavior, corrected ratio, arm-no-jump behavior, stabilized `0x417` status path, and settled-window velocity sweep as regression checks.

Latest implementation step completed:

- performed live lower-edge validation on the uploaded safer angle-mode braking revision with motor power applied
- observed pre-test steady state:
  - runtime diag stayed `0x5F7 = FB 02 02 01 01 01 00 01`
  - power stage armed bit remained `0` until explicit arm
  - output angle was stable near `165.2 deg`
- ran three conservative `OutputAngle -> 0 deg` sequences over CAN with explicit arm/disarm:
  1. first approach:
     - about `165.234 deg -> 45.181 deg`
     - no PSU OCP event
  2. second approach:
     - about `45.187 deg ->` minimum about `-3.315 deg`
     - post-disarm settle about `3.41 deg`
     - no PSU OCP event
  3. final near-edge trim:
     - about `3.422 deg ->` minimum about `-0.113 deg`
     - post-disarm settle about `-0.09 deg`
     - no PSU OCP event
- result:
  - the new angle-mode-only final slew limiter appears to have improved lower-edge settling without reproducing the earlier reported PSU OCP
- updated the runtime validation report with these new lower-edge results:
  - [docs/runtime_validation_report_2026-04-22_ratio_corrected.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/runtime_validation_report_2026-04-22_ratio_corrected.md)

The highest-priority remaining tasks are now:

1. Re-validate the uploaded safer angle-mode braking revision near the positive configured travel limit.
2. If OCP can still be reproduced in angle mode, lower or redesign the final angle-mode braking/slew strategy instead of making the stop sharper.
3. Keep the current boot-safe arm/disarm behavior, corrected ratio, arm-no-jump behavior, stabilized `0x417` status path, and settled-window velocity sweep as regression checks.

Latest implementation step completed:

- completed conservative positive-edge validation on the uploaded safer angle-mode braking revision
- method:
  - started from the lower-edge region after the earlier `0 deg` settling checks
  - streamed a large positive angle command:
    - `0x207#404B4C00` (`5000.000 deg`)
  - this lets firmware clamp internally to the stored `output_max_deg`
  - used explicit arm/disarm around the run
- observed result:
  - start angle was about `-0.08 deg`
  - angle rose continuously through the full travel
  - peak observed angle was about `2173.9 deg`
  - post-disarm settle remained near `2173.9 deg`
  - no PSU OCP event was observed
  - runtime diag returned to disarmed state after the explicit disarm command
- combined result of the latest live validation session:
  - lower-edge approach now settles to about `-0.09 deg` without PSU OCP
  - positive-edge approach now reaches and settles near about `2173.9 deg` without PSU OCP
- updated the runtime validation report with both the lower-edge and positive-edge results:
  - [docs/runtime_validation_report_2026-04-22_ratio_corrected.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/runtime_validation_report_2026-04-22_ratio_corrected.md)

The highest-priority remaining tasks are now:

1. If tighter edge stopping is still required, tune or redesign the current angle-mode braking policy based on overshoot/settling targets rather than making it more aggressive blindly.
2. Keep the current boot-safe arm/disarm behavior, corrected ratio, arm-no-jump behavior, stabilized `0x417` status path, and settled-window velocity sweep as regression checks.
3. If needed later, compare upper-edge and lower-edge overshoot/settling symmetry with shorter-distance edge-approach tests.

Latest implementation step completed:

- updated the end-user firmware guide:
  - [docs/firmware_user_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/firmware_user_guide.md)
- key documentation improvements:
  - clarified that all application-facing commands are output-axis `deg` / `deg/s`
  - clarified that angle commands are clamped to stored `output_min_deg/output_max_deg`
  - added a safer first-motion sequence using:
    - `0x237` arm
    - a small `0x207` angle command
    - `0x237` disarm
  - clarified the current mode policy:
    - velocity mode uses its own slew limit but no angle-edge braking
    - angle mode uses edge braking plus a final slew-limited settle path
  - clarified that stored/active profile means boot-time output-reference path selection, not runtime FOC feedback source
  - added troubleshooting for:
    - powered but still disarmed boards
    - large angle commands being clamped by stored travel limits
  - added a short bench-validated behavior summary:
    - boot-safe disarmed startup
    - explicit arm/disarm
    - lower-edge settling near `-0.09 deg`
    - positive-edge clamp/settle near about `2173.9 deg` on the current test unit

The highest-priority remaining tasks are now:

1. Keep user-facing docs aligned with the implemented CAN/profiling behavior as firmware changes continue.
2. Keep the current boot-safe arm/disarm behavior, corrected ratio, arm-no-jump behavior, stabilized `0x417` status path, and settled-window velocity sweep as regression checks.
3. If future control-policy changes are made, update both the user guide and runtime validation report in the same step.

Latest implementation step completed:

- created a dedicated manual test checklist for user-driven bench testing:
  - [docs/manual_can_test_checklist.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/manual_can_test_checklist.md)
- the new checklist covers:
  - `can0` bring-up and runtime diag confirmation
  - profile switching over `0x227`
  - arm/disarm over `0x237`
  - safe first-motion testing with a small angle command
  - manual angle and velocity command examples
  - large positive angle command testing to observe internal travel-limit clamp
  - profile-specific expectations and failure cases
  - quick emergency-disarm guidance

The highest-priority remaining tasks are now:

1. Keep the manual test checklist and user guide aligned with the implemented CAN/profiling behavior as firmware changes continue.
2. Keep the current boot-safe arm/disarm behavior, corrected ratio, arm-no-jump behavior, stabilized `0x417` status path, and settled-window velocity sweep as regression checks.
3. If future control-policy changes are made, update the manual checklist, user guide, and runtime validation report in the same step.

Latest implementation step completed:

- committed the current actuator-firmware refactor and documentation set locally:
  - commit: `1c0385c`
  - message: `Refactor actuator firmware and add validation docs`
- pushed the commit to GitHub:
  - remote: `origin`
  - branch: `main`
- verified that local `HEAD` and `origin/main` now match:
  - `1c0385ce61d01f4f858ea67f3834d97d8a4ebf0d`

The highest-priority remaining tasks are now:

1. Continue keeping the manual checklist, user guide, and validation reports aligned with firmware behavior as new changes are made.
2. Keep the current boot-safe arm/disarm behavior, corrected ratio, arm-no-jump behavior, stabilized `0x417` status path, and settled-window velocity sweep as regression checks.
3. If future control-policy changes are made, update the implementation-facing docs and user-facing docs in the same step.

Latest implementation step completed:

- added a dedicated integration guide for upper-layer controllers such as `Jetson`:
  - [docs/host_controller_integration_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/host_controller_integration_guide.md)
- the new document focuses on what the upper layer must know:
  - boot-safe `disarmed` startup and explicit `arm` requirement
  - distinction between profile selection and runtime feedback/status source
  - angle-command clamp against stored travel limits
  - different semantics of `velocity mode` vs `angle mode`
  - `100 ms` timeout behavior and recommended command-stream rate
  - required interpretation of `0x407`, `0x417`, `0x5F7`
  - lack of separate ack for profile changes
  - recommended upper-layer state machine and startup procedure
  - explicit warning that `0x5F7 data[3]` is `default_control_mode`, not current active control mode

The highest-priority remaining tasks are now:

1. Keep the upper-layer integration guide, manual checklist, and user guide aligned with firmware behavior as new changes are made.
2. Keep the current boot-safe arm/disarm behavior, corrected ratio, arm-no-jump behavior, stabilized `0x417` status path, and settled-window velocity sweep as regression checks.
3. If future control-policy changes are made, update the integration-facing docs and user-facing docs in the same step.

Latest implementation step completed:

- created a local web UI MVP for CAN-based manual testing:
  - server:
    - [tools/can_ui/server.py](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/server.py)
  - frontend:
    - [tools/can_ui/static/index.html](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/static/index.html)
    - [tools/can_ui/static/app.js](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/static/app.js)
    - [tools/can_ui/static/styles.css](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/static/styles.css)
- UI scope:
  - session selection (`can_iface`, `node_id`)
  - runtime diag decoding
  - profile switching
  - power-stage arm/disarm
  - angle / velocity command entry
  - latched command streaming at about `20 Hz`
  - hold-current / zero-velocity helpers
  - optional raw frame send panel
  - event log
- documented the UI MVP in:
  - [docs/can_test_web_ui_mvp.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_test_web_ui_mvp.md)
- linked the UI from the user-facing docs:
  - [docs/firmware_user_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/firmware_user_guide.md)
  - [docs/manual_can_test_checklist.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/manual_can_test_checklist.md)
- verification completed:
  - `python3 -m py_compile tools/can_ui/server.py` passed
  - `python3 tools/can_ui/server.py --help` passed
  - started the server locally on port `8876`
  - confirmed `/api/state` returned live decoded angle / velocity / diag values from `can0`
  - test server process was then terminated

The highest-priority remaining tasks are now:

1. Run an end-to-end manual test through the new web UI and refine any usability issues in the control flow.
2. Keep the upper-layer integration guide, manual checklist, and user guide aligned with firmware behavior as new changes are made.
3. If future control-policy changes are made, update the UI behavior and related docs in the same step.

Latest implementation step completed:

- improved the CAN web UI so users can visually distinguish clickable vs non-clickable actions and current pressed/selected state
- updated:
  - [tools/can_ui/static/app.js](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/static/app.js)
  - [tools/can_ui/static/styles.css](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/static/styles.css)
  - [docs/can_test_web_ui_mvp.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_test_web_ui_mvp.md)
- UI behavior changes:
  - `Arm` disabled when already armed
  - `Disarm` disabled when already disarmed
  - angle buttons disabled when current profile does not allow angle mode
  - velocity buttons disabled when current profile does not allow velocity mode
  - profile button for the currently active profile disabled and shown as selected
  - current latched stream mode shown as selected on the corresponding command button
  - preset buttons shown as selected when their value matches the current input
  - buttons now expose disabled reasons through `title`
- verification:
  - backend still passes `python3 -m py_compile tools/can_ui/server.py`
  - server still starts and `/api/state` returns live decoded CAN status after the frontend-only change

The highest-priority remaining tasks are now:

1. Run an end-to-end manual test through the new web UI and refine any remaining usability issues in the control flow.
2. Keep the upper-layer integration guide, manual checklist, and user guide aligned with firmware behavior as new changes are made.
3. If future control-policy changes are made, update the UI behavior and related docs in the same step.

Latest implementation step completed:

- cleaned up duplicate local CAN UI server instances that had been left running on ports `8876` and `8877`
- relaunched a single canonical server instance on the documented port:
  - `127.0.0.1:8765`
- verified live backend state on the relaunched instance:
  - `/api/state` returned `link_alive: true`
  - session matched `can0`, `node_id = 7`
  - live angle / velocity / diag fields were populated
- current user-facing rule is now:
  - use `http://127.0.0.1:8765`
  - if the browser still does not show `LINK ALIVE`, first confirm the page is connected to that exact port and the session is `can0` / `7`

The highest-priority remaining tasks are now:

1. Run an end-to-end manual test through the single canonical web UI instance on port `8765` and refine any remaining usability issues in the control flow.
2. Keep the upper-layer integration guide, manual checklist, and user guide aligned with firmware behavior as new changes are made.
3. If future control-policy changes are made, update the UI behavior and related docs in the same step.

Latest implementation step completed:

- launched the CAN web UI server directly:
  - `python3 tools/can_ui/server.py --host 127.0.0.1 --port 8765 --can-iface can0 --node-id 7`
- verified the physical board is connected through the UI backend:
  - `can0` is `UP`, `ERROR-ACTIVE`, `1 Mbps`
  - raw `candump` showed live `0x407`, `0x417`, and `0x5F7` frames
  - `http://127.0.0.1:8765/api/state` returned:
    - `link_alive: true`
    - `angle_deg` populated
    - `velocity_deg_s` populated
    - `diag.raw_hex = FB02020101010001`
    - stored/active profile = `TmagLut`
    - `armed = false`
- current UI server is running on:
  - `http://127.0.0.1:8765`

The highest-priority remaining tasks are now:

1. Use the running web UI to perform an end-to-end manual control test and refine any remaining usability issues in the flow.
2. Keep the upper-layer integration guide, manual checklist, and user guide aligned with firmware behavior as new changes are made.
3. If future control-policy changes are made, update the UI behavior and related docs in the same step.

Latest implementation step completed:

- diagnosed why the browser showed `Link = UNKNOWN` even though CAN traffic was present
- root cause:
  - frontend JavaScript syntax errors in [tools/can_ui/static/app.js](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/static/app.js)
  - because the script failed to execute, the HTML stayed at its initial `UNKNOWN` text
- fixes applied:
  - corrected the broken `addEventListener(...)` callback parentheses in `app.js`
  - added `?v=2` cache-busting to the static script/style references in
    [tools/can_ui/static/index.html](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/static/index.html)
  - added `Cache-Control: no-store` to JSON/static responses in
    [tools/can_ui/server.py](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/server.py)
- verification:
  - `node --check tools/can_ui/static/app.js` passed
  - `python3 -m py_compile tools/can_ui/server.py` passed
  - restarted the UI server on `http://127.0.0.1:8765`
  - confirmed the served HTML now references `app.js?v=2`
  - `/api/state` returns `link_alive: true` with live decoded board state

The highest-priority remaining tasks are now:

1. Reload the browser against `http://127.0.0.1:8765` and run an end-to-end manual control test through the fixed web UI.
2. Keep the upper-layer integration guide, manual checklist, and user guide aligned with firmware behavior as new changes are made.
3. If future control-policy changes are made, update the UI behavior and related docs in the same step.

Latest implementation step completed:

- fixed unreliable entry into `As5600` profile in the main firmware
- root cause:
  - `As5600` profile selection required an already-valid stored `AS5600` calibration
  - this prevented first-time entry even when the physical `AS5600` sensor was readable
- firmware behavior now:
  - when `As5600` profile is requested, the firmware first tries to read the physical `AS5600`
  - if no stored `AS5600` zero exists and the sensor read succeeds, the current `AS5600` absolute angle is saved as the first `0 deg` reference
  - existing stored `AS5600` zero is not overwritten by profile switching
  - if the `AS5600` read fails, profile selection still fails and stored/active profile does not change
- uploaded the fixed firmware with:
  - `pio run -e custom_f446re -t upload`
- hardware/CAN validation completed:
  - stopped the UI latched velocity stream before testing
  - confirmed `can0` is `UP`, `ERROR-ACTIVE`, `1 Mbps`
  - confirmed board link through `0x5F7`
  - sent direct CAN profile command `cansend can0 227#01`
  - observed `0x5F7#FB01010101010001`
  - decoded result:
    - stored profile = `As5600`
    - active profile = `As5600`
    - default control mode = `OutputAngle`
    - velocity mode enabled = `true`
    - output angle mode enabled = `true`
    - output feedback required = `true`
    - armed = `false`
  - verified UI API profile switching also works by changing `VelocityOnly -> As5600`
- updated docs to explain first-time `As5600` zero bootstrap behavior:
  - [docs/firmware_user_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/firmware_user_guide.md)
  - [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)
  - [docs/manual_can_test_checklist.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/manual_can_test_checklist.md)

The highest-priority remaining tasks are now:

1. Run the browser-based manual control test in `As5600` profile while staying disarmed until the user intentionally arms the driver.
2. If profile switching still feels ambiguous in the UI, add explicit command-result feedback that compares the requested profile against the next observed `0x5F7` status.
3. Keep the upper-layer integration guide, manual checklist, user guide, and UI behavior aligned with firmware behavior as new changes are made.

Latest implementation step completed:

- improved the CAN web UI profile-switching feedback
- problem addressed:
  - profile POST responses can contain the previous diag snapshot because `0x5F7` updates asynchronously
  - this could make a successful profile request look like it did not apply yet
- UI behavior now:
  - profile requests show an explicit pending message
  - the requested profile button is marked pending while waiting for the next matching `0x5F7`
  - success is shown only when both stored profile and active profile match the requested profile
  - if the requested profile does not appear within about `2.5 s`, the UI shows a failure message with the current active profile
- changed files:
  - [tools/can_ui/static/index.html](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/static/index.html)
  - [tools/can_ui/static/app.js](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/static/app.js)
  - [tools/can_ui/static/styles.css](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/static/styles.css)
  - [docs/can_test_web_ui_mvp.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_test_web_ui_mvp.md)
- verification:
  - `node --check tools/can_ui/static/app.js` passed
  - `python3 -m py_compile tools/can_ui/server.py` passed
  - `pio run -e custom_f446re` passed
  - running UI server serves `app.js?v=3` and `styles.css?v=3`
  - `/api/state` still reports `link_alive: true`, active profile `As5600`, and `armed = false`

The highest-priority remaining tasks are now:

1. Run the browser-based manual control test in `As5600` profile while staying disarmed until the user intentionally arms the driver.
2. If the user intends to command motion next, confirm the driver should be armed and start with conservative small angle/velocity commands.
3. Keep the upper-layer integration guide, manual checklist, user guide, and UI behavior aligned with firmware behavior as new changes are made.

Latest implementation step completed:

- committed and pushed the current AS5600/profile/UI/documentation work to GitHub
- pushed commit:
  - `229e672 Fix AS5600 profile entry and improve CAN UI`
- remote updated:
  - `origin/main`
- included in that pushed commit:
  - firmware fix for first-time `As5600` profile entry
  - CAN web UI MVP
  - explicit UI profile command confirmation/pending/failure feedback
  - upper-layer integration guide
  - user/manual/CAN protocol documentation updates
  - updated running-work log in this file

The highest-priority remaining tasks are now:

1. Run the browser-based manual control test in `As5600` profile while staying disarmed until the user intentionally arms the driver.
2. If the user intends to command motion next, confirm the driver should be armed and start with conservative small angle/velocity commands.
3. Keep firmware behavior, UI behavior, documentation, and this work log synchronized as future changes are made.

Latest implementation step completed:

- ran the first browser/UI-backend motion test in `As5600` profile
- initial `+1 deg` test exposed two safety-relevant firmware issues:
  - `As5600` profile first-entry saved zero to FRAM but did not immediately update RAM `ActuatorAPI::output_zero_ref_rad`
  - when current output angle was already outside stored travel limits, `Hold Current` was clamped to `output_min_deg` instead of holding current position
- immediate safety action taken:
  - disarmed the driver
  - stopped the latched command stream
  - verified final state was `armed = false`, stream off
- firmware fixes applied:
  - added profile zero-reference application during runtime profile switching
  - changed angle-limit policy so:
    - in-range targets are clamped to `output_min_deg .. output_max_deg`
    - if current position is already outside range, commands further outside are held at current position
    - hold-current and inward recovery commands are allowed
- uploaded the fixed firmware with:
  - `pio run -e custom_f446re -t upload`
- validation after the fix:
  - `arm + hold current` at about `-87 deg` no longer caused a large automatic move
  - angle window during hold: about `-87.020 .. -86.995 deg`
  - small inward command `+0.5 deg` moved from about `-87.020 deg` to `-86.506 deg`
  - final state was confirmed as `armed = false`, stream off
- documented the validation and updated behavior:
  - [docs/as5600_profile_entry_validation_2026-04-23.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/as5600_profile_entry_validation_2026-04-23.md)
  - [docs/firmware_user_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/firmware_user_guide.md)
  - [docs/host_controller_integration_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/host_controller_integration_guide.md)
  - [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)

The highest-priority remaining tasks are now:

1. Commit and push the AS5600 runtime-zero and out-of-range hold-current safety fix.
2. Continue manual UI motion validation from the current `As5600` state, starting with only small inward angle steps while the output coordinate remains outside configured travel limits.
3. Add UI-visible travel limit configuration/status before larger manual angle tests, because current output angle can be outside the stored `0 .. max` range.

Latest implementation step completed:

- committed and pushed the AS5600 runtime-zero and out-of-range hold-current safety fix
- pushed commit:
  - `2f67409 Fix AS5600 zero reference and out-of-range hold`
- remote updated:
  - `origin/main`
- final verified hardware state before push:
  - `As5600` active
  - `armed = false`
  - command stream off
- build verification:
  - `pio run -e custom_f446re` passed

The highest-priority remaining tasks are now:

1. Add UI-visible travel limit/status information before larger manual angle tests, because the current output coordinate can be outside the stored `0 .. max` range.
2. Continue manual UI motion validation from the current `As5600` state using only small inward angle steps until travel limit visibility is added.
3. Consider adding CAN-visible actuator config/status frames for `output_min_deg`, `output_max_deg`, and gear ratio so the UI and upper controller do not depend on undocumented stored config assumptions.

Latest implementation step completed:

- audited and smoke-tested the CAN web UI for non-motion failure cases
- issues found and fixed:
  - session updates could briefly show stale telemetry/diag from the previous session
  - changing session while a latched stream was active did not clear that stream
  - invalid numeric commands such as `NaN` or oversized values could break the background TX stream
  - raw send allowed invalid standard CAN IDs above `0x7FF`
  - raw send allowed payloads longer than classic CAN's `8` bytes
  - `/api/power` treated non-empty strings such as `"false"` as `true`
  - frontend action error text could be overwritten immediately by stale state rendering
  - failed profile POSTs could leave the UI in a misleading pending state
  - HTTP server shutdown could deadlock when signal handling called `server.shutdown()` from the serving thread
- implementation updates:
  - stricter backend validation for angle/velocity values, power commands, raw CAN ID, and raw payload length
  - session updates now clear telemetry, diag, errors, and active stream before restarting `candump`
  - TX loop now stops the stream and records an error if value encoding fails
  - frontend now rejects non-finite numeric inputs before POST
  - frontend profile feedback now clears pending state on POST failure
  - server uses a reusable threaded HTTP server and signal-safe shutdown thread
  - static cache-busting version advanced to `v4`
  - added reusable smoke test:
    - [tools/can_ui/smoke_test.py](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/smoke_test.py)
- verification:
  - `node --check tools/can_ui/static/app.js` passed
  - `python3 -m py_compile tools/can_ui/server.py tools/can_ui/smoke_test.py` passed
  - `python3 tools/can_ui/smoke_test.py --can-iface can0 --node-id 7 --port 8765` passed `19/19`
  - smoke test confirmed final state stayed `armed = false`, stream off
- documentation updated:
  - [docs/can_test_web_ui_mvp.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_test_web_ui_mvp.md)

The highest-priority remaining tasks are now:

1. Add UI-visible travel limit/status information before larger manual angle tests, because the current output coordinate can be outside the stored `0 .. max` range.
2. Continue manual UI motion validation from the current `As5600` state using only small inward angle steps until travel limit visibility is added.
3. Consider adding CAN-visible actuator config/status frames for `output_min_deg`, `output_max_deg`, and gear ratio so the UI and upper controller do not depend on undocumented stored config assumptions.

Latest implementation step completed:

- committed and pushed the CAN web UI hardening and smoke-test work
- pushed commit:
  - `a591890 Harden CAN UI validation and smoke tests`
- remote updated:
  - `origin/main`
- current note:
  - the smoke test starts and stops a temporary server itself
  - no persistent UI server is currently required for validation

The highest-priority remaining tasks are now:

1. Add UI-visible travel limit/status information before larger manual angle tests, because the current output coordinate can be outside the stored `0 .. max` range.
2. Continue manual UI motion validation from the current `As5600` state using only small inward angle steps until travel limit visibility is added.
3. Consider adding CAN-visible actuator config/status frames for `output_min_deg`, `output_max_deg`, and gear ratio so the UI and upper controller do not depend on undocumented stored config assumptions.

Latest implementation step completed:

- tuned the angle outer loop and checked low-speed velocity behavior
- added repeatable CAN/UI step-response measurement tool:
  - [tools/control_tuning/can_step_response.py](/home/gyungminnoh/projects/NoFW/NoFW/tools/control_tuning/can_step_response.py)
- safety improvement made before tuning:
  - UI backend now repeats `disarm` frames and clears stream before sending them
  - tuning script now repeats and confirms `disarm` before exiting
- baseline finding:
  - old `pvc.Kp = 20.0` produced excessive angle overshoot
  - `+5 deg` step overshoot was about `4.418 deg`
- tested angle Kp values:
  - `Kp = 5.0`: `+5 deg` improved, but `+10 deg` still overshot about `2.719 deg`
  - `Kp = 3.0`: `+10 deg` overshot about `1.228 deg`
  - `Kp = 2.0`: `+10 deg` overshoot was `0.000 deg`, tail error about `-0.031 deg`
- final applied tuning:
  - `pvc.Kp = 2.0f`
  - inner velocity PI left unchanged for now:
    - `P = 0.12`
    - `I = 0.4`
    - `D = 0.0`
- low-speed velocity check after angle tuning:
  - `+5 deg/s` for `3 s`
  - tail average about `4.633 deg/s`
  - tail error about `-0.367 deg/s`
  - final state confirmed `armed = false`, stream off
- build/upload:
  - `pio run -e custom_f446re` passed
  - `pio run -e custom_f446re -t upload` passed
- documented results:
  - [docs/pid_tuning_report_2026-04-23.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/pid_tuning_report_2026-04-23.md)

The highest-priority remaining tasks are now:

1. Add UI-visible travel limit/status information before larger manual angle tests, because the current output coordinate can be outside the stored `0 .. max` range.
2. Add CAN-visible actuator config/status frames for `output_min_deg`, `output_max_deg`, and gear ratio so UI/manual tuning can reason about travel range explicitly.
3. If higher-speed velocity tracking is required, tune inner velocity PI separately with longer velocity sweeps after travel status is visible.

Latest implementation step completed:

- committed and pushed the angle-loop tuning and step-response tooling
- pushed commit:
  - `2f84e49 Tune angle loop and add step response tool`
- remote updated:
  - `origin/main`
- final current firmware tuning:
  - `pvc.Kp = 2.0f`
  - inner velocity PI unchanged
- final checked board/UI state:
  - active profile `As5600`
  - `armed = false`
  - command stream off

The highest-priority remaining tasks are now:

1. Add UI-visible travel limit/status information before larger manual angle tests, because the current output coordinate can be outside the stored `0 .. max` range.
2. Add CAN-visible actuator config/status frames for `output_min_deg`, `output_max_deg`, and gear ratio so UI/manual tuning can reason about travel range explicitly.
3. If higher-speed velocity tracking is required, tune inner velocity PI separately with longer velocity sweeps after travel status is visible.

Latest implementation step completed:

- retuned angle command overshoot after the user reported overshoot remained visible
- safety first:
  - found UI state was `armed = true` with an active angle stream
  - immediately sent repeated `disarm` and `stop_stream`
  - confirmed `armed = false`, stream off before tuning
- measured current `pvc.Kp = 2.0` response in both directions:
  - `+10 deg`: overshoot about `0.083 deg`
  - `-10 deg`: overshoot about `0.071 deg`
  - `+30 deg`: overshoot about `0.278 deg`
  - `-30 deg`: overshoot about `0.271 deg`
- changed angle outer-loop gain:
  - `pvc.Kp = 2.0f -> 1.5f`
- uploaded the firmware:
  - `pio run -e custom_f446re`
  - `pio run -e custom_f446re -t upload`
- measured `pvc.Kp = 1.5` response:
  - `+30 deg`: overshoot about `0.084 deg`, tail error about `0.049 deg`
  - `-30 deg`: overshoot about `0.039 deg`, tail error about `-0.007 deg`
  - `+60 deg`: overshoot about `0.170 deg`, tail error about `0.077 deg`
- final state after tests:
  - active profile `As5600`
  - `armed = false`
  - command stream off
- updated tuning report:
  - [docs/pid_tuning_report_2026-04-23.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/pid_tuning_report_2026-04-23.md)

The highest-priority remaining tasks are now:

1. Add UI-visible travel limit/status information before larger manual angle tests, because the current output coordinate can be outside the stored `0 .. max` range.
2. Add CAN-visible actuator config/status frames for `output_min_deg`, `output_max_deg`, and gear ratio so UI/manual tuning can reason about travel range explicitly.
3. If the user still sees overshoot at specific large targets, reproduce that exact target sequence with `tools/control_tuning/can_step_response.py` and tune slew/accel limits rather than only Kp.

Latest implementation step completed:

- committed and pushed the angle overshoot retune
- pushed commit:
  - `2fb50e6 Reduce angle overshoot gain`
- remote updated:
  - `origin/main`
- final firmware in the repo and on the board now uses:
  - `pvc.Kp = 1.5f`
- final checked board/UI state:
  - active profile `As5600`
  - `armed = false`
  - command stream off

The highest-priority remaining tasks are now:

1. Add UI-visible travel limit/status information before larger manual angle tests, because the current output coordinate can be outside the stored `0 .. max` range.
2. Add CAN-visible actuator config/status frames for `output_min_deg`, `output_max_deg`, and gear ratio so UI/manual tuning can reason about travel range explicitly.
3. If the user still sees overshoot at a specific command, capture the exact start angle, target angle, direction, and command sequence, then reproduce it with `tools/control_tuning/can_step_response.py`.

Latest implementation step completed:

- continued AS5048A multi-turn based angle PID tuning without adding new AS5600 runtime telemetry
- confirmed initial board/UI state before motion:
  - active profile `As5600`
  - `armed = false`
  - command stream off
- measured `pvc.Kp = 1.5` large-step behavior:
  - `-60 deg`: overshoot about `0.175 deg`, tail error about `-0.094 deg`
  - `+120 deg`: overshoot about `16.697 deg`, tail error about `-0.006 deg`
- identified the large-step problem as target-near deceleration lag caused by the outer P command interacting with the angle-mode velocity command ramp
- changed angle outer-loop gain:
  - `pvc.Kp = 1.5f -> 1.0f`
- built and uploaded the firmware:
  - `pio run -e custom_f446re`
  - `pio run -e custom_f446re -t upload`
- measured `pvc.Kp = 1.0` response:
  - `+120 deg`: overshoot about `0.111 deg`, tail error about `0.062 deg`
  - `+100 deg`: overshoot about `0.028 deg`, tail error about `0.003 deg`
  - `-80 deg`: overshoot about `0.112 deg`, tail error about `-0.063 deg`
  - `+10 deg`: overshoot about `0.019 deg`, tail error about `0.002 deg`
- noted that a `-120 deg` command from about `69.7 deg` correctly clamped at the configured `output_min_deg = 0`, so that run is not a free negative-step response measurement
- final checked board/UI state:
  - active profile `As5600`
  - `armed = false`
  - command stream off
- updated tuning report:
  - [docs/pid_tuning_report_2026-04-23.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/pid_tuning_report_2026-04-23.md)

The highest-priority remaining tasks are now:

1. Add UI-visible travel limit/status information before more manual angle tests, because `output_min_deg = 0` can silently clamp negative targets.
2. Add CAN-visible actuator config/status frames for `output_min_deg`, `output_max_deg`, and gear ratio so UI/manual tuning can reason about travel range explicitly.
3. If faster large-angle moves are needed later, tune angle-mode velocity profiling separately instead of increasing `pvc.Kp` first.

Latest implementation step completed:

- added CAN-visible actuator travel/config status frames:
  - `0x420 + node_id`: `output_min_deg`, `output_max_deg` as `int32 mdeg`
  - `0x430 + node_id`: gear ratio as `int32 milli-ratio`, plus stored profile/default mode/enable flags
- updated the main firmware to transmit those frames alongside runtime diag every `500 ms`
- updated the CAN web UI to monitor and display:
  - `output_min_deg`
  - `output_max_deg`
  - gear ratio
  - raw actuator config frames
- updated UI button policy:
  - angle command is disabled when live travel limits are known and the target is outside range
  - out-of-range angle presets are disabled
- added a UI warning when the current output angle itself is outside the configured travel range
- bumped web UI static asset versions to `v5`
- restarted the local UI server on:
  - `http://127.0.0.1:8765`
- uploaded the firmware to the board:
  - `pio run -e custom_f446re -t upload`
- verified live UI/API state:
  - `output_min_deg = 0.000`
  - `output_max_deg = 2160.000`
  - `gear_ratio = 8.000`
  - active profile `As5600`
  - `armed = false`
  - stream off
- updated protocol/integration docs:
  - [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)
  - [docs/host_controller_integration_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/host_controller_integration_guide.md)
  - [docs/can_test_web_ui_mvp.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_test_web_ui_mvp.md)
- verification passed:
  - `pio run -e custom_f446re`
  - `python3 -m py_compile tools/can_ui/server.py tools/can_ui/smoke_test.py`
  - `node --check tools/can_ui/static/app.js`
  - `python3 tools/can_ui/smoke_test.py --use-running-server` passed `24/24`

The highest-priority remaining tasks are now:

1. Add CAN commands and UI controls to edit persistent `output_min_deg`, `output_max_deg`, and gear ratio when the user is ready to configure them from the UI instead of compile-time/default values.
2. For future manual motion tests, use the visible travel limits to avoid silent clamp cases before judging PID behavior.
3. Decide whether `output_min_deg/output_max_deg` should be reset around the current boot zero before continuing wide-range angle tests on this physical setup.

Latest implementation step completed:

- added actuator config CAN command paths in main firmware:
  - `0x240 + node_id` for `output_min_deg/output_max_deg`
  - `0x250 + node_id` for `gear_ratio`
- added protocol codec support for these new frames:
  - [include/can_protocol.h](/home/gyungminnoh/projects/NoFW/NoFW/include/can_protocol.h)
  - [src/can_protocol.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/can_protocol.cpp)
- added pending command plumbing in CAN service:
  - [include/can_service.h](/home/gyungminnoh/projects/NoFW/NoFW/include/can_service.h)
  - [src/can_service.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/can_service.cpp)
- main firmware safety/apply policy:
  - config commands are applied only while `disarmed`
  - armed loop drains but ignores profile/config change commands
  - limits require `max > min` and finite bounded values
  - gear ratio requires finite bounded value and rejects `DirectInput` when ratio is not `1:1`
  - `TmagLut` calibration validity now also checks learned ratio vs current config ratio
- runtime reconfiguration path added so config changes are persisted to FRAM and reapplied without reflashing
- web UI now supports actuator config editing:
  - new panel for min/max limits and gear ratio
  - save buttons enabled only while disarmed
  - explicit disarmed requirement feedback messages
  - new backend endpoints:
    - `/api/actuator_limits`
    - `/api/gear_ratio`
- static assets bumped to `v6`
- expanded automated verification:
  - `tools/can_ui/smoke_test.py` now checks:
    - successful disarmed saves for limits/gear
    - invalid limit and invalid gear input rejections
    - presence of new UI controls
  - latest smoke test result: `30/30` pass
- added UI-side post-save confirmation:
  - limits save waits until live `0x427` matches the requested min/max
  - gear save waits until live `0x437` matches the requested gear ratio
  - save buttons show pending state while confirmation is in progress
- fixed the UI backend disarm cache so a successful disarm request immediately allows config saves without waiting for the next `0x5F7`
- manual online verification performed:
  - sent `/api/actuator_limits` and `/api/gear_ratio` with current live values
  - confirmed TX log includes `0x247` and `0x257`
  - confirmed post-state remains link alive, profile `As5600`, `armed=false`, stream off
- found a firmware-side issue after manual config save:
  - runtime config/profile changes were calling `CanService::init()`
  - this can reinitialize CAN hardware while already running and stop later status TX
  - fixed by adding `CanService::configure()` for runtime policy updates without restarting CAN hardware
- current upload status:
  - `pio run -e custom_f446re` passes
  - `python3 -m py_compile tools/can_ui/server.py tools/can_ui/smoke_test.py` passes
  - `node --check tools/can_ui/static/app.js` passes
  - `python3 tools/can_ui/smoke_test.py --use-running-server` passed `30/30` before the CAN reinit fix
  - after the user restored board power, `pio run -e custom_f446re -t upload` passed
  - `python3 tools/can_ui/smoke_test.py --use-running-server` passed `30/30` after upload
  - explicit post-upload config-save verification passed:
    - `/api/actuator_limits` with current values kept `link_alive = true`
    - `/api/gear_ratio` with current value kept `link_alive = true`
    - final state stayed profile `As5600`, `armed = false`, stream off
- updated docs:
  - [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)
  - [docs/host_controller_integration_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/host_controller_integration_guide.md)
  - [docs/firmware_user_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/firmware_user_guide.md)
  - [docs/can_test_web_ui_mvp.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_test_web_ui_mvp.md)
  - [docs/manual_can_test_checklist.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/manual_can_test_checklist.md)

The highest-priority remaining tasks are now:

1. Decide whether to auto-fallback profile when gear-ratio changes invalidate the currently stored profile, or to keep current reject-only behavior.
2. Use the new config edit panel to set travel range around the actual mechanism working range before further wide-angle tuning.
3. Continue manual motion testing from the web UI using the now-visible and editable config values.

Latest implementation step completed:

- user clarified the output shaft currently has no attached load/mechanism, so travel limits are not serving a physical safety role in this bench setup
- changed the persisted actuator travel limits through the web UI/API to a symmetric test range:
  - `output_min_deg = -1080.000`
  - `output_max_deg = 1080.000`
- rationale:
  - keeps the same total default span of `2160 deg`
  - avoids the previous `0 deg` lower clamp interfering with negative-direction tests
  - places the current boot/output angle inside the configured range
- verified live state after saving:
  - `angle_deg` about `60.5 deg`
  - active profile `As5600`
  - `gear_ratio = 8.000`
  - `armed = false`
  - stream off
- verification passed:
  - `python3 tools/can_ui/smoke_test.py --use-running-server` passed `30/30`

The highest-priority remaining tasks are now:

1. Continue manual motion testing from the web UI inside the new `-1080 .. 1080 deg` range.
2. If manual tests show direction or response issues, capture the exact start angle, target angle, and command mode before retuning.
3. Decide later whether to auto-fallback profile when gear-ratio changes invalidate the currently stored profile, or keep current reject-only behavior.

Latest implementation step completed:

- retuned motion response after the user reported the actuator felt too slow during manual testing
- added time-domain metrics to [tools/control_tuning/can_step_response.py](/home/gyungminnoh/projects/NoFW/NoFW/tools/control_tuning/can_step_response.py):
  - `t_90_s`
  - `first_within_1deg_s`
  - `settle_within_1deg_s`
- baseline before speed retune:
  - `pvc.Kp = 1.0`
  - `ACTUATOR_OUTPUT_ANGLE_MODE_SLEW_DEG_S2 = 60`
  - `-180 deg` step: `t_90_s = 3.570`, first `±1 deg = 4.582`, overshoot `0.579 deg`
- tried more aggressive candidates:
  - `Kp = 2.0`, angle slew `360`: faster, but `+180 deg` overshoot about `2.565 deg`
  - `Kp = 1.8`, angle slew `240`: faster, but overshoot about `1.7 .. 2.2 deg`
  - `Kp = 1.6`, angle slew `240`: overshoot about `1.189 deg`, still not clearly better
- final applied balance:
  - `pvc.Kp = 1.5f`
  - `ACTUATOR_OUTPUT_ANGLE_MODE_SLEW_DEG_S2 = 180.0f`
  - `ACTUATOR_OUTPUT_VELOCITY_SLEW_DEG_S2 = 180.0f`
- final measured angle response:
  - `-180 deg`: overshoot `0.638 deg`, `t_90_s = 2.616`, first/settled `±1 deg = 3.416`
  - `+180 deg`: overshoot `1.332 deg`, `t_90_s = 2.608`, first `±1 deg = 3.407`, final tail error about `-0.004 deg`
- final measured velocity response:
  - `+60 deg/s` for `4 s`
  - tail average `59.277 deg/s`
  - max velocity `60.700 deg/s`
- verification passed:
  - `pio run -e custom_f446re`
  - `pio run -e custom_f446re -t upload`
  - `python3 -m py_compile tools/control_tuning/can_step_response.py`
  - `python3 tools/can_ui/smoke_test.py --use-running-server` passed `30/30`
- final checked board/UI state:
  - active profile `As5600`
  - travel limits `-1080 .. 1080 deg`
  - `gear_ratio = 8.000`
  - `armed = false`
  - stream off
- updated tuning report:
  - [docs/pid_tuning_report_2026-04-23.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/pid_tuning_report_2026-04-23.md)

The highest-priority remaining tasks are now:

1. Continue manual motion testing in the web UI with the faster `Kp = 1.5`, angle slew `180`, velocity slew `180` firmware.
2. If response is still too slow, consider accepting about `2 deg` overshoot and move to the more aggressive `Kp = 1.8`, angle slew `240` candidate.
3. If overshoot is visually objectionable, keep `Kp = 1.5` and tune a real braking/trajectory profile rather than raising Kp further.

Latest implementation step completed:

- diagnosed why changing the profile to `As5600` from the web UI appeared not to work
- first observed state:
  - UI/backend was sending `0x227#01`
  - firmware stayed in `VelocityOnly`
  - power stage had previously been `armed = true`, where profile/config changes are intentionally ignored
- after disarming and retrying, the profile still did not change with the old firmware, so the firmware lacked enough feedback to distinguish:
  - `armed` rejection
  - AS5600 read failure
  - not-selectable profile
  - FRAM save failure
- firmware updates:
  - [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp)
    - added last profile-select result reporting in runtime diag `0x5F7 data[6] bits4..7`
    - result codes now include `Ok`, `RejectedArmed`, `As5600ReadFailed`, `NotSelectable`, and `SaveFailed`
    - armed-state profile commands now record `RejectedArmed` instead of failing silently
  - [src/as5600_bootstrap.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/as5600_bootstrap.cpp)
    - AS5600 boot/profile reads now retry up to three sample groups before failing
- UI/backend updates:
  - [tools/can_ui/server.py](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/server.py)
    - decodes the new profile-select result field
    - rejects `/api/profile` while armed
    - stops any active command stream before sending a profile change
  - [tools/can_ui/static/app.js](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/static/app.js)
    - disables profile buttons while armed
    - displays explicit failure messages for `RejectedArmed`, `As5600ReadFailed`, `NotSelectable`, and `SaveFailed`
  - [tools/can_ui/static/index.html](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/static/index.html)
    - added a visible `Profile Result` row
    - static asset cache-busting advanced to `v7`
- documentation updated:
  - [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)
  - [docs/host_controller_integration_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/host_controller_integration_guide.md)
  - [docs/can_test_web_ui_mvp.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_test_web_ui_mvp.md)
- hardware/UI validation completed:
  - uploaded `custom_f446re` successfully
  - restarted the local UI server on `http://127.0.0.1:8765`
  - changed profile to `As5600` through the UI API
  - final live state:
    - stored profile = `As5600`
    - active profile = `As5600`
    - default mode = `OutputAngle`
    - angle/velocity modes enabled
    - `profile_select_result = Ok`
    - `armed = false`
    - stream off
- verification passed:
  - `pio run -e custom_f446re`
  - `pio run -e custom_f446re -t upload`
  - `python3 -m py_compile tools/can_ui/server.py tools/can_ui/smoke_test.py`
  - `node --check tools/can_ui/static/app.js`
  - `python3 tools/can_ui/smoke_test.py --use-running-server --port 8765` passed `30/30`

The highest-priority remaining tasks are now:

1. Continue manual web-UI motion testing from the confirmed `As5600` profile with `armed = false` until the user intentionally arms the driver.
2. If the UI reports `As5600ReadFailed` later, inspect AS5600 wiring/magnet/I2C state before retuning motion behavior.
3. If response is still too slow, retest the previously measured `Kp = 1.8`, angle slew `240` candidate and decide whether about `2 deg` overshoot is acceptable.

Latest implementation step completed:

- performed an upload-and-measure sweep to separate whether slow angle response was limited mainly by `ACTUATOR_OUTPUT_ANGLE_MODE_SLEW_DEG_S2` or `pvc.Kp`
- safety setup:
  - found the UI/backend initially had `armed = true` and an active angle stream
  - sent repeated disarm and stop-stream commands before tuning
  - all tuning runs ended with `armed = false` and stream off
- each candidate was built/uploaded to the board and measured with `+180 deg` and `-180 deg` steps using [tools/control_tuning/can_step_response.py](/home/gyungminnoh/projects/NoFW/NoFW/tools/control_tuning/can_step_response.py)
- measured comparison:
  - `Kp = 1.5`, angle slew `180`: avg overshoot `1.045 deg`, avg `t_90 = 2.613 s`, avg first `±1 deg = 3.414 s`
  - `Kp = 1.5`, angle slew `240`: avg overshoot `1.032 deg`, avg `t_90 = 2.510 s`, avg first `±1 deg = 3.312 s`
  - `Kp = 1.6`, angle slew `240`: avg overshoot `1.206 deg`, avg `t_90 = 2.398 s`, avg first `±1 deg = 3.174 s`
  - `Kp = 1.7`, angle slew `240`: avg overshoot `1.677 deg`, avg `t_90 = 2.344 s`, avg first `±1 deg = 2.984 s`
  - `Kp = 1.7`, angle slew `300`: avg overshoot `1.698 deg`, avg `t_90 = 2.291 s`, avg first `±1 deg = 2.930 s`
  - `Kp = 1.8`, angle slew `240`: avg overshoot `2.014 deg`, avg `t_90 = 2.295 s`, avg first `±1 deg = 2.854 s`
  - `Kp = 1.8`, angle slew `180`: avg overshoot `5.277 deg`, rejected
- conclusion:
  - increasing angle slew alone only modestly improved response
  - the stronger response improvement came from increasing `pvc.Kp`
  - `Kp = 1.8` was faster but overshoot crossed about `2 deg`
  - the selected balance is now:
    - `pvc.Kp = 1.7f`
    - `ACTUATOR_OUTPUT_ANGLE_MODE_SLEW_DEG_S2 = 300.0f`
    - `ACTUATOR_OUTPUT_VELOCITY_SLEW_DEG_S2 = 180.0f`
- final selected firmware is already uploaded to the board
- updated tuning report:
  - [docs/pid_tuning_report_2026-04-23.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/pid_tuning_report_2026-04-23.md)

The highest-priority remaining tasks are now:

1. Run user-visible manual web-UI tests with the new `Kp = 1.7`, angle slew `300` firmware and confirm whether the response now feels fast enough.
2. If still too slow, only then consider `Kp = 1.8`, angle slew `240` while explicitly accepting about `2 deg` overshoot.
3. If overshoot is visually objectionable, keep `Kp = 1.7` and implement a better trajectory/deceleration profile instead of raising P gain further.

Latest implementation step completed:

- expanded tuning from angle `Kp`/slew only to the inner velocity loop as well
- measurement script improved:
  - [tools/control_tuning/can_step_response.py](/home/gyungminnoh/projects/NoFW/NoFW/tools/control_tuning/can_step_response.py)
  - velocity summaries now include tail standard deviation, velocity overshoot, `t_90_s`, first/settled within 5%
- velocity loop upload-and-measure sweep:
  - baseline `P/I/D = 0.12/0.4/0.0` at `±120 deg/s`:
    - avg tail error about `1.962 deg/s`
    - avg tail std about `2.097 deg/s`
    - avg `t_90` about `1.828 s`
  - `P = 0.16`, `I = 0.4` was worse than baseline
  - increasing `I` while keeping `P = 0.12` steadily improved tracking
  - selected velocity gains:
    - `motor.PID_velocity.P = 0.12`
    - `motor.PID_velocity.I = 2.0`
    - `motor.PID_velocity.D = 0.0`
  - selected velocity result at `±120 deg/s`:
    - avg tail error about `0.018 deg/s`
    - avg tail std about `0.397 deg/s`
    - avg `t_90` about `1.507 s`
  - `velocity D = 0.001` was tested and rejected because it increased velocity overshoot and tail noise
- after improving the velocity inner loop, angle `Kp` was retuned with angle slew still at `300 deg/s^2`
- angle `Kp` results after velocity PI update:
  - `Kp = 1.7`: very low overshoot but first `±1 deg` around `3.96 s`
  - `Kp = 2.0`: improved first `±1 deg` to about `3.48 s`
  - `Kp = 2.2`: improved first `±1 deg` to about `3.17 s`
  - `Kp = 2.35`: improved first `±1 deg` to about `2.63 s` with near-zero overshoot
  - `Kp = 2.45`: selected final balance, first `±1 deg` about `2.03 s`, overshoot about `0.9 .. 1.0 deg`
  - `Kp = 2.5`: rejected because overshoot rose to about `3.3 deg`
- small-step validation for final gains:
  - `+30 deg`: overshoot about `0.023 deg`, first/settle `±1 deg` about `2.072 s`
  - `-30 deg`: overshoot about `0.017 deg`, first/settle `±1 deg` about `2.077 s`
- final selected firmware values:
  - `motor.PID_velocity.P = 0.12`
  - `motor.PID_velocity.I = 2.0`
  - `motor.PID_velocity.D = 0.0`
  - `pvc.Kp = 2.45`
  - `ACTUATOR_OUTPUT_ANGLE_MODE_SLEW_DEG_S2 = 300.0`
  - `ACTUATOR_OUTPUT_VELOCITY_SLEW_DEG_S2 = 180.0`
- angle `I/D` terms were not added because the current angle controller is P-only and the retuned velocity loop plus angle `Kp` already achieved low overshoot and near-zero tail error
- final selected firmware is already uploaded to the board
- updated tuning report:
  - [docs/pid_tuning_report_2026-04-23.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/pid_tuning_report_2026-04-23.md)

The highest-priority remaining tasks are now:

1. Run user-visible manual web-UI tests with the final `velocity P/I/D = 0.12/2.0/0.0`, `angle Kp = 2.45`, angle slew `300` firmware.
2. If manual motion feels too aggressive, step angle `Kp` down to `2.35` before changing velocity PI.
3. If future loaded-mechanism tests show steady angle error, add an explicit angle I term with anti-windup rather than increasing velocity I further.

Latest operational step completed:

- restored the local CAN interface after the user reported CAN went down
- observed initial interface state:
  - `can0` was `state DOWN`
  - CAN state was `STOPPED`
  - the CAN UI server process was still running on `127.0.0.1:8765`
- brought `can0` back up at `1 Mbps`
  - attempted `restart-ms 100`, but the current `gs_usb` adapter reported that bus-off restart is not supported
  - interface still came up successfully as `UP`, `LOWER_UP`, `ERROR-ACTIVE`
- verified raw CAN traffic with `candump can0`
  - observed live `0x407`, `0x417`, `0x5F7`, `0x427`, and `0x437` frames
- verified UI backend recovery through `/api/state`
  - `link_alive = true`
  - active profile = `As5600`
  - `armed = false`
  - stream off
  - no backend error

The highest-priority remaining tasks are now:

1. Continue manual web-UI motion testing with the final tuned firmware while confirming the UI remains `LINK ALIVE`.
2. If CAN goes down again, first check `ip -details -statistics link show can0`, then bring it up at `1 Mbps` without relying on unsupported `restart-ms`.
3. If repeated CAN drops occur during motion, inspect adapter/cabling/power and capture CAN error counters before further control tuning.

## Important Constraints For Future Work

- The actuator profile may vary by product:
  - motor type may change
  - gearbox may change
  - gear ratio may change
  - output encoder type may change
- `TMAG LUT` candidate search must depend on the stored gear ratio.
- Some actuators will use `AS5600` as the runtime output encoder.
- Some actuators will use `TMAG LUT` as the runtime output encoder.
- Some actuators may intentionally run without an output absolute encoder and allow only output-referenced velocity control.
- Some TMAG-based calibration flows may still use `AS5600` as the calibration reference.
- Output-absolute-angle validity is mandatory for output-angle motion, but not for `VelocityOnly` profiles.
- Boot should not auto-enable motion; `CAN enable` is required.
- If `can0` is down, the agent should first try to bring it up with `1 Mbps`.
  In the current shell session this may still require elevated privilege.

## Remaining Diagnostic Firmware

Only the following diagnostic environments are currently kept.

- `fram_test_f446re`
  Validates FRAM read/write behavior and power-cycle retention.
  It now also reports stored calibration-bundle presence on CAN `0x5D0 + node_id`.
- `tmag_comm_test_f446re`
  Validates TMAG SPI communication and basic register access.
- `tmag_sensor_test_f446re`
  Validates that TMAG produces magnetic field measurement data.
- `tmag_xyz_live_test_f446re`
  Streams live `TMAG5170` `X/Y/Z` and raw values while the user manually moves the mechanism.
- `tmag_lut_angle_test_f446re`
  Evaluates output-angle estimation using `TMAG5170 XYZ -> LUT -> output angle`, referenced to `AS5600`.
- `tmag_calibration_runner_f446re`
  Learns `TMAG LUT` calibration on hardware and writes `TmagCalibrationData` into the FRAM calibration bundle.

Runtime profile changes are now expected to happen through the main firmware over CAN and FRAM persistence, not by reflashing helper firmware.

Default PlatformIO environment:

- `pio run`
- resolves to `custom_f446re`

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

## 2026-04-25 - CAN node ID boot-time sync

- Completed:
  - changed actuator-config migration so boot now compares stored `can_node_id`
    against compile-time `CAN_NODE_ID`
  - if the two differ, firmware now overwrites the FRAM-stored `can_node_id`
    with the firmware value during boot
  - updated [docs/firmware_user_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/firmware_user_guide.md)
    and [docs/host_controller_integration_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/host_controller_integration_guide.md)
    to document that firmware `CAN_NODE_ID` takes precedence over stored FRAM node ID
  - built and uploaded the main firmware to hardware
  - live CAN validation passed:
    - before test: board responded on `0x407`, `0x417`, `0x5F7`
    - with temporary firmware `CAN_NODE_ID = 8`: board responded on `0x408`, `0x418`, `0x5F8`
    - restored final firmware `CAN_NODE_ID = 7`: board responded again on `0x407`, `0x417`, `0x5F7`
  - added [docs/can_node_id_provisioning.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_node_id_provisioning.md)
    with a board labeling, build, upload, and CAN verification procedure for multi-board deployment
  - added [docs/board_deployment_table.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/board_deployment_table.md)
    for the planned 9-board deployment:
    - steering `S01..S04`, `As5600`, `50:1`, node ids `2,3,4,1`
    - driving `D01..D04`, `VelocityOnly`, `78:15`, node ids `18,19,20,17`
    - gripper `G01`, `As5600`, `30:1`, node id `31`
  - updated deployment travel limits:
    - steering boards: `-120 ~ 120 deg`
    - gripper board: `0 ~ 90 deg`
  - updated the planned wheel-position CAN assignment:
    - steering: `FR=2`, `BR=3`, `FL=4`, `BL=1`
    - driving: `FR=18`, `BR=19`, `FL=20`, `BL=17`
    - documented the corresponding command/status CAN IDs in
      [docs/board_deployment_table.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/board_deployment_table.md)
  - audited deployment-related code and removed/fixed stale paths:
    - removed unused `ACTUATOR_BOOT_OUTPUT_DEG`
    - fixed `tools/can_ui/smoke_test.py` so it no longer hardcodes `node_id = 7`
    - fixed `tools/can_ui/static/app.js` so profile-wait text uses the active session diag CAN ID instead of hardcoded `0x5F7`
  - representative live hardware validation completed on the connected board:
    - steering representative: `node_id = 2`, `As5600`, `50:1`, `-120 ~ 120 deg`
    - driving representative: `node_id = 18`, `VelocityOnly`, `78:15`
    - gripper representative: `node_id = 31`, `As5600`, `30:1`, `0 ~ 90 deg`
    - gripper out-of-range recovery behavior was checked and matched the intended inward-only recovery logic
  - restored the connected board to the development default after testing:
    - `node_id = 7`, `As5600`, `gear_ratio = 8.0`, `limits = -1080 ~ 1080 deg`
  - wrote [docs/deployment_validation_report_2026-04-25.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/deployment_validation_report_2026-04-25.md)
    with the live test results and restored final state
- Next:
  - replace the temporary board labels with final physical labels or serial numbers when available
  - document the gripper zero-setting workflow once the actual mechanical zero procedure is fixed

## 2026-04-25 - Boot output-zero target

- Completed:
  - changed [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp) so `refreshBootReference()` sets the initial angle target to `0 deg` when an absolute output reference is available at boot
  - kept the existing boot-safe behavior: the power stage still starts `disarmed`, so the actuator moves toward `0 deg` only after an explicit CAN arm command
  - preserved current-position hold fallback when the output absolute reference is unavailable or when the selected profile does not require an output encoder
  - updated:
    - [docs/firmware_user_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/firmware_user_guide.md)
    - [docs/host_controller_integration_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/host_controller_integration_guide.md)
    - [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)
    - [docs/manual_can_test_checklist.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/manual_can_test_checklist.md)
  - verified build with `pio run -e custom_f446re`
  - uploaded the revised firmware with `pio run -e custom_f446re -t upload`
  - confirmed CAN runtime diagnostics after upload:
    - `0x5F7 = FB 01 01 01 01 01 00 01`
    - stored profile = `As5600`
    - active profile = `As5600`
    - calibration required = `0`
    - power stage armed = `0`
    - limits/status frames were present on `0x427` and `0x437`
- Next:
  - when the user confirms the mechanism is safe to move, arm once and validate that the first motion goes toward stored `0 deg`
  - if that first-arm motion is too abrupt, add a boot-homing speed cap or a separate explicit home command instead of using the normal angle target immediately

## 2026-04-25 - First-arm output-zero motion validation

- Completed:
  - with the user confirming that physical motion was safe, sent CAN arm command `0x237#01`
  - observed CAN position/status while the firmware moved from about `-67.3 deg` toward the boot target `0 deg`
  - observed final position near `0.05 ~ 0.10 deg`
  - sent CAN disarm command `0x237#00`
  - confirmed runtime diagnostic returned to `0x5F7 = FB 01 01 01 01 01 00 01`
    - stored profile = `As5600`
    - active profile = `As5600`
    - calibration required = `0`
    - power stage armed = `0`
- Next:
  - if the first-arm homing motion feels too fast or abrupt on the real mechanism, add a dedicated boot-homing velocity cap or explicit home command
  - otherwise keep this behavior as the default absolute-output-reference boot behavior

## 2026-04-25 - AS5600 boot-zero direction fix

- Completed:
  - identified that boot output-zero reference used raw AS5600 absolute angle and ignored `As5600CalibrationData::invert`
  - added zero-relative output encoder reads so boot reference uses the encoder-calibrated signed delta from stored zero
  - changed AS5600 zero-relative reads to apply the stored `invert` flag before boot/reference math
  - fixed the TMAG LUT output encoder read path so its already-zero-relative estimator output is not offset a second time
  - added CAN output-encoder config command:
    - `0x260 + node_id`
    - for node `7`: `0x267`
    - `267#0100` = AS5600 invert off
    - `267#0101` = AS5600 invert on
    - accepted only while disarmed
  - documented the new command in [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)
  - built and uploaded `custom_f446re`
  - set current board AS5600 invert on with `cansend can0 267#0101`
  - live validation:
    - before invert, current interpreted position was about `-59.6 deg`
    - after invert, current interpreted position became about `+59.6 deg`
    - arm moved from about `+59.6 deg` down toward `0 deg`
    - final position settled near `0.03 ~ 0.06 deg`
    - disarmed successfully, final `0x5F7 = FB 01 01 01 01 01 00 01`
- Next:
  - expose AS5600 invert in the web UI so users do not have to send raw CAN frames
  - add output encoder config status reporting if upper controllers need to read back the stored invert flag

## 2026-04-25 - AS5600 direction auto-calibration command

- Completed:
  - added CAN command `0x270 + node_id` for output encoder direction auto-calibration
    - for node `7`: `0x277#01`
    - currently supports `As5600`
    - accepted only while the power stage is disarmed
  - auto-calibration sequence:
    - read AS5600 raw angle before motion
    - temporarily arm the power stage
    - move a small positive output-axis velocity command
    - read AS5600 raw angle after motion
    - save `as5600.invert = false` when raw angle increases
    - save `as5600.invert = true` when raw angle decreases
    - disarm and refresh boot reference
  - documented the command in [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)
  - built and uploaded `custom_f446re`
  - live validation:
    - boot/status before command showed `0x5F7 = FB 01 01 01 01 01 00 01`
    - sent `cansend can0 277#01`
    - command performed a short positive-direction motion and returned to `armed = 0`
    - subsequent arm moved from about `5.3 deg` back toward `0 deg`
    - final disarmed position was around `-0.09 deg`
- Next:
  - add CAN-visible result/status for output encoder auto-calibration success/failure
  - expose the manual invert setting and auto-calibration command in the web UI

## 2026-04-25 - Host controller guide update

- Completed:
  - identified the host/upper-layer document as [docs/host_controller_integration_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/host_controller_integration_guide.md)
  - updated it with the latest output encoder direction behavior:
    - CAN ID list now includes `0x267` output encoder config and `0x277` AS5600 direction auto-calibration
    - boot procedure recommends `0x277#01` for first-time AS5600 board setup
    - `As5600` profile section now states that zero offset and direction are stored in FRAM
    - settings section documents manual invert commands `267#0100` / `267#0101`
    - settings section documents auto-calibration command `277#01`
    - troubleshooting section covers wrong 0deg return direction
- Next:
  - add CAN-visible result/status for output encoder auto-calibration success/failure
  - expose output encoder config and auto-calibration in the web UI
