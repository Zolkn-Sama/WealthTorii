if(NOT TARGET wealthtorii::warnings)
    add_library(wealthtorii_warnings INTERFACE)
    add_library(wealthtorii::warnings ALIAS wealthtorii_warnings)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        target_compile_options(wealthtorii_warnings INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
            -Wdouble-promotion
            -Wformat=2
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(wealthtorii_warnings INTERFACE /W4 /permissive-)
    endif()
endif()
