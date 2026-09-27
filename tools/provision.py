#!/usr/bin/env python3
"""Write device settings into the NVS partition over USB (no captive portal needed).

Reads a key=value file (never committed; see provision.example.env), builds an
NVS partition image with ESP-IDF's nvs_partition_gen and flashes it to the
"nvs" partition (offset 0x9000, size 0x6000 in partitions.csv).

    python tools/provision.py --port /dev/ttyACM0 --env ~/.config/esp32-air-sound/wifi.env

Keys: wifi_ssid, wifi_pass, name, rds_ps, rds_rt, freq (10 kHz units, 7650 = 76.50 MHz),
rds_pi, tx_power (88..115), volume (0..100), preemph50 (1 = 50 us), ota_auto (0/1).
Only the keys present in the file are written; the firmware fills the rest with defaults.
"""

import argparse
import csv
import os
import subprocess
import sys
import tempfile
from pathlib import Path

STR_KEYS = ["wifi_ssid", "wifi_pass", "name", "rds_ps", "rds_rt"]
U8_KEYS = ["tx_power", "volume", "preemph50", "ota_auto"]
U16_KEYS = ["freq", "rds_pi"]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--env", required=True, help="key=value file")
    ap.add_argument("--offset", default="0x9000")
    ap.add_argument("--size", default="0x6000")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    idf = os.environ.get("IDF_PATH")
    if not idf:
        sys.exit("IDF_PATH not set; run . ~/esp/esp-idf/export.sh first")
    values = {}
    for line in Path(args.env).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        values[k.strip()] = v.strip().strip('"')

    with tempfile.TemporaryDirectory() as tmp:
        csv_path = Path(tmp) / "nvs.csv"
        bin_path = Path(tmp) / "nvs.bin"
        with csv_path.open("w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["key", "type", "encoding", "value"])
            w.writerow(["as", "namespace", "", ""])
            for k in STR_KEYS:
                if k in values:
                    w.writerow([k, "data", "string", values[k]])
            for k in U8_KEYS:
                if k in values:
                    w.writerow([k, "data", "u8", str(int(values[k]))])
            for k in U16_KEYS:
                if k in values:
                    w.writerow([k, "data", "u16", str(int(values[k]))])
        gen = Path(idf) / "components" / "nvs_flash" / "nvs_partition_generator" / "nvs_partition_gen.py"
        subprocess.check_call([sys.executable, str(gen), "generate", str(csv_path), str(bin_path), args.size])
        print(f"nvs image built for keys: {', '.join(sorted(values))}")
        if args.dry_run:
            return
        subprocess.check_call(
            [
                "esptool.py",
                "--chip",
                "esp32s3",
                "--port",
                args.port,
                "write_flash",
                args.offset,
                str(bin_path),
            ]
        )
        print("nvs partition written; reset the board")


if __name__ == "__main__":
    main()
