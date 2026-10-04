---
type: readme
status: current
summary: The protobuf schema shared by host tools and tag firmware, how each side compiles it, and which tool turns a tag's default-config JSON into C.
---

# Proto Directory

`tag.proto` and `tagdata.proto` define the messages host tools and tag firmware
exchange over the monitor: configuration, status, and downloaded data logs.
They are one of the [shared contracts](../docs/shared/README.md); a change here
changes host code, the firmware's nanopb bindings, the default-config JSON, and
data already stored.

```text
proto/
├── CMakeLists.txt         host C++ library (target `proto`), and the
│                          `tag-proto-sources` INTERFACE target that hands
│                          the .proto files to the firmware build
├── tag.proto              configuration, status, monitor RPC messages
├── tagdata.proto          data-log messages
├── tagdata-archive.proto  retired log-format messages; built by nothing
└── config-gen.cc          retired C++ config-gen; built by nothing
```

## How each side compiles the schema

- **Host.** `proto/CMakeLists.txt` runs `protobuf_generate_cpp` over
  `tag.proto` and `tagdata.proto` into the `proto` library, linked against
  `protobuf::libprotobuf`. It is built only when `BUILD_HOST` is on; an
  embedded-only build does not look for the C++ protobuf library. The
  `tag-proto-sources` INTERFACE target is created either way.
- **Firmware.** [`embedded/proto-c/`](../embedded/proto-c/) builds one nanopb
  binding per protocol variant (`bittag_proto`, `bittag-ng_proto`,
  `prestag_proto`, `prestagraw_proto`, ...). The shared defaults in
  `embedded/proto-c/default-options/*.options` bound fields and elide
  tag-specific messages; each `<variant>-proto-c/` directory holds override
  options that re-enable the messages and fields that variant uses, and a
  `default-config.json`. How the options are layered and how to add a family
  are in [Embedded Source Layout](../embedded/design/source-layout.md#proto-c).

## Default configuration

Each family's `default-config.json` is the configuration message an
unconfigured tag returns, which host applications read to learn what that tag
can do. **The build converts it with
[`embedded/proto-c/config-gen.py`](../embedded/proto-c/config-gen.py)** into
`default_config.c`, compiled into the firmware. `proto/config-gen.cc` and
`embedded/proto-c/config-gen.cc` are the C++ tool it replaced and are not built
([decision 0013](../docs/decisions/0013-build-config-gen-in-python-with-pinned-protobuf.md)).

For distributed tags the generated `.pb.c`, `.pb.h` and `default_config.c` are
committed under `embedded/proto-c/<variant>-proto-c/generated/`. After changing
a `.proto`, an options file or a `default-config.json`, build normally and
commit what changed there; see
[Tag Firmware Build Reproducibility](../docs/build/firmware-reproducibility.md#changing-a-proto-an-options-file-or-a-boards-customizations).
