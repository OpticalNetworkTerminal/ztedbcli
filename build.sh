#!/bin/sh
# Compatibility entrypoint for standalone ztedbcli builds.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$SCRIPT_DIR/build_ztedbcli.sh" "$@"
