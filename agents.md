# agents.md

## Purpose

This repository is an actuator-generic BLDC firmware project for `STM32F446RE`.
The current design target is a reusable actuator firmware core with:

- output-shaft control as the primary coordinate system
- actuator commands expressed in `deg` and `deg/s`
- board-specific pin mapping selected at build time
- actuator configuration and calibration stored in external FRAM
- runtime output encoder profiles selected through stored configuration

The firmware should not be extended around product-specific percentage APIs.
Product behavior should sit above the actuator core, using generic output angle,
velocity, limits, and calibration primitives.

## Current Firmware Shape

Long-term firmware set:

- main actuator firmware
- dedicated `TMAG` calibration firmware

The repository may still contain diagnostic or historical firmware entry points.
Treat those as cleanup candidates unless they are still part of a current
validation workflow.

Primary local workflow:

- build with `PlatformIO`
- upload through `ST-Link`
- observe and command through `CAN`
- validate protocol behavior with the Python spec runner
- use dedicated firmware only when a hardware diagnostic or calibration flow
  genuinely requires it

## Hardware Context

Known hardware used by this project:

- MCU: `STM32F446RE`
- motor input encoder: `AS5048A`
- output encoder option: `AS5600`
- output encoder option: `TMAG5170` with LUT-based reconstruction
- external nonvolatile storage: `FM25CL64B-G` SPI FRAM
- host interface: `CAN`
- upload/debug interface: `ST-Link`

Known peripheral mapping:

- FRAM chip select: `SPI1_nCS_2`
- `TMAG5170` chip select: `SPI1_nCS_3`
- `AS5048A` chip select: defined in `include/board_config.h`
- `AS5600`: `I2C`

Future agents cannot physically move shafts, reposition magnets, change wiring,
or probe signals. Those actions require the user.

## Architecture Rules

- Control reference is the output shaft.
- Public command/status units are:
  - output angle: `deg`
  - output velocity: `deg/s`
- Board details stay compile-time unless the user explicitly changes that policy.
- Runtime actuator configuration and calibration live in FRAM.
- Build defaults are allowed, but stored FRAM configuration is the runtime source
  when valid.
- Output encoder profile is selected by actuator configuration.

Supported output encoder profiles:

- `VelocityOnly`: no output absolute encoder required; velocity mode only.
- `DirectInput`: `gear_ratio == 1:1`; the motor input encoder is also the output
  angle sensor.
- `As5600`: `AS5600` is the runtime output encoder.
- `TmagLut`: `TMAG5170` LUT is the runtime output encoder after calibration.

Calibration policy:

- `TmagLut` runtime requires a valid stored `TMAG` calibration.
- `TMAG` calibration uses `AS5600` as the reference encoder.
- Successful `TMAG` calibration must not silently promote runtime profile.
  Profile changes should remain explicit through the supported config path.

## CAN And Runtime Behavior

- Output angle command/status IDs use millidegrees on the wire.
- Output velocity command/status IDs use millidegrees per second on the wire.
- Receiving an angle command selects output-angle control.
- Receiving a velocity command selects output-velocity control.
- In angle mode, command timeout should fall back to current-angle hold.
- In velocity mode, command timeout should command zero velocity.
- Runtime diagnostic frames should remain sufficient to distinguish:
  - stored profile
  - active profile after fallback
  - enabled capabilities
  - calibration/trusted state
  - armed/fault state

Keep protocol changes reflected in:

- `docs/can_protocol.md`
- `docs/firmware_user_guide.md`
- `tools/can_spec_test.py`
- CAN UI decoder code, when relevant

## Safety Rules

- Do not arm or command motion unless the user has explicitly allowed it for the
  current session.
- Do not add `--allow-arm` or `--allow-motion` to tests unless arming/motion is
  explicitly safe for that session.
- If hardware/CAN is needed and `can0` is down, try to bring up `can0` at
  `1 Mbps`; if blocked, record the exact blocker and continue with non-hardware
  work.
- After upload or motion tests, send a safe stop/disarm when that is appropriate
  for the task and hardware state.
- Treat bench power, current limits, and recent fault/OCP observations as
  safety-relevant context. Do not infer that a previous session's hardware state
  is still safe.

## Standard Verification

For firmware behavior or protocol changes, run at minimum:

```bash
python3 tools/can_spec_test.py --protocol-only
```

Representative firmware build checks:

```bash
pio run -e custom_f446re_s01
pio run -e custom_f446re_d01
pio run -e tmag_calibration_runner_f446re
```

When a change affects more board profiles, build the touched profile family.
When a change affects storage or calibration, include the relevant diagnostic or
calibration firmware build.

For CAN UI changes, run its smoke test if dependencies are available:

```bash
python3 tools/can_ui/smoke_test.py
```

## Repository Hygiene

- Keep source and docs focused on current actuator-generic behavior.
- Do not keep one-off experiment firmware in active build configuration unless it
  is still part of the supported workflow.
- Generated files, caches, local captures, and virtual environments do not belong
  in tracked source:
  - `.pio/`
  - `.venv/`
  - `capture/`
  - `__pycache__/`
  - `*.pyc`
- Large experiment logs, plots, and CSV files should be either untracked,
  archived intentionally, or summarized in a maintained report.
- Prefer deleting obsolete generated artifacts over moving them deeper into the
  active docs tree.

## Worklog Policy

Do not append session logs to this file.

Use `agents.md` only for stable project guidance that should affect future work.
When operational notes need to be preserved, create or append to:

```text
docs/worklog/YYYY-MM-DD.md
```

Worklog entries should be brief and should capture only durable facts:

- what changed
- what was verified
- current hardware state, if relevant
- known blockers or follow-up tasks

Historical details removed from this file are recoverable through Git history.

## Practical Workflow

- Read the current code and docs before making architecture assumptions.
- Prefer existing helpers and local patterns over new abstractions.
- Keep edits scoped to the requested cleanup or feature.
- Do not revert user changes in a dirty worktree.
- For cleanup work, classify files as keep, archive, or delete before removing
  source that may still be used by PlatformIO or hardware workflows.
- For docs-only changes, avoid unnecessary firmware builds; use targeted grep and
  diff checks instead.
