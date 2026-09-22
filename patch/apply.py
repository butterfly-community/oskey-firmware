#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Apply OSKey's patches, or remove them before updating the west workspace."""

import argparse
from pathlib import Path
import subprocess

import yaml


PATCHES = {
    "zephyr": (
        "tf-psa-threading.patch",
        "esp32-virtual-efuse.patch",
        "fido/oskey.patch",
        "fido/usb-busy.patch",
        "fido/keepalive.patch",
        "usb-dwc2-zlp-cache.patch",
    ),
    "zephyr-lang-rust": ("rust.patch",),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path, help="west workspace directory")
    parser.add_argument("--reverse", action="store_true", help="remove patches before west update")
    args = parser.parse_args()
    patch_dir = Path(__file__).resolve().parent
    projects = yaml.safe_load((patch_dir.parent / "west.yml").read_text())["manifest"]["projects"]

    # Check both revisions before making any changes.
    for project in projects:
        repo = args.workspace / project.get("path", project["name"])
        head = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
        if head != project["revision"]:
            parser.error(f"{repo}: expected {project['revision']}, found {head}")

    for project in projects:
        repo = args.workspace / project.get("path", project["name"])
        patches = PATCHES[project["name"]]
        for patch in reversed(patches) if args.reverse else patches:
            command = ["git", "-C", str(repo), "apply"]
            direction = ["--reverse"] if args.reverse else []
            opposite = [] if args.reverse else ["--reverse"]
            path = str(patch_dir / patch)
            if subprocess.run(command + opposite + ["--check", path], capture_output=True).returncode == 0:
                print(f"Already {'removed' if args.reverse else 'applied'}: {patch}", flush=True)
                continue
            subprocess.run(command + direction + ["--check", path], check=True)
            subprocess.run(command + direction + [path], check=True)
            print(f"{'Removed' if args.reverse else 'Applied'}: {patch}", flush=True)


if __name__ == "__main__":
    main()
