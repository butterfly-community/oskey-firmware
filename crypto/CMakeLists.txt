add_library(crypto STATIC)

target_sources(
  crypto
  PRIVATE bip39_english.c
          bip39.c
          hmac.c
          memzero.c
          pbkdf2.c
          rand.c
          sha2.c
          test.c)
target_compile_definitions(crypto PRIVATE)

target_include_directories(crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
