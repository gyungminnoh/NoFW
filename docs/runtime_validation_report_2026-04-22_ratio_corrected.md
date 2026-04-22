# Runtime Validation Report

Date: `2026-04-22`

Target firmware:

- `custom_f446re`

Scope of this report:

- this report covers validation after correcting the firmware gear ratio to the real `8:1` hardware value
- it also includes the new boot-safe power-stage arm/disarm behavior
- this report supersedes the absolute `deg` / `deg/s` interpretation of the earlier ratio-contaminated report

## 1. Validated Firmware Behavior

The firmware under test now has these relevant properties:

- compile-time `GEAR_RATIO` corrected to `8.0f`
- stale stored default config migration for the old `240:1` fingerprint
- power stage stays `disarmed` at boot
- explicit CAN arm/disarm command required:
  - `0x230 + node_id`
  - default node `7`:
    - arm: `0x237#01`
    - disarm: `0x237#00`
- output-velocity commands are now applied through a conservative slew limit

## 2. Boot-Safe Arm / Disarm Verification

Observed diagnostic frame:

```text
0x5F7 = FB02020101010001
```

Interpretation:

- `data[7] = 0x01`
- bit0 = output feedback required = `1`
- bit1 = power stage armed = `0`

This confirms that after boot the firmware is not auto-arming the power stage.

Explicit arm/disarm sequence:

```bash
cansend can0 237#01
cansend can0 237#00
```

Observed diagnostic transition:

```text
FB02020101010001 -> armed = 0
FB02020101010003 -> armed = 1
FB02020101010001 -> armed = 0
```

Assessment:

- boot-safe disarmed state works
- explicit arm/disarm state transition works

## 3. Arm Should Not Shift Output Reference

An intermediate bug was found during this validation:

- simply sending `arm` used to shift reported output angle by about `12 deg`
- root cause:
  - `armPowerStage()` was re-running `refreshBootReference()`

That behavior was removed and re-tested.

Re-test result:

- pre-arm angle average: `63.974 deg`
- armed angle average: `63.977 deg`
- post-disarm angle average: `63.975 deg`

Assessment:

- arm/disarm no longer changes the reported output reference by itself

## 4. Small Angle-Step Check

Test method:

1. Read current `0x407` output angle status
2. Compute `target = current + 10 deg`
3. Arm the power stage
4. Stream that angle target on `0x207` for about `1.5 s`
5. Disarm

Observed result:

- initial status: `63.979 deg`
- commanded target: `73.979 deg`

Angle summary:

- idle average: `63.977 deg`
- command-window first/last:
  - first: `63.982 deg`
  - last: `74.210 deg`
- command-window range:
  - `63.982 deg .. 74.232 deg`
- post-window average:
  - `74.190 deg`

Assessment:

- the output-angle status follows a `+10 deg` command with the expected direction and approximately the expected magnitude
- this is a good sign that the current angle scaling is now consistent with the corrected `8:1` ratio

## 5. Conservative Velocity Check

Test method:

1. Start from the settled post-angle-test position
2. Arm the power stage
3. Stream `10 deg/s` on `0x217` for about `1.5 s`
4. Disarm

Command used:

```bash
cansend can0 217#10270000
```

An initial short run showed correct positive motion direction but still noisy velocity status.

The status path was then improved so `0x417` velocity is derived at the CAN status transmit interval instead of from very short loop-to-loop deltas.

Longer re-test result after that change:

Observed angle result:

- command-window first/last:
  - first: `13.348 deg`
  - last: `42.374 deg`
- command tail first/last:
  - first: `22.085 deg`
  - last: `42.374 deg`
- post-window average:
  - `45.763 deg`

Observed velocity status result:

- command-window full average: `7.845 deg/s`
- command-window tail average: `9.410 deg/s`
- command-window tail range: `7.471 deg/s .. 10.767 deg/s`
- post-window average: `0.662 deg/s`

Assessment:

- positive velocity command drives output angle in the positive direction
- after the status-sampling fix, reported velocity is much more stable
- in the settled command tail, reported speed is now close to the requested `10 deg/s`
- remaining mismatch is now much smaller and looks like normal closed-loop tracking error rather than a gross scaling/sign bug

## 6. Overall Assessment

Additional moderate-setpoint check:

- `+20 deg` angle-step test:
  - initial status: `45.948 deg`
  - commanded target: `65.948 deg`
  - post-window average: `66.350 deg`
- `20 deg/s` velocity test:
  - command-window tail average velocity: `19.347 deg/s`
  - command-window tail angle increased from `85.529 deg` to `127.183 deg`

This indicates that the corrected-ratio firmware is not limited to only the smallest low-risk setpoints.
At least up to the current moderate `20 deg/s` validation point, sign, scaling, and general tracking are still coherent.

## 7. Settled Velocity Sweep

After improving the `0x417` status sampling and slightly increasing the motor velocity-loop gains to:

- `P = 0.12`
- `I = 0.4`

a settled-window sweep was run.

The settled metric below uses the last approximately `1 s` of the command window rather than the full command average, so it is less contaminated by the intentional slew ramp.

### Positive direction

- `30 deg/s` command:
  - settled average: `30.125 deg/s`
- `40 deg/s` command:
  - settled average: `40.048 deg/s`
- `60 deg/s` command:
  - settled average: `59.985 deg/s`
- `100 deg/s` command:
  - settled average: `99.583 deg/s`

At these setpoints, the settled tracking error is small.

### Apparent failure above `100 deg/s` was range-related

When the sweep continued in the positive direction:

- `150 deg/s`
- `200 deg/s`

the actuator was already near the positive travel edge.

Observed symptom:

- output angle stopped near `2305 deg`
- reported velocity collapsed toward zero

Assessment:

- this is not evidence of a high-speed control failure by itself
- it is consistent with the firmware's travel-limit clamp in velocity mode

### Negative direction away from the upper travel edge

To remove that range-limit contamination, equivalent negative-speed tests were run from the high-angle region:

- `-150 deg/s` command:
  - settled average: `-149.134 deg/s`
- `-200 deg/s` command:
  - settled average: `-197.477 deg/s`

Assessment:

- once the travel-limit clamp is removed from the measurement, the tuned firmware tracks even the current `150 / 200 deg/s` sweep points closely
- the present firmware therefore appears fundamentally capable of accurate tracking through at least `|200 deg/s|`, provided the actuator is not already at a travel edge

## 8. Historical Positive Edge Clamp Behavior on an Earlier Revision

Before the later policy change that removed angle-limit intervention from `OutputVelocity` mode, an explicit long positive `200 deg/s` test was run starting from a mid-to-high angle so the actuator would eventually encounter the configured positive travel limit.

Observed behavior:

- mid command window:
  - velocity average: `197.827 deg/s`
  - angle rose normally
- late command window:
  - angle continued rising into the edge region
  - velocity average dropped to `117.340 deg/s`
  - last reported velocity in that window fell to `32.466 deg/s`
- after the command:
  - angle settled near `2412 deg`
  - post-window velocity average dropped near zero

Assessment:

- the positive-edge clamp is active
- however the present edge behavior is not an instant brake
- that earlier revision's policy was:
  - once a positive velocity command would drive further outward past the allowed range, the target velocity is forced toward `0 deg/s`
  - that zero target then decays through the configured slew limit
- because of that ramp-down, additional overshoot beyond the travel edge is expected before the system fully settles

Implication:

- current behavior is acceptable if the goal is "do not keep accelerating outward forever"
- current behavior is not a hard-stop position hold
- if tighter edge stopping is required later, edge-specific deceleration or braking logic will need to be added

## 9. Follow-up Change After OCP Event

After a later regression attempt, the user reported a bench power-supply OCP event that likely coincided with an overly aggressive stop.

In response, the firmware was changed again so that:

- velocity mode continues to ignore angle-limit braking, per the clarified requirement
- angle mode still uses edge braking
- but the angle-mode edge braking now uses a dedicated softer constant:
  - `ACTUATOR_OUTPUT_EDGE_BRAKE_DEG_S2 = 60.0`
- and the final angle-mode velocity command now also passes through an additional
  angle-mode-only slew limiter:
  - `ACTUATOR_OUTPUT_ANGLE_MODE_SLEW_DEG_S2 = 60.0`

This replaces the previous edge-braking calculation that implicitly reused the much larger outer-loop acceleration budget.

Conservative lower-edge re-validation after the softer braking change:

- current angle before the test was about `82.260 deg`
- commanded target: `0 deg`
- command-window angle moved from about `82.241 deg` to about `-0.497 deg`
- post-window average settled around `-0.060 deg`
- command-window average reported velocity was about `-48.670 deg/s`
- no new PSU OCP event was observed during this conservative run

Additional lower-edge re-validation after adding the final angle-mode slew limiter:

1. First approach from about `165.2 deg`
   - start angle: about `165.234 deg`
   - after the commanded `0 deg` approach and disarm, angle settled around `45.181 deg`
   - no PSU OCP event was observed

2. Second approach from about `45.2 deg`
   - start angle: about `45.187 deg`
   - minimum observed angle during the maneuver: about `-3.315 deg`
   - after the maneuver and disarm, angle settled around `3.41 deg`
   - no PSU OCP event was observed

3. Final near-edge trim from about `3.4 deg`
   - start angle: about `3.422 deg`
   - minimum observed angle during the maneuver: about `-0.113 deg`
   - post-disarm settle remained around `-0.09 deg`
   - no PSU OCP event was observed

Conservative positive-edge re-validation after the final angle-mode slew limiter:

- from the lower-edge region, a large positive command of `5000 deg` was streamed so that the firmware would clamp internally to the configured positive travel limit
- start angle before the run was about `-0.08 deg`
- during the run, output angle rose continuously and then slowed as it approached the stored positive limit
- peak observed angle was about `2173.9 deg`
- after the command and disarm, the output remained settled near `2173.9 deg`
- no PSU OCP event was observed during this positive-edge approach
- runtime diagnostic still showed the expected arm/disarm transitions:
  - armed during the command
  - disarmed again after the explicit `0x237#00`

Status:

- the updated firmware was uploaded successfully
- the softer angle-mode braking revision now has multiple conservative lower-edge re-validations in this report
- the latest revision now also has a conservative positive-edge re-validation
- it still has not yet been re-validated with a more aggressive high-energy edge-stop motion test

Current corrected-ratio result: `pass for boot safety and controlled low-to-high validation`

What is now validated:

- power stage is not auto-armed at boot
- explicit arm/disarm command path works
- arm/disarm no longer shifts the output reference
- small angle commands behave with the expected sign and magnitude
- conservative positive velocity command now produces positive output motion
- conservative `10 deg/s` velocity command now reports about `9.4 deg/s` in the settled tail window
- `+20 deg` angle step also follows with the expected magnitude
- `20 deg/s` velocity command now reports about `19.3 deg/s` in the settled tail window
- settled positive tracking is accurate at `30 / 40 / 60 / 100 deg/s`
- settled negative tracking is accurate at `150 / 200 deg/s`
- the apparent positive failure above `100 deg/s` in the earlier revision was caused by approaching the configured travel edge, not by an immediate control instability
- after the later policy clarification, velocity mode no longer uses angle-limit braking in the current firmware
- the softer angle-mode edge-braking revision now passes multiple conservative lower-edge command-to-zero checks without reproducing the reported PSU OCP
- after adding the final angle-mode slew limiter, lower-edge settling improved further and reached about `-0.09 deg` in the final near-zero trim run without a new PSU OCP event
- after the same change, a large positive angle command now approaches and settles near the positive configured travel limit at about `2173.9 deg` without reproducing the reported PSU OCP

What remains open:

- the latest softer braking revision has not yet been exercised with a more aggressive high-energy edge-stop motion
- PSU regeneration/current margin was inferred from OCP behavior, not measured directly on this bench setup

## 10. Recommended Next Work

1. If tighter stopping near travel edges is still required, tune or replace the current kinematic braking cap plus final slew strategy with a more explicitly limited braking policy.
2. If desired, repeat the positive-edge test with a shorter starting distance and compare overshoot/settling time against the lower-edge case.
3. Keep the current boot-safe arm/disarm, ratio-correct behavior, and settled-window sweep as regression baselines for future tuning.
