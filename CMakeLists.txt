# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rsc-app)

add_subdirectory(lib/storage)
add_subdirectory(lib/core)
add_subdirectory(lib/communication)
add_subdirectory(lib/component)
add_subdirectory(lib/data)

target_sources(app PRIVATE src/main.c)
