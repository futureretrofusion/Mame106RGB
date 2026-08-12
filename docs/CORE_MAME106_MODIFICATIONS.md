# Core MAME 0.106 modifications

This project does not only modify the Amiga frontend. The working source also
contains FRF-tagged modifications within the MAME 0.106 core tree.

The complete-source audit found FRF text in 55 files under `mame106/`.

Historically identified core modification files include:

- `mame106/drivers/jailbrek.c`
- `mame106/drivers/timeplt.c`
- `mame106/drivers/dec0.c`
- `mame106/machine/dec0.c`
- `mame106/vidhrdw/dec0.c`
- `mame106/vidhrdw/dec0n.cpp`
- `mame106/sound/sn76496.c`
- `mame106/sound/sn76496.h`
- `mame106/gamedrivers.cmake`
- `mame106/mamedriv.c`
- `mame106/CMakeLists.txt`
- `mame106/drivertuning.cpp`

The source audit confirmed every one of those files was byte-identical between
the live known-good tree and the GitHub staging copy before this repair.

FRF work in the core includes driver fixes/activation, sound work, renderer and
performance work, driver registry/build integration, and FRF performance
profiling support. Source comments and FRF markers are intentionally retained.
