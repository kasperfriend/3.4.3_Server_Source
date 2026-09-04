# Playerbots (ported from ike3/mangosbot trinity-wotlk-ai) plus the minimal
# auction-bot pricing support used by the bots.

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
