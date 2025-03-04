#ifndef TAGMON_H
#define TAGMON_H

#include "linkadapt.h"
#include "tag.pb.h"

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

private:

  size_t MaxPacket() { return maxpacket; }
  char rpcbuf[1024*64];  // communication buffer 

  bool Call(uint8_t operation, int32_t operand, uint32_t *result);
  // The following are gathered from tag monitor.
  // location of call buf in tag
  uint32_t call_buf = 0;
  // maximum tag packet
  size_t maxpacket = 0;
  // tag git sha string
  uint8_t sha_str[48] = {0};
  // tag monitor version
  uint32_t version = 0;
};
#endif