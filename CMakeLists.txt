cmake_minimum_required(VERSION 3.16)

if(DEFINED ENV{PIOENV})
    set(_pioenv "$ENV{PIOENV}")
else()
    # PlatformIO passes the generated per-environment SDKCONFIG path even
    # when PIOENV is not exported to the CMake process.
    get_filename_component(_sdkconfig_name "${SDKCONFIG}" NAME)
    string(REGEX REPLACE "^sdkconfig\\." "" _pioenv "${_sdkconfig_name}")
endif()

set(_defaults "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
if(_pioenv MATCHES "legacy")
    list(APPEND _defaults "${CMAKE_SOURCE_DIR}/config/sdkconfig.rev1_3")
else()
    list(APPEND _defaults "${CMAKE_SOURCE_DIR}/config/sdkconfig.rev3_x")
endif()
if(_pioenv MATCHES "_prod$")
    list(APPEND _defaults "${CMAKE_SOURCE_DIR}/config/sdkconfig.prod")
else()
    list(APPEND _defaults "${CMAKE_SOURCE_DIR}/config/sdkconfig.dev")
endif()

list(JOIN _defaults ";" SDKCONFIG_DEFAULTS)
message(STATUS "Home-Display SDKCONFIG_DEFAULTS=${SDKCONFIG_DEFAULTS}")

# Skip ESP-IDF's git_describe() version probe: it fails in this build
# environment (fresh CMake reconfigures run git with a working directory
# git can't resolve) and the app already carries its own version in BuildInfo.h.
if(NOT DEFINED PROJECT_VER)
    file(STRINGS "${CMAKE_SOURCE_DIR}/include/BuildInfo.h" _version_line REGEX "kVersion =")
    string(REGEX MATCH "\"([^\"]+)\"" _version_match "${_version_line}")
    set(PROJECT_VER "${CMAKE_MATCH_1}")
    if(NOT _pioenv MATCHES "_prod$")
        string(APPEND PROJECT_VER "-dev")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(home_display)

# The pinned adapter enables its PPA rotation client independently of the
# BSP's enable_ppa_accel=false. Respect that policy to avoid blocking LVGL in
# PPA SRM during DSI scanout. Use the adapter's existing CPU fallback.
include("${CMAKE_SOURCE_DIR}/cmake/adapter_rotation.cmake")
set(_adapter_target __idf_espressif__esp_lvgl_adapter)
if(TARGET ${_adapter_target})
    idf_component_get_property(_adapter_dir espressif__esp_lvgl_adapter COMPONENT_DIR)
    set(_rotation_source "${_adapter_dir}/src/display/bridge/v9/lvgl_bridge_v9.c")
    set(_rotation_fixed "${CMAKE_BINARY_DIR}/home_display_adapter/lvgl_bridge_v9.c")
    home_display_prepare_adapter_rotation("${_rotation_source}" "${_rotation_fixed}")
    get_target_property(_adapter_sources ${_adapter_target} SOURCES)
    list(FIND _adapter_sources "${_rotation_source}" _rotation_index)
    if(_rotation_index LESS 0)
        message(FATAL_ERROR "LVGL v9 rotation source missing from pinned adapter target")
    endif()
    list(REMOVE_AT _adapter_sources ${_rotation_index})
    list(APPEND _adapter_sources "${_rotation_fixed}")
    set_property(TARGET ${_adapter_target} PROPERTY SOURCES "${_adapter_sources}")
endif()

include("${CMAKE_SOURCE_DIR}/cmake/bsp_draw_buffer.cmake")
if(TARGET ${_adapter_target})
    set(_manager_source "${_adapter_dir}/src/display/display_manager.c")
    set(_manager_fixed "${CMAKE_BINARY_DIR}/home_display_adapter/display_manager.c")
    home_display_prepare_adapter_buffers("${_manager_source}" "${_manager_fixed}")
    get_target_property(_adapter_sources ${_adapter_target} SOURCES)
    list(FIND _adapter_sources "${_manager_source}" _manager_index)
    if(_manager_index LESS 0)
        message(FATAL_ERROR "Pinned adapter buffer source missing from target")
    endif()
    list(REMOVE_AT _adapter_sources ${_manager_index})
    list(APPEND _adapter_sources "${_manager_fixed}")
    set_property(TARGET ${_adapter_target} PROPERTY SOURCES "${_adapter_sources}")
endif()
set(_bsp_target __idf_waveshare__esp32_p4_platform)
if(TARGET ${_bsp_target})
    idf_component_get_property(_bsp_dir waveshare__esp32_p4_platform COMPONENT_DIR)
    set(_bsp_source "${_bsp_dir}/esp32_p4_platform.c")
    set(_bsp_fixed "${CMAKE_BINARY_DIR}/home_display_bsp/esp32_p4_platform.c")
    home_display_prepare_bsp_draw_buffer("${_bsp_source}" "${_bsp_fixed}")
    get_target_property(_bsp_sources ${_bsp_target} SOURCES)
    list(FIND _bsp_sources "${_bsp_source}" _bsp_index)
    if(_bsp_index LESS 0)
        message(FATAL_ERROR "Pinned BSP source missing from target")
    endif()
    list(REMOVE_AT _bsp_sources ${_bsp_index})
    list(APPEND _bsp_sources "${_bsp_fixed}")
    set_property(TARGET ${_bsp_target} PROPERTY SOURCES "${_bsp_sources}")
endif()

# esp_lvgl_adapter exports its private alignment header via `-include`. The
# Pioarduino ESP-IDF bridge splits that two-token option while importing the
# CMake compile model. The public alignment definition is retained.
foreach(_lvgl_target __idf_esp_lvgl_adapter __idf_lvgl)
    if(TARGET ${_lvgl_target})
        set_property(TARGET ${_lvgl_target} PROPERTY INTERFACE_COMPILE_OPTIONS "")
    endif()
endforeach()

# LV_CONF_SKIP is enabled by the BSP's Kconfig, so include/lv_conf.h does not
# configure the managed LVGL component. Apply only our memory policy to that
# real target only; keep every other pinned LVGL setting intact. In particular,
# do not export PSRAM headers into SDK components or the separate bootloader.
if(TARGET __idf_lvgl__lvgl)
    target_compile_options(__idf_lvgl__lvgl PRIVATE
        "$<$<COMPILE_LANGUAGE:C,CXX>:-include${CMAKE_SOURCE_DIR}/include/LvglMemoryConfig.h>")
else()
    message(FATAL_ERROR "Pinned LVGL target missing; review memory policy")
endif()
