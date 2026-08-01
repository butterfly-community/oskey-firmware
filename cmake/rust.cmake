if(CONFIG_OSKEY_RUST)
  add_subdirectory(lib/core/wallet/psa)

  # native_sim shares host libc/libm; a bare-metal Rust target would override them.
  if(CONFIG_BOARD_NATIVE_SIM AND CONFIG_64BIT AND
     CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64")
    function(_rust_map_target)
      set(RUST_TARGET "x86_64-unknown-linux-gnu" PARENT_SCOPE)
    endfunction()
  endif()

  rust_cargo_application()

  target_link_libraries(crypto PRIVATE zephyr_interface mbedTLS)
  target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/core/wallet)
  target_link_libraries(app PRIVATE crypto)
else()
  target_sources(app PRIVATE src/bindings.c)
endif()
