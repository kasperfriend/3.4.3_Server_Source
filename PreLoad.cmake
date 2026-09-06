# This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# This file is run right before CMake starts configuring the sourcetree

# Example: Force CMAKE_INSTALL_PREFIX to be preloaded with something before
# doing the actual first "configure"-part - allows for hardforcing
# destinations elsewhere in the CMake buildsystem (commented out on purpose)

# Override CMAKE_INSTALL_PREFIX on Windows platforms
#if(WIN32)
#  if(NOT CYGWIN)
#    set(CMAKE_INSTALL_PREFIX
#      "" CACHE PATH "Default install path")
#  endif()
#endif()

# The 64-bit hosted MSVC toolchain is enforced in CMakeLists.txt before
# project(), where CMake has already loaded the cache and can manage the
# generator toolset correctly.  Setting CMAKE_GENERATOR_TOOLSET via CACHE
# FORCE from PreLoad.cmake used to live here but it wrote the cache file
# before CMake's internal toolset state was initialised, causing spurious
# "generator toolset does not match the toolset used previously" errors on
# every reconfigure.  See the comment block above project() in CMakeLists.txt.
