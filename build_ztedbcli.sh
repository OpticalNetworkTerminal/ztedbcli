#!/bin/sh
# Build ztedbcli for the target ZTE rootfs.
#
# Examples:
#   cd ztedbcli
#   CC=arm-linux-gnueabi-gcc ./build_ztedbcli.sh
#   ROOTFS=/path/to/rootfs CC=arm-linux-gnueabi-gcc ./build_ztedbcli.sh
#   LIBDIR=/path/to/rootfs/lib CC=arm-linux-gnueabi-gcc ./build_ztedbcli.sh
#   STATIC=1 CC=arm-linux-gnueabi-gcc ./build_ztedbcli.sh
#   STATIC=1 FULL_STATIC=1 UPX=1 CC=arm-linux-gnueabi-gcc ./build_ztedbcli.sh
#   BUNDLE=1 UPX=1 CC=arm-linux-gnueabi-gcc ./build_ztedbcli.sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOTFS=${ROOTFS:-$(cd "$SCRIPT_DIR/.." && pwd)}
CC=${CC:-arm-linux-gnueabi-gcc}
OUT=${OUT:-"$SCRIPT_DIR/ztedbcli"}
CFLAGS=${CFLAGS:-"-O2 -Wall -Wextra"}
LDFLAGS=${LDFLAGS:-""}
STATIC=${STATIC:-0}
FULL_STATIC=${FULL_STATIC:-0}
UPX=${UPX:-0}
BUNDLE=${BUNDLE:-0}
UPX_BIN=${UPX_BIN:-upx}
SYS_LIBS=${SYS_LIBS:-"-ldl -lpthread -lrt -lm"}
FULL_STATIC_LIBS=${FULL_STATIC_LIBS:-"-ldl -lpthread -lrt -lm"}
RUNTIME_LIBS=${RUNTIME_LIBS:-"libdbcspview.so"}
VENDOR_LIBS=${VENDOR_LIBS:-"db oss commfun cfapi ifscfapi"}

case "$STATIC" in
  0|1) ;;
  *) echo "ERROR: STATIC must be 0 or 1" >&2; exit 2 ;;
esac

case "$FULL_STATIC" in
  0|1) ;;
  *) echo "ERROR: FULL_STATIC must be 0 or 1" >&2; exit 2 ;;
esac

case "$UPX" in
  0|1) ;;
  *) echo "ERROR: UPX must be 0 or 1" >&2; exit 2 ;;
esac

case "$BUNDLE" in
  0|1) ;;
  *) echo "ERROR: BUNDLE must be 0 or 1" >&2; exit 2 ;;
esac

if [ "$FULL_STATIC" = 1 ] && [ "$STATIC" != 1 ]; then
  echo "ERROR: FULL_STATIC=1 requires STATIC=1" >&2
  exit 2
fi

if [ "$STATIC" = 1 ]; then
  LIBEXT=a
else
  LIBEXT=so
fi

find_libdir() {
  if [ "${LIBDIR:-}" ]; then
    printf '%s\n' "$LIBDIR"
    return 0
  fi

  for dir in \
    "$SCRIPT_DIR/lib" \
    "$SCRIPT_DIR/rootfs/lib" \
    "$ROOTFS/lib" \
    "$SCRIPT_DIR/../lib" \
    "$SCRIPT_DIR/../rootfs/lib"
  do
    ok=1
    for lib in $VENDOR_LIBS; do
      if [ ! -f "$dir/lib$lib.$LIBEXT" ]; then
        ok=0
        break
      fi
    done
    if [ "$ok" = 1 ]; then
      printf '%s\n' "$dir"
      return 0
    fi
  done

  if [ -d "$SCRIPT_DIR/lib" ]; then
    printf '%s\n' "$SCRIPT_DIR/lib"
    return 0
  fi

  printf '%s\n' "$ROOTFS/lib"
}

LIBDIR=$(find_libdir)

missing=0
for lib in $VENDOR_LIBS; do
  file="lib$lib.$LIBEXT"
  if [ ! -f "$LIBDIR/$file" ]; then
    echo "ERROR: missing $LIBDIR/$file" >&2
    missing=1
  fi
done

if [ "$STATIC" = 0 ]; then
  for lib in $RUNTIME_LIBS; do
    if [ ! -f "$LIBDIR/$lib" ]; then
      echo "WARNING: missing runtime dlopen library $LIBDIR/$lib" >&2
      echo "         DBShmCliInit may fail unless the target device already has it in LD_LIBRARY_PATH." >&2
    fi
  done
fi

if [ "$missing" -ne 0 ]; then
  if [ "$STATIC" = 1 ]; then
    wanted="static archives for: $VENDOR_LIBS"
    note="STATIC=1 needs .a files. A .so cannot be embedded into a true static binary."
  else
    wanted="shared objects for: $VENDOR_LIBS"
    note="Use STATIC=1 only when you have matching .a files."
  fi

  cat >&2 <<EOF

ztedbcli must link against ARM libraries from the target ZTE rootfs.

Wanted $wanted

Use one of:
  ROOTFS=/path/to/extracted/rootfs CC=$CC $0
  LIBDIR=/path/to/extracted/rootfs/lib CC=$CC $0

Or copy the required files next to this script under ./lib/.

For dynamic builds copy:
  libdb.so
  liboss.so
  libcommfun.so
  libcfapi.so
  libifscfapi.so

For static vendor-lib builds copy:
  libdb.a
  liboss.a
  libcommfun.a
  libcfapi.a
  libifscfapi.a

Current LIBDIR: $LIBDIR
$note
EOF
  exit 1
fi

if [ "$STATIC" = 1 ]; then
  if [ "$FULL_STATIC" = 1 ]; then
    "$CC" $CFLAGS -static "$SCRIPT_DIR/ztedbcli.c" \
      -L"$LIBDIR" \
      -Wl,-E \
      -Wl,--start-group -ldb -loss -lcommfun -lcfapi -lifscfapi -Wl,--end-group \
      $FULL_STATIC_LIBS \
      $LDFLAGS \
      -o "$OUT"
  else
    "$CC" $CFLAGS "$SCRIPT_DIR/ztedbcli.c" \
      -L"$LIBDIR" \
      -Wl,-E \
      -Wl,-Bstatic \
      -Wl,--start-group -ldb -loss -lcommfun -lcfapi -lifscfapi -Wl,--end-group \
      -Wl,-Bdynamic \
      $SYS_LIBS \
      $LDFLAGS \
      -o "$OUT"
  fi
else
  "$CC" $CFLAGS "$SCRIPT_DIR/ztedbcli.c" \
    -L"$LIBDIR" \
    -Wl,-E \
    -Wl,-rpath,/lib \
    -Wl,-rpath-link,"$LIBDIR" \
    -Wl,--no-as-needed \
    -Wl,--start-group -ldb -loss -lcommfun -lcfapi -lifscfapi -Wl,--end-group \
    -Wl,--as-needed \
    $SYS_LIBS \
    $LDFLAGS \
    -o "$OUT"
fi

if [ "$UPX" = 1 ]; then
  if ! command -v "$UPX_BIN" >/dev/null 2>&1; then
    echo "ERROR: UPX=1 but '$UPX_BIN' was not found in PATH" >&2
    exit 1
  fi
  "$UPX_BIN" -9 "$OUT"
fi

if [ "$BUNDLE" = 1 ]; then
  BUNDLE_DIR=${BUNDLE_DIR:-"$OUT.bundle"}
  mkdir -p "$BUNDLE_DIR/lib"
  cp "$OUT" "$BUNDLE_DIR/ztedbcli"

  if [ "$STATIC" = 0 ]; then
    for lib in $VENDOR_LIBS; do
      cp "$LIBDIR/lib$lib.so" "$BUNDLE_DIR/lib/"
    done
    for lib in $RUNTIME_LIBS; do
      if [ -f "$LIBDIR/$lib" ]; then
        cp "$LIBDIR/$lib" "$BUNDLE_DIR/lib/"
      fi
    done
  fi

  cat >"$BUNDLE_DIR/run.sh" <<'EOF'
#!/bin/sh
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$DIR/lib:${LD_LIBRARY_PATH:-}"
exec "$DIR/ztedbcli" "$@"
EOF
  chmod +x "$BUNDLE_DIR/run.sh"

  tar -czf "$BUNDLE_DIR.tgz" -C "$(dirname "$BUNDLE_DIR")" "$(basename "$BUNDLE_DIR")"
  echo "bundle: $BUNDLE_DIR.tgz"
fi

echo "built: $OUT"
