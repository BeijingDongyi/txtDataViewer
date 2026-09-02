#!/bin/bash
set -e

APP_NAME="数据分析"
DESKTOP_ID="txt-data-viewer.desktop"
REAL_HOME="${SNAP_REAL_HOME:-$HOME}"
APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LAUNCHER="$APP_DIR/data.sh"
SOURCE_ICON="$APP_DIR/动易.png"
ICON_NAME="txt-data-viewer"
ICON_DIR="$REAL_HOME/.local/share/icons/hicolor/48x48/apps"
ICON_FILE="$ICON_DIR/$ICON_NAME.png"
APPS_DIR="$REAL_HOME/.local/share/applications"
DESKTOP_FILE="$APPS_DIR/$DESKTOP_ID"

if [ ! -x "$LAUNCHER" ]; then
    chmod +x "$LAUNCHER"
fi

run_clean_gio() {
    env -i \
        HOME="$REAL_HOME" \
        USER="$(id -un)" \
        LOGNAME="$(id -un)" \
        DISPLAY="${DISPLAY:-}" \
        WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
        XAUTHORITY="${XAUTHORITY:-}" \
        XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
        DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-}" \
        PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
        /usr/bin/gio "$@"
}

mkdir -p "$APPS_DIR" "$ICON_DIR"

if [ -f "$SOURCE_ICON" ]; then
    cp "$SOURCE_ICON" "$ICON_FILE"
fi

cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=$APP_NAME
Comment=$APP_NAME
Exec=/bin/bash "$LAUNCHER"
Icon=$ICON_FILE
Terminal=false
Categories=Utility;
Keywords=数据;分析;txt;
StartupNotify=true
EOF

chmod +x "$DESKTOP_FILE"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPS_DIR" >/dev/null 2>&1 || true
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$REAL_HOME/.local/share/icons/hicolor" >/dev/null 2>&1 || true
fi

if command -v xdg-user-dir >/dev/null 2>&1; then
    DESKTOP_DIR="$(xdg-user-dir DESKTOP)"
elif [ -d "$REAL_HOME/桌面" ]; then
    DESKTOP_DIR="$REAL_HOME/桌面"
else
    DESKTOP_DIR="$REAL_HOME/Desktop"
fi

if [ -d "$DESKTOP_DIR" ]; then
    cp "$DESKTOP_FILE" "$DESKTOP_DIR/$DESKTOP_ID"
    chmod +x "$DESKTOP_DIR/$DESKTOP_ID"

    if [ -x /usr/bin/gio ]; then
        run_clean_gio set "$DESKTOP_DIR/$DESKTOP_ID" metadata::trusted true >/dev/null 2>&1 || true
        run_clean_gio set "$DESKTOP_DIR/$DESKTOP_ID" metadata::nautilus-icon-position "" >/dev/null 2>&1 || true
    fi
fi

echo "已创建应用菜单快捷方式：$DESKTOP_FILE"
echo "已安装图标：$ICON_FILE"
if [ -d "$DESKTOP_DIR" ]; then
    echo "已创建桌面快捷方式：$DESKTOP_DIR/$DESKTOP_ID"
fi
