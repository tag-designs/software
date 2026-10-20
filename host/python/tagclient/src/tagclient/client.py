"""TagClient: the tagcore Tag API, through tag-server.

Each method is one TagService call (proto/tag_service.proto), named as the
call in snake_case, taking and returning the protocol's protobuf messages. A
failed call raises TagServerError.
"""

from __future__ import annotations

import os
import sys
from typing import Iterator, Optional

import grpc
from google.rpc import status_pb2

from ._protos import service_pb2 as svc
from ._protos import service_pb2_grpc as svc_grpc
from ._protos import tag_pb2

_DETAILS_KEY = "grpc-status-details-bin"


def default_target() -> str:
    """The server address: $TAG_SERVER, else tag-server's own default."""
    env = os.environ.get("TAG_SERVER")
    if env:
        return env
    if sys.platform == "win32":
        return "127.0.0.1:50061"
    return f"unix:///tmp/tag-server-{os.getuid()}.sock"


class TagServerError(Exception):
    """A TagService call failed.

    Attributes:
        code: the grpc.StatusCode.
        message: why, from the server.
        ack_err: the firmware's Ack.Err value, or None when there was none.
        transport_failed: the request may never have reached the tag.
    """

    def __init__(self, code: grpc.StatusCode, message: str,
                 ack_err: Optional[int] = None, transport_failed: bool = False):
        super().__init__(f"{code.name}: {message}")
        self.code = code
        self.message = message
        self.ack_err = ack_err
        self.transport_failed = transport_failed

    @property
    def ack_err_name(self) -> Optional[str]:
        """The Ack.Err name, such as "PERM", or None."""
        if self.ack_err is None:
            return None
        return tag_pb2.Ack.Err.Name(self.ack_err)

    @classmethod
    def from_rpc_error(cls, error: grpc.RpcError) -> "TagServerError":
        # tag-server sends no grpc-message header (grpcxx does not), so the
        # reason is in the google.rpc.Status in the details trailer, and
        # grpc_status.rpc_status.from_call would reject it.
        code = error.code() if hasattr(error, "code") else grpc.StatusCode.UNKNOWN
        message = (error.details() if hasattr(error, "details") else "") or ""
        ack_err = None
        transport_failed = False
        metadata = dict(error.trailing_metadata() or ()) if hasattr(
            error, "trailing_metadata") else {}
        raw = metadata.get(_DETAILS_KEY)
        if raw:
            status = status_pb2.Status.FromString(raw)
            message = status.message or message
            for detail in status.details:
                tag_error = svc.TagError()
                if detail.Unpack(tag_error):
                    if tag_error.err != tag_pb2.Ack.UNSPECIFIED:
                        ack_err = tag_error.err
                    transport_failed = tag_error.transport_failed
        return cls(code, message or code.name, ack_err, transport_failed)


class TagClient:
    """A connection to tag-server.

    Use as a context manager. If this client attached the tag, leaving the
    block detaches it, so the tag returns to low power.

        with TagClient() as tag:
            tag.attach()
            status = tag.get_status()
    """

    def __init__(self, target: Optional[str] = None, timeout: float = 30.0):
        """
        Args:
            target: a gRPC target such as "unix:///tmp/tag-server.sock" or
                "127.0.0.1:50061"; default_target() when omitted.
            timeout: seconds allowed per call.
        """
        self.target = target or default_target()
        self.timeout = timeout
        self._channel = grpc.insecure_channel(self.target)
        self._stub = svc_grpc.TagServiceStub(self._channel)
        self._attached_here = False

    # Lifetime -------------------------------------------------------------

    def close(self) -> None:
        """Detach if this client attached, then close the channel."""
        if self._attached_here:
            try:
                self.detach()
            except TagServerError:
                pass
        self._channel.close()

    def __enter__(self) -> "TagClient":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def _call(self, method: str, request):
        try:
            return getattr(self._stub, method)(request, timeout=self.timeout)
        except grpc.RpcError as error:
            raise TagServerError.from_rpc_error(error) from None

    # Bases and attachment -------------------------------------------------

    def list_devices(self) -> list:
        """The tag bases the server can see, as tagrpc.Device messages."""
        return list(self._call("ListDevices", tag_pb2.Empty()).devices)

    def attach(self, bus: int = 0, address: int = 0) -> None:
        """Attach to the base at bus:address, or to the only base."""
        self._call("Attach", svc.AttachRequest(bus=bus, address=address))
        self._attached_here = True

    def detach(self) -> None:
        self._call("Detach", tag_pb2.Empty())
        self._attached_here = False

    def is_attached(self) -> bool:
        return self._call("IsAttached", tag_pb2.Empty()).value

    # Information ------------------------------------------------------------

    def get_info(self):
        """The tag's TagInfo."""
        return self._call("GetInfo", tag_pb2.Empty())

    def get_config(self):
        """The tag's current Config."""
        return self._call("GetConfig", tag_pb2.Empty())

    def get_status(self):
        """The tag's current Status."""
        return self._call("GetStatus", tag_pb2.Empty())

    def voltage(self) -> float:
        """The tag's supply voltage, measured by the base, in volts."""
        return self._call("Voltage", tag_pb2.Empty()).volts

    def git_sha(self) -> str:
        """The git SHA of the firmware on the tag."""
        return self._call("GitSha", tag_pb2.Empty()).sha

    # Control ----------------------------------------------------------------

    def start(self, config) -> None:
        """Configure and start the tag; it must be IDLE."""
        self._call("Start", config)

    def stop(self) -> None:
        self._call("Stop", tag_pb2.Empty())

    def erase(self) -> None:
        """Erase the tag's log; it must be FINISHED or ABORTED."""
        self._call("Erase", tag_pb2.Empty())

    def calibrate(self) -> None:
        self._call("Calibrate", tag_pb2.Empty())

    def set_rtc(self) -> None:
        """Set the tag's clock from the server's, at the next whole second."""
        self._call("SetRtc", tag_pb2.Empty())

    def test(self, test: int) -> None:
        """Start a self-test; ``test`` is a TestReq value."""
        self._call("Test", svc.TestRequest(test=test))

    # Logs -------------------------------------------------------------------

    def get_state_log(self, index: int = 0):
        """One page of the state log from ``index``; empty at the end."""
        return self._call("GetStateLog", svc.LogIndex(index=index))

    def state_log(self, max_pages: int = 64) -> Iterator:
        """Every State in the state log, paging as tagcore does."""
        index = 0
        for _ in range(max_pages):
            page = self.get_state_log(index)
            if not page.states:
                return
            yield from page.states
            index += len(page.states)

    def get_data_log(self, index: int = 0):
        """One data-log page as the tag's Ack; its err is NODATA at the end.

        How ``index`` advances depends on the tag's log format; see tag-dwnld.
        """
        return self._call("GetDataLog", svc.LogIndex(index=index))

    def get_calibration_log(self):
        """The calibration samples, as the tag's Ack."""
        return self._call("GetCalibrationLog", tag_pb2.Empty())

    # Calibration constants ------------------------------------------------

    def read_calibration(self, index: int):
        """CalibrationConstants at ``index``; RESOURCE_EXHAUSTED past the end."""
        return self._call("ReadCalibration", svc.CalibrationIndex(index=index))

    def write_calibration(self, constants) -> None:
        self._call("WriteCalibration", constants)

    # Statistics -------------------------------------------------------------

    def get_link_stats(self):
        return self._call("GetLinkStats", tag_pb2.Empty())

    def reset_link_stats(self) -> None:
        self._call("ResetLinkStats", tag_pb2.Empty())

    def get_monitor_stats(self):
        return self._call("GetMonitorStats", tag_pb2.Empty())

    def reset_monitor_stats(self) -> None:
        self._call("ResetMonitorStats", tag_pb2.Empty())

    # Debug (server started with --enable-debug) -------------------------------

    def read_memory(self, address: int, length: int) -> bytes:
        """Read tag memory; ``address`` and ``length`` multiples of 4."""
        return self._call("ReadMemory", svc.MemRead(address=address, length=length)).data

    def write_memory(self, address: int, data: bytes, force: bool = False) -> None:
        """Write tag memory; ``force`` allows the U3 monitor_shared block."""
        self._call("WriteMemory", svc.MemWrite(address=address, data=data, force=force))

    def read_u32(self, address: int) -> int:
        """Read one little-endian 32-bit word."""
        return int.from_bytes(self.read_memory(address, 4), "little")

    def write_u32(self, address: int, value: int, force: bool = False) -> None:
        """Write one little-endian 32-bit word."""
        self.write_memory(address, value.to_bytes(4, "little"), force)

    def force_backup_state(self, state: int) -> None:
        """Field rescue: force the legacy BitTag backup state word."""
        self._call("ForceBackupState", svc.BackupStateRequest(state=state))

    def count_bittag_log_headers(self) -> int:
        return self._call("CountBitTagLogHeaders", tag_pb2.Empty()).count

    def get_bittag_log_from_flash(self, index: int):
        return self._call("GetBitTagLogFromFlash", svc.LogIndex(index=index))

    # Raw monitor protocol -------------------------------------------------

    def call(self, request):
        """Send any Req; returns the Ack as is (check its err)."""
        return self._call("Call", request)
