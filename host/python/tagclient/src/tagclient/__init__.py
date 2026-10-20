"""Python client for tag-server, the gRPC test server for tags.

    from tagclient import TagClient, pb

    with TagClient() as tag:
        tag.attach()
        print(tag.get_info().board_desc, pb.tag.TagState.Name(tag.get_status().state))

See host/python/tagclient/README.md.
"""

from types import SimpleNamespace

from ._protos import service_pb2, service_pb2_grpc, tag_pb2, tagdata_pb2
from .client import TagClient, TagServerError, default_target

#: The protocol modules: ``pb.tag`` (tag.proto), ``pb.tagdata``
#: (tagdata.proto) and ``pb.service`` (tag_service.proto).
pb = SimpleNamespace(tag=tag_pb2, tagdata=tagdata_pb2, service=service_pb2)

__all__ = ["TagClient", "TagServerError", "default_target", "pb",
           "service_pb2_grpc"]
