# Standalone, lightweight unit tests for self-contained client modules.
# Deliberately does NOT depend on the VPN core, so it builds with just Qt and
# runs fast in CI (see .github/workflows/tests.yml).
cmake_minimum_required(VERSION 3.16)
project(freetunnel-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Allow plaintext credential fallback only for local dev without a keyring.
# CI starts gnome-keyring via dbus-run-session (scripts/run-ctest.sh, which
# .github/workflows/tests.yml runs).
if (NOT DEFINED ENV{CI})
    add_compile_definitions(FT_ALLOW_INSECURE_CREDENTIAL_FALLBACK)
endif()

# Every test target, unconditionally: this is what makes credentialServiceName()
# resolve to a test-only keychain service. QStandardPaths test mode redirects
# files only, so without this a test run rewrites and deletes the real app's
# secrets (the single-instance auth token, per-config passwords).
add_compile_definitions(FT_ENABLE_TEST_HOOKS)

# The per-application split-tunnel units, as a library rather than a source list
# repeated in seven places. They carry platform link requirements — iphlpapi for
# the Windows socket tables, ole32/uuid for following a .lnk — and with a source
# list every target had to remember those separately. Three targets in a row
# were missed that way; a library states the requirement once and propagates it.
add_library(ft_apps STATIC
    ../src/core/AppRules.cpp
    ../src/core/AppShortcut.cpp
    ../src/core/ProcessLookup.cpp
    ../src/core/ProcessLookupSockets.cpp
    ../src/core/ProcessLookupWalk.cpp
    ../src/core/InstalledApps.cpp
    ../src/core/NetBind.cpp
)
target_include_directories(ft_apps PUBLIC ../include ../include/core)
target_link_libraries(ft_apps PUBLIC Qt6::Core Qt6::Network)
if (WIN32)
    target_link_libraries(ft_apps PUBLIC iphlpapi ws2_32 ole32 uuid shell32)
endif ()
set(CMAKE_AUTORCC ON)

# The fast test workflow compiles every file under src/, and until now it did so
# with no warning flags at all — so the only build that saw warnings was the heavy
# release one, where nothing failed on them either. This job runs on every push and
# is the earliest place a warning can be caught, so it is the one that should
# refuse them. The tree is clean under these today; keeping it that way is the
# point. MSVC is left alone: it does not speak these flags and its own /W4 set
# disagrees enough to be a separate decision.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror)
endif()

option(FT_ENABLE_COVERAGE "Instrument tests for gcov/lcov" OFF)
option(FT_ENABLE_SANITIZERS "Build tests with ASan+UBSan (Linux/clang recommended)" OFF)

if (FT_ENABLE_COVERAGE)
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif ()
if (FT_ENABLE_SANITIZERS)
    # -fno-sanitize-recover: by default UBSan prints what it found and carries on,
    # so a test that hit undefined behaviour still exited 0, and ctest keeps a
    # passing test's output to itself. The job was green whatever UBSan said.
    # ASan already stops at its first finding; this makes UBSan do the same.
    # test_ubsan_canary below fails if that ever stops being true.
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined
                        -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined)
endif ()

find_package(Qt6 COMPONENTS Core Gui Qml Quick Test Network Svg REQUIRED)
# Qt 6.9 and later ship the private modules as packages of their own, to be asked
# for by name (a local build against Homebrew's or a distro's current Qt; CI
# builds against QT_VER); before that, finding Gui brought GuiPrivate with it.
if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6 COMPONENTS GuiPrivate REQUIRED)
endif()
if(UNIX AND NOT APPLE)
    find_package(Qt6 COMPONENTS DBus REQUIRED)
endif()

# What the desktop expects of the window's own title bar (DesktopChrome), as a
# library for the same reason as ft_apps: on Linux it carries QtDBus and xcb with
# it, and every target that compiles AppGuiMain.cpp needs both.
add_library(ft_desktop STATIC
    ../src/app/DesktopChrome.cpp
    ../include/app/DesktopChrome.h
)
target_include_directories(ft_desktop PUBLIC ../include)
target_link_libraries(ft_desktop PUBLIC Qt6::Core Qt6::Gui)
if(UNIX AND NOT APPLE)
    target_sources(ft_desktop PRIVATE ../src/app/DesktopChromeLinux.cpp ../src/app/DesktopChromeLinux.h)
    target_link_libraries(ft_desktop PUBLIC Qt6::DBus xcb)
elseif(APPLE)
    target_sources(ft_desktop PRIVATE ../src/app/DesktopChromeMac.mm ../src/app/DesktopChromeMac.h)
    target_link_libraries(ft_desktop PUBLIC "-framework AppKit")
endif()
# Test-only. Main.qml uses Qt.labs.platform for the tray icon, which needs either
# a native implementation or Qt Widgets to fall back to. Under QT_QPA_PLATFORM=
# offscreen there is no native one, and without the fallback loading the real
# window aborts — on macOS with a segfault. The shipped app does not link Widgets
# and does not need to: it runs in a real session where the native path exists.
find_package(Qt6 QUIET OPTIONAL_COMPONENTS Widgets)

include(FetchContent)
set(QHOTKEY_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(
    QHotkey
    GIT_REPOSITORY https://github.com/Skycoder42/QHotkey.git
    # Pinned to the immutable commit behind tag 1.5.0 (git ls-remote
    # https://github.com/Skycoder42/QHotkey 1.5.0): this code is compiled into
    # shipped builds, and a tag can be repointed by whoever owns the repo.
    GIT_TAG 34330d6ff5d2ca111c376f6d7da66be9d1817430 # 1.5.0
)
FetchContent_MakeAvailable(QHotkey)

# REQUIRED on macOS and Windows, as it is for the app (../CMakeLists.txt): those
# are the release platforms. Without OpenSSL, ReleaseVerify is built inert and
# the update-signature tests skip their accept path, the one every release takes;
# ctest counts a skip as a pass, so a runner without it stayed green while never
# checking a signature. Optional on Linux, for local builds; CI installs it.
if(APPLE OR WIN32)
    find_package(OpenSSL REQUIRED)
else()
    find_package(OpenSSL QUIET)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/FreetunnelDeps.cmake)

# macOS SDKs may ship AGL.framework without a linkable binary; Qt still requests it.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    set_property(
        TARGET WrapOpenGL::WrapOpenGL
        PROPERTY INTERFACE_LINK_LIBRARIES "OpenGL::GL")
endif()

# Every test writes its own Qt Test report next to the build tree. ctest on the
# Windows runner captures nothing at all from these binaries — passing ones
# included — so without this a failure there arrives as "***Failed" with no test
# function, no assertion and no line number, and the only way to learn anything
# is to re-run the binary by hand, which does not always reproduce it.
# scripts/run-ctest.sh prints these reports for the tests that failed.
enable_testing()

add_executable(test_deeplink
    test_deeplink.cpp
    ../src/core/DeepLink.cpp
    ../src/core/ConfigToml.cpp
)
target_include_directories(test_deeplink PRIVATE ../include ../include/core)
target_link_libraries(test_deeplink PRIVATE Qt6::Core Qt6::Test)
add_test(NAME deeplink COMMAND test_deeplink -o ${CMAKE_CURRENT_BINARY_DIR}/report-deeplink.txt,txt)

add_executable(test_safe_fileread
    test_safe_fileread.cpp
    ../src/core/AppUiUtils.cpp
)
target_include_directories(test_safe_fileread PRIVATE ../include ../include/core)
target_link_libraries(test_safe_fileread PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME safe_fileread COMMAND test_safe_fileread -o ${CMAKE_CURRENT_BINARY_DIR}/report-safe_fileread.txt,txt)

add_executable(test_appuiutils
    test_appuiutils.cpp
    ../src/core/AppUiUtils.cpp
)
target_include_directories(test_appuiutils PRIVATE ../include ../include/core)
target_link_libraries(test_appuiutils PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME appuiutils COMMAND test_appuiutils -o ${CMAKE_CURRENT_BINARY_DIR}/report-appuiutils.txt,txt)

# AppGuiMain.cpp calls into the Objective-C++ macOS glue, so anything compiling it
# needs MacWindow.mm as well — on Linux and Windows those calls are compiled out.
# Kept in the shared source lists rather than added per target: seven targets pull
# these lists in, and a hand-maintained list of them went stale immediately.
if(APPLE)
    set(FT_MAC_GLUE_SOURCES ../src/app/MacWindow.mm ../include/app/MacWindow.h)
endif()

set(APP_STARTUP_TEST_SOURCES
    test_app_startup.cpp
    helper_ipc_mock_server.cpp
    helper_ipc_mock_server.h
    ../src/app/AppStartup.cpp
    ../src/app/AppGuiMain.cpp
    ../src/app/Backend.cpp
    ../src/app/BackendConnect.cpp
    ../src/app/BackendHotkeys.cpp
    ../src/app/BackendUpdates.cpp
    ../src/app/BackendSettings.cpp
    ../src/app/PlatformAutoStart.cpp
    ../src/core/AppImagePath.cpp
    ../src/app/BackendLogs.cpp
    ../src/app/BackendConfig.cpp
    ../src/app/BackendConfigCreate.cpp
    ../src/app/BackendConfigImport.cpp
    ../src/app/BackendConfigShared.cpp
    ../src/app/BackendSplit.cpp
    ../src/app/LogModel.cpp
    ../include/app/LogModel.h
    ../src/core/AppSettings.cpp
    ../src/core/BypassRules.cpp
    ../src/core/ConfigStore.cpp
    ../src/core/DeepLink.cpp
    ../src/core/ConfigImport.cpp
    ../src/core/ConfigPaths.cpp
    ../src/core/ControlCommand.cpp
    ../src/core/ConfigToml.cpp
    ../src/core/AppUiUtils.cpp
    ../src/core/UpdateChecker.cpp
    ../src/core/AppImagePath.cpp
    ../src/core/VersionCompare.cpp
    ../src/core/ReleaseVerify.cpp
    ../src/core/CredentialStore.cpp
    ../src/core/InstanceControl.cpp
    ../src/vpn/vpn_helper_client.cpp
    ../src/vpn/vpn_helper_elevation.cpp
    ../src/core/AppImagePath.cpp
    ../include/app/AppStartup.h
    ../include/app/Backend.h
    ../include/core/UpdateChecker.h
    ../include/vpn/vpn_helper_client.h
    ${FT_MAC_GLUE_SOURCES}
)
add_executable(test_app_startup ${APP_STARTUP_TEST_SOURCES} ../i18n/i18n.qrc ../qml.qrc)
target_include_directories(test_app_startup PRIVATE
    . ../include ../include/app ../include/core ../include/vpn)
target_compile_definitions(test_app_startup PRIVATE FREETUNNEL_VERSION="test" FT_ENABLE_TEST_HOOKS)
target_link_libraries(test_app_startup PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test Qt6::Network QHotkey::QHotkey ft_apps ft_desktop)
if(TARGET Qt6::Widgets)
    target_link_libraries(test_app_startup PRIVATE Qt6::Widgets)
endif()
freetunnel_link_linux_secrets(test_app_startup)
if(WIN32)
    target_link_libraries(test_app_startup PRIVATE shell32 advapi32)
elseif(APPLE)
    target_link_libraries(test_app_startup PRIVATE Qt6::Svg "-framework Security" "-framework AppKit")
endif()
if(OpenSSL_FOUND)
    target_link_libraries(test_app_startup PRIVATE OpenSSL::Crypto)
endif()
add_test(NAME app_startup COMMAND test_app_startup -o ${CMAKE_CURRENT_BINARY_DIR}/report-app_startup.txt,txt)

set(BACKEND_IMPORT_TEST_SOURCES
    ../src/app/AppStartup.cpp
    ../src/app/AppGuiMain.cpp
    ../src/app/Backend.cpp
    ../src/app/BackendConnect.cpp
    ../src/app/BackendHotkeys.cpp
    ../src/app/BackendUpdates.cpp
    ../src/app/BackendSettings.cpp
    ../src/app/PlatformAutoStart.cpp
    ../src/core/AppImagePath.cpp
    ../src/app/BackendLogs.cpp
    ../src/app/BackendConfig.cpp
    ../src/app/BackendConfigCreate.cpp
    ../src/app/BackendConfigImport.cpp
    ../src/app/BackendConfigShared.cpp
    ../src/app/BackendSplit.cpp
    ../src/app/LogModel.cpp
    ../include/app/LogModel.h
    ../src/core/AppSettings.cpp
    ../src/core/BypassRules.cpp
    ../src/core/ConfigStore.cpp
    ../src/core/DeepLink.cpp
    ../src/core/ConfigImport.cpp
    ../src/core/ConfigPaths.cpp
    ../src/core/ControlCommand.cpp
    ../src/core/ConfigToml.cpp
    ../src/core/AppUiUtils.cpp
    ../src/core/UpdateChecker.cpp
    ../src/core/AppImagePath.cpp
    ../src/core/VersionCompare.cpp
    ../src/core/ReleaseVerify.cpp
    ../src/core/CredentialStore.cpp
    ../src/core/InstanceControl.cpp
    ../src/vpn/vpn_helper_client.cpp
    ../src/vpn/vpn_helper_elevation.cpp
    ../src/core/AppImagePath.cpp
    ../include/app/AppStartup.h
    ../include/app/Backend.h
    ../include/core/UpdateChecker.h
    ../include/vpn/vpn_helper_client.h
    ${FT_MAC_GLUE_SOURCES}
)

add_executable(test_backend_deeplink_import
    test_backend_deeplink_import.cpp
    ${BACKEND_IMPORT_TEST_SOURCES}
    ../i18n/i18n.qrc
    ../qml.qrc
)
target_include_directories(test_backend_deeplink_import PRIVATE
    . ../include ../include/app ../include/core ../include/vpn)
target_compile_definitions(test_backend_deeplink_import PRIVATE FREETUNNEL_VERSION="test" FT_ENABLE_TEST_HOOKS)
target_link_libraries(test_backend_deeplink_import PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test Qt6::Network QHotkey::QHotkey ft_apps ft_desktop)
if(TARGET Qt6::Widgets)
    target_link_libraries(test_backend_deeplink_import PRIVATE Qt6::Widgets)
endif()
freetunnel_link_linux_secrets(test_backend_deeplink_import)
if(WIN32)
    target_link_libraries(test_backend_deeplink_import PRIVATE shell32 advapi32)
elseif(APPLE)
    target_link_libraries(test_backend_deeplink_import PRIVATE Qt6::Svg "-framework Security" "-framework AppKit")
endif()
if(OpenSSL_FOUND)
    target_link_libraries(test_backend_deeplink_import PRIVATE OpenSSL::Crypto)
endif()
add_test(NAME backend_deeplink_import COMMAND test_backend_deeplink_import -o ${CMAKE_CURRENT_BINARY_DIR}/report-backend_deeplink_import.txt,txt)

add_executable(test_vpn_helper_launch
    test_vpn_helper_launch.cpp
    ../src/vpn/vpn_helper_launch.cpp
)
target_include_directories(test_vpn_helper_launch PRIVATE ../include ../include/vpn)
target_link_libraries(test_vpn_helper_launch PRIVATE Qt6::Core Qt6::Test)
add_test(NAME vpn_helper_launch COMMAND test_vpn_helper_launch -o ${CMAKE_CURRENT_BINARY_DIR}/report-vpn_helper_launch.txt,txt)

# What macOS and Windows run elevated to start the helper, read back as each of
# them reads it. The builders live in vpn_helper_elevation.cpp; the parser that
# reads their output on the helper's side is vpn_helper_launch.cpp.
add_executable(test_helper_elevation
    test_helper_elevation.cpp
    ../src/vpn/vpn_helper_client.cpp
    ../src/vpn/vpn_helper_elevation.cpp
    ../src/vpn/vpn_helper_launch.cpp
    ../src/core/AppImagePath.cpp
    ../src/core/AppUiUtils.cpp
    ../include/vpn/vpn_helper_client.h
)
target_include_directories(test_helper_elevation PRIVATE ../include ../include/core ../include/vpn)
target_link_libraries(test_helper_elevation PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
add_test(NAME helper_elevation COMMAND test_helper_elevation -o ${CMAKE_CURRENT_BINARY_DIR}/report-helper_elevation.txt,txt)

add_executable(test_appimagepath
    test_appimagepath.cpp
    ../src/core/AppImagePath.cpp
    ../src/app/PlatformAutoStart.cpp
)
target_include_directories(test_appimagepath PRIVATE ../include ../include/core ../include/app)
target_link_libraries(test_appimagepath PRIVATE Qt6::Core Qt6::Test)
add_test(NAME appimagepath COMMAND test_appimagepath -o ${CMAKE_CURRENT_BINARY_DIR}/report-appimagepath.txt,txt)

add_executable(test_appshortcut
    test_appshortcut.cpp
)

add_executable(test_installedapps
    test_installedapps.cpp
)
target_include_directories(test_installedapps PRIVATE ../include ../include/core)
target_link_libraries(test_installedapps PRIVATE Qt6::Core Qt6::Test ft_apps)
add_test(NAME installedapps COMMAND test_installedapps -o ${CMAKE_CURRENT_BINARY_DIR}/report-installedapps.txt,txt)
target_include_directories(test_appshortcut PRIVATE ../include ../include/core)
target_link_libraries(test_appshortcut PRIVATE Qt6::Core Qt6::Test ft_apps)
add_test(NAME appshortcut COMMAND test_appshortcut -o ${CMAKE_CURRENT_BINARY_DIR}/report-appshortcut.txt,txt)

add_executable(test_apprules
    test_apprules.cpp
)
target_include_directories(test_apprules PRIVATE ../include ../include/core)
target_link_libraries(test_apprules PRIVATE Qt6::Core Qt6::Test ft_apps)
add_test(NAME apprules COMMAND test_apprules -o ${CMAKE_CURRENT_BINARY_DIR}/report-apprules.txt,txt)

add_executable(test_processlookup
    test_processlookup.cpp
)
target_include_directories(test_processlookup PRIVATE ../include ../include/core)
target_link_libraries(test_processlookup PRIVATE Qt6::Core Qt6::Network Qt6::Test ft_apps)
add_test(NAME processlookup COMMAND test_processlookup -o ${CMAKE_CURRENT_BINARY_DIR}/report-processlookup.txt,txt)

add_executable(test_bypassrules
    test_bypassrules.cpp
    ../src/core/BypassRules.cpp
)
target_include_directories(test_bypassrules PRIVATE ../include ../include/core)
target_link_libraries(test_bypassrules PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME bypassrules COMMAND test_bypassrules -o ${CMAKE_CURRENT_BINARY_DIR}/report-bypassrules.txt,txt)

add_executable(test_configpaths
    test_configpaths.cpp
    ../src/core/ConfigPaths.cpp
)
target_include_directories(test_configpaths PRIVATE ../include ../include/core)
target_link_libraries(test_configpaths PRIVATE Qt6::Core Qt6::Test)
add_test(NAME configpaths COMMAND test_configpaths -o ${CMAKE_CURRENT_BINARY_DIR}/report-configpaths.txt,txt)

add_executable(test_configimport
    test_configimport.cpp
    ../src/core/ConfigImport.cpp
    ../src/core/ConfigPaths.cpp
    ../src/core/DeepLink.cpp
    ../src/core/ConfigToml.cpp
)
target_include_directories(test_configimport PRIVATE ../include ../include/core)
target_link_libraries(test_configimport PRIVATE Qt6::Core Qt6::Test)
add_test(NAME configimport COMMAND test_configimport -o ${CMAKE_CURRENT_BINARY_DIR}/report-configimport.txt,txt)

add_executable(test_appsettings
    test_appsettings.cpp
    ../src/core/AppSettings.cpp
    ../src/core/BypassRules.cpp
)
target_include_directories(test_appsettings PRIVATE ../include ../include/core)
target_link_libraries(test_appsettings PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME appsettings COMMAND test_appsettings -o ${CMAKE_CURRENT_BINARY_DIR}/report-appsettings.txt,txt)

add_executable(test_configstore
    test_configstore.cpp
    ../src/core/ConfigStore.cpp
)
target_include_directories(test_configstore PRIVATE ../include ../include/core)
target_link_libraries(test_configstore PRIVATE Qt6::Core Qt6::Test)
add_test(NAME configstore COMMAND test_configstore -o ${CMAKE_CURRENT_BINARY_DIR}/report-configstore.txt,txt)

add_executable(test_control
    test_control.cpp
    ../src/core/ControlCommand.cpp
)
target_include_directories(test_control PRIVATE ../include ../include/core)
target_link_libraries(test_control PRIVATE Qt6::Core Qt6::Test)
add_test(NAME control COMMAND test_control -o ${CMAKE_CURRENT_BINARY_DIR}/report-control.txt,txt)

add_executable(test_instance_control
    test_instance_control.cpp
    ../src/core/InstanceControl.cpp
    ../src/core/CredentialStore.cpp
    ../src/core/ConfigToml.cpp
)
target_include_directories(test_instance_control PRIVATE ../include ../include/core)
target_link_libraries(test_instance_control PRIVATE Qt6::Core Qt6::Network Qt6::Test)
freetunnel_link_linux_secrets(test_instance_control)
if(APPLE)
    target_link_libraries(test_instance_control PRIVATE "-framework Security" "-framework AppKit")
elseif(WIN32)
    target_link_libraries(test_instance_control PRIVATE advapi32)
endif()
add_test(NAME instance_control COMMAND test_instance_control -o ${CMAKE_CURRENT_BINARY_DIR}/report-instance_control.txt,txt)

add_executable(test_configtoml
    test_configtoml.cpp
    ../src/core/ConfigToml.cpp
)
target_include_directories(test_configtoml PRIVATE ../include ../include/core)
target_link_libraries(test_configtoml PRIVATE Qt6::Core Qt6::Test)
add_test(NAME configtoml COMMAND test_configtoml -o ${CMAKE_CURRENT_BINARY_DIR}/report-configtoml.txt,txt)

add_executable(test_versioncompare
    test_versioncompare.cpp
    ../src/core/VersionCompare.cpp
)
target_include_directories(test_versioncompare PRIVATE ../include ../include/core)
target_link_libraries(test_versioncompare PRIVATE Qt6::Core Qt6::Test)
add_test(NAME versioncompare COMMAND test_versioncompare -o ${CMAKE_CURRENT_BINARY_DIR}/report-versioncompare.txt,txt)

add_executable(test_netbind
    test_netbind.cpp
)
target_include_directories(test_netbind PRIVATE ../include ../include/core)
target_link_libraries(test_netbind PRIVATE Qt6::Core Qt6::Network Qt6::Test ft_apps)
add_test(NAME netbind COMMAND test_netbind -o ${CMAKE_CURRENT_BINARY_DIR}/report-netbind.txt,txt)

add_executable(test_releaseverify
    test_releaseverify.cpp
    ../src/core/ReleaseVerify.cpp
)
target_include_directories(test_releaseverify PRIVATE ../include ../include/core)
target_link_libraries(test_releaseverify PRIVATE Qt6::Core Qt6::Test)
find_package(OpenSSL QUIET)
if (OpenSSL_FOUND)
    target_link_libraries(test_releaseverify PRIVATE OpenSSL::Crypto)
    # As in the app: with OpenSSL linked, a ReleaseVerify.cpp that cannot see its
    # headers is a build error here, not a run of skipped tests.
    target_compile_definitions(test_releaseverify PRIVATE FT_REQUIRE_OPENSSL)
endif()
add_test(NAME releaseverify COMMAND test_releaseverify -o ${CMAKE_CURRENT_BINARY_DIR}/report-releaseverify.txt,txt)

# ...and that FT_REQUIRE_OPENSSL really stops such a build: compile
# ReleaseVerify.cpp with it, without OpenSSL's include path, and expect its
# #error. Only where the compiler cannot find the headers on its own (macOS,
# Windows, a Linux box without libssl-dev); where they sit in /usr/include,
# nothing can hide them. A target of its own, built by the test, not by `all`,
# and from a file outside src/, so clang-tidy never sees it.
include(CheckIncludeFileCXX)
check_include_file_cxx(openssl/evp.h FT_OPENSSL_ON_DEFAULT_PATH)
if (NOT FT_OPENSSL_ON_DEFAULT_PATH)
    add_library(releaseverify_without_openssl OBJECT EXCLUDE_FROM_ALL
                releaseverify_without_openssl.cpp)
    target_include_directories(releaseverify_without_openssl PRIVATE ../include ../include/core)
    target_link_libraries(releaseverify_without_openssl PRIVATE Qt6::Core)
    add_test(NAME releaseverify_requires_openssl
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                     --target releaseverify_without_openssl)
    set_tests_properties(releaseverify_requires_openssl PROPERTIES
        PASS_REGULAR_EXPRESSION "OpenSSL is linked but <openssl/evp.h> is not visible")
endif ()

add_executable(test_credentialstore
    test_credentialstore.cpp
    ../src/core/CredentialStore.cpp
    ../src/core/ConfigToml.cpp
)
target_include_directories(test_credentialstore PRIVATE ../include ../include/core)
target_link_libraries(test_credentialstore PRIVATE Qt6::Core Qt6::Test)
freetunnel_link_linux_secrets(test_credentialstore)
if(APPLE)
    target_link_libraries(test_credentialstore PRIVATE "-framework Security" "-framework AppKit")
elseif(WIN32)
    target_link_libraries(test_credentialstore PRIVATE advapi32)
endif()
add_test(NAME credentialstore COMMAND test_credentialstore -o ${CMAKE_CURRENT_BINARY_DIR}/report-credentialstore.txt,txt)

# Whether Linux has a Secret Service to keep passwords in, asked of a private
# dbus-daemon, so unlike credentialstore it needs no keyring on the machine.
if(UNIX AND NOT APPLE)
    add_executable(test_secret_service_probe
        test_secret_service_probe.cpp
        ../src/core/CredentialStore.cpp
        ../src/core/ConfigToml.cpp
    )
    target_include_directories(test_secret_service_probe PRIVATE ../include ../include/core)
    target_link_libraries(test_secret_service_probe PRIVATE Qt6::Core Qt6::Test)
    freetunnel_link_linux_secrets(test_secret_service_probe)
    add_test(NAME secret_service_probe COMMAND test_secret_service_probe -o ${CMAKE_CURRENT_BINARY_DIR}/report-secret_service_probe.txt,txt)
endif()

add_executable(test_helper_ipc
    test_helper_ipc.cpp
    helper_ipc_mock_server.cpp
    helper_ipc_mock_server.h
)
target_include_directories(test_helper_ipc PRIVATE ../include ../include/vpn)
target_link_libraries(test_helper_ipc PRIVATE Qt6::Core Qt6::Test Qt6::Network)
add_test(NAME helper_ipc COMMAND test_helper_ipc -o ${CMAKE_CURRENT_BINARY_DIR}/report-helper_ipc.txt,txt)

add_executable(test_helper_ipc_fuzz
    test_helper_ipc_fuzz.cpp
    helper_ipc_mock_server.cpp
    helper_ipc_mock_server.h
)
target_include_directories(test_helper_ipc_fuzz PRIVATE ../include ../include/vpn)
target_link_libraries(test_helper_ipc_fuzz PRIVATE Qt6::Core Qt6::Test Qt6::Network)
add_test(NAME helper_ipc_fuzz COMMAND test_helper_ipc_fuzz -o ${CMAKE_CURRENT_BINARY_DIR}/report-helper_ipc_fuzz.txt,txt)

add_executable(test_logmodel
    test_logmodel.cpp
    ../src/app/LogModel.cpp
    ../include/app/LogModel.h
)
target_include_directories(test_logmodel PRIVATE ../include)
target_link_libraries(test_logmodel PRIVATE Qt6::Core Qt6::Test)
add_test(NAME logmodel COMMAND test_logmodel -o ${CMAKE_CURRENT_BINARY_DIR}/report-logmodel.txt,txt)

add_executable(test_qml_ui
    test_qml_ui.cpp
    ui/MockBackend.cpp
    ui/MockShell.cpp
    ui/MockDesktop.h
    ui/UiTheme.cpp
    ../src/app/LogModel.cpp
    ../include/app/LogModel.h
    ../qml.qrc
    ../assets/app.qrc
    ../i18n/i18n.qrc
)
target_include_directories(test_qml_ui PRIVATE . ui ../include)
# GuiPrivate for one thing only: standing in for the platform theme, so a test can
# say what Qt reports about light and dark (see QtSays in test_qml_ui.cpp).
target_link_libraries(test_qml_ui PRIVATE Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick Qt6::Test)
# And the real DesktopChrome, for bringWindowForward(), which needs a window to act
# on; the QML itself is given MockDesktop.
target_link_libraries(test_qml_ui PRIVATE ft_desktop)
if(TARGET Qt6::Widgets)
    target_link_libraries(test_qml_ui PRIVATE Qt6::Widgets)
endif()
add_test(NAME qml_ui COMMAND test_qml_ui -o ${CMAKE_CURRENT_BINARY_DIR}/report-qml_ui.txt,txt)

# The Windows window chrome (src/app/WindowsChrome.cpp) on the one platform it runs
# on, and on the real Windows platform plugin: under offscreen, which the rest of
# the suite uses, the app does not set it up at all. QWindowKit comes from the
# file the app takes it from, so the same commit and options are tested.
if(WIN32)
    include(${CMAKE_CURRENT_LIST_DIR}/../cmake/QWindowKit.cmake)

    add_executable(test_windows_chrome
        test_windows_chrome.cpp
        ../src/app/WindowsChrome.cpp
        ../include/app/WindowsChrome.h
    )
    target_include_directories(test_windows_chrome PRIVATE ../include)
    target_compile_definitions(test_windows_chrome PRIVATE FT_HAVE_QWINDOWKIT)
    target_link_libraries(test_windows_chrome PRIVATE
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test QWindowKit::Quick)
    add_test(NAME windows_chrome COMMAND test_windows_chrome
        -o ${CMAKE_CURRENT_BINARY_DIR}/report-windows_chrome.txt,txt)
    # Over the offscreen run-ctest.sh sets: there the window id is a counter, not
    # an HWND, and the agent would hand it to Win32 as one.
    set_tests_properties(windows_chrome PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
endif()

add_executable(test_desktopchrome test_desktopchrome.cpp)
target_link_libraries(test_desktopchrome PRIVATE ft_desktop Qt6::Test)
if(UNIX AND NOT APPLE)
    # The portal tests drive the Linux half directly, on a private bus.
    target_include_directories(test_desktopchrome PRIVATE ../src/app)
    target_compile_definitions(test_desktopchrome PRIVATE FT_TEST_PORTAL)
endif()
add_test(NAME desktopchrome COMMAND test_desktopchrome -o ${CMAKE_CURRENT_BINARY_DIR}/report-desktopchrome.txt,txt)

add_executable(test_integration_config_workflow
    test_integration_config_workflow.cpp
    ../src/core/ConfigImport.cpp
    ../src/core/ConfigPaths.cpp
    ../src/core/DeepLink.cpp
    ../src/core/ConfigStore.cpp
    ../src/core/ConfigToml.cpp
    ../src/core/CredentialStore.cpp
)
target_include_directories(test_integration_config_workflow PRIVATE ../include ../include/core)
target_link_libraries(test_integration_config_workflow PRIVATE Qt6::Core Qt6::Test)
freetunnel_link_linux_secrets(test_integration_config_workflow)
if(APPLE)
    target_link_libraries(test_integration_config_workflow PRIVATE "-framework Security" "-framework AppKit")
elseif(WIN32)
    target_link_libraries(test_integration_config_workflow PRIVATE advapi32)
endif()
add_test(NAME integration_config_workflow COMMAND test_integration_config_workflow -o ${CMAKE_CURRENT_BINARY_DIR}/report-integration_config_workflow.txt,txt)

add_executable(test_integration_single_instance
    test_integration_single_instance.cpp
    # Production sender + token/framing logic, so this exercises the auth
    # boundary instead of re-testing Qt's socket delivery.
    ../src/core/InstanceControl.cpp
    ../src/core/CredentialStore.cpp
    ../src/core/ConfigToml.cpp
)
target_include_directories(test_integration_single_instance PRIVATE ../include ../include/core)
target_link_libraries(test_integration_single_instance PRIVATE Qt6::Core Qt6::Test Qt6::Network)
freetunnel_link_linux_secrets(test_integration_single_instance)
if(APPLE)
    target_link_libraries(test_integration_single_instance PRIVATE "-framework Security" "-framework AppKit")
endif()
add_test(NAME integration_single_instance COMMAND test_integration_single_instance -o ${CMAKE_CURRENT_BINARY_DIR}/report-integration_single_instance.txt,txt)

add_executable(test_integration_helper_client
    test_integration_helper_client.cpp
    helper_ipc_mock_server.cpp
    helper_ipc_mock_server.h
    # The real GUI-side client, so the handshake tests exercise production code
    # rather than two test doubles talking to each other.
    ../src/vpn/vpn_helper_client.cpp
    ../src/vpn/vpn_helper_elevation.cpp
    ../src/core/AppImagePath.cpp
    ../src/core/AppUiUtils.cpp
    ../include/vpn/vpn_helper_client.h
)
target_include_directories(test_integration_helper_client PRIVATE
    ../include ../include/core ../include/vpn)
target_link_libraries(test_integration_helper_client PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test Qt6::Network)
add_test(NAME integration_helper_client COMMAND test_integration_helper_client -o ${CMAKE_CURRENT_BINARY_DIR}/report-integration_helper_client.txt,txt)

set(BACKEND_TEST_SOURCES
    test_integration_backend_vpn.cpp
    helper_ipc_mock_server.cpp
    helper_ipc_mock_server.h
    ../i18n/i18n.qrc
    ../src/app/Backend.cpp
    ../src/app/BackendConnect.cpp
    ../src/app/BackendHotkeys.cpp
    ../src/app/BackendUpdates.cpp
    ../src/app/BackendSettings.cpp
    ../src/app/PlatformAutoStart.cpp
    ../src/core/AppImagePath.cpp
    ../src/app/BackendLogs.cpp
    ../src/app/BackendConfig.cpp
    ../src/app/BackendConfigCreate.cpp
    ../src/app/BackendConfigImport.cpp
    ../src/app/BackendConfigShared.cpp
    ../src/app/BackendSplit.cpp
    ../src/app/LogModel.cpp
    ../include/app/LogModel.h
    ../src/core/AppSettings.cpp
    ../src/core/BypassRules.cpp
    ../src/core/ConfigStore.cpp
    ../src/core/DeepLink.cpp
    ../src/core/ConfigImport.cpp
    ../src/core/ConfigPaths.cpp
    ../src/core/ControlCommand.cpp
    ../src/core/ConfigToml.cpp
    ../src/core/AppUiUtils.cpp
    ../src/core/UpdateChecker.cpp
    ../src/core/AppImagePath.cpp
    ../src/core/VersionCompare.cpp
    ../src/core/ReleaseVerify.cpp
    ../src/core/CredentialStore.cpp
    ../src/core/InstanceControl.cpp
    ../src/vpn/vpn_helper_client.cpp
    ../src/vpn/vpn_helper_elevation.cpp
    ../src/core/AppImagePath.cpp
    ../include/app/Backend.h
    ../include/core/UpdateChecker.h
    ../include/vpn/vpn_helper_client.h
    # Backend::startWindowDrag moves the window through the macOS glue, so this
    # list needs it too — the same reason as the two lists above.
    ${FT_MAC_GLUE_SOURCES}
)
add_executable(test_integration_backend_vpn ${BACKEND_TEST_SOURCES})
target_include_directories(test_integration_backend_vpn PRIVATE
    . ../include ../include/app ../include/core ../include/vpn)
target_compile_definitions(test_integration_backend_vpn PRIVATE FREETUNNEL_VERSION="test" FT_ENABLE_TEST_HOOKS)
target_link_libraries(test_integration_backend_vpn PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test Qt6::Network QHotkey::QHotkey ft_apps)
freetunnel_link_linux_secrets(test_integration_backend_vpn)
if(WIN32)
    target_link_libraries(test_integration_backend_vpn PRIVATE shell32 advapi32)
elseif(APPLE)
    target_link_libraries(test_integration_backend_vpn PRIVATE "-framework Security" "-framework AppKit")
endif()
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    target_link_libraries(test_integration_backend_vpn PRIVATE OpenSSL::Crypto)
endif()
add_test(NAME integration_backend_vpn COMMAND test_integration_backend_vpn -o ${CMAKE_CURRENT_BINARY_DIR}/report-integration_backend_vpn.txt,txt)

add_executable(test_update_checker_e2e
    test_update_checker_e2e.cpp
    mock_http_server.cpp
    mock_http_server.h
    ../src/core/UpdateChecker.cpp
    ../src/core/AppImagePath.cpp
    ../src/core/VersionCompare.cpp
    ../src/core/ReleaseVerify.cpp
    ../include/core/UpdateChecker.h
)
target_include_directories(test_update_checker_e2e PRIVATE . ../include ../include/core)
target_compile_definitions(test_update_checker_e2e PRIVATE FT_ENABLE_TEST_HOOKS)
target_link_libraries(test_update_checker_e2e PRIVATE Qt6::Core Qt6::Test Qt6::Network)
if(OpenSSL_FOUND)
    target_link_libraries(test_update_checker_e2e PRIVATE OpenSSL::Crypto)
endif()
add_test(NAME update_checker_e2e COMMAND test_update_checker_e2e -o ${CMAKE_CURRENT_BINARY_DIR}/report-update_checker_e2e.txt,txt)

# QtTrustTunnelClient state machine against the mock core in tests/mock_core —
# connect/disconnect/config-switch/reconnect behavior without the native VPN
# core. The mock include dir must come FIRST so "vpn/vpn.h", "net/*.h" and
# <toml++/toml.h> resolve to the stubs.
# The core-event translation layer on its own: pure functions that decide which
# error the user is shown and how the core's log tail is folded into ours.
add_executable(test_vpn_events
    test_vpn_events.cpp
    ../src/vpn/qt_trusttunnel_events.cpp
    ../src/vpn/qt_trusttunnel_platform.cpp
)
target_include_directories(test_vpn_events PRIVATE
    mock_core ../include ../include/vpn ../src/vpn)
target_compile_definitions(test_vpn_events PRIVATE FT_ENABLE_TEST_HOOKS)
target_link_libraries(test_vpn_events PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME vpn_events COMMAND test_vpn_events -o ${CMAKE_CURRENT_BINARY_DIR}/report-vpn_events.txt,txt)

add_executable(test_qt_trusttunnel_client
    test_qt_trusttunnel_client.cpp
    ../src/vpn/qt_trusttunnel_client.cpp
    ../src/vpn/qt_trusttunnel_corelog.cpp
    ../src/vpn/qt_trusttunnel_fdwatch.cpp
    ../src/vpn/qt_trusttunnel_uplink.cpp
    ../src/vpn/qt_trusttunnel_connect.cpp
    ../src/vpn/qt_trusttunnel_callbacks.cpp
    ../src/vpn/qt_trusttunnel_events.cpp
    ../src/vpn/qt_trusttunnel_platform.cpp
    ../include/vpn/qt_trusttunnel_client.h
)
target_include_directories(test_qt_trusttunnel_client PRIVATE
    mock_core ../include ../include/vpn ../src/vpn)
target_compile_definitions(test_qt_trusttunnel_client PRIVATE FT_ENABLE_TEST_HOOKS)
target_link_libraries(test_qt_trusttunnel_client PRIVATE Qt6::Core Qt6::Network Qt6::Test ft_apps)
add_test(NAME qt_trusttunnel_client COMMAND test_qt_trusttunnel_client -o ${CMAKE_CURRENT_BINARY_DIR}/report-qt_trusttunnel_client.txt,txt)

# Split-tunnel behaviour (routes, profiles, and which edits disturb a live
# tunnel). Reuses the Backend source set — the logic under test lives on Backend.
add_executable(test_backend_logs
    test_backend_logs.cpp
    ${BACKEND_IMPORT_TEST_SOURCES}
    ../i18n/i18n.qrc
    ../qml.qrc
)
target_include_directories(test_backend_logs PRIVATE
    . ../include ../include/app ../include/core ../include/vpn ../src)
target_compile_definitions(test_backend_logs PRIVATE FREETUNNEL_VERSION="test" FT_ENABLE_TEST_HOOKS)
target_link_libraries(test_backend_logs PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test Qt6::Network QHotkey::QHotkey ft_apps ft_desktop)
freetunnel_link_linux_secrets(test_backend_logs)
if(WIN32)
    target_link_libraries(test_backend_logs PRIVATE shell32 advapi32)
elseif(APPLE)
    target_link_libraries(test_backend_logs PRIVATE Qt6::Svg "-framework Security" "-framework AppKit")
endif()
if(OpenSSL_FOUND)
    target_link_libraries(test_backend_logs PRIVATE OpenSSL::Crypto)
endif()
add_test(NAME backend_logs COMMAND test_backend_logs -o ${CMAKE_CURRENT_BINARY_DIR}/report-backend_logs.txt,txt)

add_executable(test_backend_settings
    test_backend_settings.cpp
    ${BACKEND_IMPORT_TEST_SOURCES}
    ../i18n/i18n.qrc
    ../qml.qrc
)
target_include_directories(test_backend_settings PRIVATE
    . ../include ../include/app ../include/core ../include/vpn ../src)
target_compile_definitions(test_backend_settings PRIVATE FREETUNNEL_VERSION="test" FT_ENABLE_TEST_HOOKS)
target_link_libraries(test_backend_settings PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test Qt6::Network QHotkey::QHotkey ft_apps ft_desktop)
freetunnel_link_linux_secrets(test_backend_settings)
if(WIN32)
    target_link_libraries(test_backend_settings PRIVATE shell32 advapi32)
elseif(APPLE)
    target_link_libraries(test_backend_settings PRIVATE Qt6::Svg "-framework Security" "-framework AppKit")
endif()
if(OpenSSL_FOUND)
    target_link_libraries(test_backend_settings PRIVATE OpenSSL::Crypto)
endif()
add_test(NAME backend_settings COMMAND test_backend_settings -o ${CMAKE_CURRENT_BINARY_DIR}/report-backend_settings.txt,txt)

add_executable(test_backend_config
    test_backend_config.cpp
    ${BACKEND_IMPORT_TEST_SOURCES}
    ../i18n/i18n.qrc
    ../qml.qrc
)
target_include_directories(test_backend_config PRIVATE
    . ../include ../include/app ../include/core ../include/vpn ../src)
target_compile_definitions(test_backend_config PRIVATE FREETUNNEL_VERSION="test" FT_ENABLE_TEST_HOOKS)
target_link_libraries(test_backend_config PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test Qt6::Network QHotkey::QHotkey ft_apps ft_desktop)
freetunnel_link_linux_secrets(test_backend_config)
if(WIN32)
    target_link_libraries(test_backend_config PRIVATE shell32 advapi32)
elseif(APPLE)
    target_link_libraries(test_backend_config PRIVATE Qt6::Svg "-framework Security" "-framework AppKit")
endif()
if(OpenSSL_FOUND)
    target_link_libraries(test_backend_config PRIVATE OpenSSL::Crypto)
endif()
add_test(NAME backend_config COMMAND test_backend_config -o ${CMAKE_CURRENT_BINARY_DIR}/report-backend_config.txt,txt)

add_executable(test_backend_split
    test_backend_split.cpp
    ${BACKEND_IMPORT_TEST_SOURCES}
    ../i18n/i18n.qrc
    ../qml.qrc
)
target_include_directories(test_backend_split PRIVATE
    . ../include ../include/app ../include/core ../include/vpn)
target_compile_definitions(test_backend_split PRIVATE FREETUNNEL_VERSION="test" FT_ENABLE_TEST_HOOKS)
target_link_libraries(test_backend_split PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test Qt6::Network QHotkey::QHotkey ft_apps ft_desktop)
freetunnel_link_linux_secrets(test_backend_split)
if(WIN32)
    target_link_libraries(test_backend_split PRIVATE shell32 advapi32)
elseif(APPLE)
    target_link_libraries(test_backend_split PRIVATE Qt6::Svg "-framework Security" "-framework AppKit")
endif()
if(OpenSSL_FOUND)
    target_link_libraries(test_backend_split PRIVATE OpenSSL::Crypto)
endif()
add_test(NAME backend_split COMMAND test_backend_split -o ${CMAKE_CURRENT_BINARY_DIR}/report-backend_split.txt,txt)

# Backend and the MockBackend the QML tests run against, side by side, so the two
# cannot drift apart without a test saying so.
add_executable(test_qml_backend_parity
    test_qml_backend_parity.cpp
    ui/MockBackend.cpp
    ${BACKEND_IMPORT_TEST_SOURCES}
    ../i18n/i18n.qrc
)
target_include_directories(test_qml_backend_parity PRIVATE
    . ../include ../include/app ../include/core ../include/vpn)
target_compile_definitions(test_qml_backend_parity PRIVATE FREETUNNEL_VERSION="test" FT_ENABLE_TEST_HOOKS)
target_link_libraries(test_qml_backend_parity PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test Qt6::Network QHotkey::QHotkey ft_apps ft_desktop)
freetunnel_link_linux_secrets(test_qml_backend_parity)
if(WIN32)
    target_link_libraries(test_qml_backend_parity PRIVATE shell32 advapi32)
elseif(APPLE)
    target_link_libraries(test_qml_backend_parity PRIVATE Qt6::Svg "-framework Security" "-framework AppKit")
endif()
if(OpenSSL_FOUND)
    target_link_libraries(test_qml_backend_parity PRIVATE OpenSSL::Crypto)
endif()
add_test(NAME qml_backend_parity COMMAND test_qml_backend_parity -o ${CMAKE_CURRENT_BINARY_DIR}/report-qml_backend_parity.txt,txt)

# The REAL privileged helper server, built against the mock core so it needs
# neither the VPN core nor root. Previously vpn_helper_server.cpp — the code that
# runs as root and parses everything the GUI sends — was in no test target at all
# and the IPC tests drove a hand-written double with a different pre-auth policy.
add_executable(freetunnel_test_helper
    helper_server_main.cpp
    ../src/vpn/vpn_helper_server.cpp
    ../src/vpn/vpn_helper_launch.cpp
    ../src/vpn/qt_trusttunnel_client.cpp
    ../src/vpn/qt_trusttunnel_corelog.cpp
    ../src/vpn/qt_trusttunnel_fdwatch.cpp
    ../src/vpn/qt_trusttunnel_uplink.cpp
    ../src/vpn/qt_trusttunnel_connect.cpp
    ../src/vpn/qt_trusttunnel_callbacks.cpp
    ../src/vpn/qt_trusttunnel_events.cpp
    ../src/vpn/qt_trusttunnel_platform.cpp
    ../include/vpn/qt_trusttunnel_client.h  # AUTOMOC needs the Q_OBJECT header listed
)
target_include_directories(freetunnel_test_helper PRIVATE
    mock_core ../include ../include/vpn ../src/vpn)
target_compile_definitions(freetunnel_test_helper PRIVATE FT_ENABLE_TEST_HOOKS)
target_link_libraries(freetunnel_test_helper PRIVATE Qt6::Core Qt6::Network ft_apps)

add_executable(test_helper_server
    test_helper_server.cpp
    ../src/vpn/vpn_helper_client.cpp
    ../src/vpn/vpn_helper_elevation.cpp
    ../src/core/AppImagePath.cpp
    ../src/core/AppUiUtils.cpp
    ../include/vpn/vpn_helper_client.h  # AUTOMOC needs the Q_OBJECT header listed
)
target_include_directories(test_helper_server PRIVATE . ../include ../include/core ../include/vpn)
target_compile_definitions(test_helper_server PRIVATE FT_ENABLE_TEST_HOOKS
    FT_TEST_HELPER_BINARY="$<TARGET_FILE:freetunnel_test_helper>")
target_link_libraries(test_helper_server PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
add_dependencies(test_helper_server freetunnel_test_helper)
add_test(NAME helper_server COMMAND test_helper_server -o ${CMAKE_CURRENT_BINARY_DIR}/report-helper_server.txt,txt)

# The same real helper, for what it promises about its own life (when it leaves,
# what it refuses once claimed), for the settings only it acts on, and for random
# input: each used to be checked against MockHelperServer, or not at all.
# tests/real_helper.h starts it.
foreach(suite IN ITEMS lifecycle settings fuzz)
    add_executable(test_helper_server_${suite}
        test_helper_server_${suite}.cpp
        real_helper.h
        ../src/vpn/vpn_helper_client.cpp
        ../src/vpn/vpn_helper_elevation.cpp
        ../src/core/AppImagePath.cpp
        ../src/core/AppUiUtils.cpp
        ../include/vpn/vpn_helper_client.h
    )
    target_include_directories(test_helper_server_${suite} PRIVATE . ../include ../include/core ../include/vpn)
    target_compile_definitions(test_helper_server_${suite} PRIVATE FT_ENABLE_TEST_HOOKS
        FT_TEST_HELPER_BINARY="$<TARGET_FILE:freetunnel_test_helper>")
    target_link_libraries(test_helper_server_${suite} PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
    add_dependencies(test_helper_server_${suite} freetunnel_test_helper)
    add_test(NAME helper_server_${suite} COMMAND test_helper_server_${suite}
        -o ${CMAKE_CURRENT_BINARY_DIR}/report-helper_server_${suite}.txt,txt)
endforeach()
# A config as the app writes it, for the endpoint keys only the core acts on.
target_sources(test_helper_server_settings PRIVATE ../src/core/ConfigToml.cpp)

# Backend's update state machine: the code that ends in launching an installer
# sat at 2.3% line coverage, and both bugs found in it were found by review.
add_executable(test_backend_updates
    test_backend_updates.cpp
    mock_http_server.cpp
    mock_http_server.h
    ${BACKEND_IMPORT_TEST_SOURCES}
    ../i18n/i18n.qrc
    ../qml.qrc
)
target_include_directories(test_backend_updates PRIVATE
    . ../include ../include/app ../include/core ../include/vpn)
target_compile_definitions(test_backend_updates PRIVATE FREETUNNEL_VERSION="1.0.0" FT_ENABLE_TEST_HOOKS)
target_link_libraries(test_backend_updates PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test Qt6::Network QHotkey::QHotkey ft_apps ft_desktop)
freetunnel_link_linux_secrets(test_backend_updates)
if(WIN32)
    target_link_libraries(test_backend_updates PRIVATE shell32 advapi32)
elseif(APPLE)
    target_link_libraries(test_backend_updates PRIVATE Qt6::Svg "-framework Security" "-framework AppKit")
endif()
if(OpenSSL_FOUND)
    target_link_libraries(test_backend_updates PRIVATE OpenSSL::Crypto)
endif()
add_test(NAME backend_updates COMMAND test_backend_updates -o ${CMAKE_CURRENT_BINARY_DIR}/report-backend_updates.txt,txt)

# Release-signing fixture for test_update_checker_e2e.
#
# UpdateChecker checks SHA256SUMS.txt against the key compiled in from
# include/core/ReleaseSigning.h, whose private half is a GitHub Actions secret.
# No test can therefore produce a signature the shipped key *accepts*, which is
# why every signature test so far only proved we fail closed, and the
# verifyEd25519Signature() == true -> fetchInstaller() branch — the one every
# genuine release takes — ran in no test at all. A wiring mistake there (wrong
# buffer, wrong argument order, wrong key) rejects every real update and freezes
# the whole user base on its current build.
#
# So generate a stand-in ReleaseSigning.h holding a throwaway key whose seed the
# test knows, and put it first on that one target's include path: UpdateChecker
# includes "ReleaseSigning.h" unqualified, so this copy wins there and nowhere
# else — every other target still compiles the real key. The test re-derives
# this PEM from the seed and compares it against what got compiled in, so the
# two halves of the fixture cannot drift apart without a red test.
set(FT_SIGNING_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/signing_fixture")
file(GENERATE OUTPUT "${FT_SIGNING_FIXTURE_DIR}/ReleaseSigning.h" CONTENT [==[
// Generated by tests/CMakeLists.txt — do not edit, and do not ship. Test-only
// stand-in for include/core/ReleaseSigning.h: the public half of the throwaway
// Ed25519 key that test_update_checker_e2e.cpp signs its manifests with.
#pragma once

namespace freetunnel {

inline constexpr const char *kReleaseSigningPublicKeyPem =
    "-----BEGIN PUBLIC KEY-----\n"
    "MCowBQYDK2VwAyEAlAi6MehEBu+WUcE7TmGHYGuqvFiSQuhdf5rXAq2iij4=\n"
    "-----END PUBLIC KEY-----\n";

} // namespace freetunnel
]==])
target_include_directories(test_update_checker_e2e BEFORE PRIVATE "${FT_SIGNING_FIXTURE_DIR}")

# Proof that a UBSan finding fails the sanitizer job (see FT_ENABLE_SANITIZERS
# above). The canary overflows a signed int and has to die doing it; if it ever
# exits 0, UBSan has gone back to reporting and carrying on.
if (FT_ENABLE_SANITIZERS)
    add_executable(test_ubsan_canary test_ubsan_canary.cpp)
    add_test(NAME ubsan_canary COMMAND test_ubsan_canary)
    set_tests_properties(ubsan_canary PROPERTIES WILL_FAIL TRUE)
endif ()

# The release job's check that Tests and Security passed on the tagged commit
# (scripts/check-release-ci.sh) only ever runs on a tag, so it is tested here,
# on every push, against a stand-in gh. Linux only: it is a bash script for an
# ubuntu runner, and a bash found on Windows can be WSL's.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(FT_BASH bash)
    find_program(FT_JQ jq)
    if (FT_BASH AND FT_JQ)
        add_test(NAME release_ci_gate
                 COMMAND ${FT_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/test_release_ci_gate.sh)
    else ()
        message(STATUS "release_ci_gate not built: it needs bash and jq")
    endif ()
endif ()
