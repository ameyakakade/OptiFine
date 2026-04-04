add_executable(test_ir test_ir.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/codegen/select.c
)
target_include_directories(test_ir PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_ir PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME test_ir COMMAND test_ir)
