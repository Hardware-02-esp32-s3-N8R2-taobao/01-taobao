cmake_minimum_required(VERSION 3.16)

set(APP_CONFIG_HEADER "${CMAKE_CURRENT_LIST_DIR}/main/include/app_config.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${APP_CONFIG_HEADER}")
file(READ "${APP_CONFIG_HEADER}" APP_CONFIG_CONTENT)
string(REGEX MATCH "#define[ \t]+APP_FIRMWARE_VERSION[ \t]+\"([^\"]+)\"" _app_fw_version_match "${APP_CONFIG_CONTENT}")
if(CMAKE_MATCH_1)
    set(APP_FIRMWARE_VERSION "${CMAKE_MATCH_1}")
else()
    message(FATAL_ERROR "APP_FIRMWARE_VERSION not found in ${APP_CONFIG_HEADER}")
endif()
set(PROJECT_VER "${APP_FIRMWARE_VERSION}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(yd_esp32c3_wifi_mqtt_forward_demo)

set(VERSIONED_FIRMWARE_BIN "${CMAKE_BINARY_DIR}/${PROJECT_NAME}_v${APP_FIRMWARE_VERSION}.bin")
add_custom_target(versioned_firmware ALL
    DEPENDS gen_project_binary
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin"
            "${VERSIONED_FIRMWARE_BIN}"
    COMMENT "Creating versioned firmware binary: ${PROJECT_NAME}_v${APP_FIRMWARE_VERSION}.bin"
    VERBATIM
)
