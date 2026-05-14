add_library(wealthtorii_ledger
        src/account.cpp
        src/transaction.cpp
        src/journal.cpp
)

add_library(wealthtorii::ledger ALIAS wealthtorii_ledger)

target_include_directories(wealthtorii_ledger
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(wealthtorii_ledger PUBLIC cxx_std_20)

target_link_libraries(wealthtorii_ledger
        PUBLIC
        wealthtorii::money
        wealthtorii::sanitizers
        PRIVATE
        wealthtorii::warnings
)

if(WEALTHTORII_BUILD_TESTS AND BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    add_executable(wealthtorii_ledger_tests
            tests/account_tests.cpp
            tests/transaction_tests.cpp
            tests/journal_tests.cpp
    )

    target_link_libraries(wealthtorii_ledger_tests
            PRIVATE
            wealthtorii::ledger
            wealthtorii::warnings
            GTest::gtest_main
    )

    gtest_discover_tests(wealthtorii_ledger_tests)
endif()
