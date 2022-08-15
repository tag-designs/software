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

void Tag::Detach()
{
  std::lock_guard<std::mutex> lck(mtx);
  monitor.Detach();
}

bool Tag::GitSha(std::string &str)
{
  return IsAttached() ? monitor.GitShaString(str) : false;
}

// Tag Control

bool Tag::Start(Config &cfg)
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_start(new Config(cfg));
  return monitor.Rpc(req,ack);
}

bool Tag::Stop()
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_stop(new Empty);
  return monitor.Rpc(req,ack);
}

bool Tag::Erase()
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_erase(new Empty);
  return monitor.Rpc(req,ack);
}

bool Tag::Test(TestReq test)
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_test(test);
  return monitor.Rpc(req,ack);
}

bool Tag::GetStatus(Status &status)
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_get_status(new Empty);
  if (monitor.Rpc(req,ack) && (ack.payload_case() == Ack::kStatus))
  {
    status = ack.status();
    return true;
  }
  return false;
}

// Tag Configuration

bool Tag::GetConfig(Config &cfg)
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_get_config(new Empty);
  if (monitor.Rpc(req,ack) && ack.has_config())
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
  return monitor.Rpc(req,ack);
}

// Tag Information

bool Tag::GetTagInfo(TagInfo &info)
{
  std::lock_guard<std::mutex> lck(mtx);
  req.Clear();
  req.set_allocated_get_info(new Empty);
  if (monitor.Rpc(req,ack) && ack.has_info())
  {
    info = std::move(ack.info());
    //std::cerr << "info" << info.DebugString();
    return true;
  }
  //std::cerr << "info failed\n";
  return false;
}

// Tag voltage measured by base

bool Tag::Voltage(float &voltage)
{
  std::lock_guard<std::mutex> lck(mtx);
  return monitor.Voltage(voltage);
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
  if (monitor.Rpc(req,ack) && ack.has_system_log())
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
  if (monitor.Rpc(req,ack)) {
     data_log.CopyFrom(ack);
     return true;
  }
  else 
  {
    return false;
  }
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
