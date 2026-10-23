# Keep the pinned adapter intact. Compile a build-local copy with the BSP's
# acceleration policy applied to BOTH rotation paths (partial and full).
function(home_display_prepare_adapter_rotation source destination)
    file(READ "${source}" contents)
    set(needle "if (hw_resource.ppa_handle && (color_bytes == 2 || color_bytes == 3)) {")
    set(replacement "if (impl->cfg.base.profile.enable_ppa_accel && hw_resource.ppa_handle && (color_bytes == 2 || color_bytes == 3)) {")
    string(REPLACE "${needle}" "${replacement}" patched "${contents}")
    string(REGEX MATCHALL "impl->cfg.base.profile.enable_ppa_accel && hw_resource.ppa_handle" guards "${patched}")
    list(LENGTH guards guard_count)
    if(NOT guard_count EQUAL 2 OR patched STREQUAL contents)
        message(FATAL_ERROR "Pinned LVGL adapter rotation paths changed; review the CPU rotation fix before building")
    endif()
    # CPU rotation writes the cached framebuffer. Flush it before the adapter's
    # M2C invalidation, even though an unused PPA client is still registered.
    set(cache_needle "if (!hw_resource.ppa_handle) {")
    string(FIND "${patched}" "${cache_needle}" cache_guard)
    if(cache_guard LESS 0)
        message(FATAL_ERROR "Pinned adapter CPU framebuffer cache guard changed; review before building")
    endif()
    string(REPLACE "${cache_needle}" "if (!impl->cfg.base.profile.enable_ppa_accel || !hw_resource.ppa_handle) {" patched "${patched}")
    # The pinned MIPI DSI driver writes back its own framebuffer in
    # esp_lcd_panel_draw_bitmap(). In CPU mode we no longer invalidate the
    # entire frame below, so a second full writeback here is redundant.
    set(cpu_writeback "        if (!impl->cfg.base.profile.enable_ppa_accel || !hw_resource.ppa_handle) {\n            display_cache_msync_framebuffer(impl->draw_fb, impl->runtime.frame_buffer_size);\n        }")
    string(FIND "${patched}" "${cpu_writeback}" cpu_writeback_offset)
    if(cpu_writeback_offset LESS 0)
        message(FATAL_ERROR "Pinned CPU writeback path changed; review DSI cache ownership")
    endif()
    string(REPLACE "${cpu_writeback}" "        if (!impl->cfg.base.profile.enable_ppa_accel || !hw_resource.ppa_handle) {\n            if (impl->cfg.base.profile.interface != ESP_LV_ADAPTER_PANEL_IF_MIPI_DSI ||\n                    impl->cfg.base.profile.enable_ppa_accel) {\n                display_cache_msync_framebuffer(impl->draw_fb, impl->runtime.frame_buffer_size);\n            }\n        }" patched "${patched}")
    # Partial CPU rotation and DMA2D repair may share cache lines. Synchronize
    # each destination window BEFORE DMA and invalidate it AFTER completion.
    # Otherwise final CPU writeback can restore stale pixels over the repair.
    set(diff_dma "            ESP_ERROR_CHECK(display_bridge_dma2d_copy_sync(\n                                &transfer, portMAX_DELAY));")
    set(baseline_dma "            ESP_ERROR_CHECK(display_bridge_dma2d_copy_sync(&tr, portMAX_DELAY));")
    foreach(kind diff baseline)
        if(kind STREQUAL "diff")
            set(call "${diff_dma}")
            set(rect "rect")
        else()
            set(call "${baseline_dma}")
            set(rect "r")
        endif()
        string(FIND "${patched}" "${call}" copy_offset)
        if(copy_offset LESS 0)
            message(FATAL_ERROR "Pinned adapter ${kind} DMA repair changed; review cache ownership")
        endif()
        set(sync "            uint8_t *copy_cache_start = (uint8_t *)impl->draw_fb +\n                ((size_t)${rect}.y1 * hor_res + ${rect}.x1) * color_bytes;\n            size_t copy_cache_size =\n                ((copy_h_px - 1) * hor_res + copy_w_px) * color_bytes;\n            display_cache_msync_range(copy_cache_start, copy_cache_size, impl->cache_line_size);\n${call}\n            display_cache_msync_invalidate_framebuffer(copy_cache_start, copy_cache_size);")
        string(REPLACE "${call}" "${sync}" patched "${patched}")
    endforeach()
    # Window-local DMA invalidation now handles repairs. Keep the CPU-written
    # frame cached rather than discarding it wholesale before the next frame.
    set(invalidate "#if SOC_DMA2D_SUPPORTED || CONFIG_SOC_PPA_SUPPORTED\n        display_cache_msync_invalidate_framebuffer(impl->draw_fb, impl->runtime.frame_buffer_size);\n#endif")
    string(FIND "${patched}" "${invalidate}" invalidate_offset)
    if(invalidate_offset LESS 0)
        message(FATAL_ERROR "Pinned rotated partial cache invalidation changed; review before building")
    endif()
    string(REPLACE "${invalidate}" "#if SOC_DMA2D_SUPPORTED || CONFIG_SOC_PPA_SUPPORTED\n        if (impl->cfg.base.profile.enable_ppa_accel) {\n            display_cache_msync_invalidate_framebuffer(impl->draw_fb, impl->runtime.frame_buffer_size);\n        }\n#endif" patched "${patched}")
    # LVGL and CPU rotation use the same cached draw buffer; only PPA needs its
    # source written back to PSRAM. Avoid this redundant flush on every strip.
    set(source_sync "    display_cache_msync_range(color_map, stride_bytes * rect_h, impl->cache_line_size);")
    string(FIND "${patched}" "${source_sync}" source_offset)
    if(source_offset LESS 0)
        message(FATAL_ERROR "Pinned rotated partial source cache sync changed; review before building")
    endif()
    string(REPLACE "${source_sync}" "    if (impl->cfg.base.profile.enable_ppa_accel && hw_resource.ppa_handle) {\n${source_sync}\n    }" patched "${patched}")
    # Optimize the existing CPU fallback only; PPA and framebuffer/cache flow
    # stay untouched. Refuse a changed vendor call graph rather than guessing.
    string(REGEX MATCHALL "    display_rotate_copy_region\\(" cpu_calls "${patched}")
    list(LENGTH cpu_calls cpu_call_count)
    if(NOT cpu_call_count EQUAL 2)
        message(FATAL_ERROR "Pinned adapter CPU rotation calls changed; review before building")
    endif()
    string(REPLACE "    display_rotate_copy_region(" "    home_display_rotate_copy_region(" patched "${patched}")
    set(common_include "#include \"common/display_bridge_common.h\"")
    string(FIND "${patched}" "${common_include}" common_header)
    if(common_header LESS 0)
        message(FATAL_ERROR "Pinned adapter common header changed; review before building")
    endif()
    string(REPLACE "${common_include}" "${common_include}\n#include \"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cpu_rotation.h\"" patched "${patched}")
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/render_diagnostics.cmake")
    home_display_add_render_diagnostics("${patched}" patched)
    get_filename_component(output_dir "${destination}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_dir}")
    # configure_file avoids changing the timestamp on every configure.
    file(WRITE "${destination}.in" "${patched}")
    configure_file("${destination}.in" "${destination}" COPYONLY)
endfunction()
