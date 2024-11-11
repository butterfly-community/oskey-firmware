add_library(temp STATIC)

target_sources(
  temp
  PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/test.c)
