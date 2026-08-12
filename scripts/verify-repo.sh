#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

fail(){ echo "VERIFY FAILED: $*" >&2; exit 1; }

echo "=== Mame106RGB repository verification ==="

for d in amigamame106 mame106; do
    [ -d "$d" ] || fail "missing source directory: $d"
done

for marker in \
  FRF89K_INPUT_PRESERVING_END_FRAME_SWITCH \
  FRF90B1_PAUSED_COLOR_SWITCH \
  FRF91B_5MODE_BIDIRECTIONAL_OVERLAY \
  FRF91C2_WALLCLOCK_PAUSE_ENVELOPE \
  FRF91C3_RELIABLE_POST_RECREATE_OVERLAY
do
    grep -R -q --exclude-dir=.git -- "$marker" amigamame106 \
      || fail "missing source marker: $marker"
    echo "OK marker: $marker"
done

# Disallow common game-data/media formats.
BAD="$(find . -type f \
  \( -iname '*.chd' -o -iname '*.rom' -o -iname '*.adf' -o -iname '*.hdf' \
     -o -iname '*.iso' -o -iname '*.cue' -o -iname '*.ccd' -o -iname '*.nrg' \
     -o -iname '*.zip' -o -iname '*.7z' -o -iname '*.rar' \
     -o -iname '*.lha' -o -iname '*.lzx' \) \
  ! -path './.git/*' -print)"
[ -z "$BAD" ] || { echo "$BAD"; fail "ROM/archive/disk-image material found"; }

# No generated object/build tree.
BAD_BUILD="$(find . -type f \
  \( -name '*.o' -o -name '*.obj' -o -name '*.o.d' -o -name '*.obj.d' \
     -o -name 'CMakeCache.txt' \) \
  ! -path './.git/*' -print)"
[ -z "$BAD_BUILD" ] || { echo "$BAD_BUILD"; fail "generated build files found"; }

# GitHub hard per-file limit is 100 MiB; refuse well before it.
BIG="$(find . -type f ! -path './.git/*' -size +90M -print)"
[ -z "$BIG" ] || { echo "$BIG"; fail "file over 90 MiB found"; }

echo
echo "Verification PASS."
