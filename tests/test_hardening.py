"""Source guards plus native fault injection of the production upload commit.

The mock filesystem verifies control flow, not real FAT/LittleFS power-cut safety.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import importlib.util
from unittest.mock import patch
from contextlib import redirect_stdout
import io

ROOT = Path(__file__).resolve().parents[1]

def source(path):
    return (ROOT / path).read_text(encoding="utf-8")

class HardeningTests(unittest.TestCase):
    def test_blank_partition_checks_entire_partition(self):
        body = source("src/services/StorageService.cpp").split(
            "bool StorageService::partitionLooksBlank", 1)[1].split(
            "bool StorageService::mountInternal", 1)[0]
        self.assertIn("offset < p->size", body)
        self.assertIn("esp_partition_read(p, offset", body)
        self.assertIn("return false", body)

    def test_paths_reject_dot_segments_and_control_bytes(self):
        body = source("src/services/StorageService.cpp").split(
            "bool StorageService::validateRelativePath", 1)[1].split(
            "fs::FS* StorageService::fsFor(StorageArea", 1)[0]
        self.assertIn('part == "." || part == ".."', body)
        self.assertIn("< 32", body)
        self.assertIn("p.indexOf('\\\\')", body)

    def test_worker_failure_releases_queue_and_stats_are_atomic(self):
        self.assertIn("vQueueDelete(q_)", source("src/services/NetworkWorker.cpp"))
        self.assertIn("std::atomic<uint32_t>", source("src/services/NetworkWorker.h"))

    def test_password_not_reset_or_logged(self):
        body = source("src/services/FileTransferService.cpp").split(
            "bool initializeWebAdminPassword", 1)[1].split("bool webAdminAuthorized", 1)[0]
        self.assertIn("WebAdminAuth::initialize()", body)
        shared = source("src/services/WebAdminAuth.cpp")
        self.assertIn("if (password.length()) return true", shared)
        self.assertIn('if (config.hasSecret("web_admin_pwd")) return false', shared)
        self.assertIn('!config.putSecretString("web_admin_pwd", "Passwort")', shared)
        self.assertNotIn("Serial.println(password)", shared)
        self.assertNotIn("if (!migrated ||", body)
        self.assertNotIn("Serial.println(webAdminPassword)", body)

    def test_network_reset_includes_profiles(self):
        text = source("src/services/ConfigService.cpp")
        for i in range(3):
            for prefix in ("wifi_ssid_", "wifi_pwd_", "wifi_ip_", "wifi_mask_"):
                self.assertIn(f'"{prefix}{i}"', text)

    def test_native_upload_rename_failures(self):
        production = source("src/services/FileTransferService.cpp").split(
            "bool commitUploadedFile", 1)[1].split("// HTTP Basic", 1)[0]
        fixture = source("tests/native/upload_commit.cpp").replace(
            "// PRODUCTION_COMMIT_FUNCTION", "bool commitUploadedFile" + production)
        storage = source("src/services/StorageService.cpp")
        blank = "bool StorageService::partitionLooksBlank" + storage.split(
            "bool StorageService::partitionLooksBlank", 1)[1].split(
            "bool StorageService::mountInternal", 1)[0]
        paths = "bool StorageService::validateRelativePath" + storage.split(
            "bool StorageService::validateRelativePath", 1)[1].split(
            "fs::FS* StorageService::fsFor(StorageArea", 1)[0]
        fixture = fixture.replace("// PRODUCTION_STORAGE_FUNCTIONS", blank + paths)
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
        with tempfile.TemporaryDirectory(prefix="home-display-test-") as folder:
            path = Path(folder)
            (path / "check.cpp").write_text(fixture, encoding="utf-8")
            exe = path / ("check.exe" if os.name == "nt" else "check")
            if compiler:
                cmd = [compiler, "-std=c++17", "check.cpp", "-o", str(exe)]
            else:
                cmd = f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /EHsc /std:c++17 check.cpp /Fe:check.exe"'
            result = subprocess.run(cmd, cwd=path, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], cwd=path, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_scheduler_cleans_failed_start_and_never_drops_completion(self):
        cpp = source("src/core/BackgroundScheduler.cpp")
        self.assertIn("vTaskDelete(worker)", cpp)
        self.assertIn("vQueueDelete(blockingQueue_)", cpp)
        self.assertIn("vSemaphoreDelete(stateLock_)", cpp)
        self.assertIn("compare_exchange_weak", cpp)
        completion = cpp.split("void BackgroundScheduler::clearInFlight", 1)[1].split(
            "void BackgroundScheduler::runScheduler", 1)[0]
        self.assertIn("portMAX_DELAY", completion)

    def test_build_rejects_cached_wrong_silicon_revision(self):
        spec = importlib.util.spec_from_file_location("hardening_dev", ROOT / "tools/dev.py")
        dev = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(dev)
        with tempfile.TemporaryDirectory(prefix="home-display-profile-") as folder:
            root = Path(folder)
            header = root / ".pio/build/home_display_p4_r3_dev/config/sdkconfig.h"
            header.parent.mkdir(parents=True)
            header.write_text("#define CONFIG_ESP32P4_REV_MIN_100 1\n")
            with patch.object(dev, "ROOT", root), patch.object(dev, "pio", return_value=["pio"]), \
                 patch.object(dev, "run", return_value=0), redirect_stdout(io.StringIO()):
                self.assertEqual(dev.build("home_display_p4_r3_dev"), 4)
                header.write_text("#define CONFIG_ESP32P4_REV_MIN_300 1\n")
                self.assertEqual(dev.build("home_display_p4_r3_dev"), 0)
