cmake_minimum_required(VERSION 3.16)
project(FreeTunnel VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Gui Qml Quick Network Svg REQUIRED)
set(TRUSTTUNNEL_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "Path to TrustTunnelClient repository root")

# QHotkey: cross-platform global (system-wide) hotkeys. Fetched at configure
# time; builds a small static lib linked into the app.
include(FetchContent)
set(QHOTKEY_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# QHotkey 1.5.0 declares cmake_minimum_required(VERSION 3.1), which modern CMake
# rejects; relax the floor so its subproject configures.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(
    QHotkey
    GIT_REPOSITORY https://github.com/Skycoder42/QHotkey.git
    GIT_TAG 1.5.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(QHotkey)

set(FREETUNNEL_GIT_VERSION "unknown")
set(FREETUNNEL_CORE_REF "unknown")
find_package(Git QUIET)
if (GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        OUTPUT_VARIABLE FREETUNNEL_GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    # The TrustTunnel core lives in the parent (upstream) checkout. Prefer the
    # nearest *stable* release tag (skip beta/rc/alpha pre-releases), falling
    # back to a short commit when no stable tag is reachable.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${TRUSTTUNNEL_REPO_ROOT}" describe --tags --always
                --exclude "*beta*" --exclude "*rc*" --exclude "*alpha*" --exclude "*-dev*"
        OUTPUT_VARIABLE FREETUNNEL_CORE_REF
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if (NOT FREETUNNEL_CORE_REF)
    set(FREETUNNEL_CORE_REF "unknown")
endif()

set(APP_ICON_RC)
if (WIN32)
    set(APP_ICON_PATH "${CMAKE_CURRENT_LIST_DIR}/assets/logo.ico")
    set(APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/appicon.rc")
    configure_file(win/appicon.rc.in "${APP_ICON_RC}" @ONLY)
endif ()

add_executable(FreeTunnel WIN32 MACOSX_BUNDLE
    main.cpp
    src/app/Backend.cpp
    src/app/BackendHotkeys.cpp
    src/app/BackendUpdates.cpp
    src/app/BackendSettings.cpp
    src/app/BackendLogs.cpp
    src/app/BackendConfig.cpp
    src/app/BackendSplit.cpp
    include/app/Backend.h
    src/core/AppSettings.cpp
    include/core/AppSettings.h
    src/core/ConfigStore.cpp
    include/core/ConfigStore.h
    src/core/DeepLink.cpp
    include/core/DeepLink.h
    src/core/ConfigImport.cpp
    include/core/ConfigImport.h
    src/core/ControlCommand.cpp
    include/core/ControlCommand.h
    src/core/ConfigToml.cpp
    include/core/ConfigToml.h
    src/core/AppUiUtils.cpp
    include/core/AppUiUtils.h
    src/core/UpdateChecker.cpp
    include/core/UpdateChecker.h
    src/core/VersionCompare.cpp
    include/core/VersionCompare.h
    src/core/CredentialStore.cpp
    include/core/CredentialStore.h
    src/core/ReleaseVerify.cpp
    include/core/ReleaseVerify.h
    src/vpn/qt_trusttunnel_client.cpp
    include/vpn/qt_trusttunnel_client.h
    src/vpn/vpn_helper_client.cpp
    include/vpn/vpn_helper_client.h
    src/vpn/vpn_helper_server.cpp
    include/vpn/vpn_helper_server.h
    assets/app.qrc
    qml.qrc
    i18n/i18n.qrc
    ${APP_ICON_RC}
)

target_compile_definitions(FreeTunnel PRIVATE
    FREETUNNEL_VERSION="${PROJECT_VERSION}"
    FREETUNNEL_GIT_VERSION="${FREETUNNEL_GIT_VERSION}"
    FREETUNNEL_CORE_REF="${FREETUNNEL_CORE_REF}"
)

target_include_directories(FreeTunnel PRIVATE
    include
    include/app
    include/core
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
target_link_libraries(FreeTunnel PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network Qt6::Svg QHotkey::QHotkey vpnlibs_trusttunnel)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/FreetunnelDeps.cmake)
freetunnel_link_linux_secrets(FreeTunnel)
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
    MACOSX_BUNDLE_ICON_FILE "logo"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    # Custom Info.plist adds CFBundleURLTypes so macOS routes freetunnel:// and
    # tt:// links to the app (delivered as QFileOpenEvent).
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/macos/Info.plist.in"
)

if (WIN32)
    # The GUI runs as the normal user (asInvoker, the default). Only the helper
    # process is elevated on demand via ShellExecuteEx "runas", which needs
    # shell32 for ShellExecuteExW.
    target_link_libraries(FreeTunnel PRIVATE shell32)
endif()

# OpenSSL for in-app Ed25519 verification (UpdateChecker). On Linux the VPN
# core links Conan BoringSSL statically; FindOpenSSL would pick distro libcrypto
# and break HTTP/3 builds (ECH/HPKE live in BoringSSL libssl.a, not in 22.04's
# OpenSSL 3.0). macOS/Windows use the system/Homebrew/MSVC package explicitly.
if (UNIX AND NOT APPLE)
    if (NOT DISABLE_HTTP3)
        # Upstream lists libcrypto before libssl; ECH needs libssl→libcrypto refs.
        target_link_options(FreeTunnel PRIVATE "LINKER:--start-group")
        target_link_libraries(FreeTunnel PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        target_link_options(FreeTunnel PRIVATE "LINKER:--end-group")
    endif ()
else ()
    find_package(OpenSSL QUIET)
    if (OpenSSL_FOUND)
        target_link_libraries(FreeTunnel PRIVATE OpenSSL::Crypto)
    endif ()
endif ()

if (APPLE)
    # Place macOS icon into the bundle Resources dir
    set_source_files_properties(assets/logo.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(FreeTunnel PRIVATE assets/logo.icns)
    # Unified title bar (transparent bar + full-size content) needs a tiny bit of
    # Objective-C++ talking to AppKit.
    target_sources(FreeTunnel PRIVATE src/app/MacWindow.mm include/app/MacWindow.h)
    target_link_libraries(FreeTunnel PRIVATE "-framework AppKit" "-framework Security")
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
