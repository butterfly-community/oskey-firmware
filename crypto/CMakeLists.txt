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

include_directories(${CMAKE_CURRENT_SOURCE_DIR})