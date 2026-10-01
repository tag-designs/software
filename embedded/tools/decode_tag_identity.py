#!/usr/bin/env python3
"""Decode the tag identity record from a firmware image or a flash capture.

The record (embedded/tags/common/core/inc/tag_identity.h) sits directly after
the interrupt vectors: 0x1A0 bytes into flash on STM32L432 tags, 0x240 on
STM32U375. This reads it from any of:

  - a raw image or capture whose first byte is flash address 0x08000000:
    the firmware .bin, or internal_flash.bin from a tag-capture directory;
  - a firmware .elf (the record is read from its .tag_identity section);
  - a tag-capture directory (its internal_flash.bin).

  decode_tag_identity.py build/PresTag.bin
  decode_tag_identity.py captures/capture-20261001-120000/
  decode_tag_identity.py --json build/PresTag.elf

Given a capture, it also decodes the session facts stored in the stored
configuration (the RV3028 clock offset recorded at start), when the record
says where they are.

Exit status: 0 decoded, 1 no record found or the record is malformed.
"""

import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile

MAGIC = 0x44494754          # "TGID"
FLASH_BASE = 0x08000000
# Where the vector table ends, per MCU; the record starts there.
OFFSETS = {"STM32L432": 0x1A0, "STM32U375": 0x240}

STRING_IDS = {
    0x0101: "target", 0x0102: "family", 0x0103: "board",
    0x0104: "board_desc", 0x0105: "board_revision", 0x0106: "firmware",
    0x0107: "git_repo", 0x0108: "git_hash", 0x0109: "git_sha",
    0x010A: "git_date", 0x010B: "source_path", 0x010C: "flash_part",
    0x010D: "rtc_part", 0x010E: "loader", 0x010F: "decoder",
    0x0110: "monitor_version",
}
REGION_IDS = {
    0x0301: "image", 0x0302: "persistent", 0x0303: "state_log",
    0x0304: "stored_config", 0x0305: "data_headers", 0x0306: "calibration",
    0x0307: "nand_map", 0x0308: "scratchpad",
}
MAPPINGS = {0: "none", 1: "stride", 2: "block_field", 3: "checkpoint"}
FLAGS = {1: "scratchpad", 2: "scratchpad_ring", 4: "retained_run_diag",
         8: "stored_config_own_page"}


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def decode_entry(eid, value):
    """Return (name, decoded value) for one entry."""
    if eid in STRING_IDS:
        return STRING_IDS[eid], value.split(b"\0", 1)[0].decode("utf-8", "replace")
    if eid in REGION_IDS:
        start, end, rsize, rcount, ver = struct.unpack_from("<5I", value)
        return REGION_IDS[eid], {
            "start": hex(start), "end": hex(end) if end else "persistent end",
            "record_size": rsize,
            "record_count": rcount if rcount else "until erased",
            "layout_version": ver}
    if eid == 0x0201:
        tt, qt, acc, mag, smax, emax = struct.unpack_from("<IfffII", value)
        return "numbers", {"tag_type": tt, "qtmonitor_min_version": round(qt, 4),
                           "accel_constant": acc, "mag_constant": mag,
                           "tag_state_max": smax, "state_event_max": emax}
    if eid == 0x0309:
        addr, len_addr = struct.unpack_from("<2I", value)
        return "default_config", {"address": hex(addr),
                                  "length_address": hex(len_addr)}
    if eid == 0x0401:
        f = struct.unpack_from("<8I", value)
        return "backup_state", {
            "base": hex(f[0]), "word_count": f[1], "valid_magic": hex(f[2]),
            "word_valid": f[3], "word_state": f[4], "word_pages": f[5],
            "word_external_blocks": f[6], "word_reset_cause": f[7]}
    if eid == 0x0402:
        f = struct.unpack_from("<5I", value)
        return "external_flash", {"jedec_id": hex(f[0]), "size": f[1],
                                  "program_page": f[2], "erase_unit": f[3],
                                  "spare": f[4]}
    if eid == 0x0403:
        f = struct.unpack_from("<8I", value)
        return "data_format", {
            "layout_version": f[0], "mapping": MAPPINGS.get(f[1], f[1]),
            "header_size": f[2], "page_bytes": f[3], "samples_per_page": f[4],
            "sample_bytes": f[5], "sample_period_s": f[6], "subsecond_hz": f[7]}
    if eid == 0x0404:
        n = len(value) // 4
        return "scales", [round(x, 9) for x in struct.unpack_from("<%df" % n, value)]
    if eid == 0x0601:
        off, size, ver = struct.unpack_from("<3I", value)
        return "session_facts", {"offset_in_stored_config": off, "size": size,
                                 "version": ver}
    if eid == 0x0501:
        digest, flags = struct.unpack_from("<2I", value)
        return "build", {"options_digest": hex(digest),
                         "flags": [n for b, n in FLAGS.items() if flags & b]}
    return "unknown_0x%04x" % eid, value.hex()


def decode(record):
    """Decode a record that starts at offset 0 of @p record."""
    if len(record) < 8 or u32(record, 0) != MAGIC:
        raise ValueError("no identity record (magic not found)")
    version, size = struct.unpack_from("<HH", record, 4)
    if size > len(record):
        raise ValueError("record claims %d bytes, only %d available" % (size, len(record)))
    out = {"format_version": version, "size": size}
    off = 8
    while off + 4 <= size:
        eid, length = struct.unpack_from("<HH", record, off)
        off += 4
        if eid == 0xFFFF:
            if off != size:
                raise ValueError("end entry at %d but record size is %d" % (off, size))
            return out
        if length % 4 or off + length > size:
            raise ValueError("entry 0x%04x has bad length %d" % (eid, length))
        name, value = decode_entry(eid, record[off:off + length])
        out[name] = value
        off += length
    raise ValueError("record has no end entry")


def stored_session_facts(image, rec):
    """Decode sconfig.session when @p image reaches it (a capture, not a .bin)."""
    loc = rec.get("session_facts")
    if not loc or "stored_config" not in rec:
        return None
    addr = int(rec["stored_config"]["start"], 16) + loc["offset_in_stored_config"]
    off = addr - FLASH_BASE
    if off < 0 or off + 16 > len(image):
        return None
    version, steps, flags, ppm, _ = struct.unpack_from("<IhHfI", image, off)
    if version == 0xFFFFFFFF:
        return "erased (the tag has not been started since its data were cleared)"
    return {"version": version, "rtc_offset_steps": steps,
            "rtc_offset_valid": bool(flags & 1), "rtc_offset_ppm": round(ppm, 6)}


def image_from(path):
    """Return flash bytes starting at FLASH_BASE from a .bin, .elf or capture dir."""
    if os.path.isdir(path):
        path = os.path.join(path, "internal_flash.bin")
    if path.endswith(".elf"):
        with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
            subprocess.run(["arm-none-eabi-objcopy", "-O", "binary",
                            "--only-section=.vectors",
                            "--only-section=.tag_identity", path, tmp.name],
                           check=True)
            return open(tmp.name, "rb").read()
    return open(path, "rb").read()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("path", help=".bin, .elf, internal_flash.bin, or a capture directory")
    ap.add_argument("--mcu", choices=sorted(OFFSETS),
                    help="MCU, if known; otherwise each known offset is tried")
    ap.add_argument("--json", action="store_true", help="print JSON")
    args = ap.parse_args()

    image = image_from(args.path)
    mcus = [args.mcu] if args.mcu else sorted(OFFSETS)
    errors = []
    for mcu in mcus:
        off = OFFSETS[mcu]
        try:
            rec = decode(image[off:])
        except ValueError as e:
            errors.append("%s @0x%08X: %s" % (mcu, FLASH_BASE + off, e))
            continue
        rec = {"mcu": mcu, "address": hex(FLASH_BASE + off), **rec}
        stored = stored_session_facts(image, rec)
        if stored is not None:
            rec["stored_session_facts"] = stored
        if args.json:
            print(json.dumps(rec, indent=2))
        else:
            for k, v in rec.items():
                print("%-16s %s" % (k, v))
        return 0
    for e in errors:
        print(e, file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
