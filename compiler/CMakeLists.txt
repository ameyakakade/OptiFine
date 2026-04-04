cmake_minimum_required(VERSION 3.16)
project(optifine C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_executable(optifine
    src/main.c
    src/ir.c
    src/ingest.c
    src/cost_model.c
    src/locality.c
    src/emit.c
    src/codegen/candidates.c
    src/codegen/regalloc.c
    src/codegen/select.c
)

target_include_directories(optifine PRIVATE include)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(optifine PRIVATE -Wall -Wextra)
endif()
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(optifine PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

enable_testing()
add_subdirectory(tests)
