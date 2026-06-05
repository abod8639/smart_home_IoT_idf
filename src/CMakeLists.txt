# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources} "wifi_manager.c" "mqtt_manager.c" "matter_manager.cpp" "firebase_manager.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash esp_http_client esp_http_server esp_wifi esp_event esp_netif driver esp_timer json esp_https_ota lwip vfs mqtt)
