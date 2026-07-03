#!/usr/bin/env python3
"""Select an ST-LINK probe before invoking STM32_Programmer_CLI.

STM32CubeProgrammer accepts an ST-LINK serial number or index in the connection
string, but it does not filter by USB VID:PID itself. This wrapper lets CMake
targets prefer a known USB PID, then invokes the programmer with a stable
`sn=...` selector whenever possible.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import subprocess
import sys
from contextlib import ExitStack
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional, Sequence, Tuple


ST_VID = "0483"
KNOWN_STLINK_PIDS = {
    "3748",
    "374a",
    "374b",
    "374d",
    "374e",
    "374f",
    "3752",
    "3753",
    "3757",
}


@dataclass
class Probe:
    vid: str
    pid: str
    serial: Optional[str] = None
    product: Optional[str] = None
    location: Optional[str] = None
    source: Optional[str] = None

    def summary(self) -> str:
        fields = [f"{self.vid}:{self.pid}"]
        if self.product:
            fields.append(self.product)
        if self.serial:
            fields.append(f"sn={self.serial}")
        if self.location:
            fields.append(self.location)
        return "  ".join(fields)


@dataclass
class Selection:
    kind: str
    value: str
    probe: Optional[Probe] = None

    def connection_arg(self) -> str:
        if self.kind == "serial":
            return f"sn={self.value}"
        if self.kind == "index":
            return f"index={self.value}"
        raise ValueError(f"unsupported selection kind: {self.kind}")


def normalize_hex(value: str, width: int = 4) -> str:
    value = value.strip().lower()
    if value.startswith("0x"):
        value = value[2:]
    match = re.search(r"([0-9][0-9a-f]*)", value)
    if not match:
        return value.zfill(width)
    return match.group(1).zfill(width)


def parse_vid_pid(value: str) -> Tuple[str, str]:
    parts = value.split(":")
    if len(parts) == 1:
        return ST_VID, normalize_hex(parts[0])
    if len(parts) == 2:
        return normalize_hex(parts[0]), normalize_hex(parts[1])
    raise ValueError(f"expected PID or VID:PID, got {value!r}")


def read_text(path: Path) -> Optional[str]:
    try:
        return path.read_text(encoding="utf-8").strip()
    except OSError:
        return None


def enumerate_linux_usb() -> List[Probe]:
    root = Path("/sys/bus/usb/devices")
    probes: List[Probe] = []
    if not root.exists():
        return probes

    for dev in root.iterdir():
        vid = read_text(dev / "idVendor")
        pid = read_text(dev / "idProduct")
        if not vid or not pid:
            continue

        vid = normalize_hex(vid)
        pid = normalize_hex(pid)
        serial = read_text(dev / "serial")
        product = read_text(dev / "product")
        busnum = read_text(dev / "busnum")
        devnum = read_text(dev / "devnum")
        location = None
        if busnum and devnum:
            location = f"usb bus {busnum} device {devnum}"

        probes.append(
            Probe(
                vid=vid,
                pid=pid,
                serial=serial,
                product=product,
                location=location,
                source="sysfs",
            )
        )

    return probes


def walk_system_profiler_items(items: Iterable[dict]) -> Iterable[dict]:
    for item in items:
        yield item
        children = item.get("_items") or item.get("items") or []
        yield from walk_system_profiler_items(children)


def enumerate_macos_usb() -> List[Probe]:
    try:
        completed = subprocess.run(
            ["system_profiler", "SPUSBDataType", "-json"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return []

    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError:
        return []

    probes: List[Probe] = []
    for root in payload.get("SPUSBDataType", []):
        for item in walk_system_profiler_items(root.get("_items", [])):
            vid = item.get("vendor_id")
            pid = item.get("product_id")
            if not vid or not pid:
                continue

            probes.append(
                Probe(
                    vid=normalize_hex(str(vid)),
                    pid=normalize_hex(str(pid)),
                    serial=item.get("serial_num"),
                    product=item.get("_name"),
                    location=item.get("location_id"),
                    source="system_profiler",
                )
            )

    return probes


def enumerate_windows_usb() -> List[Probe]:
    script = r"""
$ErrorActionPreference = 'SilentlyContinue'
Get-CimInstance Win32_PnPEntity |
  Where-Object { $_.PNPDeviceID -match 'VID_[0-9A-Fa-f]{4}.*PID_[0-9A-Fa-f]{4}' } |
  Select-Object Name,PNPDeviceID |
  ConvertTo-Json -Compress
"""
    try:
        completed = subprocess.run(
            [
                "powershell",
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-Command",
                script,
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return []

    payload = completed.stdout.strip()
    if not payload:
        return []

    try:
        rows = json.loads(payload)
    except json.JSONDecodeError:
        return []
    if isinstance(rows, dict):
        rows = [rows]
    if not isinstance(rows, list):
        return []

    probes: List[Probe] = []
    seen = set()
    for row in rows:
        device_id = str(row.get("PNPDeviceID") or "")
        upper = device_id.upper()
        vid_marker = "VID_"
        pid_marker = "PID_"
        vid_pos = upper.find(vid_marker)
        pid_pos = upper.find(pid_marker)
        if vid_pos < 0 or pid_pos < 0:
            continue

        vid = normalize_hex(upper[vid_pos + len(vid_marker) : vid_pos + len(vid_marker) + 4])
        pid = normalize_hex(upper[pid_pos + len(pid_marker) : pid_pos + len(pid_marker) + 4])
        serial = None
        parts = device_id.split("\\")
        if len(parts) >= 3 and "&MI_" not in parts[1].upper():
            serial = parts[-1]

        key = (vid, pid, serial or device_id)
        if key in seen:
            continue
        seen.add(key)

        probes.append(
            Probe(
                vid=vid,
                pid=pid,
                serial=serial,
                product=row.get("Name"),
                location=device_id,
                source="win32-pnp",
            )
        )

    return probes


def enumerate_usb() -> List[Probe]:
    system = platform.system()
    if system == "Linux":
        return enumerate_linux_usb()
    if system == "Darwin":
        return enumerate_macos_usb()
    if system == "Windows":
        return enumerate_windows_usb()
    return []


def stlink_probes(probes: Sequence[Probe]) -> List[Probe]:
    return [p for p in probes if p.vid == ST_VID and p.pid in KNOWN_STLINK_PIDS]


def filter_by_vid_pid(probes: Sequence[Probe], vid: str, pid: str) -> List[Probe]:
    return [p for p in probes if p.vid == vid and p.pid == pid]


def prompt_readline(candidates: Sequence[Probe]) -> str:
    with ExitStack() as stack:
        if os.name == "nt":
            try:
                tty_in = stack.enter_context(open("CONIN$", "r", encoding="utf-8"))
                tty_out = stack.enter_context(open("CONOUT$", "w", encoding="utf-8"))
            except OSError:
                if not sys.stdin.isatty():
                    raise RuntimeError(
                        "multiple matching ST-LINK probes were found, but no "
                        "terminal is available for a prompt"
                    )
                tty_in = sys.stdin
                tty_out = sys.stderr
        else:
            try:
                tty = stack.enter_context(open("/dev/tty", "r+", encoding="utf-8"))
            except OSError as exc:
                raise RuntimeError(
                    "multiple matching ST-LINK probes were found, but no "
                    "terminal is available for a prompt"
                ) from exc
            tty_in = tty
            tty_out = tty

        print("Select an ST-LINK probe:", file=tty_out)
        for i, probe in enumerate(candidates, start=1):
            print(f"  {i}) {probe.summary()}", file=tty_out)
        print(
            "Enter a number, or enter serial:<sn> / index:<n> for "
            "STM32CubeProgrammer:",
            file=tty_out,
        )
        tty_out.flush()
        return tty_in.readline().strip()


def prompt_for_probe(candidates: Sequence[Probe]) -> Selection:
    if not candidates:
        raise RuntimeError("no matching ST-LINK probes were found")

    answer = prompt_readline(candidates)

    if not answer:
        raise RuntimeError("no ST-LINK probe was selected")

    lower = answer.lower()
    if lower.startswith("serial:"):
        serial = answer.split(":", 1)[1].strip()
        if not serial:
            raise RuntimeError("serial selector is empty")
        return Selection("serial", serial)
    if lower.startswith("sn:"):
        serial = answer.split(":", 1)[1].strip()
        if not serial:
            raise RuntimeError("serial selector is empty")
        return Selection("serial", serial)
    if lower.startswith("index:"):
        index = answer.split(":", 1)[1].strip()
        if not index:
            raise RuntimeError("index selector is empty")
        return Selection("index", index)

    try:
        choice = int(answer)
    except ValueError as exc:
        raise RuntimeError(f"unrecognized ST-LINK selection: {answer!r}") from exc

    if choice < 1 or choice > len(candidates):
        raise RuntimeError(f"ST-LINK selection {choice} is out of range")

    probe = candidates[choice - 1]
    if not probe.serial:
        raise RuntimeError(
            "the selected probe does not expose a USB serial number; rerun with "
            "STM32_PROGRAMMER_PROBE=index:<n>"
        )
    return Selection("serial", probe.serial, probe)


def select_probe(selector: str, probes: Sequence[Probe]) -> Selection:
    selector = selector.strip()
    lower = selector.lower()

    if lower.startswith("serial:"):
        value = selector.split(":", 1)[1].strip()
        if not value:
            raise RuntimeError("serial selector is empty")
        return Selection("serial", value)
    if lower.startswith("sn:"):
        value = selector.split(":", 1)[1].strip()
        if not value:
            raise RuntimeError("serial selector is empty")
        return Selection("serial", value)
    if lower.startswith("index:"):
        value = selector.split(":", 1)[1].strip()
        if not value:
            raise RuntimeError("index selector is empty")
        return Selection("index", value)
    if lower.isdigit():
        return Selection("index", lower)

    if lower == "prompt":
        return prompt_for_probe(stlink_probes(probes))

    if lower == "auto":
        candidates = stlink_probes(probes)
    elif lower.startswith("pid:"):
        vid, pid = parse_vid_pid(selector.split(":", 1)[1])
        candidates = filter_by_vid_pid(probes, vid, pid)
    else:
        raise RuntimeError(
            "unsupported STM32_PROGRAMMER_PROBE value. Use auto, prompt, "
            "pid:<pid>, pid:<vid>:<pid>, serial:<sn>, or index:<n>."
        )

    if len(candidates) == 1:
        probe = candidates[0]
        if not probe.serial:
            raise RuntimeError(
                f"matched {probe.summary()}, but it has no USB serial number; "
                "rerun with STM32_PROGRAMMER_PROBE=index:<n>"
            )
        return Selection("serial", probe.serial, probe)

    if len(candidates) > 1:
        return prompt_for_probe(candidates)

    found = stlink_probes(probes)
    if found:
        details = "\n".join(f"  - {probe.summary()}" for probe in found)
        raise RuntimeError(f"no probes matched {selector!r}. Found:\n{details}")
    raise RuntimeError(f"no ST-LINK probes found while resolving {selector!r}")


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Select an ST-LINK probe and invoke STM32_Programmer_CLI."
    )
    parser.add_argument("--programmer", required=True, help="STM32_Programmer_CLI path")
    parser.add_argument(
        "--selector",
        default="pid:0483:3748",
        help=(
            "Probe selector: pid:0483:3748, serial:<sn>, index:<n>, prompt, "
            "or auto. The STM32_PROGRAMMER_PROBE environment variable overrides "
            "this value."
        ),
    )
    parser.add_argument(
        "programmer_args",
        nargs=argparse.REMAINDER,
        help="Arguments passed to STM32_Programmer_CLI after the SWD connection.",
    )
    args = parser.parse_args(argv)

    programmer_args = list(args.programmer_args)
    if programmer_args and programmer_args[0] == "--":
        programmer_args = programmer_args[1:]
    if not programmer_args:
        parser.error("missing STM32_Programmer_CLI operation arguments")

    selector = os.environ.get("STM32_PROGRAMMER_PROBE", args.selector)

    try:
        selection = select_probe(selector, enumerate_usb())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        print(
            "Set STM32_PROGRAMMER_PROBE=serial:<sn> or index:<n>, or configure "
            "with -DSTM32_PROGRAMMER_PROBE=prompt to choose interactively.",
            file=sys.stderr,
        )
        return 2

    command = [
        args.programmer,
        "-c",
        "port=SWD",
        "mode=UR",
        selection.connection_arg(),
        *programmer_args,
    ]
    if selection.probe:
        print(f"Using ST-LINK {selection.probe.summary()}", flush=True)
    else:
        print(f"Using ST-LINK {selection.kind}:{selection.value}", flush=True)
    return subprocess.call(command)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
