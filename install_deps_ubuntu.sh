#!/bin/bash
set -e

if ! command -v apt-get >/dev/null 2>&1; then
    echo "当前脚本仅适用于 Ubuntu/Debian apt 系统。"
    exit 1
fi

sudo apt-get update

COMMON_PACKAGES=(
    build-essential
    cmake
    libcanberra-gtk-module
    libcanberra-gtk3-module
)

QT6_PACKAGES=(
    qt6-base-dev
    qt6-base-dev-tools
)

QT5_PACKAGES=(
    qtbase5-dev
    qt5-qmake
    qtbase5-dev-tools
    libqt5core5a
    libqt5gui5
    libqt5widgets5
)

if apt-cache show qt6-base-dev >/dev/null 2>&1; then
    sudo apt-get install -y "${COMMON_PACKAGES[@]}" "${QT6_PACKAGES[@]}"
else
    sudo apt-get install -y "${COMMON_PACKAGES[@]}" "${QT5_PACKAGES[@]}"
fi

echo "依赖安装完成。"
