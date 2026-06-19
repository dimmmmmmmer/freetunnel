# Optional Linux Secret Service (libsecret) for CredentialStore.
get_filename_component(FREETUNNEL_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

macro(freetunnel_link_linux_secrets target)
    if(UNIX AND NOT APPLE)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LIBSECRET QUIET libsecret-1)
        endif()
        if(LIBSECRET_FOUND)
            target_compile_definitions(${target} PRIVATE FT_HAVE_LIBSECRET)
            target_sources(${target} PRIVATE
                ${FREETUNNEL_REPO_ROOT}/src/core/CredentialStoreLibsecret.cpp)
            target_include_directories(${target} PRIVATE ${LIBSECRET_INCLUDE_DIRS})
            target_link_libraries(${target} PRIVATE ${LIBSECRET_LIBRARIES})
        endif()
    endif()
endmacro()
