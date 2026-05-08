include(GoogleTest)

# ── Unit tests ────────────────────────────────────────────────────────────────
file(GLOB UNIT_SOURCES "unit/*.cpp")
add_executable(orcdb_unit_tests ${UNIT_SOURCES})
target_link_libraries(orcdb_unit_tests PRIVATE
    orcdb_core
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)
target_include_directories(orcdb_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)
apply_compiler_options(orcdb_unit_tests)
gtest_discover_tests(orcdb_unit_tests)

# ── Integration tests ─────────────────────────────────────────────────────────
file(GLOB INTEG_SOURCES "integration/*.cpp")
add_executable(orcdb_integration_tests ${INTEG_SOURCES})
target_link_libraries(orcdb_integration_tests PRIVATE
    orcdb_core
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)
target_include_directories(orcdb_integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)
apply_compiler_options(orcdb_integration_tests)
gtest_discover_tests(orcdb_integration_tests
    PROPERTIES TIMEOUT 60)
