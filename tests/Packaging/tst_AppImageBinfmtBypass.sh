#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"

# shellcheck source=/dev/null
source "$PROJECT_DIR/packaging/linux/appimage-helpers.sh"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

APPIMAGE="$TMP_DIR/test.AppImage"
head -c 32 /dev/zero >"$APPIMAGE"
printf '\177ELF' | dd of="$APPIMAGE" bs=1 seek=0 conv=notrunc status=none
printf 'AI\002' | dd of="$APPIMAGE" bs=1 seek=8 conv=notrunc status=none

before="$(od -An -tx1 -j 8 -N 3 "$APPIMAGE" | tr -d ' \n')"
if [ "$before" != "414902" ]; then
  echo "test setup failed: expected type 2 AppImage magic at offset 8, got $before" >&2
  exit 1
fi

mask_appimage_binfmt_magic "$APPIMAGE"

after="$(od -An -tx1 -j 8 -N 3 "$APPIMAGE" | tr -d ' \n')"
if [ "$after" != "000000" ]; then
  echo "expected AppImage binfmt magic to be masked, got $after" >&2
  exit 1
fi

elf_magic="$(od -An -tx1 -j 0 -N 4 "$APPIMAGE" | tr -d ' \n')"
if [ "$elf_magic" != "7f454c46" ]; then
  echo "expected ELF header to remain intact, got $elf_magic" >&2
  exit 1
fi
