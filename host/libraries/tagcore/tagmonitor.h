#ifndef TAGMON_H
#define TAGMON_H

#include <cstddef>
#include <mutex>

#include "linkadapt.h"
#include "monitor.h"
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

enum class TargetFamily
{
  Unknown,
  STM32L4,
  STM32U3
};

/**
 * @enum    MemAccess
 * @brief   Outcome of a TagMonitor::ReadMem() or TagMonitor::WriteMem() call.
 *
 * @details Distinguishes the caller's mistakes from link failures so that a
 *          caller such as a test server can report each one differently.
 */
enum class MemAccess
{
  Ok,           ///< Every byte was transferred.
  NotAttached,  ///< No monitor session; the debug clocks may be off.
  InvalidRange, ///< Address or length not a multiple of 4, or the range wraps past 0xFFFFFFFF.
  Protected,    ///< A write overlapped the U3 monitor_shared block without @c force.
  LinkError     ///< The ST-LINK transfer failed; part of the range may have been transferred.
};

/**
 * @brief   Short, stable name for a MemAccess value, for logs and messages.
 * @param[in] result  The value to name.
 * @return  A static string such as "ok" or "protected"; never null.
 */
const char *MemAccessName(MemAccess result);

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
 // Field-rescue helpers implemented in legacy_bittag_rescue.cc and used only
 // by tag-dwnld --rescue-exception.
 bool ForceBackupState(TagState state);
 bool CountBitTagLogHeaders(int &count);
 bool ReadBitTagLogFromFlash(Ack &ack, int index);
 void ResetMonitorStats();
 TagMonitorStats GetMonitorStats() const;

 /**
  * @brief   Reads tag memory or peripheral space while the tag runs.
  *
  * @details Uses ST-LINK 32-bit memory-access-port reads, which do not halt
  *          the core, so the firmware and its monitor keep running. The
  *          transfer is split into chunks of at most kMemChunkBytes. It is
  *          serialised with Rpc(), KeepAlive() and the other link calls, so
  *          it always falls between monitor RPCs, never inside one.
  *
  * @param[in]  addr    Tag address; must be a multiple of 4.
  * @param[out] buf     Receives @p length bytes. Unchanged past the failing
  *                     chunk on LinkError.
  * @param[in]  length  Byte count; must be a multiple of 4. Zero is a no-op.
  *
  * @return  MemAccess::Ok, or the reason the read was refused or failed.
  *
  * @pre     Attached. The firmware keeps the debug clocks running only while
  *          a monitor session is active, so reads are refused otherwise.
  * @warning Reading some peripheral registers has side effects, such as
  *          clearing status flags or popping a FIFO.
  */
 MemAccess ReadMem(uint32_t addr, uint8_t *buf, size_t length);

 /**
  * @brief   Writes tag memory or peripheral space while the tag runs.
  *
  * @details Same access path, chunking and serialisation as ReadMem(). On
  *          STM32U3 targets a write that overlaps the monitor_shared block
  *          (MONITOR_SHARED_ADDR, MONITOR_SHARED_SIZE bytes) is refused
  *          unless @p force is set, because changing that block can end the
  *          monitor session. STM32L4 targets have no such block, so nothing
  *          is guarded there.
  *
  * @param[in] addr    Tag address; must be a multiple of 4.
  * @param[in] data    @p length bytes to write.
  * @param[in] length  Byte count; must be a multiple of 4. Zero is a no-op.
  * @param[in] force   Allow a write that overlaps the U3 monitor_shared block.
  *
  * @return  MemAccess::Ok, or the reason the write was refused or failed.
  *          On LinkError, earlier chunks may already have been written.
  *
  * @pre     Attached.
  * @warning Nothing else is checked: a write can corrupt firmware state,
  *          reconfigure peripherals or flash, or reset the part.
  */
 MemAccess WriteMem(uint32_t addr, const uint8_t *data, size_t length,
                    bool force = false);

 /**
  * @brief   Keeps the monitor session alive while the host is otherwise idle.
  *
  * @details The firmware ends a session when the host goes quiet: within one
  *          to two MONITOR_HEARTBEAT_PERIOD_S periods (5-10 s) on STM32U3,
  *          after 60 s without a request on STM32L4. On U3 this sets
  *          monitor_shared.host_activity with one SWD write, which runs no
  *          code on the tag. On L4 it issues a get_status monitor RPC.
  *          Normal traffic also counts as activity, so a caller polling the
  *          tag anyway does not need this.
  *
  * @return  true if the keepalive reached the tag; false if not attached or
  *          the link or RPC failed.
  *
  * @note    Call at least every 2-3 s for U3; L4 tolerates up to 60 s.
  */
 bool KeepAlive();

 /// Largest single ST-LINK transfer ReadMem() and WriteMem() issue.
 static constexpr size_t kMemChunkBytes = 1024;

private:
 /// Serialises every use of the SWD link: Attach, Detach, Rpc, Voltage,
 /// ReadMem, WriteMem, KeepAlive and the field-rescue helpers. Recursive
 /// because Attach() calls Detach() and KeepAlive() calls Rpc() on L4.
 std::recursive_mutex link_mtx;
 bool ReadMemWord(uint32_t addr, uint32_t *value);

  size_t MaxPacket() { return maxpacket; }
  bool DetectTargetFamily();
  bool ReadTargetRccCsr(uint32_t *addr, uint32_t *value);
  bool AttachL4();
  bool AttachU3();
  // Clear a DHCSR.C_MASKINTS another SWD tool left set; see tagmonitor.cc.
  void ClearStaleMaskInts();
  bool CallU3(uint8_t operation, int32_t operand, uint32_t *result);
  bool WriteMemWord(uint32_t addr, uint32_t value);
  bool ReadMonitorShared(monitor_shared_t &shared);
  bool WriteMonitorSharedWord(size_t offset, uint32_t value);
  void LogMonitorShared(const char *prefix, const monitor_shared_t &shared);
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
  TargetFamily target_family = TargetFamily::Unknown;
  uint32_t target_idcode = 0;
  TagMonitorStats monitor_stats;
};
#endif
