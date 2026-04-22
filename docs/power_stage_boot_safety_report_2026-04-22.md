# Power Stage Boot Safety Report

Date: `2026-04-22`

Target firmware:

- `custom_f446re`

## 1. Background

Earlier firmware behavior still raised concern that simple board power-up could activate the motor power stage and contribute to power-supply over-current protection.

Code inspection confirmed that the main firmware had previously:

- enabled `PIN_EN_GATE` during boot
- executed `motor.init()`
- executed `initFOC()`

That meant the documentation claim "`CAN enable` is required" was not actually true in the code path at the time.

## 2. Firmware Change

The main firmware was changed so that boot now:

- initializes sensors, FRAM, config, and CAN
- restores output-reference state
- leaves the power stage `disarmed`

The power stage now requires an explicit CAN arm command:

- CAN ID: `0x230 + node_id`
- default node `7`:
  - arm: `0x237#01`
  - disarm: `0x237#00`

Related implementation areas:

- [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp)
- [src/can_service.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/can_service.cpp)
- [src/can_protocol.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/can_protocol.cpp)

## 3. Diagnostic Visibility

Runtime diagnostic frame `0x5F0 + node_id` was extended so `data[7]` is now a bitfield:

- bit0 = output feedback required
- bit1 = power stage armed

This allows boot-safe state to be checked from CAN without immediately commanding motion.

## 4. Upload Result

The updated firmware was uploaded successfully:

```bash
pio run -e custom_f446re -t upload
```

Observed result:

- programming finished
- verify OK
- target reset completed

## 5. Post-Boot CAN Observation

With no arm command sent after upload, CAN traffic showed:

```text
5F7#FB02020101010001
```

Interpretation:

- `data[7] = 0x01`
- bit0 = `1`:
  - output feedback required
- bit1 = `0`:
  - power stage armed = `false`

Assessment:

- after boot, the uploaded firmware reports that the power stage is not armed
- this is the first direct CAN-side confirmation that the main firmware no longer auto-arms at boot

## 6. Arm / Disarm Verification

An explicit CAN arm/disarm sequence was then tested:

```bash
cansend can0 237#01
cansend can0 237#00
```

Observed diagnostic sequence:

```text
FB02020101010001  -> armed = 0
FB02020101010003  -> armed = 1
FB02020101010001  -> armed = 0
```

Interpretation:

- before arm, `data[7] = 0x01`
  - feedback required = `1`
  - armed = `0`
- after `0x237#01`, `data[7] = 0x03`
  - feedback required = `1`
  - armed = `1`
- after `0x237#00`, `data[7] = 0x01`
  - feedback required = `1`
  - armed = `0`

Assessment:

- explicit CAN arm/disarm control is functioning
- the power stage is no longer auto-armed at boot
- arm state is visible and reversible from CAN

## 7. What Is Still Not Verified

This report still does not claim full motion validation.

Still pending:

1. Re-run motion validation with the corrected `8:1` ratio.
2. Confirm commanded angle/velocity magnitudes against physically meaningful output motion.
3. Validate the new velocity-command slew limit during controlled motion tests.
