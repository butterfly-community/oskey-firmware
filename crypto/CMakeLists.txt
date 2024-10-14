cmake_minimum_required(VERSION 3.20.0)
project(crypto)

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


target_compile_options(crypto PRIVATE
    -W
    -Wall
    -Wextra
    -Wimplicit-function-declaration
    -Wredundant-decls
    -Wstrict-prototypes
    -Wundef
    -Wshadow
    -Wpointer-arith
    -Wformat
    -Wreturn-type
    -Wsign-compare
    -Wmultichar
    -Wformat-nonliteral
    -Winit-self
    -Wuninitialized
    -Wformat-security
    -Wno-missing-braces
    -Werror
)