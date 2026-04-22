# Runtime Validation Report

Date: `2026-04-22`

Important note:

- this report was captured while the firmware still had a stale compile-time `GEAR_RATIO = 240.0f`
- the real hardware ratio was later confirmed to be `8:1`
- therefore the absolute `deg` / `deg/s` magnitudes printed by the firmware in this report are ratio-contaminated and should not be treated as trustworthy physical output values
- the report remains useful only for relative observations such as:
  - boot repeatability
  - CAN protocol behavior
  - command-direction response
  - velocity-status coherence after the sampling fix

Target firmware:

- `custom_f446re`

Validated control policy:

- output encoder is used only for boot-time `0 deg` alignment and explicit zero capture
- runtime FOC control and CAN status use input-encoder `AS5048A` multi-turn estimation

Validated test policy:

- after upload, allow the system to settle before evaluating idle behavior
- because `CAN_TIMEOUT_MS = 100 ms`, control commands must be refreshed faster than `10 Hz`
- this report uses repeated command transmission at `20 Hz` during active control windows

Test environment:

- board: `STM32F446RE`
- CAN: `can0`, `1 Mbps`
- node id: `7`
- key CAN frames:
  - angle command: `0x207`
  - angle status: `0x407`
  - velocity command: `0x217`
  - velocity status: `0x417`
  - runtime diag: `0x5F7`

## 1. Firmware Changes Validated Before This Report

Two important fixes were applied before the final validation run.

1. Boot hold behavior

- at boot, the firmware no longer forces target angle to `0 deg`
- it now captures the current aligned output estimate as the initial target
- this prevents unintended automatic motion immediately after reset/upload

2. Velocity status calculation fix

- `0x417` had been under-reporting speed because the internal sampling state was updated even when `millis()` had not advanced
- this was corrected so velocity is now calculated from the last non-zero time step

## 2. Test Procedure

The final validation sequence was:

1. Upload `custom_f446re`
2. Wait about `3 s` for post-upload settling
3. Capture boot-time `0x407`, `0x417`, `0x5F7`
4. Upload `custom_f446re` again
5. Wait about `3 s`
6. Capture boot-time `0x407`, `0x417`, `0x5F7` again
7. Capture `idle` traffic for `5 s`
8. Run streamed angle command test at `20 Hz`:
   - `20 deg` for `1 s`
   - `40 deg` for `1 s`
   - `60 deg` for `1 s`
   - `10 deg` for `1 s`
   - `0 deg` for `1 s`
9. Run streamed velocity command test at `20 Hz`:
   - `60 deg/s` for `1 s`
   - `0 deg/s` for `1 s`

Representative commands:

```bash
pio run -e custom_f446re -t upload

# streamed angle commands
cansend can0 207#204E0000   # 20.000 deg
cansend can0 207#409C0000   # 40.000 deg
cansend can0 207#60EA0000   # 60.000 deg
cansend can0 207#10270000   # 10.000 deg
cansend can0 207#00000000   # 0.000 deg

# streamed velocity command
cansend can0 217#60EA0000   # 60.000 deg/s
cansend can0 217#00000000   # stop
```

## 3. Boot Consistency

Boot capture 1:

- angle status average: `38.6717 deg`
- angle status range: `38.671 deg .. 38.672 deg`
- velocity status average: `0.097 deg/s`
- runtime diag:
  - `FB02020101010001`

Boot capture 2:

- angle status average: `38.6718 deg`
- angle status range: `38.671 deg .. 38.672 deg`
- velocity status average: `0.006 deg/s`
- runtime diag:
  - `FB02020101010001`

Observed result:

- boot 1 and boot 2 are effectively identical
- boot angle difference is below visible resolution in the captured status range

Assessment:

- boot reference restoration is now repeatable

## 4. Idle Stability

Idle capture window: `5 s`

- angle status first/last:
  - first: `38.672 deg`
  - last: `38.672 deg`
- angle status range:
  - `38.671 deg .. 38.672 deg`
- angle status average:
  - `38.6719 deg`
- velocity status range:
  - `-1.007 deg/s .. 0.824 deg/s`
- velocity status average:
  - `0.043 deg/s`
- runtime diag:
  - `FB02020101010001`

Observed result:

- output angle status stayed effectively fixed through the entire idle capture
- velocity status remained centered near zero with small noise

Assessment:

- idle behavior is acceptable under the clarified policy

## 5. Streamed Angle Command Response

The angle command test used repeated transmissions at `20 Hz`.

### Angle status summary

- idle average: `38.667 deg`
- `20 deg` average: `36.906 deg`
- `40 deg` average: `36.400 deg`
- `60 deg` average: `40.001 deg`
- `10 deg` average: `41.211 deg`
- `0 deg` average: `36.314 deg`

### Velocity status summary

- idle average: `0.050 deg/s`
- `20 deg` average: `-3.575 deg/s`
- `40 deg` average: `2.811 deg/s`
- `60 deg` average: `4.230 deg/s`
- `10 deg` average: `-2.029 deg/s`
- `0 deg` average: `-2.614 deg/s`

Observed result:

- `20 deg` command drove the estimated output angle downward from the initial `38.67 deg` region
- `40 deg` reversed direction upward relative to the `20 deg` segment
- `60 deg` continued upward as expected
- `10 deg` and `0 deg` drove the estimate downward again
- velocity status sign matched the expected direction of motion in each segment

Assessment:

- streamed angle control responds in the expected direction
- response is not instantaneous, but the control path is behaving coherently

## 6. Streamed Velocity Command Response

The velocity command test used repeated transmissions at `20 Hz`.

### Angle status summary

- idle average: `34.951 deg`
- `60 deg/s` average: `36.807 deg`
- stop average: `39.586 deg`

### Reported velocity status summary (`0x417`)

- idle average: `0.096 deg/s`
- `60 deg/s` average: `3.488 deg/s`
- stop average: `0.348 deg/s`

### Derived velocity from `0x407`

- idle average: `0.080 deg/s`
- `60 deg/s` average: `3.641 deg/s`
- stop average: `0.273 deg/s`

Observed result:

- during the streamed `60 deg/s` command window, angle status increased monotonically
- reported velocity status and velocity derived directly from angle status now match closely
- after stop, velocity decayed back toward zero

Important note:

- the requested `60 deg/s` is not achieved literally on this hardware/configuration
- measured output velocity during the command window was around `3.5 .. 3.6 deg/s`
- this indicates that the command path is working, but the current motor-side speed limit / plant response remains lower than the commanded setpoint

Assessment:

- velocity command path is functioning correctly
- achieved speed is limited and should be treated as a tuning / performance issue, not a protocol correctness issue

## 7. Overall Assessment

Current result: `pass with performance limits`

What is now validated:

- boot reference is repeatable after upload/reset
- idle angle estimate is stable
- runtime CAN angle/velocity status are coherent under the motor-side multi-turn policy
- streamed angle commands move in the expected direction
- streamed velocity commands move in the expected direction
- `0x417` now matches angle-derived velocity closely after the sampling fix

What remains open:

- achieved output speed is much lower than the requested `60 deg/s`
- angle motion is directionally correct but still relatively slow

## 8. Recommended Next Work

1. Tune performance limits on the motor-side control path.
   Candidate areas:
   - motor velocity PID gains
   - outer-loop velocity clamp
   - practical motor-side speed limit
2. If user-facing expectations require one-shot CAN commands, reconsider the timeout policy.
   Current behavior assumes continuous command refresh faster than `100 ms`.
