# Pioarduino hybrid mode injects the Arduino framework itself.
# This component only registers Home-Display sources and include paths,
# including shared web authentication, input keyboard, calendar/reminder and intercom runtime.
file(GLOB_RECURSE HOMEDISPLAY_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
)

# Only active native apps and shared backends remain in the source tree.

idf_component_register(
    SRCS ${HOMEDISPLAY_SOURCES}
    INCLUDE_DIRS
        "."
        "../include"
    EMBED_TXTFILES
        "../LICENSE.md"
        "../THIRD_PARTY_NOTICES.md"
    REQUIRES
        esp_https_server
        esp_http_client
        chmorgan__esp-audio-player
        espressif__usb_host_msc
        espressif__lua
        espressif__zlib
        espressif__libpng
        espressif__esp_new_jpeg
        espressif__esp_h264
        esp_driver_jpeg
        home_image_codecs
        sahlberg__libsmb2
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
