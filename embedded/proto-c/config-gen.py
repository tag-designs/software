#!/usr/bin/env python3
"""Translate a JSON tag configuration into the proto3 wire encoding, emitted as C.

A drop-in replacement for config-gen.cc, with the same positional command line
and byte-identical output:

    config-gen.py --descriptor-set tag.desc json-file [output-file]

The schema comes from a protobuf DESCRIPTOR SET rather than generated Python
modules, and that choice is deliberate. Generated code carries a gencode
version that the protobuf runtime checks and can refuse, so a protoc and a
runtime from different eras are an error waiting to happen -- and the protoc
that is available is rarely the one matching the installed runtime. A
descriptor set is ordinary wire-format data with no such gate: any protoc that
can emit one produces something any modern runtime can read.

That is what lets this depend on `protobuf` alone. protoc comes from whatever
the build already has -- nanopb's binary distribution ships one beside its
generator, which is guaranteed present exactly when regeneration is possible.

Strictness
----------
json_format.Parse delegates to the standard library's json module, which is
strict. config-gen.cc's C++ parser accepted some malformed input -- notably a
trailing comma before a closing brace, which RFC 8259 disallows, and which two
default-config.json files relied on until they were corrected. That leniency
was never chosen; it was a property of whichever libprotobuf happened to be
installed, recorded nowhere. A malformed input file should be fixed.
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

CONFIG_MESSAGE = "Config"


def _descriptor_set_from_protos(proto_dir, protoc):
    """Render a descriptor set from the .proto sources, for standalone use."""
    proto_dir = Path(proto_dir)
    protos = sorted(str(p) for p in proto_dir.glob("*.proto"))
    if not protos:
        raise SystemExit("No .proto files in %s" % proto_dir)
    out = Path(tempfile.mkdtemp(prefix="config-gen-desc-")) / "tag.desc"
    if not protoc:
        import shutil

        protoc = shutil.which("protoc")
        if not protoc:
            raise SystemExit(
                "No protoc available: pass --protoc, or put one on PATH. "
                "nanopb's binary distribution ships one in generator-bin."
            )
    subprocess.run(
        [protoc, "-I%s" % proto_dir, "--include_imports",
         "--descriptor_set_out=%s" % out] + protos,
        check=True,
    )
    return out


def load_config_message(descriptor_set=None, proto_dir=None, protoc=None):
    """Return the Config message class, built from a descriptor set."""
    from google.protobuf import descriptor_pb2, descriptor_pool, message_factory

    if descriptor_set is None:
        if proto_dir is None:
            raise SystemExit("Pass --descriptor-set or --proto-dir.")
        descriptor_set = _descriptor_set_from_protos(proto_dir, protoc)

    file_set = descriptor_pb2.FileDescriptorSet()
    file_set.ParseFromString(Path(descriptor_set).read_bytes())

    pool = descriptor_pool.DescriptorPool()
    # --include_imports can repeat a file; adding one twice is an error.
    added = set()
    for proto_file in file_set.file:
        if proto_file.name not in added:
            pool.Add(proto_file)
            added.add(proto_file.name)

    try:
        descriptor = pool.FindMessageTypeByName(CONFIG_MESSAGE)
    except KeyError:
        raise SystemExit(
            "No message named %s in %s" % (CONFIG_MESSAGE, descriptor_set)
        )
    return message_factory.GetMessageClass(descriptor)


def emit(blob):
    """Format the wire encoding exactly as config-gen.cc does.

    Ten values per line, ", " after every value but the last, three spaces of
    indent. The trailing space before each newline is deliberate: it is what
    the C++ version writes, and the outputs are compared byte for byte.
    """
    n = len(blob)
    parts = ["const unsigned char tag_default_config[%d] = {\n   " % n]
    for i, b in enumerate(blob):
        parts.append(str(b))
        if i + 1 < n:
            parts.append(", ")
            if i % 10 == 9:
                parts.append("\n   ")
    parts.append("\n};\n")
    parts.append("const unsigned int tag_default_config_len = %d;\n" % n)
    return "".join(parts)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Translate a JSON tag configuration into C source."
    )
    parser.add_argument(
        "json_file", nargs="?", help="the default-config.json to translate"
    )
    parser.add_argument(
        "output_file", nargs="?", help="where to write; stdout when omitted"
    )
    parser.add_argument("--descriptor-set", help="protobuf descriptor set holding Config")
    parser.add_argument("--proto-dir", help="directory of .proto sources, for standalone use")
    parser.add_argument("--protoc", help="protoc to use with --proto-dir")
    parser.add_argument(
        "--print-config-only",
        action="store_true",
        help="build the Config message and exit; a self-test for the build, "
        "which uses it to prove the protoc and the protobuf runtime work "
        "together before any generation is attempted",
    )
    args = parser.parse_args(argv)

    if args.print_config_only:
        config_cls = load_config_message(
            args.descriptor_set, args.proto_dir, args.protoc
        )
        print(config_cls.DESCRIPTOR.full_name)
        return 0

    if not args.json_file:
        parser.error("json_file is required unless --print-config-only is given")

    from google.protobuf import json_format

    try:
        text = Path(args.json_file).read_text()
    except OSError:
        print("Couldn't open %s" % args.json_file, file=sys.stderr)
        return 1

    config_cls = load_config_message(
        args.descriptor_set, args.proto_dir, args.protoc
    )
    config = config_cls()
    try:
        json_format.Parse(text, config)
    except json_format.ParseError as err:
        print("Couldn't parse %s: %s" % (args.json_file, err), file=sys.stderr)
        return 1

    rendered = emit(config.SerializeToString())
    if args.output_file:
        Path(args.output_file).write_text(rendered)
    else:
        sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    sys.exit(main())
