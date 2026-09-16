# SPDX-License-Identifier: MPL-2.0
if(CONFIG_OSKEY_NXP_SE)
  set(nano ${CMAKE_CURRENT_SOURCE_DIR}/lib/nano-package/lib)
  set(nxp ${CMAKE_CURRENT_SOURCE_DIR}/src/security/nxp)
  # Select the transport and SCP03 sources explicitly: upstream's Zephyr module
  # selects TinyCrypt, while this application's host crypto uses PSA exclusively.
  add_library(oskey_nxp STATIC
    ${nxp}/store.c ${nxp}/device.c ${nxp}/credentials.c
    ${nxp}/host_crypto_psa.c ${nxp}/port/zephyr.c
    ${nano}/apdu/se05x_APDU_impl.c ${nano}/apdu/se05x_tlv.c
    ${nano}/apdu/scp03/se05x_scp03.c ${nano}/apdu/scp03/se05x_auth_utils.c
    ${nano}/apdu/smCom.c ${nano}/t1oi2c/phNxpEse_Api.c
    ${nano}/t1oi2c/phNxpEsePal_i2c.c ${nano}/t1oi2c/phNxpEseProto7816_3.c
  )
  target_include_directories(oskey_nxp PRIVATE
    ${nxp}/port ${nxp} ${nano}/apdu ${nano}/apdu/scp03
    ${nano}/t1oi2c ${nano}/platform/zephyr)
  target_compile_definitions(oskey_nxp PRIVATE WITH_PLATFORM_SCP03 T1oI2C
    T1oI2C_UM11225 CONFIG_PLUGANDTRUST_APDU_BUFFER_SIZE=255)
  if(NOT "${CONFIG_OSKEY_NXP_CREDENTIALS_HEADER}" STREQUAL "")
    if(NOT EXISTS "${CONFIG_OSKEY_NXP_CREDENTIALS_HEADER}")
      message(FATAL_ERROR "NXP credential header does not exist")
    endif()
    target_compile_definitions(oskey_nxp PRIVATE
      OSKEY_NXP_CREDENTIALS_HEADER="${CONFIG_OSKEY_NXP_CREDENTIALS_HEADER}")
  endif()
  target_link_libraries(oskey_nxp PRIVATE zephyr_interface mbedTLS)
  add_dependencies(oskey_nxp zephyr_generated_headers)
  target_link_libraries(app PRIVATE oskey_nxp)
else()
  target_sources(app PRIVATE src/security/nxp/disabled.c)
endif()
