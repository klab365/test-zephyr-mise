cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mcumgr_actor_endpoint)

get_filename_component(PYTHON_BIN_DIR "${Python3_EXECUTABLE}" DIRECTORY)
find_program(ZCBOR_EXECUTABLE NAMES zcbor HINTS "${PYTHON_BIN_DIR}" REQUIRED)

set(APP_CDDL ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/app_mcumgr.cddl)
set(APP_CDDL_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(APP_CDDL_GENERATED_SOURCES
  ${APP_CDDL_GENERATED_DIR}/app_mcumgr_decode.c
  ${APP_CDDL_GENERATED_DIR}/app_mcumgr_encode.c
)
set(APP_CDDL_GENERATED_HEADERS
  ${APP_CDDL_GENERATED_DIR}/app_mcumgr_decode.h
  ${APP_CDDL_GENERATED_DIR}/app_mcumgr_encode.h
  ${APP_CDDL_GENERATED_DIR}/app_mcumgr_types.h
)

add_custom_command(
  OUTPUT ${APP_CDDL_GENERATED_SOURCES} ${APP_CDDL_GENERATED_HEADERS}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_CDDL_GENERATED_DIR}
  COMMAND ${ZCBOR_EXECUTABLE} code --decode --encode --short-names
          -c ${APP_CDDL}
          -t echo-request echo-response ping-request ping-response
             set-matrix-symbol-request set-matrix-symbol-response
          --oc ${APP_CDDL_GENERATED_DIR}/app_mcumgr.c
          --oh ${APP_CDDL_GENERATED_DIR}/app_mcumgr.h
          --oht ${APP_CDDL_GENERATED_DIR}/app_mcumgr_types.h
  DEPENDS ${APP_CDDL}
  COMMENT "Generating CBOR codecs from app_mcumgr.cddl"
  VERBATIM
)
add_custom_target(app_mcumgr_cddl DEPENDS ${APP_CDDL_GENERATED_SOURCES})
add_dependencies(app app_mcumgr_cddl)

target_sources(app PRIVATE
  src/main.c
  src/app_events.c
  src/echo_actor.c
  src/ping_actor.c
  src/mcumgr_app.c
  src/smp_ble.c
  ${APP_CDDL_GENERATED_SOURCES}
)

target_include_directories(app PRIVATE
  src/include
  ${APP_CDDL_GENERATED_DIR}
)

add_subdirectory(src/hmi)
