# Mame106RGB project lineage and credits

Mame106RGB is not an independent direct-from-MAME Amiga port. It continues a line of work built by several projects and contributor groups.

## Lineage

**MAME 0.106 -> MAME106 MiniMix / amigamame -> Mame106RGB**

### MAME 0.106

The emulator core originates from MAME 0.106 by the MAME development team and contributors.

Historical upstream:
https://github.com/mamedev/historic-mame

Tag used for provenance:
`mame0106`

The historical MAME 0.106 licence is preserved in `LICENSES/MAME-0.106-LICENSE.txt` with provenance recorded in `LICENSES/MAME-0.106-SOURCE.txt`.

### MAME106 MiniMix / amigamame

Mame106RGB's Amiga platform foundation descends substantially from **MAME106 MiniMix / amigamame by krabobmkd**:

https://github.com/krabobmkd/amigamame

MiniMix provided a working modern AmigaOS 68k/68060 MAME 0.106 port and substantial platform infrastructure, including frontend/build integration, Amiga input support, audio, MUI/configuration, filesystem/platform glue, and native/RTG display infrastructure.

The preserved original project README states that MAME106 MiniMix is a MAME 0.106 fork for classic 68060 Amigas and that it is itself heavily based on Mame060 from Triumph for the MUI portion. That historical README is retained byte-for-byte at `docs/ORIGINAL_PROJECT_README.md`.

### Future Retro Fusion / Mame106RGB

Mame106RGB continues development on that foundation. Future Retro Fusion work includes substantial changes and additions such as:

- native HAM6 rendering and optimisation
- EHB64 output and fused EHB conversion paths
- native RGB32, GREYSCALE and RGB16 modes
- runtime native display-mode switching
- safe core/audio pause envelopes during native screen recreation
- input-preserving display handover
- specialised FRF 5-plane and 6-plane chunky-to-planar paths
- specialised HAM dirty-line/reuse and fast-fit work
- PiStorm/Emu68-oriented performance work
- modern Poseidon HID controller support while retaining classic LowLevel/CD32 paths
- driver/core fixes, activation and performance modifications inside the MAME 0.106 tree
- ongoing AmigaOS native video, audio, controller and compatibility development

## Attribution policy

Public Mame106RGB documentation and releases should preserve this ancestry clearly. A concise description is:

> Mame106RGB is a Future Retro Fusion development branch based on MAME106 MiniMix / amigamame by krabobmkd, itself an AmigaOS 68k/68060 port of MAME 0.106. Mame106RGB continues that platform with additional native RGB/EHB/HAM rendering, performance, controller and driver work.

When redistributing source or binaries, retain all applicable upstream copyright notices, file-level notices and licence texts. See `LICENSES/README.md` and `docs/LICENSE_INVENTORY.md`.

No ROMs, CHDs or copyrighted game data are part of the Mame106RGB source distribution.
