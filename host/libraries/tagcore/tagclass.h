
#ifndef TAG_H
#define TAG_H
#include <mutex>
#include <string>

using namespace std;

#include "linkadapt.h"
#include "tagmonitor.h"

/**
 * @struct  TagError
 * @brief   Why the most recent monitor RPC made through a Tag failed.
 *
 * @details Separates a link or monitor failure, where the request may never
 *          have reached the firmware, from a refusal by the firmware, which
 *          arrives as an Ack error code with an optional message.
 */
struct TagError
{
  bool transport_failed = false;    ///< The RPC did not complete over the link.
  Ack::Err err = Ack::UNSPECIFIED;  ///< The firmware's Ack code; UNSPECIFIED after a transport failure.
  std::string message;              ///< Human-readable reason; empty when the call succeeded.
};

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
  void ResetLinkStats();
  LinkAdaptStats GetLinkStats();
  void ResetMonitorStats();
  TagMonitorStats GetMonitorStats();
  bool Voltage(float &voltage);
  bool GitSha(std::string &str);

  // Tag information

  bool GetTagInfo(TagInfo &info);
  bool GetConfig(Config &cfg);
  bool GetStatus(Status &status);
  // Field-rescue helpers used by tag-dwnld --rescue-exception.
  bool ForceBackupState(TagState state);
  bool CountBitTagLogHeaders(int &count);
  bool GetBitTagLogFromFlash(Ack &data_log, int index);

  /**
   * @brief   Reads live tag memory or peripheral space between monitor RPCs.
   * @details Thin wrapper over TagMonitor::ReadMem(); see it for alignment,
   *          chunking and the attached-session requirement.
   * @param[in]  addr    Tag address, a multiple of 4.
   * @param[out] buf     Receives @p length bytes.
   * @param[in]  length  Byte count, a multiple of 4.
   * @return  MemAccess::Ok, or why the read was refused or failed.
   */
  MemAccess ReadMemory(uint32_t addr, uint8_t *buf, size_t length);

  /**
   * @brief   Writes live tag memory or peripheral space between monitor RPCs.
   * @details Thin wrapper over TagMonitor::WriteMem(); on STM32U3 a write
   *          overlapping the monitor_shared block needs @p force.
   * @param[in] addr    Tag address, a multiple of 4.
   * @param[in] data    @p length bytes to write.
   * @param[in] length  Byte count, a multiple of 4.
   * @param[in] force   Allow a write over the U3 monitor_shared block.
   * @return  MemAccess::Ok, or why the write was refused or failed.
   * @warning Unchecked beyond the monitor_shared guard; see TagMonitor::WriteMem().
   */
  MemAccess WriteMemory(uint32_t addr, const uint8_t *data, size_t length,
                        bool force = false);

  /**
   * @brief   Keeps the monitor session alive while the caller is idle.
   * @details See TagMonitor::KeepAlive(). Call at least every 2-3 s for an
   *          STM32U3 tag when nothing else is talking to it.
   * @return  true if the keepalive reached the tag.
   */
  bool KeepAlive();

  /**
   * @brief   Sends an arbitrary monitor request and returns the reply as is.
   * @details For callers that forward the monitor protocol, such as
   *          tag-server's raw Call, so that a request without a dedicated
   *          method is still usable. The reply's Ack code is not checked.
   * @param[in]  request  The request to send.
   * @param[out] reply    The firmware's Ack, whatever its code. Cleared on a
   *                      transport failure.
   * @return  true if the RPC completed over the link; check @p reply.err().
   *          LastError() describes a failure.
   */
  bool Request(const Req &request, Ack &reply);

  /**
   * @brief   Describes the outcome of the most recent monitor RPC.
   * @details Covers the calls that send a request to the firmware (GetStatus,
   *          Start, GetDataLog and so on). Memory access, KeepAlive() and the
   *          field-rescue helpers do not change it.
   * @return  The transport flag, the Ack code and a message; the message is
   *          empty if the last RPC succeeded.
   */
  TagError LastError();

  // Tag control

  bool Erase();
  bool Start(Config &cfg);
  bool Stop();
  bool Calibrate();

  bool SetRtc();
  bool Test(TestReq test);

  // Logs
  // next defines next log entry to fetch, returns the number of entries fetched
  // for data logs, copy the ack structure
 
  int GetStateLog(StateLog &state_log, int index);
  bool GetDataLog(Ack &data_log, int index);
  bool GetCalibrationLog(Ack &calibration_log);

  // Calibration Constants
  
  bool ReadCalibration(CalibrationConstants &constants, uint32_t index);
  /**
   * @brief   Stores a set of calibration constants in the tag's flash log.
   * @param[in] constants  The constants to append; the firmware requires
   *                       the magnetometer block.
   * @return  true only if the RPC completed and the firmware answered OK.
   *          A refusal (INVAL without magnetometer constants, PERM when the
   *          tag is not IDLE or its firmware has no calibration store) or a
   *          transport failure returns false; LastError() and DebugMessage()
   *          say which.
   */
  bool WriteCalibration(CalibrationConstants &constants);

  std::string DebugMessage();


private:
  /// Sends @c req, fills @c ack and records whether the transport failed.
  bool Rpc();

  mutex mtx;
  Ack ack;
  Req req;
  bool rpc_failed = false;  ///< The last Rpc() did not complete over the link.
  TagMonitor monitor;
};

#endif
