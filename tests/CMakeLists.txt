# Standalone, lightweight unit tests for self-contained client modules.
# Deliberately does NOT depend on the VPN core, so it builds with just Qt and
# runs fast in CI (see .github/workflows/tests.yml).
cmake_minimum_required(VERSION 3.16)
project(freetunnel-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core Test Network REQUIRED)

enable_testing()

add_executable(test_deeplink
    test_deeplink.cpp
    ../src/core/DeepLink.cpp
)
target_include_directories(test_deeplink PRIVATE ../include ../include/core)
target_link_libraries(test_deeplink PRIVATE Qt6::Core Qt6::Test)
add_test(NAME deeplink COMMAND test_deeplink)

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
add_test(NAME credentialstore COMMAND test_credentialstore)

add_executable(test_helper_ipc
    test_helper_ipc.cpp
    helper_ipc_mock_server.cpp
    helper_ipc_mock_server.h
)
target_include_directories(test_helper_ipc PRIVATE ../include ../include/vpn)
target_link_libraries(test_helper_ipc PRIVATE Qt6::Core Qt6::Test Qt6::Network)
add_test(NAME helper_ipc COMMAND test_helper_ipc)
