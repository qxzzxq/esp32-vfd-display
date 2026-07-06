# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources}
                       PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_driver_i2c
                                     esp_wifi esp_netif esp_event nvs_flash lwip
                                     esp_http_server esp_http_client esp-tls mbedtls
                                     json esp_timer)
