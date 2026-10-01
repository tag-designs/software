/**
 * @file    swdsession.h
 * @brief   Exclusive SWD session that attaches to a tag without booting it.
 *
 * @details A recovery session owns the tag's one SWD connection -- one claimed
 *          USB interface on one base -- for its lifetime. It is an alternative
 *          to a monitor session (TagMonitor), never concurrent with one.
 *
 *          Open() stops the core at its reset vector before the firmware's
 *          first instruction, so what is read afterwards is the tag as it was
 *          left, not as a fresh boot would rewrite it. Close() clears the
 *          debug state the session set -- in particular DEMCR.VC_CORERESET,
 *          which the STM32L4 monitor uses as its "attached" flag and which
 *          would otherwise keep the tag awake -- and ends in a declared way.
 *
 * @see     host/libraries/tagcore/design/swd-recovery.md
 */

#ifndef TAGCORE_RECOVERY_SWDSESSION_H
#define TAGCORE_RECOVERY_SWDSESSION_H

#include "linkadapt.h"
#include "recovery/swdmcu.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace tagcore::recovery {

/**
 * @enum    SwdExit
 * @brief   How Close() leaves the tag.
 */
enum class SwdExit {
  HardwareReset, ///< Disable debug, pulse NRST: the tag boots as after a plain connection.
  LeaveHalted,   ///< Clear vector catch but leave the core halted.
};

/**
 * @struct  SwdAttachInfo
 * @brief   What Open() observed, recorded for the capture manifest.
 */
struct SwdAttachInfo {
  uint32_t idcode = 0;          ///< DBGMCU_IDCODE.
  float voltage = 0.0f;         ///< Target voltage reported by the base, V.
  uint32_t dhcsr_before = 0;    ///< DHCSR before the session changed it.
  uint32_t demcr_before = 0;    ///< DEMCR before; monitor bits here mean a monitor was left attached.
  uint32_t fz_before = 0;       ///< DBGMCU_APB1FZR1 before.
  uint32_t dhcsr_halted = 0;    ///< DHCSR once halted.
  uint32_t pc = 0;              ///< PC once halted.
  uint32_t reset_vector = 0;    ///< Word at flash_base + 4.
  bool halted = false;          ///< The core reached S_HALT after reset release.
  bool at_reset_vector = false; ///< PC equals the reset vector: no firmware ran.
  bool flash_blank = false;     ///< The reset vector is erased; nothing could have run.
};

/// A failed address range: first byte, length.
using AddressRange = std::pair<uint32_t, uint32_t>;

/**
 * @class   SwdSession
 * @brief   Attach-without-boot SWD access for capture and recovery.
 *
 * @details Not thread-safe. Only one session, or one TagMonitor, may hold a
 *          base at a time; a second Open() on a claimed base fails.
 */
class SwdSession : private LinkAdapt {
public:
  SwdSession() = default;

  /** @brief Closes the session with SwdExit::HardwareReset if still open. */
  ~SwdSession();

  SwdSession(const SwdSession &) = delete;
  SwdSession &operator=(const SwdSession &) = delete;

  using LinkAdapt::Available;
  using LinkAdapt::GetLinkStats;
  using LinkAdapt::ResetLinkStats;

  /**
   * @brief   Claim a base and stop the tag's core at its reset vector.
   *
   * @details With NRST asserted: identify the MCU, record DHCSR, DEMCR and the
   *          watchdog freeze register, set C_HALT and DEMCR.VC_CORERESET, then
   *          release NRST. The core halts on the reset vector-catch. PC must
   *          equal the reset vector; otherwise the firmware has run and Open()
   *          fails. On success the independent watchdog is frozen while halted.
   *
   * @param[in] usbdev  Base to use; default selects the first ST-LINK found.
   * @return  true when the core is halted at reset. On false the base is
   *          released and the tag left as a plain connection would leave it.
   *
   * @post    On success, IsOpen(); Mcu() and AttachInfo() are valid.
   * @warning The attach is a system reset. On a part whose option bytes erase
   *          SRAM on reset, that SRAM is erased by this call.
   */
  bool Open(UsbDev usbdev = UsbDev());

  /**
   * @brief   Release the tag and the base.
   *
   * @details Restores the watchdog freeze register, clears DEMCR, then ends as
   *          @p exit says. Safe to call when not open.
   *
   * @param[in] exit  How to leave the tag.
   */
  void Close(SwdExit exit = SwdExit::HardwareReset);

  /** @brief True between a successful Open() and Close(). */
  bool IsOpen() const { return open_; }

  /** @brief The attached part's map; valid while open. */
  const McuMap *Mcu() const { return mcu_; }

  /** @brief What Open() observed. */
  const SwdAttachInfo &AttachInfo() const { return info_; }

  /**
   * @brief   Read one 32-bit word.
   * @return  false on an SWD fault.
   */
  bool ReadWord(uint32_t addr, uint32_t &value);

  /**
   * @brief   Write one 32-bit word.
   * @return  false on an SWD fault.
   */
  bool WriteWord(uint32_t addr, uint32_t value);

  /**
   * @brief   Read a range, continuing past faults.
   *
   * @details Reads in @p chunk-byte transfers. A transfer that faults is
   *          re-read in 512-byte pieces -- the base's own transfer unit -- so a
   *          fault is pinned to a small range; bytes of a faulting piece are
   *          left zero and the piece is reported in @p failed.
   *
   * @param[in]  addr    First byte; 4-byte aligned.
   * @param[out] buf     Destination, @p len bytes.
   * @param[in]  len     Length; a multiple of 4.
   * @param[in]  chunk   Transfer size; a multiple of 512, at most 32768.
   * @param[out] failed  Ranges that could not be read; may be nullptr.
   * @return  true when every byte was read.
   */
  bool Read(uint32_t addr, uint8_t *buf, uint32_t len, uint32_t chunk,
            std::vector<AddressRange> *failed);

  /**
   * @brief   Read a core register of the halted core through DCRSR/DCRDR.
   *
   * @param[in]  reg    Register selector (0-12 R0-R12, 13 SP, 14 LR, 15 PC,
   *                    16 xPSR, 17 MSP, 18 PSP).
   * @param[out] value  Register value.
   * @return  false if the core is not halted or the transfer failed.
   */
  bool ReadCoreRegister(uint32_t reg, uint32_t &value);

  /**
   * @brief   Write a range of target memory.
   *
   * @param[in] addr  First byte; 4-byte aligned.
   * @param[in] buf   Source, @p len bytes.
   * @param[in] len   Length; a multiple of 4.
   * @return  false on the first SWD fault; earlier pieces stay written.
   */
  bool Write(uint32_t addr, const uint8_t *buf, uint32_t len);

  /**
   * @brief   Write a core register of the halted core through DCRSR/DCRDR.
   *
   * @param[in] reg    Register selector, as for ReadCoreRegister().
   * @param[in] value  Value to write.
   * @return  false if the core is not halted or the transfer failed.
   */
  bool WriteCoreRegister(uint32_t reg, uint32_t value);

  /**
   * @brief   Let the halted core run, with interrupts masked by the debugger.
   *
   * @details Sets DHCSR.C_MASKINTS while still halted, as the architecture
   *          requires, then clears C_HALT. The tag's own interrupts are
   *          therefore not taken while code downloaded by the host runs:
   *          one taken then would run tag firmware from internal flash.
   *
   * @return  false on an SWD fault.
   * @pre     The core is halted.
   */
  bool Run();

  /**
   * @brief   Wait for the core to halt.
   *
   * @param[in]  timeout_ms  Longest wait, in milliseconds.
   * @param[out] dhcsr       DHCSR as last read; may be nullptr.
   * @return  true when DHCSR.S_HALT was seen within the timeout.
   */
  bool WaitHalt(int timeout_ms, uint32_t *dhcsr = nullptr);

  /**
   * @brief   Request a halt and wait briefly for it.
   * @return  true when the core is halted.
   */
  bool Halt();

private:
  bool open_ = false;
  const McuMap *mcu_ = nullptr;
  SwdAttachInfo info_;
};

} // namespace tagcore::recovery

#endif
