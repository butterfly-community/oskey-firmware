file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/compile_options.txt"
     CONTENT "${options}"
)
add_library(crypto STATIC
    bip39_english.c
    bip39.c
    hmac.c
    memzero.c
    pbkdf2.c
    rand.c
    sha2.c
)


target_compile_definitions(crypto PRIVATE
)

string(REGEX REPLACE "\\$<SEMICOLON>\\$<TARGET_PROPERTY:compiler,imacros>\\$<SEMICOLON>[^$]+/autoconf\\.h" "" options "${options}")

separate_arguments(options_list UNIX_COMMAND "${options}")

message(STATUS "Zephyr Options Log: ${options_list}")

target_compile_options(crypto PRIVATE ${options_list})
