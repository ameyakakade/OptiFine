add_executable(test_ir test_ir.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/codegen/select.c
)
target_include_directories(test_ir PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_ir PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME test_ir COMMAND test_ir)

add_executable(test_dsp_build test_dsp_build.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
)
target_include_directories(test_dsp_build PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_dsp_build PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(test_dsp_build PRIVATE m)
endif()
add_test(NAME test_dsp_build COMMAND test_dsp_build)

add_executable(test_avr_interp_opcodes test_avr_interp_opcodes.c
    avr_interp.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
    ${CMAKE_SOURCE_DIR}/src/codegen/instr_buf.c
    ${CMAKE_SOURCE_DIR}/src/cost_model.c
)
target_include_directories(test_avr_interp_opcodes PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_avr_interp_opcodes PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    # instr_buf.c uses llround when pricing; see the libm note on the other targets.
    target_link_libraries(test_avr_interp_opcodes PRIVATE m)
endif()
add_test(NAME test_avr_interp_opcodes COMMAND test_avr_interp_opcodes)

add_executable(test_pb_reader test_pb_reader.c
    ${CMAKE_SOURCE_DIR}/src/pb_reader.c
)
target_include_directories(test_pb_reader PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_pb_reader PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME test_pb_reader COMMAND test_pb_reader)

add_executable(test_ingest test_ingest.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/pb_reader.c
    ${CMAKE_SOURCE_DIR}/src/ingest.c
)
target_include_directories(test_ingest PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_ingest PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME test_ingest COMMAND test_ingest ${CMAKE_SOURCE_DIR}/../models/tiny_classifier.onnx)

add_executable(test_lower test_lower.c
    avr_interp.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/pb_reader.c
    ${CMAKE_SOURCE_DIR}/src/ingest.c
    ${CMAKE_SOURCE_DIR}/src/cost_model.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
    ${CMAKE_SOURCE_DIR}/src/codegen/instr_buf.c
    ${CMAKE_SOURCE_DIR}/src/codegen/registers.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
    ${CMAKE_SOURCE_DIR}/src/codegen/lower.c
    ${CMAKE_SOURCE_DIR}/src/codegen/dsp32.c
    ${CMAKE_SOURCE_DIR}/src/codegen/regalloc.c
    ${CMAKE_SOURCE_DIR}/src/codegen/candidates.c
    ${CMAKE_SOURCE_DIR}/src/codegen/select.c
    ${CMAKE_SOURCE_DIR}/src/locality.c
    ${CMAKE_SOURCE_DIR}/src/emit.c
    ${CMAKE_SOURCE_DIR}/src/codegen/program.c
)
target_include_directories(test_lower PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_lower PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(test_lower PRIVATE m)
endif()
add_test(NAME test_lower COMMAND test_lower
    ${CMAKE_SOURCE_DIR}/../models/tiny_classifier.onnx
    ${CMAKE_SOURCE_DIR}/../cost_table.toml
    ${CMAKE_SOURCE_DIR}/../models/tiny_classifier_golden_input.txt
    ${CMAKE_SOURCE_DIR}/../models/tiny_classifier_golden_output.txt)

add_executable(test_dsp_lower test_dsp_lower.c
    avr_interp.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
    ${CMAKE_SOURCE_DIR}/src/cost_model.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
    ${CMAKE_SOURCE_DIR}/src/codegen/instr_buf.c
    ${CMAKE_SOURCE_DIR}/src/codegen/registers.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
    ${CMAKE_SOURCE_DIR}/src/codegen/lower.c
    ${CMAKE_SOURCE_DIR}/src/codegen/dsp32.c
    ${CMAKE_SOURCE_DIR}/src/codegen/candidates.c
    ${CMAKE_SOURCE_DIR}/src/codegen/regalloc.c
)
target_include_directories(test_dsp_lower PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_dsp_lower PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(test_dsp_lower PRIVATE m)
endif()
add_test(NAME test_dsp_lower COMMAND test_dsp_lower
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

add_executable(test_sram_layout_dsp test_sram_layout_dsp.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
)
target_include_directories(test_sram_layout_dsp PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_sram_layout_dsp PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    # dsp_build.c calls cos/lround to bake the Hamming window at compile
    # time, so every target compiling it needs libm -- this one too, not
    # just the tests that call libm from their own code.
    target_link_libraries(test_sram_layout_dsp PRIVATE m)
endif()
add_test(NAME test_sram_layout_dsp COMMAND test_sram_layout_dsp)

add_executable(test_periodic test_periodic.c
    ${CMAKE_SOURCE_DIR}/src/codegen/periodic.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
)
target_include_directories(test_periodic PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_periodic PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME test_periodic COMMAND test_periodic
    $<TARGET_FILE:optifine>
    ${CMAKE_SOURCE_DIR}/../models/tiny_classifier.onnx
    ${CMAKE_SOURCE_DIR}/../cost_table.toml
    ${CMAKE_SOURCE_DIR}/../models/tiny_classifier_golden_input.txt)

add_executable(test_loops test_loops.c
    avr_interp.c
    ${CMAKE_SOURCE_DIR}/src/cost_model.c
    ${CMAKE_SOURCE_DIR}/src/codegen/candidates.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
    ${CMAKE_SOURCE_DIR}/src/codegen/lower.c
    ${CMAKE_SOURCE_DIR}/src/codegen/dsp32.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
    ${CMAKE_SOURCE_DIR}/src/codegen/instr_buf.c
    ${CMAKE_SOURCE_DIR}/src/codegen/registers.c
)
target_include_directories(test_loops PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_loops PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(test_loops PRIVATE m)
endif()
add_test(NAME test_loops COMMAND test_loops
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

add_executable(test_dsp32 test_dsp32.c
    avr_interp.c
    ${CMAKE_SOURCE_DIR}/src/cost_model.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
    ${CMAKE_SOURCE_DIR}/src/codegen/instr_buf.c
    ${CMAKE_SOURCE_DIR}/src/codegen/registers.c
    ${CMAKE_SOURCE_DIR}/src/codegen/dsp32.c
    ${CMAKE_SOURCE_DIR}/src/codegen/candidates.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
    ${CMAKE_SOURCE_DIR}/src/codegen/lower.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
)
target_include_directories(test_dsp32 PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_dsp32 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(test_dsp32 PRIVATE m)
endif()
add_test(NAME test_dsp32 COMMAND test_dsp32 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

add_executable(test_dsp_fft_stage0 test_dsp_fft_stage0.c
    avr_interp.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
    ${CMAKE_SOURCE_DIR}/src/cost_model.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
    ${CMAKE_SOURCE_DIR}/src/codegen/instr_buf.c
    ${CMAKE_SOURCE_DIR}/src/codegen/registers.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
    ${CMAKE_SOURCE_DIR}/src/codegen/lower.c
    ${CMAKE_SOURCE_DIR}/src/codegen/dsp32.c
    ${CMAKE_SOURCE_DIR}/src/codegen/candidates.c
    ${CMAKE_SOURCE_DIR}/src/codegen/regalloc.c
)
add_executable(test_dsp_bitreverse test_dsp_bitreverse.c
    avr_interp.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
    ${CMAKE_SOURCE_DIR}/src/cost_model.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
    ${CMAKE_SOURCE_DIR}/src/codegen/instr_buf.c
    ${CMAKE_SOURCE_DIR}/src/codegen/registers.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
    ${CMAKE_SOURCE_DIR}/src/codegen/lower.c
    ${CMAKE_SOURCE_DIR}/src/codegen/dsp32.c
    ${CMAKE_SOURCE_DIR}/src/codegen/candidates.c
    ${CMAKE_SOURCE_DIR}/src/codegen/regalloc.c
)
target_include_directories(test_dsp_bitreverse PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_dsp_bitreverse PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(test_dsp_bitreverse PRIVATE m)
endif()
add_test(NAME test_dsp_bitreverse COMMAND test_dsp_bitreverse
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

target_include_directories(test_dsp_fft_stage0 PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_dsp_fft_stage0 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(test_dsp_fft_stage0 PRIVATE m)
endif()
add_test(NAME test_dsp_fft_stage0 COMMAND test_dsp_fft_stage0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

add_executable(test_dsp_fft test_dsp_fft.c
    avr_interp.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
    ${CMAKE_SOURCE_DIR}/src/cost_model.c
    ${CMAKE_SOURCE_DIR}/src/emit.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
    ${CMAKE_SOURCE_DIR}/src/codegen/instr_buf.c
    ${CMAKE_SOURCE_DIR}/src/codegen/registers.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
    ${CMAKE_SOURCE_DIR}/src/codegen/lower.c
    ${CMAKE_SOURCE_DIR}/src/codegen/dsp32.c
    ${CMAKE_SOURCE_DIR}/src/codegen/candidates.c
    ${CMAKE_SOURCE_DIR}/src/codegen/regalloc.c
)
target_include_directories(test_dsp_fft PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_dsp_fft PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(test_dsp_fft PRIVATE m)
endif()
add_test(NAME test_dsp_fft COMMAND test_dsp_fft
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

add_executable(test_asm_unit test_asm_unit.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
    ${CMAKE_SOURCE_DIR}/src/cost_model.c
    ${CMAKE_SOURCE_DIR}/src/emit.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
    ${CMAKE_SOURCE_DIR}/src/codegen/instr_buf.c
    ${CMAKE_SOURCE_DIR}/src/codegen/registers.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
    ${CMAKE_SOURCE_DIR}/src/codegen/lower.c
    ${CMAKE_SOURCE_DIR}/src/codegen/dsp32.c
    ${CMAKE_SOURCE_DIR}/src/codegen/candidates.c
    ${CMAKE_SOURCE_DIR}/src/codegen/regalloc.c
)
target_include_directories(test_asm_unit PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_asm_unit PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(test_asm_unit PRIVATE m)
endif()
add_test(NAME test_asm_unit COMMAND test_asm_unit
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

add_executable(test_dsp_isqrt test_dsp_isqrt.c
    avr_interp.c
    ${CMAKE_SOURCE_DIR}/src/cost_model.c
    ${CMAKE_SOURCE_DIR}/src/emit.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
    ${CMAKE_SOURCE_DIR}/src/codegen/instr_buf.c
    ${CMAKE_SOURCE_DIR}/src/codegen/registers.c
    ${CMAKE_SOURCE_DIR}/src/codegen/dsp32.c
    ${CMAKE_SOURCE_DIR}/src/codegen/candidates.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
    ${CMAKE_SOURCE_DIR}/src/codegen/lower.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
)
target_include_directories(test_dsp_isqrt PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_dsp_isqrt PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(test_dsp_isqrt PRIVATE m)
endif()
add_test(NAME test_dsp_isqrt COMMAND test_dsp_isqrt
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

add_executable(test_dsp_magnitude test_dsp_magnitude.c
    avr_interp.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
    ${CMAKE_SOURCE_DIR}/src/cost_model.c
    ${CMAKE_SOURCE_DIR}/src/emit.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
    ${CMAKE_SOURCE_DIR}/src/codegen/instr_buf.c
    ${CMAKE_SOURCE_DIR}/src/codegen/registers.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
    ${CMAKE_SOURCE_DIR}/src/codegen/lower.c
    ${CMAKE_SOURCE_DIR}/src/codegen/dsp32.c
    ${CMAKE_SOURCE_DIR}/src/codegen/candidates.c
    ${CMAKE_SOURCE_DIR}/src/codegen/regalloc.c
)
target_include_directories(test_dsp_magnitude PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_dsp_magnitude PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(test_dsp_magnitude PRIVATE m)
endif()
add_test(NAME test_dsp_magnitude COMMAND test_dsp_magnitude
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

add_executable(test_dsp_peak test_dsp_peak.c
    avr_interp.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
    ${CMAKE_SOURCE_DIR}/src/cost_model.c
    ${CMAKE_SOURCE_DIR}/src/emit.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
    ${CMAKE_SOURCE_DIR}/src/codegen/instr_buf.c
    ${CMAKE_SOURCE_DIR}/src/codegen/registers.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
    ${CMAKE_SOURCE_DIR}/src/codegen/lower.c
    ${CMAKE_SOURCE_DIR}/src/codegen/dsp32.c
    ${CMAKE_SOURCE_DIR}/src/codegen/candidates.c
    ${CMAKE_SOURCE_DIR}/src/codegen/regalloc.c
)
target_include_directories(test_dsp_peak PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_dsp_peak PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(test_dsp_peak PRIVATE m)
endif()
add_test(NAME test_dsp_peak COMMAND test_dsp_peak
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

add_executable(test_dsp_pipeline test_dsp_pipeline.c
    avr_interp.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
    ${CMAKE_SOURCE_DIR}/src/cost_model.c
    ${CMAKE_SOURCE_DIR}/src/emit.c
    ${CMAKE_SOURCE_DIR}/src/locality.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
    ${CMAKE_SOURCE_DIR}/src/codegen/instr_buf.c
    ${CMAKE_SOURCE_DIR}/src/codegen/registers.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
    ${CMAKE_SOURCE_DIR}/src/codegen/lower.c
    ${CMAKE_SOURCE_DIR}/src/codegen/dsp32.c
    ${CMAKE_SOURCE_DIR}/src/codegen/candidates.c
    ${CMAKE_SOURCE_DIR}/src/codegen/regalloc.c
    ${CMAKE_SOURCE_DIR}/src/codegen/select.c
    ${CMAKE_SOURCE_DIR}/src/codegen/program.c
    ${CMAKE_SOURCE_DIR}/src/codegen/periodic.c
)
target_include_directories(test_dsp_pipeline PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_dsp_pipeline PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(test_dsp_pipeline PRIVATE m)
endif()
add_test(NAME test_dsp_pipeline COMMAND test_dsp_pipeline
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

add_executable(test_dsp_cli test_dsp_cli.c)
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_dsp_cli PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME test_dsp_cli COMMAND test_dsp_cli
    $<TARGET_FILE:optifine>
    ${CMAKE_SOURCE_DIR}/../cost_table.toml
    ${CMAKE_CURRENT_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
