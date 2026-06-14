# Local-only QML design preview (not part of the app build / CI).
cmake_minimum_required(VERSION 3.16)
project(freetunnel_preview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Gui Widgets Qml Quick Svg REQUIRED)

qt_add_executable(preview main.cpp MockBackend.h preview.qrc)
target_link_libraries(preview PRIVATE Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::Svg)
