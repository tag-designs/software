
#ifndef TAG_H
#define TAG_H
using namespace std;
#include <mutex>

#include "tagmonitor.h"

class Tag 
{
public:
  // Constructor/Destructor

  Tag();
  ~Tag();

  // Disable copying

  Tag(const Tag &) = delete;
  void operator=(const Tag &) = delete;

  // Monitor interface

  bool Available(std::vector<UsbDev> &usbdevs);
  bool Attach(UsbDev usbdev = UsbDev());
  void Detach();
  bool IsAttached();
  bool Voltage(float &voltage);
  bool GitSha(std::string &str);

  // Tag information

  bool GetTagInfo(TagInfo &info);
  bool GetConfig(Config &cfg);
  bool GetStatus(Status &status);

  // Tag control

  bool Erase();
  bool Start(Config &cfg);
  bool Stop();

  bool SetRtc();
  bool Test(TestReq test);

  // Logs
  // next defines next log entry to fetch, returns the number of entries fetched
  // for data logs, copy the ack structure

 
  int GetStateLog(StateLog &state_log, int index);
  bool GetDataLog(Ack &data_log, int index);

private:
  mutex mtx;
  Ack ack;
  Req req;
  TagMonitor monitor;
};

#endif
