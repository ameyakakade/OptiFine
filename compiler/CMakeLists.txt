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
    src/pb_reader.c
    src/ingest.c
    src/dsp_build.c
    src/codegen/dsp32.c
    src/cost_model.c
    src/locality.c
    src/emit.c
    src/codegen/candidates.c
    src/codegen/regalloc.c
    src/codegen/select.c
    src/codegen/cost_category.c
    src/codegen/instr_buf.c
    src/codegen/registers.c
    src/codegen/sram_layout.c
    src/codegen/lower.c
    src/codegen/program.c
    src/codegen/periodic.c
)

target_include_directories(optifine PRIVATE include)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(optifine PRIVATE -Wall -Wextra)
endif()
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(optifine PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    # glibc/musl split lround/llround (instr_buf.c, lower.c) into libm;
    # MSVC's CRT provides them without a separate link, so this is
    # conditional rather than an unconditional target_link_libraries.
    target_link_libraries(optifine PRIVATE m)
endif()

enable_testing()
add_subdirectory(tests)
