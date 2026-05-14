if(NOT TARGET wealthtorii::sanitizers)
    add_library(wealthtorii_sanitizers INTERFACE)
    add_library(wealthtorii::sanitizers ALIAS wealthtorii_sanitizers)

    set(_sanitizer_flags "")

    if(WEALTHTORII_ENABLE_ASAN)
        list(APPEND _sanitizer_flags -fsanitize=address -fno-omit-frame-pointer)
    endif()

    if(WEALTHTORII_ENABLE_UBSAN)
        list(APPEND _sanitizer_flags -fsanitize=undefined -fno-omit-frame-pointer)
    endif()

    if(_sanitizer_flags AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        target_compile_options(wealthtorii_sanitizers INTERFACE ${_sanitizer_flags})
        target_link_options(wealthtorii_sanitizers INTERFACE ${_sanitizer_flags})
    endif()
endif()
