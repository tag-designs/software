/**
 * @file    sramcall.h
 * @brief   Download an image into a halted tag's SRAM and call its functions,
 *          with the calling convention STM32CubeProgrammer uses for external
 *          loaders.
 *
 * @details The convention, as traced from CubeProgrammer
 *          (embedded/loaders/design/loader-runtime.md, "The contract"):
 *          - the loadable SRAM segments are written at their link addresses;
 *            segments outside SRAM, such as a loader's StorageInfo descriptor
 *            at address 0, are not downloaded;
 *          - a BKPT return trap sits at the start of SRAM, and LR points at it
 *            with the Thumb bit set, so a function returns into a halt;
 *          - MSP is set about 1 KB past the image;
 *          - arguments go in R0-R3, every other register is zeroed, and the
 *            result comes back in R0.
 *
 *          The transfer buffer follows the stack and runs to the end of the
 *          first SRAM region, so a caller can pass BufferAddress() as a
 *          pointer argument and then read it back over SWD.
 *
 *          The core runs with DHCSR.C_MASKINTS set (SwdSession::Run()), so the
 *          tag's interrupts are not taken even if the image enables them.
 *
 * @see     host/libraries/tagcore/design/swd-recovery.md, step 3
 */

#ifndef TAGCORE_RECOVERY_SRAMCALL_H
#define TAGCORE_RECOVERY_SRAMCALL_H

#include "recovery/swdsession.h"
#include "recovery/targetimage.h"

#include <cstdint>
#include <initializer_list>
#include <string>

namespace tagcore::recovery {

/**
 * @class   SramCall
 * @brief   Calls into an image downloaded to SRAM of a halted tag.
 *
 * @details Download() must succeed before Call(). The session must stay open
 *          and the core halted between calls; Call() leaves it halted. Not
 *          thread-safe.
 *
 * @warning Download() overwrites the start of SRAM1. Capture SRAM first if it
 *          matters.
 */
class SramCall {
public:
  /** @param[in] session  An open session; must outlive this object. */
  explicit SramCall(SwdSession &session) : s_(session) {}

  /**
   * @brief   Write the image's SRAM segments and the return trap.
   *
   * @param[in]  image  Image to download.
   * @param[out] error  Why it failed; may be nullptr.
   * @return  true when every SRAM segment was written and read back
   *          identical, and there is room for a stack and a buffer.
   * @pre     The session is open and the core halted.
   */
  bool Download(const TargetImage &image, std::string *error = nullptr);

  /**
   * @brief   Call the function named @p symbol and wait for it to return.
   *
   * @param[in]  symbol      Function name in the downloaded image.
   * @param[in]  args        Up to four arguments, in R0-R3.
   * @param[out] result      R0 on return.
   * @param[in]  timeout_ms  Longest wait for the return, in milliseconds. On
   *                         timeout the core is halted where it is.
   * @param[out] error       Why it failed; may be nullptr.
   * @return  true when the function returned into the trap. false on a
   *          timeout, an SWD fault, or a halt anywhere else, such as a fault
   *          handler or a breakpoint in the image.
   */
  bool Call(const std::string &symbol, std::initializer_list<uint32_t> args,
            uint32_t &result, int timeout_ms, std::string *error = nullptr);

  /**
   * @brief   Start the function named @p symbol and return at once.
   *
   * @details Sets up the same frame as Call() and lets the core run. For a
   *          function that does not return until told to, such as a loader's
   *          Serve(); finish with WaitReturn().
   *
   * @param[in]  symbol  Function name in the downloaded image.
   * @param[in]  args    Up to four arguments, in R0-R3.
   * @param[out] error   Why it failed; may be nullptr.
   * @return  true when the core is running at the function's entry.
   */
  bool Start(const std::string &symbol, std::initializer_list<uint32_t> args,
             std::string *error = nullptr);

  /**
   * @brief   Wait for a function started by Start() to return.
   *
   * @param[out] result      R0 on return.
   * @param[in]  timeout_ms  Longest wait; on timeout the core is halted.
   * @param[out] error       Why it failed; may be nullptr.
   * @return  true when the function returned into the trap.
   */
  bool WaitReturn(uint32_t &result, int timeout_ms, std::string *error = nullptr);

  /** @brief The downloaded image, or nullptr before Download(). */
  const TargetImage *Image() const { return image_; }

  /** @brief First byte of the transfer buffer; valid after Download(). */
  uint32_t BufferAddress() const { return buffer_; }

  /** @brief Transfer buffer length in bytes, a multiple of 512. */
  uint32_t BufferSize() const { return buffer_size_; }

  /** @brief Initial MSP for each call. */
  uint32_t StackTop() const { return stack_top_; }

private:
  SwdSession &s_;
  const TargetImage *image_ = nullptr;
  uint32_t trap_ = 0;
  uint32_t stack_top_ = 0;
  uint32_t buffer_ = 0;
  uint32_t buffer_size_ = 0;
  std::string running_; ///< Symbol started by Start(), for messages.
};

} // namespace tagcore::recovery

#endif // TAGCORE_RECOVERY_SRAMCALL_H
