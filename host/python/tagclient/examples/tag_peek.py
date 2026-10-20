#!/usr/bin/env python3
"""Python counterpart of tag-peek: read words of live tag memory or
peripheral space without resetting the tag. The server must run with
--enable-debug; addresses must be word aligned."""

from __future__ import annotations

from _common import open_tag, parser, run


def main() -> int:
    p = parser(__doc__)
    p.add_argument("addr", nargs="+", help="address(es), hex (0xE0042000) or decimal")
    args = p.parse_args()

    with open_tag(args) as tag:
        for text in args.addr:
            address = int(text, 0)
            print(f"{address:#010x}: {tag.read_u32(address):#010x}")
    return 0


if __name__ == "__main__":
    run(main)
