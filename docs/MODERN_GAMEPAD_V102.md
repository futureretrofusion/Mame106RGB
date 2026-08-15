# Modern Gamepad / Poseidon HID Support

This source snapshot adds a modern Xbox-style gamepad path for AmigaOS 68k
while retaining the existing classic joystick and CD32 controller paths.

## Tested direction

The current implementation was tested with an 8BitDo wireless Xbox-360-style
gamepad.

The legacy `lowlevel.library` path remains responsible for ordinary direction
input and classic CD32-compatible controls. The extended mode adds a Poseidon
HID Raw Key bridge so MAME can publish controls that do not fit into the
classic CD32-era button set.

## Controls menu

Select the physical LowLevel game-controller protocol for the relevant port,
then choose:

- `Classic CD32 Pad`
- `Modern Gamepad (A/B/X/Y/L1/R1)`
- `Modern HID Extended (Poseidon)`

Existing LowLevel numeric controller-type values are intentionally preserved
so older joystick, mouse, proportional-controller, paddle and light-gun
configuration values do not shift.

## Extended Xbox-style mapping

The extended Poseidon profile publishes:

| Physical control | MAME input |
|---|---|
| A | Button 1 |
| B | Button 2 |
| X | Button 3 |
| Y | Button 4 |
| LB | Button 5 |
| RB | Button 6 |
| LT | Button 7 |
| RT | Button 8 |
| L3 | Button 9 |
| R3 | Button 10 |
| Start | Start |
| Back / View | Select |

The MAME 0.106 input core already provides up to 16 joystick buttons plus
Start and Select per player.

## Poseidon Raw Key bridge

The current bridge expects these Poseidon/hid.class Raw Key actions:

| Physical control | Raw Key action |
|---|---|
| A | Keypad 1 |
| B | Keypad 2 |
| X | Keypad 3 |
| Y | Keypad 4 |
| LB | Keypad 5 |
| RB | Keypad 6 |
| LT | Keypad 7 |
| RT | Keypad 8 |
| L3 | Keypad 9 |
| R3 | Keypad 0 |
| Start | Keypad Enter |
| Back / View | Keypad Decimal |

For controllers exposing LT/RT as analog axes, configure the Poseidon action
to generate the corresponding Raw Key press above a suitable threshold and
release it when the trigger returns.

## Compatibility

The extended HID bridge is additive. Existing classic joystick/CD32 mappings
remain available and existing LowLevel type numbering is preserved.
