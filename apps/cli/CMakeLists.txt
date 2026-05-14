add_library(wealthtorii_cli_lib
        src/commands.cpp
        src/budget_config.cpp
        src/rules_config.cpp
)

target_include_directories(wealthtorii_cli_lib
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(wealthtorii_cli_lib
        PUBLIC
        wealthtorii::analytics
        wealthtorii::budget
        wealthtorii::import
        wealthtorii::ledger
        wealthtorii::money
        wealthtorii::storage
        wealthtorii::sanitizers
        PRIVATE
        wealthtorii::warnings
)

target_compile_features(wealthtorii_cli_lib PUBLIC cxx_std_20)

add_executable(wt src/main.cpp)
target_link_libraries(wt PRIVATE wealthtorii_cli_lib wealthtorii::warnings)

if(WEALTHTORII_BUILD_TESTS AND BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    add_executable(wealthtorii_cli_tests
            tests/commands_tests.cpp
            tests/budget_config_tests.cpp
            tests/rules_config_tests.cpp
    )

    target_compile_definitions(wealthtorii_cli_tests
            PRIVATE
            WEALTHTORII_DATA_CSV="${CMAKE_SOURCE_DIR}/DATA.csv"
    )

    target_link_libraries(wealthtorii_cli_tests
            PRIVATE
            wealthtorii_cli_lib
            wealthtorii::warnings
            GTest::gtest_main
    )

    gtest_discover_tests(wealthtorii_cli_tests)
endif()
