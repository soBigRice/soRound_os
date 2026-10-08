cmake_minimum_required(VERSION 3.20)
project(soround_site_previews LANGUAGES C)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
# Reuse the firmware's host renderer and service fixtures, without compiling or flashing ESP-IDF.
add_subdirectory("${ROOT}/GeekTool-IDF/tests/host" host)
set(MAIN "${ROOT}/GeekTool-IDF/main")
add_executable(site_native renderer.c
  ${MAIN}/app_calendar.c ${MAIN}/app_countdown.c ${MAIN}/app_stopwatch.c
  ${MAIN}/app_maze.c ${MAIN}/maze_levels.c ${MAIN}/app_fluid.c ${MAIN}/fluid_ink.c ${MAIN}/app_i2c.c ${MAIN}/app_twin.c
  ${MAIN}/font_watchface.c ${MAIN}/font_watchface_hands.c
  ${MAIN}/glyph.c ${MAIN}/ui_list.c ${MAIN}/i18n.c ${MAIN}/font_cn16.c)
target_include_directories(site_native PRIVATE stubs
  "${ROOT}/GeekTool-IDF/tests/dice/stubs" "${ROOT}/GeekTool-IDF/tests/host/stubs"
  "${ROOT}/GeekTool-IDF/tests/ota/stubs" "${MAIN}")
target_link_libraries(site_native PRIVATE lvgl m)
target_compile_options(site_native PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
# Calendar uses a fixed date; the rendered UI still comes from app_calendar.c.
set_source_files_properties(${MAIN}/app_calendar.c PROPERTIES COMPILE_DEFINITIONS "time=site_fixture_time")
