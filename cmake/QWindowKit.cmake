# QWindowKit, Windows only: gives the frameless window back the shadow, corners,
# Aero Snap and Snap Layouts that Qt::FramelessWindowHint takes away (see
# src/app/WindowsChrome.cpp). A library rather than our own WM_NCCALCSIZE and
# WM_NCHITTEST, because nobody working on this can test Windows by hand, and this
# one has years of field use in Jami's Qt Quick client.
#
# Configured the way Jami ships it: static, the Quick module only, and Windows
# system borders OFF — which keeps the Windows 10 top-border artefact away at the
# price of an 8 px resize band inside the window's edge. Turning borders ON is a
# one-line change here, and one that needs a Windows hand test after it.
#
# Pinned to the commit behind release 1.5.0 rather than to main: main carries
# Windows code no shipped application has run yet, and the library uses Qt's
# private API, so a bump is a deliberate change, not a drift.
#
# Included by the app (../CMakeLists.txt) and by the tests (tests/CMakeLists.txt,
# for test_windows_chrome), so that the chrome the tests check is built from the
# same commit with the same options as the one the app ships. This is the only
# place either is written; scripts/check-pinned-deps.sh fails on a second one.
include(FetchContent)
set(QWINDOWKIT_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_QUICK ON CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_WIDGETS OFF CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_DOCUMENTATIONS OFF CACHE BOOL "" FORCE)
set(QWINDOWKIT_INSTALL OFF CACHE BOOL "" FORCE)
set(QWINDOWKIT_ENABLE_STYLE_AGENT OFF CACHE BOOL "" FORCE)
set(QWINDOWKIT_ENABLE_WINDOWS_SYSTEM_BORDERS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    QWindowKit
    GIT_REPOSITORY https://github.com/stdware/qwindowkit.git
    GIT_TAG 35e88f3655720ed0537c7dd1dde243bf4a70c94c # 1.5.0
    # qmsetup is a submodule, and has one of its own.
    GIT_SUBMODULES_RECURSE ON
)
FetchContent_MakeAvailable(QWindowKit)
