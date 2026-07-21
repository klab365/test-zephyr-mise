cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble_actor_endpoint)

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)

target_sources(app PRIVATE
  src/main.c
  src/app_events.c
  src/echo_actor.c
  src/ping_actor.c
)

target_include_directories(app PRIVATE
  src/include
)

add_subdirectory(src/ble)
add_subdirectory(src/hmi)

zephyr_nanopb_sources(app
  assets/proto/ble_transport.proto
  assets/proto/app_protocol.proto
)
