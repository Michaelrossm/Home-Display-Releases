# Decoder-only, portable libwebp 1.6.0. No encoder tools, SIMD or new task.
file(GLOB IMAGE_WEBP_SOURCES "libwebp/src/dec/*.c" "libwebp/src/dsp/*.c" "libwebp/src/utils/*.c")
idf_component_register(SRCS ${IMAGE_WEBP_SOURCES} INCLUDE_DIRS "." "libwebp" "libwebp/src" REQUIRES esp_psram)
# Keep decoder-side scaling: WEBP_REDUCE_SIZE disables it and makes reduced
# photo previews fail with VP8_STATUS_INVALID_PARAM. No encoder is registered.
target_compile_definitions(${COMPONENT_LIB} PRIVATE malloc=hp_codec_malloc calloc=hp_codec_calloc realloc=hp_codec_realloc free=hp_codec_free)
