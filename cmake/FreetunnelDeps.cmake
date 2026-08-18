# Optional Linux Secret Service (libsecret) for CredentialStore.
get_filename_component(FREETUNNEL_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

macro(freetunnel_link_linux_secrets target)
    if(UNIX AND NOT APPLE)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            # IMPORTED_TARGET rather than the bare variables, for two reasons that
            # both bit us. It carries the library SEARCH PATH, which
            # ${LIBSECRET_LIBRARIES} alone does not — linking by bare name only
            # worked because libsecret happened to sit in the default path. And an
            # imported target's include directories count as SYSTEM, so warnings
            # and clang-tidy findings from glib's own headers stop being attributed
            # to us: with -Werror and clang-tidy set to fail on findings, glib was
            # failing our build for its own code.
            pkg_check_modules(LIBSECRET QUIET IMPORTED_TARGET libsecret-1)
        endif()
        if(LIBSECRET_FOUND)
            target_compile_definitions(${target} PRIVATE FT_HAVE_LIBSECRET)
            target_sources(${target} PRIVATE
                ${FREETUNNEL_REPO_ROOT}/src/core/CredentialStoreLibsecret.cpp)
            target_link_libraries(${target} PRIVATE PkgConfig::LIBSECRET)
        endif()
    endif()
endmacro()
