project('memmo', 'c')
add_project_arguments('-g','-O0', language: 'c')
# Find Clang-Format and Clang-Tidy
clang_format = find_program('clang-format', required: false)
# clang_tidy = find_program('clang-tidy', required: false)

if clang_format.found()
    run_command(clang_format, '--Werror', 'main.c')
endif

# Build the executable and link it with the static library
executable('memmo', 'main.c')