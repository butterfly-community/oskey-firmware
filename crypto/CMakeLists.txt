add_library(crypto STATIC)

target_sources(
  crypto
  PRIVATE
          bip39.c
          bip32.c
          codec/base58.c)
target_compile_definitions(crypto PRIVATE)


target_link_libraries(crypto PRIVATE mbedTLS)
