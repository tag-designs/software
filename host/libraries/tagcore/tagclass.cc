#include <iostream>
#include <list>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
//#include <host.pb.h>
#include <tag.pb.h>
#include <tagmonitor.h>

extern "C"
{
#include "monitor.h"
#include "log.h"
}

using namespace google::protobuf;

#include <tagclass.h>

// Constructor/Destructor

Tag::Tag() {}
Tag::~Tag() { Detach(); }

// Attach/Detach

bool Tag::Available(std::vector<UsbDev> &usbdevs)
{
  std::lock_guard<std::mutex> lck(mtx);
  return monitor.Available(usbdevs);
}

bool Tag::Attach(UsbDev usbdev)
{
  std::lock_guard<std::mutex> lck(mtx);
  if (usbdev.vid && usbdev.pid)
  {
    return monitor.Attach(usbdev);
  }
  else
  {
    std::vector<UsbDev> devs;
    monitor.Available(devs);
    if (devs.size())
    {
      return monitor.Attach(devs[0]);
    }
    else
    {
      return false;
    }
  }
}

bool Tag::IsAttached() { return monitor.IsAttached(); }

void Tag::ResetLinkStats()
{
  std::lock_guard<std::mutex> lck(mtx);
  monitor.ResetLinkStats();
}

LinkAdaptStats Tag::GetLinkStats()
{
  std::lock_guard<std::mutex> lck(mtx);
  return monitor.GetLinkStats();
}

void Tag::ResetMonitorStats()
{
  std::lock_guard<std::mutex> lck(mtx);
  monitor.ResetMonitorStats();
}

TagMonitorStats Tag::GetMonitorStats()
{
  std::lock_guard<std::mutex> lck(mtx);
  return monitor.GetMonitorStats();
}

void Tag::Detach()
{
  std::lock_guard<std::mutex> lck(mtx);
  monitor.Detach();
}

bool Tag::GitSha(std::string &str)
{
  std::lock_guard<std::mutex> lck(mtx);
  return IsAttached() ? monitor.GitShaString(str) : false;
}

// Tag Control

bool Tag::Start(Config &cfg)
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_start(new Config(cfg));
  if (!Rpc())
  {
    ack.Clear();
    ack.set_err(Ack::MONITOR);
    ack.set_error_message("Monitor call failed");
    return false;
  }
  return ack.err() == Ack::OK;
}

bool Tag::Stop()
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_stop(new Empty);
  return Rpc() && (ack.err() == Ack::OK);
}

bool Tag::Calibrate()
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_calibrate(new Empty);
  return Rpc() && (ack.err() == Ack::OK);
}
bool Tag::Erase()
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_erase(new Empty);
  return Rpc() && (ack.err() == Ack::OK);
}

bool Tag::Test(TestReq test)
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_test(test);
  return Rpc() && (ack.err() == Ack::OK);
}

std::string Tag::DebugMessage()
{
  std::lock_guard<std::mutex> lck(mtx);
  if (!ack.error_message().empty())
    return ack.error_message();
  if (ack.err() == Ack::UNSPECIFIED)
    return "Tag returned an empty or invalid acknowledgement";
  if (ack.err() != Ack::OK)
    return "Tag returned " + Ack_Err_Name(ack.err());
  return "";
}

bool Tag::GetStatus(Status &status)
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_get_status(new Empty);
  if (Rpc() && (ack.payload_case() == Ack::kStatus))
  {
    status = ack.status();
    return true;
  }
  return false;
}

bool Tag::ForceBackupState(TagState state)
{
  std::lock_guard<std::mutex> lck(mtx);
  return monitor.ForceBackupState(state);
}

bool Tag::CountBitTagLogHeaders(int &count)
{
  std::lock_guard<std::mutex> lck(mtx);
  return monitor.CountBitTagLogHeaders(count);
}

bool Tag::GetBitTagLogFromFlash(Ack &data_log, int index)
{
  std::lock_guard<std::mutex> lck(mtx);
  return monitor.ReadBitTagLogFromFlash(data_log, index);
}

// Tag Configuration

bool Tag::GetConfig(Config &cfg)
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_get_config(new Empty);
  if (Rpc() && ack.has_config())
  {
    cfg = std::move(ack.config());
    return true;
  }
  return false;
}

bool Tag::SetRtc()
{
  using Sec = std::chrono::seconds;
  using MS = std::chrono::milliseconds;
  std::lock_guard<std::mutex> lck(mtx);
  // synchronize request to even seconds
  auto now = std::chrono::system_clock::now();
  auto then = std::chrono::ceil<Sec>(now);
  std::this_thread::sleep_until(then);
  // get the current time and cast to milliseconds
  now = std::chrono::system_clock::now();
  auto ts = std::chrono::time_point_cast<MS>(now);

  req.Clear();
  req.set_set_rtc(ts.time_since_epoch().count());
  if (!Rpc())
  {
    ack.Clear();
    ack.set_err(Ack::MONITOR);
    ack.set_error_message("Monitor call failed while setting RTC");
    return false;
  }
  if (ack.err() == Ack::UNSPECIFIED)
  {
    ack.set_error_message("Tag returned an empty or invalid RTC sync acknowledgement");
    return false;
  }
  return ack.err() == Ack::OK;
}

// Tag Information

bool Tag::GetTagInfo(TagInfo &info)
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_get_info(new Empty);
  if (Rpc() && ack.has_info())
  {
    info = std::move(ack.info());
    //std::cerr << "info" << info.DebugString();
    return true;
  }
  //std::cerr << "info failed\n";
  //std::cerr << ack.err() << "\n";
  return false;
}

// Tag voltage measured by base

bool Tag::Voltage(float &voltage)
{
  std::lock_guard<std::mutex> lck(mtx);
  return monitor.Voltage(voltage);
}

MemAccess Tag::ReadMemory(uint32_t addr, uint8_t *buf, size_t length)
{
  std::lock_guard<std::mutex> lck(mtx);
  return monitor.ReadMem(addr, buf, length);
}

MemAccess Tag::WriteMemory(uint32_t addr, const uint8_t *data, size_t length,
                           bool force)
{
  std::lock_guard<std::mutex> lck(mtx);
  return monitor.WriteMem(addr, data, length, force);
}

bool Tag::KeepAlive()
{
  std::lock_guard<std::mutex> lck(mtx);
  return monitor.KeepAlive();
}

// Caller holds mtx.
bool Tag::Rpc()
{
  rpc_failed = !monitor.Rpc(req, ack);
  return !rpc_failed;
}

bool Tag::Request(const Req &request, Ack &reply)
{
  std::lock_guard<std::mutex> lck(mtx);
  req.CopyFrom(request);
  const bool ok = Rpc();
  reply.CopyFrom(ack);
  return ok;
}

TagError Tag::LastError()
{
  std::lock_guard<std::mutex> lck(mtx);
  TagError error;
  error.transport_failed = rpc_failed;
  error.err = ack.err();
  error.message = ack.error_message();
  if (error.message.empty())
  {
    if (rpc_failed)
      error.message = "Monitor call failed";
    else if (ack.err() != Ack::OK)
      error.message = "Tag returned " + Ack_Err_Name(ack.err());
  }
  return error;
}

/*
 *   The following are for handling various repeated data responses
 */

int Tag::GetStateLog(StateLog &system_log, int index)
{
  std::lock_guard<std::mutex> lck(mtx);
  //system_log.clear();
  LogReq *logreq = new LogReq();
  logreq->set_index(index);
  logreq->set_fmt(LogReq_LogDataType_SYSTEM_LOG);
  req.Clear();
  req.set_allocated_log(logreq);
  //std::cerr << "asking for log data at index " << index << std::endl;
  if (Rpc() && ack.has_system_log())
  {
    system_log.CopyFrom(ack.system_log());// = std::move(ack.system_log());
    return system_log.states().size();
  }
  return 0;
}

bool Tag::GetDataLog(Ack &data_log, int index)
{
  std::lock_guard<std::mutex> lck(mtx);
  LogReq *logreq = new LogReq();
  logreq->set_index(index);
  logreq->set_fmt(LogReq_LogDataType_INTERNAL_DATA);
  req.Clear();
  req.set_allocated_log(logreq);
  if (Rpc()) {
     data_log.CopyFrom(ack);
     return true;
  }
  else 
  {
    return false;
  }
}

bool Tag::GetCalibrationLog(Ack &calibration_log)
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_caldata(new Empty);
  if (Rpc()) {
     calibration_log.CopyFrom(ack);
     return true;
  }
  else 
  {
    return false;
  }
}

bool Tag::ReadCalibration(CalibrationConstants &constants, uint32_t index)
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_read_calibration(index);
  if (Rpc() && ack.has_calibration_constants())
  {
    constants = std::move(ack.calibration_constants());
    return true;
  }
  else 
  {
    return false;
  }
}

bool Tag::WriteCalibration(CalibrationConstants &constants)
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_write_calibration(new CalibrationConstants(constants));
  //log_info("%s",req.DebugString().c_str());
  return Rpc() && (ack.err() == Ack::OK);
}


// instantiate GetLog for bittags
#if 0
template <>
int Tag::GetLog(BitTagLog &data_log, int index)
{
  std::lock_guard<std::mutex> lck(mtx);
  LogReq *logreq = new LogReq();
  logreq->set_index(index);
  logreq->set_fmt(LogReq_LogDataType_INTERNAL_DATA);
  req.Clear();
  req.set_allocated_log(logreq);
  //std::cerr << "asking for log data at index " << index << std::endl;
  if (monitor.Rpc(req,ack) && ack.has_bittag_data_log())
  {
    data_log = std::move(ack.bittag_data_log());
    return data_log.data().size();
  }
  return 0;
}

template <>
int Tag::GetLog(PresTagLog &data_log, int index)
{
std::lock_guard<std::mutex> lck(mtx);
  LogReq *logreq = new LogReq();
  logreq->set_index(index);
  logreq->set_fmt(LogReq_LogDataType_INTERNAL_DATA);
  req.Clear();
  req.set_allocated_log(logreq);
  //std::cerr << "asking for log data at index " << index << std::endl;
  if (monitor.Rpc(req,ack) && ack.has_prestag_data_log())
  {
    data_log = std::move(ack.prestag_data_log());
    return 1;
  }
  return 0;
}

template <>
int Tag::GetLog(GeoTagLog &data_log, int index)
{
std::lock_guard<std::mutex> lck(mtx);
  LogReq *logreq = new LogReq();
  logreq->set_index(index);
  logreq->set_fmt(LogReq_LogDataType_INTERNAL_DATA);
  req.Clear();
  req.set_allocated_log(logreq);
  //std::cerr << "asking for log data at index " << index << std::endl;
  if (monitor.Rpc(req,ack) && ack.has_geotag_data_log())
  {
    data_log = std::move(ack.geotag_data_log());
    return 1;
  }
  return 0;
}

template <>
int Tag::GetLog(LuxTagLog &data_log, int index)
{
std::lock_guard<std::mutex> lck(mtx);
  LogReq *logreq = new LogReq();
  logreq->set_index(index);
  logreq->set_fmt(LogReq_LogDataType_INTERNAL_DATA);
  req.Clear();
  req.set_allocated_log(logreq);
  //std::cerr << "asking for log data at index " << index << std::endl;
  if (monitor.Rpc(req,ack) && ack.has_luxtag_data_log())
  {
    data_log = std::move(ack.luxtag_data_log());
    return 1;
  }
  return 0;
}

template <>
int Tag::GetLog(AccelTagLog &data_log, int index)
{
std::lock_guard<std::mutex> lck(mtx);
  LogReq *logreq = new LogReq();
  logreq->set_index(index);
  logreq->set_fmt(LogReq_LogDataType_INTERNAL_DATA);
  req.Clear();
  req.set_allocated_log(logreq);
  //std::cerr << "asking for log data at index " << index << std::endl;
  if (monitor.Rpc(req,ack) && ack.has_acceltag_data_log())
  {
    data_log = std::move(ack.acceltag_data_log());
    return 1;
  }
  return 0;
}
#endif
