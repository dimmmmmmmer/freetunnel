cmake_minimum_required(VERSION 3.16)
project(FreeTunnel VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Widgets Network Svg REQUIRED)
set(TRUSTTUNNEL_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "Path to TrustTunnelClient repository root")

set(FREETUNNEL_GIT_VERSION "unknown")
find_package(Git QUIET)
if (GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE FREETUNNEL_GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

set(APP_ICON_RC)
if (WIN32)
    set(APP_ICON_PATH "${CMAKE_CURRENT_LIST_DIR}/assets/logo.ico")
    set(APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/appicon.rc")
    configure_file(win/appicon.rc.in "${APP_ICON_RC}" @ONLY)
endif ()

add_executable(FreeTunnel WIN32 MACOSX_BUNDLE
    main.cpp
    src/ui/MainWindow.cpp
    include/ui/MainWindow.h
    src/core/AppSettings.cpp
    include/core/AppSettings.h
    src/core/ConfigStore.cpp
    include/core/ConfigStore.h
    src/core/ConfigInspector.cpp
    include/core/ConfigInspector.h
    src/core/DeepLink.cpp
    include/core/DeepLink.h
    src/core/ConfigImport.cpp
    include/core/ConfigImport.h
    src/core/AppUiUtils.cpp
    include/core/AppUiUtils.h
    src/core/NetworkAdapterManager.cpp
    include/core/NetworkAdapterManager.h
    src/core/ProcessManager.cpp
    include/core/ProcessManager.h
    src/core/UpdateChecker.cpp
    include/core/UpdateChecker.h
    src/ui/SettingsDialog.cpp
    include/ui/SettingsDialog.h
    src/ui/ConfigWizard.cpp
    include/ui/ConfigWizard.h
    src/ui/ConnectionRing.cpp
    include/ui/ConnectionRing.h
    src/ui/TrafficGraph.cpp
    include/ui/TrafficGraph.h
    src/vpn/qt_trusttunnel_client.cpp
    include/vpn/qt_trusttunnel_client.h
    assets/app.qrc
    ${APP_ICON_RC}
)

target_compile_definitions(FreeTunnel PRIVATE
    FREETUNNEL_VERSION="${PROJECT_VERSION}"
    FREETUNNEL_GIT_VERSION="${FREETUNNEL_GIT_VERSION}"
)

target_include_directories(FreeTunnel PRIVATE
    include
    include/core
    include/ui
    include/vpn
)

# The client must be built from inside the TrustTunnelClient tree, where the
# `vpnlibs_trusttunnel` target is defined (see .github/workflows/build.yml).
if (NOT TARGET vpnlibs_trusttunnel)
    message(FATAL_ERROR
        "vpnlibs_trusttunnel target not found.\n"
        "Build FreeTunnel from inside the TrustTunnel/TrustTunnelClient tree "
        "(this directory injected as a subdirectory of the core's root CMake), "
        "so the VPN core library is available to link against.")
endif()
target_link_libraries(FreeTunnel PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg vpnlibs_trusttunnel)
target_include_directories(FreeTunnel PRIVATE
    ${TRUSTTUNNEL_REPO_ROOT}/trusttunnel/include
    ${TRUSTTUNNEL_REPO_ROOT}/common/include
    ${TRUSTTUNNEL_REPO_ROOT}/core/include
    ${TRUSTTUNNEL_REPO_ROOT}/net/include
    ${TRUSTTUNNEL_REPO_ROOT}/tcpip/include
)

set_target_properties(FreeTunnel PROPERTIES
    OUTPUT_NAME "FreeTunnel"
    MACOSX_BUNDLE_BUNDLE_NAME "FreeTunnel"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.freetunnel.app"
    MACOSX_BUNDLE_ICON_FILE "logo.icns"
)

if (WIN32 AND MSVC)
    # Request administrator privileges via UAC at launch (needed for TUN interface).
    # Using /MANIFESTUAC linker flag directly — avoids conflict with CMake's
    # auto-generated manifest that defaults to asInvoker.
    set_property(TARGET FreeTunnel APPEND_STRING PROPERTY
        LINK_FLAGS " /MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
endif()

if (APPLE)
    # Place macOS icon into the bundle Resources dir
    set_source_files_properties(assets/logo.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(FreeTunnel PRIVATE assets/logo.icns)
endif()

# Bundle assets
add_custom_command(TARGET FreeTunnel POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_LIST_DIR}/assets/logo.png
        $<TARGET_FILE_DIR:FreeTunnel>/assets/logo.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${TRUSTTUNNEL_REPO_ROOT}/LICENSE
        $<TARGET_FILE_DIR:FreeTunnel>/assets/LICENSE
)

# Lightweight, core-independent unit tests (off by default; the standalone
# tests/CMakeLists.txt is what CI builds directly).
option(BUILD_FREETUNNEL_TESTS "Build FreeTunnel unit tests" OFF)
if (BUILD_FREETUNNEL_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
