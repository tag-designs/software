"""Load the tag protocol and TagService modules from the repository's proto/.

The modules are compiled from the .proto files at import time by grpcio-tools
(``grpc.protos_and_services``), so nothing generated is committed and the
client always matches the schema in the checkout. Set TAGCLIENT_PROTO_DIR to
use another proto directory, for example with an installed (non-editable)
copy of this package.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import grpc


def _proto_dir() -> Path:
    env = os.environ.get("TAGCLIENT_PROTO_DIR")
    if env:
        return Path(env)
    # host/python/tagclient/src/tagclient/_protos.py -> repository root
    return Path(__file__).resolve().parents[5] / "proto"


PROTO_DIR = _proto_dir()
if not (PROTO_DIR / "tag_service.proto").is_file():
    raise ImportError(
        f"tag_service.proto not found in {PROTO_DIR}; set TAGCLIENT_PROTO_DIR "
        "to the repository's proto directory")

# The generated modules import each other by top-level name (tag_pb2 imports
# tagdata_pb2), which grpcio-tools resolves through sys.path.
if str(PROTO_DIR) not in sys.path:
    sys.path.append(str(PROTO_DIR))

tag_pb2 = grpc.protos("tag.proto")
tagdata_pb2 = grpc.protos("tagdata.proto")
service_pb2, service_pb2_grpc = grpc.protos_and_services("tag_service.proto")
