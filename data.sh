#!/bin/bash
set -e

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$APP_DIR"

TARGET="$APP_DIR/build/main"
if [ ! -x "$TARGET" ]; then
    TARGET="$APP_DIR/main"
fi

exec env -i \
    HOME="${SNAP_REAL_HOME:-$HOME}" \
    USER="$(id -un)" \
    LOGNAME="$(id -un)" \
    DISPLAY="${DISPLAY:-}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
    XAUTHORITY="${XAUTHORITY:-}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
    DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-}" \
    QT_IM_MODULE="${QT_IM_MODULE:-}" \
    PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
    "$TARGET"
