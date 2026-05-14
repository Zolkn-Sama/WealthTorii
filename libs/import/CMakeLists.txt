add_library(wealthtorii_import
        src/bp_csv.cpp
)

add_library(wealthtorii::import ALIAS wealthtorii_import)

target_include_directories(wealthtorii_import
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(wealthtorii_import PUBLIC cxx_std_20)

target_link_libraries(wealthtorii_import
        PUBLIC
        wealthtorii::ledger
        wealthtorii::money
        wealthtorii::sanitizers
        PRIVATE
        wealthtorii::warnings
)

if(WEALTHTORII_BUILD_TESTS AND BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    add_executable(wealthtorii_import_tests
            tests/bp_csv_tests.cpp
            tests/bp_real_data_tests.cpp
    )

    target_compile_definitions(wealthtorii_import_tests
            PRIVATE
            WEALTHTORII_DATA_CSV="${CMAKE_SOURCE_DIR}/DATA.csv"
    )

    target_link_libraries(wealthtorii_import_tests
            PRIVATE
            wealthtorii::import
            wealthtorii::budget
            wealthtorii::warnings
            GTest::gtest_main
    )

    gtest_discover_tests(wealthtorii_import_tests)
endif()
