#!/usr/bin/env python3
"""Run the existing UART integration client against native_sim over a PTY pair."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("firmware", type=Path)
parser.add_argument("--wallet", action="store_true")
parser.add_argument("--runner", type=Path, default=Path("tests/integration/target/debug/oskey-test"))
args = parser.parse_args()
firmware, runner = args.firmware.resolve(), args.runner.resolve()

with tempfile.TemporaryDirectory(prefix="oskey-nxp-regression-") as directory:
    root = Path(directory)
    processes = []
    with (root / "firmware.log").open("w+") as log:
        try:
            processes.append(subprocess.Popen([
                "socat", f"pty,raw,echo=0,link={root / 'device'}",
                f"pty,raw,echo=0,link={root / 'client'}",
            ], stdout=log, stderr=log))
            deadline = time.monotonic() + 5
            while not (root / "client").exists():
                if time.monotonic() > deadline:
                    raise RuntimeError("PTY setup timed out")
                time.sleep(0.02)
            processes.append(subprocess.Popen([
                str(firmware), "-rt", "-flash_in_ram", "-uart_1_stdinout",
                f"-uart_port={root / 'device'}",
            ], cwd=root, env={**os.environ, "SDL_VIDEODRIVER": "dummy"}, stdout=log, stderr=log))
            time.sleep(1)
            if processes[-1].poll() is not None:
                raise RuntimeError("Firmware exited during startup")
            command = [str(runner), "uart", "--port", str(root / "client")]
            if args.wallet:
                command.append("--wallet")
            subprocess.run(command, check=True, timeout=90)
        except Exception:
            log.seek(0)
            print(log.read())
            raise
        finally:
            for process in reversed(processes):
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
