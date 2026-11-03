#!/usr/bin/env bash
# Create the identical test file on both drives.
# Usage: DIR_A=/mnt/diskA DIR_B=/mnt/diskB [SIZE_BYTES=8589934592] scripts/make_testfiles.sh
# gen_data refuses to overwrite an existing file, so a rerun is safe.
set -euo pipefail
source "$(dirname "$0")/common.sh"
[[ -x "$BIN/gen_data" ]] || die "build first (BIN=$BIN)"
[[ -n "$DIR_A" && -n "$DIR_B" ]] || die "set DIR_A and DIR_B"
for d in "$DIR_A" "$DIR_B"; do [[ -d "$d" && -w "$d" ]] || die "$d is not a writable directory"; done
if [[ "${ALLOW_SAME_DEVICE:-0}" != 1 ]]; then
  [[ "$(stat -c %d "$DIR_A")" != "$(stat -c %d "$DIR_B")" ]] || die "DIR_A and DIR_B are on the same filesystem"
fi
for d in A B; do
  f=$(file_of "$d")
  echo "creating $f ($SIZE_BYTES bytes)"
  "$BIN/gen_data" "$SIZE_BYTES" "$f"
done
sync
echo "done. Check the drives with: lsblk -d -o NAME,ROTA,MODEL,SIZE,TRAN"
