# Smart Home IoT — ESP32 IDF Component Registration

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c ${CMAKE_SOURCE_DIR}/src/*.cpp)

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash esp_http_client esp_http_server esp_wifi
                                esp_event esp_netif driver esp_timer json
                                esp_https_ota lwip vfs mqtt mdns esp_sntp)
