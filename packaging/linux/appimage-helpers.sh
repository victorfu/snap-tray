#!/usr/bin/env bash

appimage_squashfs_offset() {
  local appimage="$1"
  local offset

  if ! command -v unsquashfs >/dev/null 2>&1; then
    echo "AppImage extraction requires unsquashfs." >&2
    echo "Install squashfs-tools, then rerun this script." >&2
    exit 1
  fi

  while IFS=: read -r offset _; do
    if [ -n "$offset" ] && unsquashfs -s -o "$offset" "$appimage" >/dev/null 2>&1; then
      echo "$offset"
      return 0
    fi
  done < <(LC_ALL=C grep -aob "hsqs" "$appimage" || true)

  echo "Failed to locate SquashFS payload in $appimage." >&2
  return 1
}

mask_appimage_binfmt_magic() {
  local appimage="$1"
  local magic

  magic="$(od -An -tx1 -j 8 -N 3 "$appimage" | tr -d ' \n')"
  case "$magic" in
    414901|414902)
      # Ubuntu 22.04 AppImageLauncher 2.2.0 registers binfmt handlers for this
      # marker and can break before the embedded AppImage runtime starts.
      # Masking the marker keeps the ELF runtime and SquashFS payload intact.
      printf '\0\0\0' | dd of="$appimage" bs=1 seek=8 conv=notrunc status=none
      ;;
  esac
}
