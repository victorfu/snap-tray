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

validate_appimage_header() {
  local appimage="$1"
  local elf_magic appimage_magic

  elf_magic="$(od -An -tx1 -N 4 "$appimage" | tr -d ' \n')" || return 1
  appimage_magic="$(od -An -tx1 -j 8 -N 3 "$appimage" | tr -d ' \n')" || return 1
  # Type 2 AppImages must retain AI\002 at offset 8. Catalogs and desktop
  # integration tools use it to identify the image before extracting metadata.
  if [ "$elf_magic" != "7f454c46" ] || [ "$appimage_magic" != "414902" ]; then
    echo "Invalid type 2 AppImage header in $appimage: expected ELF and AI\\002 magic at offset 8." >&2
    return 1
  fi
}
