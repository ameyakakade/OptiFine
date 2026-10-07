# Every test links the same optifine_core the compiler is built from, plus
# the test-only AVR interpreter where it executes generated code. Tests run
# from the repository root so they can read models/ and cost_table.toml by
# their repository-relative paths.
set(OPTIFINE_REPO_ROOT ${CMAKE_SOURCE_DIR}/..)

# The tests check results with assert(). Keep it compiled in for test code in
# every build type, so a Release configuration runs the tests against the
# NDEBUG-built optifine_core instead of turning every check into a no-op.
add_library(optifine_test_settings INTERFACE)
target_link_libraries(optifine_test_settings INTERFACE optifine_build_settings)
target_compile_options(optifine_test_settings INTERFACE -UNDEBUG)

# The interpreters and shared MIR programs need only the generic backend.
add_library(optifine_test_support STATIC avr_interp.c mir_interp.c mir_programs.c)
target_include_directories(optifine_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(optifine_test_support PUBLIC optifine_backend optifine_test_settings)

# optifine_add_test(<name> [BACKEND_ONLY] [ARGS <arg>...])
#   Builds <name>.c against optifine_core and the test support library and
#   registers it with CTest. BACKEND_ONLY links optifine_backend instead of
#   optifine_core: the test then also proves that what it exercises builds
#   and links without the workload graph or its lowering.
function(optifine_add_test name)
    cmake_parse_arguments(T "BACKEND_ONLY" "" "ARGS" ${ARGN})
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE optifine_test_support)
    if(NOT T_BACKEND_ONLY)
        target_link_libraries(${name} PRIVATE optifine_core)
    endif()
    add_test(NAME ${name} COMMAND ${name} ${T_ARGS} WORKING_DIRECTORY ${OPTIFINE_REPO_ROOT})
endfunction()

optifine_add_test(test_ir)
optifine_add_test(test_mir BACKEND_ONLY)
optifine_add_test(test_avr_mir BACKEND_ONLY ARGS ${OPTIFINE_REPO_ROOT}/cost_table.toml)
optifine_add_test(test_backend_link BACKEND_ONLY ARGS ${OPTIFINE_REPO_ROOT}/cost_table.toml)
optifine_add_test(test_hir_to_mir ARGS
    ${OPTIFINE_REPO_ROOT}/models/tiny_classifier.onnx
    ${OPTIFINE_REPO_ROOT}/cost_table.toml)
optifine_add_test(test_pb_reader)
optifine_add_test(test_cost_model ARGS ${OPTIFINE_REPO_ROOT}/cost_table.toml ${CMAKE_CURRENT_BINARY_DIR})
optifine_add_test(test_ingest ARGS ${OPTIFINE_REPO_ROOT}/models/tiny_classifier.onnx)
optifine_add_test(test_dsp_build)
optifine_add_test(test_ir_verify ARGS ${OPTIFINE_REPO_ROOT}/models/tiny_classifier.onnx)
optifine_add_test(test_sram_layout_dsp)
optifine_add_test(test_avr_interp_opcodes)
optifine_add_test(test_pricing ARGS
    ${OPTIFINE_REPO_ROOT}/models/tiny_classifier.onnx
    ${OPTIFINE_REPO_ROOT}/cost_table.toml
    ${CMAKE_CURRENT_BINARY_DIR})
optifine_add_test(test_lower ARGS
    ${OPTIFINE_REPO_ROOT}/models/tiny_classifier.onnx
    ${OPTIFINE_REPO_ROOT}/cost_table.toml
    ${OPTIFINE_REPO_ROOT}/models/tiny_classifier_golden_input.txt
    ${OPTIFINE_REPO_ROOT}/models/tiny_classifier_golden_output.txt)
optifine_add_test(test_loops)
optifine_add_test(test_dsp32)
optifine_add_test(test_dsp_lower)
optifine_add_test(test_dsp_bitreverse)
optifine_add_test(test_dsp_fft_stage0)
optifine_add_test(test_dsp_fft)
optifine_add_test(test_dsp_isqrt)
optifine_add_test(test_dsp_magnitude)
optifine_add_test(test_dsp_peak)
optifine_add_test(test_dsp_pipeline)
optifine_add_test(test_asm_unit)

# test_periodic replaces the program emitters (program.c) with small fixed
# stand-ins so it checks the periodic wrapper alone. Because optifine_core is
# a static archive, the linker takes periodic.c and sram_layout.c from it and
# never pulls program.c, whose definitions the test supplies itself.
optifine_add_test(test_periodic ARGS
    $<TARGET_FILE:optifine>
    ${OPTIFINE_REPO_ROOT}/models/tiny_classifier.onnx
    ${OPTIFINE_REPO_ROOT}/cost_table.toml
    ${OPTIFINE_REPO_ROOT}/models/tiny_classifier_golden_input.txt)

# Drives the built compiler as a subprocess; it links no compiler code.
add_executable(test_dsp_cli test_dsp_cli.c)
target_link_libraries(test_dsp_cli PRIVATE optifine_test_settings)
add_test(NAME test_dsp_cli COMMAND test_dsp_cli
    $<TARGET_FILE:optifine>
    ${OPTIFINE_REPO_ROOT}/cost_table.toml
    ${CMAKE_CURRENT_BINARY_DIR}
    WORKING_DIRECTORY ${OPTIFINE_REPO_ROOT})
