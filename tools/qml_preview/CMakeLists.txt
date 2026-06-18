# Local-only QML design preview (not part of the app build / CI).
cmake_minimum_required(VERSION 3.16)
project(freetunnel_preview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Gui Qml Quick Svg REQUIRED)

qt_add_executable(preview main.cpp MockBackend.h preview.qrc)
target_link_libraries(preview PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Svg)
if(APPLE)
    # Recent macOS SDKs dropped AGL; Qt still links it. Stub from OpenGL.tbd.
    set(_agl_fw "${CMAKE_CURRENT_BINARY_DIR}/AGL.framework")
    set(_agl_tbd "${_agl_fw}/Versions/A/AGL.tbd")
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE _macos_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(_opengl_tbd "${_macos_sdk}/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL.tbd")
    add_custom_command(
        OUTPUT "${_agl_tbd}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_agl_fw}/Versions/A/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy "${_opengl_tbd}" "${_agl_tbd}"
        COMMAND ${CMAKE_COMMAND} -E create_symlink A "${_agl_fw}/Versions/Current"
        COMMAND ${CMAKE_COMMAND} -E create_symlink Versions/Current/AGL.tbd "${_agl_fw}/AGL"
        COMMAND ${CMAKE_COMMAND} -E create_symlink Versions/Current/Resources "${_agl_fw}/Resources"
        COMMENT "Stub AGL.framework for linking on macOS"
    )
    add_custom_target(agl_stub DEPENDS "${_agl_tbd}")
    add_dependencies(preview agl_stub)
    target_link_options(preview PRIVATE "LINKER:-F${CMAKE_CURRENT_BINARY_DIR}")
endif()
