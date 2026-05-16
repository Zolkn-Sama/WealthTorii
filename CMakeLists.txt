cmake_minimum_required(VERSION 3.28)
project(wealthtorii VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ProjectOptions)
include(Warnings)
include(Sanitizers)

if(WEALTHTORII_BUILD_TESTS)
    include(CTest)
    enable_testing()
endif()

add_subdirectory(libs/money)
add_subdirectory(libs/ledger)
add_subdirectory(libs/budget)
add_subdirectory(libs/import)
add_subdirectory(libs/storage)
add_subdirectory(libs/analytics)
add_subdirectory(apps/cli)
add_subdirectory(apps/api)
