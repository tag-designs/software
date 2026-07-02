#ifndef TAGMON_H
#define TAGMON_H

#include "linkadapt.h"
#include "tag.pb.h"

struct TagMonitorStats
{
 bool enabled = TAGCORE_ENABLE_INSTRUMENTATION != 0;
 uint64_t rpc_calls = 0;
 uint64_t request_bytes = 0;
 uint64_t response_bytes = 0;
 uint64_t rpc_total_ns = 0;
 uint64_t serialize_ns = 0;
 uint64_t write_request_ns = 0;
 uint64_t monitor_call_ns = 0;
 uint64_t read_response_ns = 0;
 uint64_t parse_ns = 0;
};

class TagMonitor : public LinkAdapt
{
public:

TagMonitor();
~TagMonitor();
 bool Attach(UsbDev dev = UsbDev());
 void Detach();
 bool GitShaString(std::string &str);
 bool Voltage(float &voltage);
 bool Rpc(Req &req, Ack &ack);
 // Field-rescue helpers used by tag-dwnld --rescue-exception.
 bool ForceBackupState(TagState state);
 bool CountBitTagLogHeaders(int &count);
 bool ReadBitTagLogFromFlash(Ack &ack, int index);
 void ResetMonitorStats();
 TagMonitorStats GetMonitorStats() const;

private:

  size_t MaxPacket() { return maxpacket; }
  char rpcbuf[1024*64];  // communication buffer 

  bool Call(uint8_t operation, int32_t operand, uint32_t *result);
  bool ReadCoreRegister(uint32_t reg, uint32_t *value);
  // The following are gathered from tag monitor.
  // location of call buf in tag
  uint32_t call_buf = 0;
  // maximum tag packet
  size_t maxpacket = 0;
  // tag git sha string
  uint8_t sha_str[48] = {0};
  // tag monitor version
  uint32_t version = 0;
  TagMonitorStats monitor_stats;
};
#endif
