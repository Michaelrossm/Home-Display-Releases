#!/usr/bin/env python3
"""Build the ESP-Hosted ESP32-C6 slave firmware separately from the P4 image.

Run this from an ESP-IDF 5.5.5 PowerShell/CMD environment. The C6 has its own
flash; this image must never be appended to the P4 firmware binary.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build" / "c6-esp-hosted-slave"
EXAMPLE = "espressif/esp_hosted:slave"
REPOSITORY = "https://github.com/espressif/esp-hosted-mcu.git"
TAG = "v2.12.13"
EXPECTED_VERSION = "2.12.13"


def idf() -> list[str]:
    value = os.environ.get("IDF_PY") or shutil.which("idf.py")
    if not value:
        raise SystemExit(
            "idf.py fehlt. Oeffne zuerst eine ESP-IDF 5.5.5 PowerShell und "
            "starte dieses Tool erneut."
        )
    if value.lower().endswith(".py"):
        return [sys.executable, value]
    return [value]


def run(args: list[str], cwd: Path) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=cwd, check=True)


def ensure_project(idf_py: list[str]) -> Path:
    source = WORK / "esp-hosted-mcu"
    project = source / "slave"
    if project.is_dir() and (project / "CMakeLists.txt").is_file():
        return project
    WORK.mkdir(parents=True, exist_ok=True)
    if not source.exists():
        run(["git", "clone", "--depth", "1", "--branch", TAG, REPOSITORY, str(source)], ROOT)
    if project.is_dir():
        return project
    candidates = [p for p in WORK.iterdir() if p.is_dir() and (p / "CMakeLists.txt").is_file()]
    if len(candidates) == 1:
        return candidates[0]
    raise SystemExit(f"ESP-Hosted-Beispielprojekt wurde nicht unter {WORK} erzeugt.")


def write_defaults(project: Path) -> None:
    (project / "sdkconfig.defaults").write_text(
        "\n".join(
            [
                "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y",
                "CONFIG_BT_ENABLED=y",
                "CONFIG_BT_CONTROLLER_ONLY=y",
                "CONFIG_BT_LE_HCI_INTERFACE_USE_RAM=y",
                "CONFIG_PARTITION_TABLE_CUSTOM=y",
                'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.esp32c6.csv"',
                "CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y",
                "CONFIG_ESP_CACHE_MALLOC=n",
                "CONFIG_WIFI_CMD_BASIC_ONLY=y",
                "CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=10",
                "CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32",
                "CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=32",
                "CONFIG_ESP_WIFI_AMPDU_TX_ENABLED=y",
                "CONFIG_ESP_WIFI_AMPDU_RX_ENABLED=y",
                "CONFIG_ESP_WIFI_RX_IRAM_OPT=y",
                "CONFIG_ESP_WIFI_IRAM_OPT=y",
                "CONFIG_ESP_SDIO_HOST_INTERFACE=y",
                "CONFIG_ESP_SDIO_STREAMING_MODE=y",
                "CONFIG_ESP_SDIO_HIGH_SPEED=y",
                "CONFIG_ESP_SDIO_GPIO_RESET=-1",
                "CONFIG_ESP_SDIO_PIN_CMD=18",
                "CONFIG_ESP_SDIO_PIN_CLK=19",
                "CONFIG_ESP_SDIO_PIN_D0=20",
                "CONFIG_ESP_SDIO_PIN_D1=21",
                "CONFIG_ESP_SDIO_PIN_D2=22",
                "CONFIG_ESP_SDIO_PIN_D3=23",
                "CONFIG_FREERTOS_HZ=1000",
                "CONFIG_COMPILER_OPTIMIZATION_PERF=y",
                "",
            ]
        ),
        encoding="ascii",
    )
    version = project / "main" / "esp_hosted_coprocessor_fw_ver.h"
    if version.is_file():
        text = version.read_text(encoding="ascii")
        if 'PROJECT_VERSION_MAJOR_1 2' not in text or 'PROJECT_VERSION_MINOR_1 12' not in text:
            raise SystemExit(
                f"ESP-Hosted-Slave-Version ist nicht {EXPECTED_VERSION}: {version}. "
                "Kein nicht kompatibles C6-Image wird gebaut."
            )
    kconfig = project / "main" / "Kconfig.projbuild"
    if kconfig.is_file():
        text = kconfig.read_text(encoding="utf-8")
        text = text.replace('\n\t\t\t\t\tdepends on $(ESP_IDF_VERSION) >= "6.1"', "")
        kconfig.write_text(text, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="ESP-Hosted ESP32-C6 SDIO Slave bauen")
    parser.add_argument("--flash", metavar="PORT", help="C6-Programmierport, z.B. COM8")
    parser.add_argument("--monitor", action="store_true", help="Nach Flash/Build Monitor starten")
    args = parser.parse_args()

    idf_py = idf()
    project = ensure_project(idf_py)
    write_defaults(project)
    run(idf_py + ["set-target", "esp32c6"], project)
    build_dir = project / "build"
    ninja = shutil.which("ninja") or "ninja"
    toolchain = Path(os.environ.get("ESP_TOOLCHAIN_PATH", ""))
    if toolchain.is_dir():
        os.environ["PATH"] = str(toolchain) + os.pathsep + os.environ.get("PATH", "")
    run([ninja, "all"], build_dir)
    if args.flash:
        run(idf_py + ["-p", args.flash, "flash"], project)
        if args.monitor:
            run(idf_py + ["-p", args.flash, "monitor"], project)
    print(f"C6-Image bereit unter: {project / 'build'}")
    print("Wichtig: C6 separat flashen; nicht in die P4 firmware.bin einbetten.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
