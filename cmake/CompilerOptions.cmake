function(apply_compiler_options target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -Wno-missing-field-initializers
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
        $<$<CONFIG:Debug>:-O0 -g3>
        $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG>
    )

    if(ORCDB_ENABLE_SANITIZERS)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()

    if(ORCDB_ENABLE_COVERAGE)
        target_compile_options(${target} PRIVATE --coverage -fprofile-arcs -ftest-coverage)
        target_link_options(${target} PRIVATE --coverage)
    endif()
endfunction()
