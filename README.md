# 数据分析脚本 - 部署与使用指南

## 概述
本文档用于指导如何编译、安装并创建桌面快捷方式来运行数据分析脚本。

## 步骤 1：安装依赖库
首先更新系统包列表并安装 Qt5 相关依赖：
sudo apt update && sudo apt install -y qt5-default libqt5core5a libqt5gui5 libqt5widgets5 qtbase5-dev

## 步骤 2：清理旧编译文件
rm -rf CMakeCache.txt CMakeFiles/ cmake_install.cmake Makefile build/

## 步骤 3：编译项目
cmake ./
make

## 步骤 4：创建桌面快捷方式
sudo gedit /usr/share/applications/myscript.desktop
# 4.2 粘贴以下内容（关键修正版）
[Desktop Entry]
Type=Application
Name=数据分析
Comment=数据分析
Exec=/home/dykj/Graduation_project/my_script.sh  # 替换为你的脚本绝对路径
Icon=/usr/share/icons/gnome/48x48/apps/terminal.png  # 可选：用系统自带图标（无需额外下载）
Terminal=true
Categories=System;Utility;
Keywords=脚本;运行;  # 搜索关键词
# 4.3 赋予文件权限
sudo chmod +x /usr/share/applications/myscript.desktop
# 4.4 刷新桌面缓存
update-desktop-database /usr/share/applications/