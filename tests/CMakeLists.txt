# Standalone, lightweight unit tests for self-contained client modules.
# Deliberately does NOT depend on the VPN core, so it builds with just Qt and
# runs fast in CI (see .github/workflows/tests.yml).
cmake_minimum_required(VERSION 3.16)
project(freetunnel-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Core Gui Qml Quick Test Network REQUIRED)

include(FetchContent)
set(QHOTKEY_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(
    QHotkey
    GIT_REPOSITORY https://github.com/Skycoder42/QHotkey.git
    GIT_TAG 1.5.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(QHotkey)

find_package(OpenSSL QUIET)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/FreetunnelDeps.cmake)

# macOS SDKs may ship AGL.framework without a linkable binary; Qt still requests it.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    set_property(
        TARGET WrapOpenGL::WrapOpenGL
        PROPERTY INTERFACE_LINK_LIBRARIES "OpenGL::GL")
endif()

enable_testing()

add_executable(test_deeplink
    test_deeplink.cpp
    ../src/core/DeepLink.cpp
)
target_include_directories(test_deeplink PRIVATE ../include ../include/core)
target_link_libraries(test_deeplink PRIVATE Qt6::Core Qt6::Test)
add_test(NAME deeplink COMMAND test_deeplink)

add_executable(test_safe_fileread
    test_safe_fileread.cpp
    ../src/core/AppUiUtils.cpp
)
target_include_directories(test_safe_fileread PRIVATE ../include ../include/core)
target_link_libraries(test_safe_fileread PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME safe_fileread COMMAND test_safe_fileread)

add_executable(test_configimport
    test_configimport.cpp
    ../src/core/ConfigImport.cpp
    ../src/core/DeepLink.cpp
)
target_include_directories(test_configimport PRIVATE ../include ../include/core)
target_link_libraries(test_configimport PRIVATE Qt6::Core Qt6::Test)
add_test(NAME configimport COMMAND test_configimport)

add_executable(test_appsettings
    test_appsettings.cpp
    ../src/core/AppSettings.cpp
)
target_include_directories(test_appsettings PRIVATE ../include ../include/core)
target_link_libraries(test_appsettings PRIVATE Qt6::Core Qt6::Test)
add_test(NAME appsettings COMMAND test_appsettings)

add_executable(test_configstore
    test_configstore.cpp
    ../src/core/ConfigStore.cpp
)
target_include_directories(test_configstore PRIVATE ../include ../include/core)
target_link_libraries(test_configstore PRIVATE Qt6::Core Qt6::Test)
add_test(NAME configstore COMMAND test_configstore)

add_executable(test_control
    test_control.cpp
    ../src/core/ControlCommand.cpp
)
target_include_directories(test_control PRIVATE ../include ../include/core)
target_link_libraries(test_control PRIVATE Qt6::Core Qt6::Test)
add_test(NAME control COMMAND test_control)

add_executable(test_instance_control
    test_instance_control.cpp
    ../src/core/InstanceControl.cpp
)
target_include_directories(test_instance_control PRIVATE ../include ../include/core)
target_link_libraries(test_instance_control PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME instance_control COMMAND test_instance_control)

add_executable(test_configtoml
    test_configtoml.cpp
    ../src/core/ConfigToml.cpp
)
target_include_directories(test_configtoml PRIVATE ../include ../include/core)
target_link_libraries(test_configtoml PRIVATE Qt6::Core Qt6::Test)
add_test(NAME configtoml COMMAND test_configtoml)

add_executable(test_versioncompare
    test_versioncompare.cpp
    ../src/core/VersionCompare.cpp
)
target_include_directories(test_versioncompare PRIVATE ../include ../include/core)
target_link_libraries(test_versioncompare PRIVATE Qt6::Core Qt6::Test)
add_test(NAME versioncompare COMMAND test_versioncompare)

add_executable(test_releaseverify
    test_releaseverify.cpp
    ../src/core/ReleaseVerify.cpp
)
target_include_directories(test_releaseverify PRIVATE ../include ../include/core)
target_link_libraries(test_releaseverify PRIVATE Qt6::Core Qt6::Test)
find_package(OpenSSL QUIET)
if (OpenSSL_FOUND)
    target_link_libraries(test_releaseverify PRIVATE OpenSSL::Crypto)
endif()
add_test(NAME releaseverify COMMAND test_releaseverify)

add_executable(test_credentialstore
    test_credentialstore.cpp
    ../src/core/CredentialStore.cpp
    ../src/core/ConfigToml.cpp
)
target_include_directories(test_credentialstore PRIVATE ../include ../include/core)
target_link_libraries(test_credentialstore PRIVATE Qt6::Core Qt6::Test)
freetunnel_link_linux_secrets(test_credentialstore)
if(APPLE)
    target_link_libraries(test_credentialstore PRIVATE "-framework Security")
elseif(WIN32)
    target_link_libraries(test_credentialstore PRIVATE advapi32)
endif()
add_test(NAME credentialstore COMMAND test_credentialstore)

add_executable(test_helper_ipc
    test_helper_ipc.cpp
    helper_ipc_mock_server.cpp
    helper_ipc_mock_server.h
)
target_include_directories(test_helper_ipc PRIVATE ../include ../include/vpn)
target_link_libraries(test_helper_ipc PRIVATE Qt6::Core Qt6::Test Qt6::Network)
add_test(NAME helper_ipc COMMAND test_helper_ipc)

add_executable(test_qml_ui
    test_qml_ui.cpp
    ui/MockBackend.cpp
    ui/MockShell.cpp
    ui/UiTheme.cpp
    ../qml.qrc
    ../assets/app.qrc
    ../i18n/i18n.qrc
)
target_include_directories(test_qml_ui PRIVATE . ui)
target_link_libraries(test_qml_ui PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
add_test(NAME qml_ui COMMAND test_qml_ui)

add_executable(test_integration_config_workflow
    test_integration_config_workflow.cpp
    ../src/core/ConfigImport.cpp
    ../src/core/DeepLink.cpp
    ../src/core/ConfigStore.cpp
    ../src/core/ConfigToml.cpp
    ../src/core/CredentialStore.cpp
)
target_include_directories(test_integration_config_workflow PRIVATE ../include ../include/core)
target_link_libraries(test_integration_config_workflow PRIVATE Qt6::Core Qt6::Test)
freetunnel_link_linux_secrets(test_integration_config_workflow)
if(APPLE)
    target_link_libraries(test_integration_config_workflow PRIVATE "-framework Security")
elseif(WIN32)
    target_link_libraries(test_integration_config_workflow PRIVATE advapi32)
endif()
add_test(NAME integration_config_workflow COMMAND test_integration_config_workflow)

add_executable(test_integration_single_instance
    test_integration_single_instance.cpp
)
target_link_libraries(test_integration_single_instance PRIVATE Qt6::Core Qt6::Test Qt6::Network)
add_test(NAME integration_single_instance COMMAND test_integration_single_instance)

add_executable(test_integration_helper_client
    test_integration_helper_client.cpp
    helper_ipc_mock_server.cpp
    helper_ipc_mock_server.h
)
target_include_directories(test_integration_helper_client PRIVATE ../include ../include/vpn)
target_link_libraries(test_integration_helper_client PRIVATE Qt6::Core Qt6::Test Qt6::Network)
add_test(NAME integration_helper_client COMMAND test_integration_helper_client)

set(BACKEND_TEST_SOURCES
    test_integration_backend_vpn.cpp
    helper_ipc_mock_server.cpp
    helper_ipc_mock_server.h
    ../src/app/Backend.cpp
    ../src/app/BackendHotkeys.cpp
    ../src/app/BackendUpdates.cpp
    ../src/app/BackendSettings.cpp
    ../src/app/BackendLogs.cpp
    ../src/app/BackendConfig.cpp
    ../src/app/BackendSplit.cpp
    ../src/core/AppSettings.cpp
    ../src/core/ConfigStore.cpp
    ../src/core/DeepLink.cpp
    ../src/core/ConfigImport.cpp
    ../src/core/ControlCommand.cpp
    ../src/core/ConfigToml.cpp
    ../src/core/NetBind.cpp
    ../src/core/AppUiUtils.cpp
    ../src/core/UpdateChecker.cpp
    ../src/core/VersionCompare.cpp
    ../src/core/ReleaseVerify.cpp
    ../src/core/CredentialStore.cpp
    ../src/vpn/vpn_helper_client.cpp
    ../include/app/Backend.h
    ../include/core/UpdateChecker.h
    ../include/vpn/vpn_client.h
    ../include/vpn/vpn_helper_client.h
)
add_executable(test_integration_backend_vpn ${BACKEND_TEST_SOURCES})
target_include_directories(test_integration_backend_vpn PRIVATE
    . ../include ../include/app ../include/core ../include/vpn)
target_compile_definitions(test_integration_backend_vpn PRIVATE FREETUNNEL_VERSION="test" FT_ENABLE_TEST_HOOKS)
target_link_libraries(test_integration_backend_vpn PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test Qt6::Network QHotkey::QHotkey)
freetunnel_link_linux_secrets(test_integration_backend_vpn)
if(WIN32)
    target_link_libraries(test_integration_backend_vpn PRIVATE shell32 advapi32)
elseif(APPLE)
    target_link_libraries(test_integration_backend_vpn PRIVATE "-framework Security")
endif()
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    target_link_libraries(test_integration_backend_vpn PRIVATE OpenSSL::Crypto)
endif()
add_test(NAME integration_backend_vpn COMMAND test_integration_backend_vpn)

add_executable(test_update_checker_e2e
    test_update_checker_e2e.cpp
    mock_http_server.cpp
    mock_http_server.h
    ../src/core/UpdateChecker.cpp
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
add_test(NAME update_checker_e2e COMMAND test_update_checker_e2e)
