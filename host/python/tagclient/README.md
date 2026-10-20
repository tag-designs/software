---
type: readme
status: current
summary: tagclient, the Python client for tag-server - installing it, using TagClient and TagServerError, and running its tests with and without a tag.
---

# tagclient

The Python client for [`tag-server`](../../tagserver/README.md): the tagcore
`Tag` API from Python, taking and returning the protocol's protobuf messages.

```python
from tagclient import TagClient, TagServerError, pb

with TagClient() as tag:            # $TAG_SERVER, or tag-server's default socket
    tag.attach()                    # the only base
    print(tag.get_info().board_desc, tag.git_sha())
    print(pb.tag.TagState.Name(tag.get_status().state))
    for state in tag.state_log():
        print(state)
    try:
        tag.start(pb.tagdata.Config())
    except TagServerError as e:
        print(e.code.name, e.message, e.ack_err_name, e.transport_failed)
```

## Installing

From a checkout, in a virtual environment:

```bash
pip install -e host/python/tagclient[test]
```

It depends only on `grpcio`, `grpcio-tools`, `protobuf` and
`googleapis-common-protos`; no native build. The protocol modules are compiled
from the repository's [`proto/`](../../../proto/) when `tagclient` is first
imported, so nothing generated is committed and the client always matches the
checkout. For a copy installed outside the checkout, set `TAGCLIENT_PROTO_DIR`
to a `proto/` directory.

## Using it

- **`TagClient(target=None, timeout=30.0)`** connects to `target`, a gRPC
  address such as `unix:///tmp/tag-server.sock` or `127.0.0.1:50061`. With no
  target it uses `$TAG_SERVER`, or the server's default socket. As a context
  manager it detaches on exit if it attached.
- **Methods** are the service's calls in snake_case: `attach`, `get_info`,
  `get_status`, `start`, `get_data_log`, `read_memory`, `call` and the rest.
  They take and return messages from `pb.tag`, `pb.tagdata` and `pb.service`.
  Helpers: `state_log()` pages through the state log, and
  `read_u32`/`write_u32` access one little-endian word.
- **`TagServerError`** is raised on any failure. It carries `code` (a
  `grpc.StatusCode`), `message`, `ack_err` (the firmware's `Ack.Err`, or
  `None`, with `ack_err_name`), and `transport_failed`. The codes are listed in
  [`tag_service.proto`](../../../proto/tag_service.proto).
- The memory calls need the server started with `--enable-debug`.
- The client does not page data logs: how the index advances depends on the
  tag's log format (see `tag-dwnld`), so `get_data_log(index)` returns one raw
  page.

## Tests

```bash
TAG_SERVER_BIN=<build-dir>/Release/bin/tag-server pytest host/python/tagclient
```

Each test starts its own `tag-server` on a temporary socket.
[`tests/test_server.py`](tests/test_server.py) needs no tag: it covers errors,
the debug switch, attaching to a missing base, TCP, shutdown and a second
server on a busy socket. [`tests/test_hardware.py`](tests/test_hardware.py)
runs only with `TAG_SERVER_HARDWARE=1` and one base connected. It reads
information, status, the state log and memory, and holds an idle session on
keepalives. It never starts, stops, erases or writes.
