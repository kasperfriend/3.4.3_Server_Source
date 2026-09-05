# This project is built for Windows only.
#
# Linux / macOS support was intentionally dropped: the server is shipped as
# Windows binaries, the CI and the release pipeline only target
# Windows + MSVC, and carrying half-tested Unix build files around just
# produced build breakage nobody consumes.  Fail loudly and early instead of
# letting a Unix configure run get halfway and die with a confusing error.
if(NOT WIN32)
  message(FATAL_ERROR
    "  Unsupported platform: this source tree only builds on Windows.\n"
    "  Use Windows 10/11 x64 with Visual Studio 2022 (MSVC) and CMake.\n"
    "  See README.md for the required prerequisites.")
endif()

# check what platform we're on (64-bit or 32-bit), and create a simpler test than CMAKE_SIZEOF_VOID_P
if(CMAKE_SIZEOF_VOID_P MATCHES 8)
    set(PLATFORM 64)
    MESSAGE(STATUS "Detected 64-bit platform")
else()
    set(PLATFORM 32)
    MESSAGE(STATUS "Detected 32-bit platform")
endif()

include("${CMAKE_SOURCE_DIR}/cmake/platform/win/settings.cmake")
