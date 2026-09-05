add_definitions(-D_WIN32_WINNT=0x0A00)      # Windows 10
add_definitions(-DNTDDI_VERSION=0x0A000007) # 19H1 (1903)
add_definitions(-DWIN32_LEAN_AND_MEAN)
add_definitions(-DNOMINMAX)
add_definitions(-DTRINITY_REQUIRED_WINDOWS_BUILD=18362)

# MSVC (Visual Studio 2022) is the only supported toolchain.  MinGW / clang
# settings were removed together with the Unix build files.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  include(${CMAKE_SOURCE_DIR}/cmake/compiler/msvc/settings.cmake)
else()
  message(FATAL_ERROR
    "  Unsupported compiler: '${CMAKE_CXX_COMPILER_ID}'.\n"
    "  This source tree only builds with MSVC (Visual Studio 2022).\n"
    "  Configure with: cmake -G \"Visual Studio 17 2022\" -A x64 ...")
endif()
