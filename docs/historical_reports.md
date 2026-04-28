# Historical Validation Reports

This file preserves durable conclusions from old one-off validation reports.
The original long-form reports were removed from the active docs tree because
they described transient bench sessions and older firmware revisions.

## Power Stage Boot Safety - 2026-04-22

- The power stage must remain disarmed after boot.
- Arm/disarm is controlled explicitly through `0x230 + node_id`.
- For the default `node_id = 7`, arm is `0x237#01` and disarm is `0x237#00`.
- Runtime diagnostic byte `data[7]` uses bit0 for output-feedback availability
  and bit1 for power-stage armed state.
- Observed diagnostic transitions confirmed disarmed-at-boot, armed after the
  CAN arm command, and disarmed again after the CAN disarm command.

## Runtime Validation - 2026-04-22

- The first runtime report was captured with a stale compile-time
  `GEAR_RATIO = 240.0f`.
- Its absolute `deg` and `deg/s` values should not be used as physical output
  measurements.
- It remains useful only as historical evidence for boot behavior, CAN command
  flow, direction checks, and relative control observations.

## Ratio-Corrected Runtime Validation - 2026-04-22

- Later validation used the corrected real hardware ratio of `8:1`.
- Boot-safe arm/disarm behavior was confirmed.
- Arming no longer shifted the reported output reference by itself.
- Conservative angle commands such as `+10 deg` and `+20 deg` followed the
  requested direction.
- Velocity status became stable after the sampling fix.
- Conservative velocity checks at `10`, `20`, `30`, `40`, `60`, `100`, `150`,
  and `200 deg/s` validated the basic direction and reporting behavior used by
  that firmware revision.
- A later edge-braking revision reduced harsh travel-limit behavior and avoided
  reproducing the previously reported bench supply OCP event during the tested
  lower-edge recovery commands.

## AS5600 Profile Entry Validation - 2026-04-23

- Entering the `As5600` profile is valid when the sensor read succeeds, even if
  the initial output position is exactly zero.
- The stored RAM zero position is refreshed after successful profile entry.
- Out-of-range hold-current behavior was changed so it no longer drives farther
  toward a travel-limit edge.
- Small inward commands from an out-of-range hold state were validated on the
  bench.

## PID Tuning - 2026-04-23

- This was an angle-loop tuning session for an older firmware state.
- Aggressive outer angle gains caused overshoot and oscillation.
- The durable conclusion is to keep angle-loop defaults conservative and tune
  with step-response evidence instead of relying on high proportional gain.
- The current firmware keeps the default outer angle proportional gain at
  `OUTER_ANGLE_KP = 3.0`.
- `tools/control_tuning/can_step_response.py` remains the preferred helper for
  repeatable CAN step-response captures.

## Deployment Validation - 2026-04-25

- Representative steering, driving, and auxiliary angle-actuator configurations
  were exercised through CAN.
- A UI-side hardcoded node-id issue was found and fixed during that session.
- Representative checks included steering with `As5600` feedback, a
  velocity-only driving actuator, and an auxiliary angle actuator with
  out-of-range recovery behavior.
- The connected bench board was restored to the development default profile
  after validation.
