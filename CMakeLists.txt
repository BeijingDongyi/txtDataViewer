cmake_minimum_required(VERSION 3.16)
project(main CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动查找 Qt6 或 Qt5，适配不同 Ubuntu 版本
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Gui)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Gui)

# 自动处理MOC/UIC/RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 源文件和头文件
set(SOURCES main.cpp mainwindow.cpp)
set(HEADERS mainwindow.h)

# 生成可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 链接 Qt 库
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Gui
)
