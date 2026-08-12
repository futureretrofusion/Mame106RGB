# Known-good native display milestone

This source snapshot is prepared from the working Mame106RGB development tree
after the five-mode native display work.

Expected source markers include:

- `FRF89K_INPUT_PRESERVING_END_FRAME_SWITCH`
- `FRF90B1_PAUSED_COLOR_SWITCH`
- `FRF91B_5MODE_BIDIRECTIONAL_OVERLAY`
- `FRF91C2_WALLCLOCK_PAUSE_ENVELOPE`
- `FRF91C3_RELIABLE_POST_RECREATE_OVERLAY`

## Display navigation

Forward:

`HAM6 -> EHB64 -> RGB32 -> GREYSCALE -> RGB16 -> HAM6`

Reverse:

`HAM6 -> RGB16 -> GREYSCALE -> RGB32 -> EHB64 -> HAM6`

## Important implementation properties

- display recreation does not free the input subsystem during a runtime switch;
- the MAME core/audio pause state surrounds the native screen handover;
- the switch uses a wall-clock pause envelope rather than assuming paused
  `updatescreen()` calls occur exactly at monitor refresh rate;
- the screen-mode popup is queued on a clean update after the new screen has
  been recreated and verified;
- RGB16 is a native 4-plane / 16-colour mode;
- RGB32 and GREYSCALE are native 5-plane modes;
- EHB64 and HAM6 are native 6-plane modes.

This is a development milestone, not a claim that every MAME 0.106 driver has
been exhaustively validated on every Amiga/PiStorm/Emu68 configuration.
