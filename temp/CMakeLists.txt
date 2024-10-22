add_library(temp STATIC)

target_sources(
  temp
  PRIVATE
          test.c)
target_compile_definitions(temp PRIVATE)
