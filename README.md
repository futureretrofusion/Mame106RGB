# Mame106RGB

**Mame106RGB** is an AmigaOS 68k development branch descended from **MAME 0.106** through **MAME106 MiniMix / amigamame by krabobmkd**, focused on native Amiga display output, responsive input, and practical PiStorm/Emu68-era performance work.

## Project lineage and credits

The project lineage is:

**MAME 0.106 -> MAME106 MiniMix / amigamame -> Mame106RGB**

MAME106 MiniMix / amigamame by **krabobmkd** supplied much of the working AmigaOS 68k/68060 platform foundation on which Mame106RGB continues to build, including significant frontend, build, input, audio, MUI/configuration and display infrastructure. MiniMix's own documentation also credits substantial Mame060/Triumph ancestry for the MUI side.

Upstream projects:

- MAME 0.106 historical source: https://github.com/mamedev/historic-mame — tag `mame0106`
- MAME106 MiniMix / amigamame: https://github.com/krabobmkd/amigamame

Future Retro Fusion's subsequent work includes native RGB/EHB/HAM rendering, runtime display switching, specialised C2P and performance paths, PiStorm/Emu68 optimisation, modern controller support, driver/core changes and related Amiga-specific development.

See [`docs/PROJECT_LINEAGE.md`](docs/PROJECT_LINEAGE.md) for the fuller ancestry and attribution statement. The preserved original MiniMix project README remains at [`docs/ORIGINAL_PROJECT_README.md`](docs/ORIGINAL_PROJECT_README.md).

## Current native display modes

The current known-good display-switching baseline provides:

| Mode | Native output |
|---|---|
| HAM6 | 6-plane Hold-And-Modify |
| EHB64 | 6-plane Extra Half-Brite / 64 colours |
| RGB32 | 5-plane / 32 colours |
| GREYSCALE | 5-plane / 32 greys |
| RGB16 | 4-plane / 16 colours |

Runtime controls:

- **Shift+F9** — next display mode
- **Shift+F8** — previous display mode

The live switch path preserves input state, pauses the MAME core/audio around
native screen recreation, verifies the new display, and identifies the selected
mode with a temporary MAME UI overlay.

## Repository layout

- `amigamame106/` — AmigaOS frontend, native video/audio/input and platform code
- `mame106/` — MAME 0.106 core source
- `docs/PROJECT_LINEAGE.md` — project ancestry and attribution
- `docs/KNOWN_GOOD_V91C3.md` — milestone/source notes
- `scripts/verify-repo.sh` — repository hygiene and marker check

## Build environment

This project is developed with a 68k AmigaOS cross-toolchain. The current local
development configuration uses `m68k-amigaos-g++` and 68060/hard-float oriented
build settings.

The configured local build directory is intentionally **not** committed. SDK and
library paths are machine-specific and should be supplied by the developer's
local toolchain/environment.

## ROMs and game data

No ROM sets, CHDs, disk images, copyrighted game archives, or local emulator
runtime data should be committed to this repository.

## Upstream and licensing

This repository contains source derived from MAME 0.106 and MAME106 MiniMix / amigamame, plus Future Retro Fusion and third-party modifications. Upstream and third-party source files retain their existing copyright and licence notices.

The Amiga frontend ancestry must not be described as a direct-from-MAME port: a substantial portion of that platform layer comes from MAME106 MiniMix / amigamame by krabobmkd.

Before redistributing binaries, review the licence/notices included with the specific upstream and third-party components used by the build. See [`LICENSES/README.md`](LICENSES/README.md) and [`docs/LICENSE_INVENTORY.md`](docs/LICENSE_INVENTORY.md).

## Original project documentation

The original working-tree `README.md` from the known-good MiniMix-derived development source is preserved byte-for-byte as [`docs/ORIGINAL_PROJECT_README.md`](docs/ORIGINAL_PROJECT_README.md). The repository root README is intentionally a cleaner GitHub-facing overview while retaining explicit upstream attribution.

## MAME 0.106 licence

The MAME-derived source retains the historical MAME 0.106 licence.
The exact licence from the official `mamedev/historic-mame` tag `mame0106`
is preserved at [`LICENSES/MAME-0.106-LICENSE.txt`](LICENSES/MAME-0.106-LICENSE.txt).

Licence SHA-256: `1af19840b4175b49f3b84fe019afd8f5120afe57426e68eb0ffdc72c338230c5`

ROMs, CHDs and copyrighted game data are not included.
