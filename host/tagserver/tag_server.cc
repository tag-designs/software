/**
 * @file    tag_server.cc
 * @brief   TagService handlers and the mapping of tagcore failures to gRPC.
 *
 * @details Each handler checks attachment (and, for debug calls, the debug
 *          switch), calls one Tag method, and returns its reply or a status
 *          built from Tag::LastError() or the MemAccess result. The status
 *          codes are listed in proto/tag_service.proto.
 */
#include "tag_server.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <tagclass.h>

#include "google/rpc/status.pb.h"
#include "tag_service.pb.h"

namespace svc = tagrpc::TagService;
using code = grpcxx::status::code_t;

namespace
{

/// Largest single ReadMemory/WriteMemory the server accepts, in bytes.
constexpr uint32_t kMaxMemTransfer = 16U * 1024U * 1024U;

/// Standard base64; grpcxx sends header values as given, and gRPC carries
/// -bin headers base64-encoded.
std::string base64(std::string_view in)
{
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(4 * ((in.size() + 2) / 3));
  uint32_t bits = 0;
  int count = -6;
  for (unsigned char c : in)
  {
    bits = (bits << 8) | c;
    count += 8;
    while (count >= 0)
    {
      out.push_back(alphabet[(bits >> count) & 0x3F]);
      count -= 6;
    }
  }
  if (count > -6)
    out.push_back(alphabet[((bits << 8) >> (count + 8)) & 0x3F]);
  while (out.size() % 4)
    out.push_back('=');
  return out;
}

/**
 * @brief   A failed-call status: the code, plus a google.rpc.Status holding
 *          @p message and a tagrpc.TagError in grpc-status-details-bin.
 */
grpcxx::status error_status(code c, const std::string &message,
                            Ack::Err err = Ack::UNSPECIFIED,
                            bool transport_failed = false)
{
  tagrpc::TagError detail;
  detail.set_err(err);
  detail.set_transport_failed(transport_failed);

  google::rpc::Status status;
  status.set_code(static_cast<int>(c));
  status.set_message(message);
  status.add_details()->PackFrom(detail);
  return grpcxx::status(c, base64(status.SerializeAsString()));
}

template <typename Rpc>
typename Rpc::result_type fail(grpcxx::status status)
{
  return {std::move(status), std::nullopt};
}

template <typename Rpc>
typename Rpc::result_type reply(typename Rpc::response_type response)
{
  return {grpcxx::status(code::ok), std::move(response)};
}

template <typename Rpc>
typename Rpc::result_type done()
{
  return reply<Rpc>(typename Rpc::response_type());
}

grpcxx::status not_attached()
{
  return error_status(code::failed_precondition, "not attached");
}

grpcxx::status debug_disabled()
{
  return error_status(code::permission_denied,
                      "debug calls need tag-server --enable-debug");
}

/// Status for a Tag call that returned false, from Tag::LastError().
grpcxx::status from_last_error(Tag &tag, const char *call)
{
  const ::TagError e = tag.LastError();
  const std::string message = std::string(call) + ": " +
                              (e.message.empty() ? "failed" : e.message);
  if (e.transport_failed)
    return error_status(code::unavailable, message, e.err, true);
  switch (e.err)
  {
  case Ack::PERM:
    return error_status(code::failed_precondition, message, e.err);
  case Ack::INVAL:
    return error_status(code::invalid_argument, message, e.err);
  case Ack::NODATA:
    return error_status(code::resource_exhausted, message, e.err);
  case Ack::OK:
    return error_status(code::internal,
                        std::string(call) + ": unexpected reply from the tag", e.err);
  default:
    return error_status(code::internal, message, e.err);
  }
}

grpcxx::status from_mem(MemAccess result, const char *call)
{
  const std::string message = std::string(call) + ": " + MemAccessName(result);
  switch (result)
  {
  case MemAccess::NotAttached:
    return error_status(code::failed_precondition, message);
  case MemAccess::InvalidRange:
    return error_status(code::invalid_argument, message);
  case MemAccess::Protected:
    return error_status(code::permission_denied, message);
  case MemAccess::LinkError:
    return error_status(code::unavailable, message, Ack::UNSPECIFIED, true);
  case MemAccess::Ok:
    break;
  }
  return error_status(code::internal, message);
}

tagrpc::Device to_device(const UsbDev &dev)
{
  tagrpc::Device d;
  d.set_vid(dev.vid);
  d.set_pid(dev.pid);
  d.set_bus(dev.bus);
  d.set_address(dev.address);
  return d;
}

} // namespace

// Attachment ---------------------------------------------------------------

template <>
svc::rpcListDevices::result_type
TagServer::call<svc::rpcListDevices>(grpcxx::context &, const Empty &)
{
  std::vector<UsbDev> devs;
  if (!tag_.Available(devs))
    return fail<svc::rpcListDevices>(
        error_status(code::unavailable, "ListDevices: USB enumeration failed",
                     Ack::UNSPECIFIED, true));
  tagrpc::DeviceList list;
  for (const auto &dev : devs)
    *list.add_devices() = to_device(dev);
  return reply<svc::rpcListDevices>(std::move(list));
}

template <>
svc::rpcAttach::result_type
TagServer::call<svc::rpcAttach>(grpcxx::context &, const tagrpc::AttachRequest &request)
{
  using R = svc::rpcAttach;
  if (tag_.IsAttached())
    return fail<R>(error_status(code::failed_precondition, "Attach: already attached"));

  std::vector<UsbDev> devs;
  if (!tag_.Available(devs))
    return fail<R>(error_status(code::unavailable, "Attach: USB enumeration failed",
                                Ack::UNSPECIFIED, true));

  std::optional<UsbDev> chosen;
  if (request.bus() == 0 && request.address() == 0)
  {
    if (devs.empty())
      return fail<R>(error_status(code::not_found, "Attach: no tag base found"));
    if (devs.size() > 1)
      return fail<R>(error_status(code::failed_precondition,
                                  "Attach: several bases found; give bus and address"));
    chosen = devs.front();
  }
  else
  {
    for (const auto &dev : devs)
      if (dev.bus == request.bus() && dev.address == request.address())
        chosen = dev;
    if (!chosen)
      return fail<R>(error_status(code::not_found, "Attach: no base at bus " +
                                                       std::to_string(request.bus()) +
                                                       " address " +
                                                       std::to_string(request.address())));
  }

  if (!tag_.Attach(*chosen))
    return fail<R>(error_status(code::unavailable, "Attach: attach failed",
                                Ack::UNSPECIFIED, true));
  return done<R>();
}

template <>
svc::rpcDetach::result_type
TagServer::call<svc::rpcDetach>(grpcxx::context &, const Empty &)
{
  tag_.Detach();
  return done<svc::rpcDetach>();
}

template <>
svc::rpcIsAttached::result_type
TagServer::call<svc::rpcIsAttached>(grpcxx::context &, const Empty &)
{
  tagrpc::BoolReply r;
  r.set_value(tag_.IsAttached());
  return reply<svc::rpcIsAttached>(std::move(r));
}

// Information --------------------------------------------------------------

template <>
svc::rpcGetInfo::result_type
TagServer::call<svc::rpcGetInfo>(grpcxx::context &, const Empty &)
{
  using R = svc::rpcGetInfo;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  TagInfo info;
  if (!tag_.GetTagInfo(info))
    return fail<R>(from_last_error(tag_, "GetInfo"));
  return reply<R>(std::move(info));
}

template <>
svc::rpcGetConfig::result_type
TagServer::call<svc::rpcGetConfig>(grpcxx::context &, const Empty &)
{
  using R = svc::rpcGetConfig;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  Config config;
  if (!tag_.GetConfig(config))
    return fail<R>(from_last_error(tag_, "GetConfig"));
  return reply<R>(std::move(config));
}

template <>
svc::rpcGetStatus::result_type
TagServer::call<svc::rpcGetStatus>(grpcxx::context &, const Empty &)
{
  using R = svc::rpcGetStatus;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  Status status;
  if (!tag_.GetStatus(status))
    return fail<R>(from_last_error(tag_, "GetStatus"));
  return reply<R>(std::move(status));
}

template <>
svc::rpcVoltage::result_type
TagServer::call<svc::rpcVoltage>(grpcxx::context &, const Empty &)
{
  using R = svc::rpcVoltage;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  float volts = 0.0f;
  if (!tag_.Voltage(volts))
    return fail<R>(error_status(code::unavailable, "Voltage: read failed",
                                Ack::UNSPECIFIED, true));
  tagrpc::VoltageReply r;
  r.set_volts(volts);
  return reply<R>(std::move(r));
}

template <>
svc::rpcGitSha::result_type
TagServer::call<svc::rpcGitSha>(grpcxx::context &, const Empty &)
{
  using R = svc::rpcGitSha;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  std::string sha;
  if (!tag_.GitSha(sha))
    return fail<R>(error_status(code::unavailable, "GitSha: not available"));
  // The monitor's SHA buffer is fixed-size: cut at the first NUL and trim.
  sha = sha.substr(0, sha.find('\0'));
  while (!sha.empty() && std::isspace(static_cast<unsigned char>(sha.back())))
    sha.pop_back();
  tagrpc::GitShaReply r;
  r.set_sha(sha);
  return reply<R>(std::move(r));
}

// Control ------------------------------------------------------------------

template <>
svc::rpcStart::result_type
TagServer::call<svc::rpcStart>(grpcxx::context &, const Config &request)
{
  using R = svc::rpcStart;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  Config config(request);
  if (!tag_.Start(config))
    return fail<R>(from_last_error(tag_, "Start"));
  return done<R>();
}

template <>
svc::rpcStop::result_type
TagServer::call<svc::rpcStop>(grpcxx::context &, const Empty &)
{
  using R = svc::rpcStop;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  if (!tag_.Stop())
    return fail<R>(from_last_error(tag_, "Stop"));
  return done<R>();
}

template <>
svc::rpcErase::result_type
TagServer::call<svc::rpcErase>(grpcxx::context &, const Empty &)
{
  using R = svc::rpcErase;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  if (!tag_.Erase())
    return fail<R>(from_last_error(tag_, "Erase"));
  return done<R>();
}

template <>
svc::rpcCalibrate::result_type
TagServer::call<svc::rpcCalibrate>(grpcxx::context &, const Empty &)
{
  using R = svc::rpcCalibrate;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  if (!tag_.Calibrate())
    return fail<R>(from_last_error(tag_, "Calibrate"));
  return done<R>();
}

template <>
svc::rpcSetRtc::result_type
TagServer::call<svc::rpcSetRtc>(grpcxx::context &, const Empty &)
{
  using R = svc::rpcSetRtc;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  if (!tag_.SetRtc())
    return fail<R>(from_last_error(tag_, "SetRtc"));
  return done<R>();
}

template <>
svc::rpcTest::result_type
TagServer::call<svc::rpcTest>(grpcxx::context &, const tagrpc::TestRequest &request)
{
  using R = svc::rpcTest;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  if (!tag_.Test(request.test()))
    return fail<R>(from_last_error(tag_, "Test"));
  return done<R>();
}

// Logs ---------------------------------------------------------------------

template <>
svc::rpcGetStateLog::result_type
TagServer::call<svc::rpcGetStateLog>(grpcxx::context &, const tagrpc::LogIndex &request)
{
  using R = svc::rpcGetStateLog;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  StateLog log;
  if (tag_.GetStateLog(log, request.index()) > 0)
    return reply<R>(std::move(log));
  // Zero entries is either the end of the log or a failure.
  const ::TagError e = tag_.LastError();
  if (e.transport_failed || (e.err != Ack::OK && e.err != Ack::UNSPECIFIED))
    return fail<R>(from_last_error(tag_, "GetStateLog"));
  return reply<R>(StateLog());
}

template <>
svc::rpcGetDataLog::result_type
TagServer::call<svc::rpcGetDataLog>(grpcxx::context &, const tagrpc::LogIndex &request)
{
  using R = svc::rpcGetDataLog;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  Ack page;
  if (!tag_.GetDataLog(page, request.index()))
    return fail<R>(from_last_error(tag_, "GetDataLog"));
  return reply<R>(std::move(page));
}

template <>
svc::rpcGetCalibrationLog::result_type
TagServer::call<svc::rpcGetCalibrationLog>(grpcxx::context &, const Empty &)
{
  using R = svc::rpcGetCalibrationLog;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  Ack page;
  if (!tag_.GetCalibrationLog(page))
    return fail<R>(from_last_error(tag_, "GetCalibrationLog"));
  return reply<R>(std::move(page));
}

// Calibration constants ----------------------------------------------------

template <>
svc::rpcReadCalibration::result_type
TagServer::call<svc::rpcReadCalibration>(grpcxx::context &,
                                         const tagrpc::CalibrationIndex &request)
{
  using R = svc::rpcReadCalibration;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  CalibrationConstants constants;
  if (tag_.ReadCalibration(constants, request.index()))
    return reply<R>(std::move(constants));
  // tagcore reads past the last entry as false with no error.
  const ::TagError e = tag_.LastError();
  if (!e.transport_failed && e.err == Ack::OK)
    return fail<R>(error_status(code::resource_exhausted,
                                "ReadCalibration: no constants at index " +
                                    std::to_string(request.index()),
                                Ack::OK));
  return fail<R>(from_last_error(tag_, "ReadCalibration"));
}

template <>
svc::rpcWriteCalibration::result_type
TagServer::call<svc::rpcWriteCalibration>(grpcxx::context &,
                                          const CalibrationConstants &request)
{
  using R = svc::rpcWriteCalibration;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  CalibrationConstants constants(request);
  if (!tag_.WriteCalibration(constants))
    return fail<R>(from_last_error(tag_, "WriteCalibration"));
  return done<R>();
}

// Statistics ---------------------------------------------------------------

template <>
svc::rpcGetLinkStats::result_type
TagServer::call<svc::rpcGetLinkStats>(grpcxx::context &, const Empty &)
{
  const LinkAdaptStats s = tag_.GetLinkStats();
  tagrpc::LinkStats r;
  r.set_enabled(s.enabled);
  r.set_usb_in_calls(s.usb_in_calls);
  r.set_usb_out_calls(s.usb_out_calls);
  r.set_usb_in_bytes(s.usb_in_bytes);
  r.set_usb_out_bytes(s.usb_out_bytes);
  r.set_usb_in_ns(s.usb_in_ns);
  r.set_usb_out_ns(s.usb_out_ns);
  r.set_cmd_calls(s.cmd_calls);
  r.set_cmd_ns(s.cmd_ns);
  r.set_read_mem_calls(s.read_mem_calls);
  r.set_read_mem_bytes(s.read_mem_bytes);
  r.set_read_mem_ns(s.read_mem_ns);
  r.set_write_mem_calls(s.write_mem_calls);
  r.set_write_mem_bytes(s.write_mem_bytes);
  r.set_write_mem_ns(s.write_mem_ns);
  r.set_read_debug_calls(s.read_debug_calls);
  r.set_read_debug_ns(s.read_debug_ns);
  r.set_write_debug_calls(s.write_debug_calls);
  r.set_write_debug_ns(s.write_debug_ns);
  r.set_rw_status_calls(s.rw_status_calls);
  r.set_rw_status_ns(s.rw_status_ns);
  return reply<svc::rpcGetLinkStats>(std::move(r));
}

template <>
svc::rpcResetLinkStats::result_type
TagServer::call<svc::rpcResetLinkStats>(grpcxx::context &, const Empty &)
{
  tag_.ResetLinkStats();
  return done<svc::rpcResetLinkStats>();
}

template <>
svc::rpcGetMonitorStats::result_type
TagServer::call<svc::rpcGetMonitorStats>(grpcxx::context &, const Empty &)
{
  const TagMonitorStats s = tag_.GetMonitorStats();
  tagrpc::MonitorStats r;
  r.set_enabled(s.enabled);
  r.set_rpc_calls(s.rpc_calls);
  r.set_request_bytes(s.request_bytes);
  r.set_response_bytes(s.response_bytes);
  r.set_rpc_total_ns(s.rpc_total_ns);
  r.set_serialize_ns(s.serialize_ns);
  r.set_write_request_ns(s.write_request_ns);
  r.set_monitor_call_ns(s.monitor_call_ns);
  r.set_read_response_ns(s.read_response_ns);
  r.set_parse_ns(s.parse_ns);
  return reply<svc::rpcGetMonitorStats>(std::move(r));
}

template <>
svc::rpcResetMonitorStats::result_type
TagServer::call<svc::rpcResetMonitorStats>(grpcxx::context &, const Empty &)
{
  tag_.ResetMonitorStats();
  return done<svc::rpcResetMonitorStats>();
}

// Debug --------------------------------------------------------------------

template <>
svc::rpcReadMemory::result_type
TagServer::call<svc::rpcReadMemory>(grpcxx::context &, const tagrpc::MemRead &request)
{
  using R = svc::rpcReadMemory;
  if (!debug_enabled_)
    return fail<R>(debug_disabled());
  if (request.length() > kMaxMemTransfer)
    return fail<R>(error_status(code::invalid_argument, "ReadMemory: length over 16 MiB"));
  std::string data(request.length(), '\0');
  const MemAccess result = tag_.ReadMemory(
      request.address(), reinterpret_cast<uint8_t *>(data.data()), data.size());
  if (result != MemAccess::Ok)
    return fail<R>(from_mem(result, "ReadMemory"));
  tagrpc::MemData r;
  r.set_data(std::move(data));
  return reply<R>(std::move(r));
}

template <>
svc::rpcWriteMemory::result_type
TagServer::call<svc::rpcWriteMemory>(grpcxx::context &, const tagrpc::MemWrite &request)
{
  using R = svc::rpcWriteMemory;
  if (!debug_enabled_)
    return fail<R>(debug_disabled());
  if (request.data().size() > kMaxMemTransfer)
    return fail<R>(error_status(code::invalid_argument, "WriteMemory: data over 16 MiB"));
  const MemAccess result = tag_.WriteMemory(
      request.address(), reinterpret_cast<const uint8_t *>(request.data().data()),
      request.data().size(), request.force());
  if (result != MemAccess::Ok)
    return fail<R>(from_mem(result, "WriteMemory"));
  return done<R>();
}

template <>
svc::rpcForceBackupState::result_type
TagServer::call<svc::rpcForceBackupState>(grpcxx::context &,
                                          const tagrpc::BackupStateRequest &request)
{
  using R = svc::rpcForceBackupState;
  if (!debug_enabled_)
    return fail<R>(debug_disabled());
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  if (!tag_.ForceBackupState(request.state()))
    return fail<R>(error_status(code::unavailable, "ForceBackupState: failed",
                                Ack::UNSPECIFIED, true));
  return done<R>();
}

template <>
svc::rpcCountBitTagLogHeaders::result_type
TagServer::call<svc::rpcCountBitTagLogHeaders>(grpcxx::context &, const Empty &)
{
  using R = svc::rpcCountBitTagLogHeaders;
  if (!debug_enabled_)
    return fail<R>(debug_disabled());
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  int count = 0;
  if (!tag_.CountBitTagLogHeaders(count))
    return fail<R>(error_status(code::unavailable, "CountBitTagLogHeaders: failed",
                                Ack::UNSPECIFIED, true));
  tagrpc::CountReply r;
  r.set_count(count);
  return reply<R>(std::move(r));
}

template <>
svc::rpcGetBitTagLogFromFlash::result_type
TagServer::call<svc::rpcGetBitTagLogFromFlash>(grpcxx::context &,
                                               const tagrpc::LogIndex &request)
{
  using R = svc::rpcGetBitTagLogFromFlash;
  if (!debug_enabled_)
    return fail<R>(debug_disabled());
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  Ack page;
  if (!tag_.GetBitTagLogFromFlash(page, request.index()))
    return fail<R>(error_status(code::unavailable, "GetBitTagLogFromFlash: failed",
                                Ack::UNSPECIFIED, true));
  return reply<R>(std::move(page));
}

// Raw monitor protocol -----------------------------------------------------

template <>
svc::rpcCall::result_type
TagServer::call<svc::rpcCall>(grpcxx::context &, const Req &request)
{
  using R = svc::rpcCall;
  if (!tag_.IsAttached())
    return fail<R>(not_attached());
  Ack response;
  if (!tag_.Request(request, response))
    return fail<R>(from_last_error(tag_, "Call"));
  return reply<R>(std::move(response));
}
