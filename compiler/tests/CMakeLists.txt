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
add_test(NAME test_dsp_build COMMAND test_dsp_build)

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
    ${CMAKE_SOURCE_DIR}/src/codegen/regalloc.c
    ${CMAKE_SOURCE_DIR}/src/codegen/candidates.c
    ${CMAKE_SOURCE_DIR}/src/codegen/select.c
    ${CMAKE_SOURCE_DIR}/src/locality.c
)
target_include_directories(test_lower PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_lower PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME test_lower COMMAND test_lower
    ${CMAKE_SOURCE_DIR}/../models/tiny_classifier.onnx
    ${CMAKE_SOURCE_DIR}/../cost_table.toml
    ${CMAKE_SOURCE_DIR}/../models/tiny_classifier_golden_input.txt
    ${CMAKE_SOURCE_DIR}/../models/tiny_classifier_golden_output.txt)
