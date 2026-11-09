# Jedes Example ist eine Quelldatei mit dem Namen des Examples. Es wird gebaut
# und als Test registriert, damit ctest (und die CI) es ausfuehrt. Es gilt als
# bestanden, wenn das Programm mit 0 endet.
function(lunaris_add_example name)
    add_executable(example_${name} ${name}.cpp)
    target_link_libraries(example_${name} PRIVATE Lunaris::Lunaris)
    set_target_properties(example_${name} PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_options(example_${name} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    )
    add_test(NAME Example.${name} COMMAND example_${name})
endfunction()

lunaris_add_example(open_libraries)
