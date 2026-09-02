# 数据分析脚本 - 部署与使用指南

## 概述
本文档用于指导如何编译、安装并创建桌面快捷方式来运行数据分析脚本。

## 步骤 1：安装依赖库
执行依赖安装脚本。脚本会优先安装 Qt6；如果当前 Ubuntu 源没有 Qt6，则自动安装 Qt5。

chmod +x install_deps_ubuntu.sh
./install_deps_ubuntu.sh

说明：Ubuntu 22.04 及更新版本已移除 qt5-default，本项目不再依赖 qt5-default。

## 步骤 2：清理旧编译文件
rm -rf CMakeCache.txt CMakeFiles/ cmake_install.cmake Makefile build/ main_autogen/

## 步骤 3：编译项目
chmod +x build_release.sh
./build_release.sh

运行程序：
./build/main

## 步骤 4：创建桌面快捷方式
执行安装脚本即可，不需要 sudo，也不需要手动打开 gedit 粘贴内容：

chmod +x install_desktop.sh
./install_desktop.sh

安装后可以在应用菜单里搜索“数据分析”启动；如果系统允许桌面启动器，也会在桌面生成一个快捷方式。
