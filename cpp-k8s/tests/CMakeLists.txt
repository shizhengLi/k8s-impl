# 测试配置
add_executable(unit_tests
    test_storage.cpp
    test_api_types.cpp
    test_http_server.cpp
)

target_link_libraries(unit_tests
    PRIVATE
    k8s-core
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME unit_tests COMMAND unit_tests)

# 集成测试
add_executable(integration_tests
    test_integration.cpp
)

target_link_libraries(integration_tests
    PRIVATE
    k8s-core
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME integration_tests COMMAND integration_tests)

# 性能测试 (可选)
if(BUILD_PERFORMANCE_TESTS)
    add_executable(performance_tests
        test_performance.cpp
    )

    target_link_libraries(performance_tests
        PRIVATE
        k8s-core
        GTest::gtest
        GTest::gtest_main
    )

    add_test(NAME performance_tests COMMAND performance_tests)

    # 性能测试标记
    set_tests_properties(performance_tests PROPERTIES
        LABELS "performance"
        TIMEOUT 60
    )
endif()

# 测试套件
set(TEST_DEPENDENCIES unit_tests integration_tests)
if(BUILD_PERFORMANCE_TESTS)
    list(APPEND TEST_DEPENDENCIES performance_tests)
endif()

add_custom_target(test_all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS ${TEST_DEPENDENCIES}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# 独立运行测试
add_custom_target(run_unit_tests
    COMMAND unit_tests
    DEPENDS unit_tests
)

add_custom_target(run_integration_tests
    COMMAND integration_tests
    DEPENDS integration_tests
)

if(BUILD_PERFORMANCE_TESTS)
    add_custom_target(run_performance_tests
        COMMAND performance_tests
        DEPENDS performance_tests
    )
endif()