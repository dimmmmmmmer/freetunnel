# May also be configured on its own for the focused Unix helper checks.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(installed-helper-tests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_AUTOMOC ON)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Test)
    find_package(OpenSSL REQUIRED)
endif()

set(ft_root "${CMAKE_CURRENT_LIST_DIR}/../..")
set(policy_sources "${ft_root}/src/vpn/installed_helper_policy.cpp"
    "${ft_root}/src/core/ReleaseVerify.cpp" "${ft_root}/src/core/VersionCompare.cpp")
add_executable(test_installed_helper test_installed_helper.cpp ${policy_sources})
target_include_directories(test_installed_helper PRIVATE "${ft_root}/include" "${ft_root}/include/core")
target_link_libraries(test_installed_helper PRIVATE Qt6::Core Qt6::Test OpenSSL::Crypto)
target_compile_definitions(test_installed_helper PRIVATE FT_REQUIRE_OPENSSL FT_ENABLE_TEST_HOOKS
    FT_INSTALLED_HELPER_TEST_KEY FT_HELPER_TEST_RESOURCES="${ft_root}/tests/resources/installed-helper")
add_test(NAME installed_helper COMMAND test_installed_helper)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME installed_helper_package COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test_package.py")

add_executable(installed_helper_fixture fixture.cpp ${policy_sources}
    "${ft_root}/src/vpn/installed_helper.cpp" "${ft_root}/src/vpn/installed_helper_launcher.cpp"
    "${ft_root}/src/vpn/vpn_helper_client.cpp" "${ft_root}/src/vpn/vpn_helper_elevation.cpp"
    "${ft_root}/src/vpn/vpn_helper_launch.cpp" "${ft_root}/include/vpn/vpn_helper_client.h"
    "${ft_root}/src/core/AppImagePath.cpp" "${ft_root}/src/core/AppUiUtils.cpp")
target_include_directories(installed_helper_fixture PRIVATE "${ft_root}/include" "${ft_root}/include/core")
target_link_libraries(installed_helper_fixture PRIVATE Qt6::Core Qt6::Gui Qt6::Network OpenSSL::Crypto)
target_compile_definitions(installed_helper_fixture PRIVATE FT_REQUIRE_OPENSSL FT_ENABLE_TEST_HOOKS
    FT_INSTALLED_HELPER_TEST_KEY FT_INSTALLED_HELPER FREETUNNEL_VERSION="1.2.5")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_installed_helper PRIVATE -Wall -Wextra -Werror)
    target_compile_options(installed_helper_fixture PRIVATE -Wall -Wextra -Werror)
endif()
