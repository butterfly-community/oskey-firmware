if(CONFIG_OSKEY_WIFI)
  set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
  generate_inc_file_for_target(app
    src/net/wifi_portal.html
    ${gen_dir}/wifi_portal.html.gz.inc
    --gzip
  )

  zephyr_linker_sources(SECTIONS src/net/wifi_portal-sections.ld)
  # Enable when supporting CMake linker generator targets.
  # zephyr_iterable_section(
  #   NAME http_resource_desc_wifi_portal_service
  #   GROUP RODATA_REGION
  # )
endif()
