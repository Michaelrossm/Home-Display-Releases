# Reserve internal SRAM for Ethernet/SDIO DMA. The P4 can draw into PSRAM;
# panel framebuffers already live there. Keep one BSP/display instance.
function(home_display_prepare_bsp_draw_buffer source destination)
    file(READ "${source}" contents)
    set(needle ".use_psram = false,")
    string(REPLACE "${needle}" ".use_psram = true," patched "${contents}")
    if(patched STREQUAL contents)
        message(FATAL_ERROR "Pinned BSP draw buffer profile changed; review PSRAM policy")
    endif()
    get_filename_component(output_dir "${destination}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_dir}")
    file(WRITE "${destination}.in" "${patched}")
    configure_file("${destination}.in" "${destination}" COPYONLY)
endfunction()

# The pinned adapter's tear-buffer path hardcodes internal RAM, unlike its
# normal allocator. Honor the profile here too, so the BSP policy takes effect.
function(home_display_prepare_adapter_buffers source destination)
    file(READ "${source}" contents)
    set(needle "display_manager_alloc_draw_buffer(cfg->draw_buf_pixels * color_size, false)")
    string(REPLACE "${needle}" "display_manager_alloc_draw_buffer(cfg->draw_buf_pixels * color_size, profile->use_psram)" patched "${contents}")
    if(patched STREQUAL contents)
        message(FATAL_ERROR "Pinned adapter partial buffer allocator changed; review PSRAM policy")
    endif()
    get_filename_component(output_dir "${destination}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_dir}")
    file(WRITE "${destination}.in" "${patched}")
    configure_file("${destination}.in" "${destination}" COPYONLY)
endfunction()
