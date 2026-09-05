# Playerbots (ported from ike3/mangosbot trinity-wotlk-ai) plus the minimal
# auction-bot pricing support used by the bots.
#
# IMPORTANT: The playerbot plugin calls many core functions that are not
# marked TC_GAME_API.  When WITH_DYNAMIC_LINKING is ON the core libraries
# use -fvisibility=hidden and those symbols become unavailable.  The
# plugin therefore requires static linking (WITH_DYNAMIC_LINKING=OFF).

if(BUILD_SHARED_LIBS)
  message(WARNING
    "  Playerbot plugin: BUILD_SHARED_LIBS is ON (WITH_DYNAMIC_LINKING=1).\n"
    "  The bot code calls core functions that lack TC_GAME_API exports,\n"
    "  so the build will almost certainly fail to link.  Reconfigure with\n"
    "    -DWITH_DYNAMIC_LINKING=0\n"
    "  to build the bots correctly.")
endif()

file(GLOB_RECURSE sources_playerbot playerbot/*.cpp playerbot/*.h)
file(GLOB_RECURSE sources_ahbot ahbot/*.cpp ahbot/*.h)

add_library(plugins STATIC
  ${sources_playerbot}
  ${sources_ahbot})

target_include_directories(plugins
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/playerbot
    ${CMAKE_CURRENT_SOURCE_DIR}/ahbot
  PRIVATE
    ${CMAKE_BINARY_DIR})

target_link_libraries(plugins
  PUBLIC
    game-interface
    mysql)

# Modern TrinityCore uses flat includes across all core subdirectories.
set(PLUGIN_INCLUDES)
foreach(INCLUDE_ROOT
    ${CMAKE_SOURCE_DIR}/src/server/game
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/server/shared
    ${CMAKE_SOURCE_DIR}/src/server/database)
  CollectIncludeDirectories(${INCLUDE_ROOT} ROOT_INCLUDES)
  list(APPEND PLUGIN_INCLUDES ${ROOT_INCLUDES})
endforeach()

target_include_directories(plugins
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/server
    ${PLUGIN_INCLUDES})

add_definitions(-DTRINITY_API_EXPORT_plugins)

set_target_properties(plugins
  PROPERTIES
    FOLDER
      "server")

# Generate precompiled header.
# Every plugin translation unit already starts with #include "../pchdef.h",
# so turning it into a real PCH is free and cuts the compile time of the ~500
# bot sources down massively.
if(USE_COREPCH)
  add_cxx_pch(plugins ${CMAKE_CURRENT_SOURCE_DIR}/pchdef.h)
endif()

# Install the playerbot configuration next to worldserver.conf so that the
# worldserver picks it up automatically from worldserver.conf.d.
install(FILES playerbot/aiplayerbot.conf.dist DESTINATION "${CMAKE_INSTALL_PREFIX}/worldserver.conf.d")
