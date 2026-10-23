"""Exercise the build overlay, including failure on dependency drift."""
import shutil
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE = shutil.which("cmake")
if not CMAKE:
    candidate = Path.home() / ".platformio/packages/tool-cmake/bin/cmake.exe"
    if candidate.is_file():
        CMAKE = str(candidate)


@unittest.skipUnless(CMAKE, "CMake required for adapter overlay test")
class AdapterRotationTests(unittest.TestCase):
    def run_overlay(self, text, module="adapter_rotation", function="home_display_prepare_adapter_rotation"):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            source = folder / "vendor.c"
            output = folder / "build/rotation.c"
            source.write_text(text, encoding="utf-8")
            script = folder / "check.cmake"
            script.write_text(
                f'include("{(ROOT / "cmake" / (module + ".cmake")).as_posix()}")\n'
                f'{function}("{source.as_posix()}" "{output.as_posix()}")\n',
                encoding="utf-8",
            )
            result = subprocess.run([CMAKE, "-P", str(script)], capture_output=True, text=True)
            self.assertEqual(source.read_text(encoding="utf-8"), text)
            return result, output.read_text(encoding="utf-8") if output.exists() else None

    def test_partial_and_full_rotation_respect_disabled_ppa(self):
        vendor = ROOT / "managed_components/espressif__esp_lvgl_adapter/src/display/bridge/v9/lvgl_bridge_v9.c"
        if not vendor.is_file():
            self.skipTest("Adapter downloaded by firmware build")
        result, patched = self.run_overlay(vendor.read_text(encoding="utf-8"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(patched.count("    home_display_rotate_copy_region("), 2)
        self.assertIn("cpu_rotation.h", patched)
        for function in ("rotate_copy_strided_region", "rotate_copy_region"):
            definition = patched.rfind(f"static void IRAM_ATTR {function}(")
            self.assertGreaterEqual(definition, 0)
            body = patched[definition:]
            guard = body.index("if (impl->cfg.base.profile.enable_ppa_accel && hw_resource.ppa_handle")
            ppa_call = body.index("ppa_do_scale_rotate_mirror(")
            cpu_call = body.index("display_rotate_copy_region(")
            self.assertLess(guard, ppa_call)
            self.assertLess(ppa_call, cpu_call)
        # An allocated but disabled PPA client must not suppress CPU writeback
        # before invalidation discards the just-rendered pixels.
        start = patched.rfind("static void display_bridge_v9_flush_partial_rotate(")
        flush = patched[start:patched.index("/**********************", start)]
        guard = flush.index("if (!impl->cfg.base.profile.enable_ppa_accel || !hw_resource.ppa_handle)")
        writeback = flush.index("display_cache_msync_framebuffer(", guard)
        invalidate = flush.index("display_cache_msync_invalidate_framebuffer(")
        self.assertLess(guard, writeback)
        self.assertLess(writeback, invalidate)

    def test_cpu_rotation_pixels_stride_edges_and_fallback(self):
        compiler = shutil.which("g++") or shutil.which("clang++")
        vcvars = None
        if not compiler and os.name == "nt":
            vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
            if vswhere.exists():
                install = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
                    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
                if install:
                    vcvars = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
        if not compiler and not vcvars:
            self.skipTest("Native C++ compiler unavailable")
        fixture = ROOT / "tests/native/cpu_rotation.cpp"
        with tempfile.TemporaryDirectory(prefix="home-display-rotation-") as temp:
            exe = Path(temp) / ("check.exe" if os.name == "nt" else "check")
            if compiler:
                command = [compiler, "-std=c++17", str(fixture), "-o", str(exe)]
            else:
                command = f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /EHsc /std:c++17 "{fixture}" /Fe:check.exe"'
            result = subprocess.run(command, cwd=temp, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_changed_dependency_fails_closed(self):
        result, patched = self.run_overlay("/* unexpected adapter version */\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("rotation paths changed", result.stderr)
        self.assertIsNone(patched)

    def test_dma_repairs_preserve_cpu_pixels_in_shared_cache_lines(self):
        vendor = ROOT / "managed_components/espressif__esp_lvgl_adapter/src/display/bridge/v9/lvgl_bridge_v9.c"
        if not vendor.is_file():
            self.skipTest("Adapter downloaded by firmware build")
        result, patched = self.run_overlay(vendor.read_text(encoding="utf-8"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(patched.count("uint8_t *copy_cache_start"), 2)
        for function, end in (("copy_diff_repair_from_front_to_back", "static bool display_bridge_v9_prepare_partial_back_buffer"),
                              ("copy_unrendered_area_from_front_to_back", "/**\n * @brief Save current dirty region")):
            start = patched.rfind("static void " + function + "(")
            body = patched[start:patched.index(end, start)]
            writeback = body.index("display_cache_msync_range(copy_cache_start")
            dma = body.index("ESP_ERROR_CHECK(display_bridge_dma2d_copy_sync(")
            invalidate = body.index("display_cache_msync_invalidate_framebuffer(copy_cache_start")
            self.assertLess(writeback, dma)
            self.assertLess(dma, invalidate)
            self.assertIn("((copy_h_px - 1) * hor_res + copy_w_px) * color_bytes", body)
        start = patched.rfind("static void display_bridge_v9_flush_partial_rotate(")
        flush = patched[start:patched.index("/**********************", start)]
        self.assertIn("if (impl->cfg.base.profile.enable_ppa_accel) {\n            display_cache_msync_invalidate_framebuffer", flush)
        self.assertIn("if (impl->cfg.base.profile.enable_ppa_accel && hw_resource.ppa_handle) {\n    display_cache_msync_range(color_map", flush)

    def test_missing_dma_repair_guard_fails_closed(self):
        vendor = ROOT / "managed_components/espressif__esp_lvgl_adapter/src/display/bridge/v9/lvgl_bridge_v9.c"
        if not vendor.is_file():
            self.skipTest("Adapter downloaded by firmware build")
        source = vendor.read_text(encoding="utf-8").replace("&transfer, portMAX_DELAY", "&transfer, 1")
        result, patched = self.run_overlay(source)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("DMA repair changed", result.stderr)
        self.assertIsNone(patched)

    def test_unchanged_lua_text_does_not_reallocate_or_invalidate(self):
        source = (ROOT / "src/apps/lua/LuaBasicUiBridge.cpp").read_text(encoding="utf-8")
        body = source.split("int bridgeSetText(", 1)[1].split("int bridgeSetBgColor(", 1)[0]
        self.assertIn("strcmp(lv_label_get_text(obj), text) != 0", body)
        self.assertLess(body.index("strcmp("), body.index("lv_label_set_text("))

    def test_dsi_cpu_path_uses_driver_writeback_and_large_frame_diagnostics(self):
        vendor = ROOT / "managed_components/espressif__esp_lvgl_adapter/src/display/bridge/v9/lvgl_bridge_v9.c"
        if not vendor.is_file():
            self.skipTest("Adapter downloaded by firmware build")
        result, patched = self.run_overlay(vendor.read_text(encoding="utf-8"))
        self.assertEqual(result.returncode, 0, result.stderr)
        start = patched.rfind("static void display_bridge_v9_flush_partial_rotate(")
        flush = patched[start:patched.index("/**********************", start)]
        gate = "impl->cfg.base.profile.interface != ESP_LV_ADAPTER_PANEL_IF_MIPI_DSI ||"
        self.assertIn(gate, flush)
        self.assertIn("impl->cfg.base.profile.enable_ppa_accel) {\n                display_cache_msync_framebuffer", flush)
        self.assertIn("RENDER-DIAG pixels=%lu frame_ms=%lu rotation_ms=%lu", flush)
        self.assertLess(flush.index("display_bridge_v9_blit_full_frame("), flush.index("RENDER-DIAG"))
        # The driver really owns the C2M flush for its panel-owned buffers.
        driver = Path.home() / ".platformio/packages/framework-espidf/components/esp_lcd/dsi/esp_lcd_panel_dpi.c"
        if driver.is_file():
            source = driver.read_text(encoding="utf-8")
            section = source.split("if (!do_copy)", 1)[1].split("} else", 1)[0]
            self.assertIn("ESP_CACHE_MSYNC_FLAG_DIR_C2M", section)
            self.assertLess(section.index("esp_cache_msync("), section.index("dpi_panel->cur_fb_index"))

    def test_bsp_draw_buffer_reserves_internal_dma_memory(self):
        vendor = ROOT / "managed_components/waveshare__esp32_p4_platform/esp32_p4_platform.c"
        if not vendor.is_file():
            self.skipTest("BSP downloaded by firmware build")
        result, patched = self.run_overlay(
            vendor.read_text(encoding="utf-8"), "bsp_draw_buffer", "home_display_prepare_bsp_draw_buffer"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(".use_psram = true,", patched)
        self.assertNotIn(".use_psram = false,", patched)
        manager = ROOT / "managed_components/espressif__esp_lvgl_adapter/src/display/display_manager.c"
        result, patched = self.run_overlay(
            manager.read_text(encoding="utf-8"), "bsp_draw_buffer", "home_display_prepare_adapter_buffers"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("display_manager_alloc_draw_buffer(cfg->draw_buf_pixels * color_size, profile->use_psram)", patched)
        self.assertNotIn("display_manager_alloc_draw_buffer(cfg->draw_buf_pixels * color_size, false)", patched)
