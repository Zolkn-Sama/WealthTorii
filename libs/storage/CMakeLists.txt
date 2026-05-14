find_package(libpqxx CONFIG REQUIRED)

add_library(wealthtorii_storage
        src/connection.cpp
        src/migrations.cpp
        src/repository.cpp
)

add_library(wealthtorii::storage ALIAS wealthtorii_storage)

target_include_directories(wealthtorii_storage
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(wealthtorii_storage PUBLIC cxx_std_20)

target_compile_definitions(wealthtorii_storage
        PRIVATE
        WEALTHTORII_DEFAULT_MIGRATIONS_DIR="${CMAKE_SOURCE_DIR}/migrations"
)

target_link_libraries(wealthtorii_storage
        PUBLIC
        wealthtorii::ledger
        wealthtorii::budget
        wealthtorii::money
        libpqxx::pqxx
        wealthtorii::sanitizers
        PRIVATE
        wealthtorii::warnings
)

if(WEALTHTORII_BUILD_TESTS AND BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    add_executable(wealthtorii_storage_tests
            tests/storage_tests.cpp
    )

    target_link_libraries(wealthtorii_storage_tests
            PRIVATE
            wealthtorii::storage
            wealthtorii::warnings
            GTest::gtest_main
    )

    gtest_discover_tests(wealthtorii_storage_tests)
endif()
