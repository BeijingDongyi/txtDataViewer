cmake_minimum_required(VERSION 3.16)
project(main CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找Qt5核心模块
find_package(Qt5 REQUIRED COMPONENTS Core Widgets Gui)

# 自动处理MOC/UIC/RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 源文件和头文件
set(SOURCES main.cpp mainwindow.cpp)
set(HEADERS mainwindow.h)

# 生成可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 链接Qt5库
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core
    Qt5::Widgets
    Qt5::Gui
)
