# D02 Velocity Tuning Summary - 2026-04-26

## Context

D02 was tested as a `VelocityOnly` driving actuator with `gear_ratio = 5.2`,
`21` pole pairs, and a temporary `40.0V` firmware torque-limit override during
part of the bench investigation. The source defaults later returned to the
normal torque-limit policy unless a PlatformIO environment overrides it.

This file preserves the durable conclusions from the raw CSV/log/plot artifacts
that were removed from the active docs tree during repository cleanup.

## Findings

- Repeated forced FOC calibration completed successfully and reported trusted
  calibration afterward. After the first run, calibration movement was
  repeatable at about `2.5 deg`, so the poor velocity response did not look like
  random FOC calibration variation.
- Raising the firmware torque limit to `40.0V` helped low-speed `+5 deg/s`
  response, but it did not solve sustained `+10 deg/s` tracking.
- Increasing bus voltage produced only small changes. The `+10 deg/s` tail still
  collapsed near zero, which points away from bus voltage alone as the cause.
- Increasing D02 velocity P gain from `4.0` to `8.0` increased peak response, but
  did not improve sustained tracking. At `+10/-10 deg/s`, tail velocity still
  collapsed near zero.
- The remaining likely causes are non-PID issues: power/current limiting under
  load, driver or phase output limits, phase order, FOC effectiveness under
  gearbox load, mechanical stiction, or binding.

## Representative Measurements

- FOC reliability cycles all ended with diagnostic state equivalent to:
  FOC valid, output calibration valid, trusted calibration loaded, no fault, and
  disarmed.
- With `40.0V` torque limit:
  - `+5 deg/s`: average about `+2.8..3.1 deg/s`, tail about `+3.8 deg/s`
  - `-5 deg/s`: average about `-2.0..-2.3 deg/s`, tail about `-1.4..-1.6 deg/s`
  - `+10 deg/s`: average about `+2.3 deg/s`, tail near zero
  - `-10 deg/s`: average about `-2.3..-2.5 deg/s`, tail varied and did not track
    the command consistently
- With `P=8.0`, `I=2.0`, `40.0V` torque limit:
  - `+5 deg/s`: average about `+3.3 deg/s`, tail about `+3.5 deg/s`
  - `-5 deg/s`: average about `-2.4 deg/s`, tail about `-1.3 deg/s`
  - `+10 deg/s`: peak reached about `+10.9 deg/s`, but tail stayed near zero
  - `-10 deg/s`: peak reached about `-7.1 deg/s`, but tail stayed near zero

## Follow-Up

Before further gain tuning, expose or inspect lower-level drive evidence such as
phase output, supply current behavior, FOC alignment details, and mechanical
load/stiction. More PID gain alone is unlikely to fix the sustained tracking
failure observed in this session.
