/**
 * @file    rv3028_probe.c
 * @brief   SRAM-resident probe that reads RV3028 registers without the tag's
 *          firmware. Shared by the RV3028_<Board> probe targets, which differ
 *          only in board.h and PROBE_SWAP_I2C.
 *
 * @details Downloaded and called by the host through SramCall
 *          (tagcore/recovery), with the same convention as an external-flash
 *          loader. It bit-bangs I2C on the board's RTC lines and performs one
 *          register-read transaction: a write of the register pointer, a
 *          repeated start, and a read. It never writes a register, never
 *          issues an EEPROM command, and never sets EERD, so the part is left
 *          exactly as found; the pointer write changes only the address the
 *          read starts from.
 *
 *          On a part that has never run firmware, the RAM mirror of the
 *          configuration EEPROM (0x30-0x37) holds the EEPROM contents, copied
 *          about 66 ms after power-up, so reading 0x36 and 0x37 returns the
 *          factory EEOffset.
 *
 *          Interrupts are disabled on entry and left disabled, as in the
 *          loaders. Every wait is bounded.
 *
 * @see     embedded/loaders/design/loader-runtime.md
 */

#include "loader.h"

/**
 * @def     PROBE_SWAP_I2C
 * @brief   1 when the board's RTC_SDA and RTC_SCL labels are the wrong way
 *          round, as on UIUCTag (whose firmware defines SWAP_I2C).
 */
#ifndef PROBE_SWAP_I2C
#define PROBE_SWAP_I2C 0
#endif

#if PROBE_SWAP_I2C
#define PROBE_SDA LINE_RTC_SCL
#define PROBE_SCL LINE_RTC_SDA
#else
#define PROBE_SDA LINE_RTC_SDA
#define PROBE_SCL LINE_RTC_SCL
#endif

/** @brief RV3028 7-bit I2C address. */
#define RV3028_I2C_ADDR 0x52U

/** @brief Half an SCL period, in microseconds: about 100 kHz. */
#define PROBE_I2C_HALF_US 5U

/** @brief SCL pulses that free a slave holding SDA low mid-byte. */
#define PROBE_I2C_RECOVERY_CLOCKS 9U

/** @name Probe results returned in R0
 * @{
 */
#define PROBE_OK 1                 ///< The registers were read.
#define PROBE_ERR_CLOCK (-1)       ///< The clock switch failed.
#define PROBE_ERR_BUS (-2)         ///< SDA stayed low after recovery.
#define PROBE_ERR_ADDR_W (-3)      ///< No ACK to the address, write phase.
#define PROBE_ERR_REG (-4)         ///< No ACK to the register pointer.
#define PROBE_ERR_ADDR_R (-5)      ///< No ACK to the address, read phase.
#define PROBE_ERR_ARGS (-6)        ///< The range runs past register 0x3F.
/** @} */

/** @brief Release a line: open drain, so high is the pull-up. */
static void release(ioline_t line)
{
  palSetLine(line);
  loaderDelayUs(PROBE_I2C_HALF_US);
}

/** @brief Drive a line low. */
static void pull(ioline_t line)
{
  palClearLine(line);
  loaderDelayUs(PROBE_I2C_HALF_US);
}

/** @brief Clock out one byte, MSB first; true when the slave ACKs. */
static bool sendByte(uint8_t b)
{
  bool ack;

  for (int i = 7; i >= 0; i--) {
    if (b & (1U << i))
      release(PROBE_SDA);
    else
      pull(PROBE_SDA);
    release(PROBE_SCL);
    pull(PROBE_SCL);
  }
  release(PROBE_SDA);
  release(PROBE_SCL);
  ack = palReadLine(PROBE_SDA) == PAL_LOW;
  pull(PROBE_SCL);
  return ack;
}

/** @brief Clock in one byte; ACK it unless it is the last. */
static uint8_t readByte(bool last)
{
  uint8_t b = 0;

  release(PROBE_SDA);
  for (int i = 7; i >= 0; i--) {
    release(PROBE_SCL);
    if (palReadLine(PROBE_SDA) == PAL_HIGH)
      b |= (uint8_t)(1U << i);
    pull(PROBE_SCL);
  }
  if (last)
    release(PROBE_SDA);
  else
    pull(PROBE_SDA);
  release(PROBE_SCL);
  pull(PROBE_SCL);
  release(PROBE_SDA);
  return b;
}

/** @brief START (or repeated START) with SCL ending low. */
static void start(void)
{
  release(PROBE_SDA);
  release(PROBE_SCL);
  pull(PROBE_SDA);
  pull(PROBE_SCL);
}

/** @brief STOP, leaving both lines released. */
static void stop(void)
{
  pull(PROBE_SDA);
  release(PROBE_SCL);
  release(PROBE_SDA);
}

/**
 * @brief   Placeholder for the loader link map's entry symbol.
 * @return  1.
 */
__attribute__((used, noinline, section(".loader_entry"))) int Init(void)
{
  return 1;
}

/**
 * @brief   Read @p count RV3028 registers starting at @p first into @p buf.
 *
 * @param[out] buf    Destination in SRAM, chosen by the host.
 * @param[in]  first  First register, 0x00-0x3F.
 * @param[in]  count  Registers to read; first + count must not exceed 0x40.
 * @return  ::PROBE_OK, or a negative PROBE_ERR_* code.
 */
__attribute__((used, noinline, section(".loader_entry")))
int Rv3028ReadRegs(uint8_t *buf, uint32_t first, uint32_t count)
{
  __disable_irq();
  if (first + count > 0x40U || count == 0U)
    return PROBE_ERR_ARGS;
  if (!loaderClockInit())
    return PROBE_ERR_CLOCK;
  loaderDelayInit();

  rccEnableAHB2(RCC_AHB2ENR_GPIOBEN, false);
  palSetLine(PROBE_SDA);
  palSetLine(PROBE_SCL);
  palSetLineMode(PROBE_SDA, PAL_MODE_OUTPUT_OPENDRAIN | PAL_STM32_PUPDR_PULLUP);
  palSetLineMode(PROBE_SCL, PAL_MODE_OUTPUT_OPENDRAIN | PAL_STM32_PUPDR_PULLUP);
  loaderDelayUs(100U);

  /* A slave left mid-byte holds SDA low; clocking frees it. */
  for (uint32_t i = 0; i < PROBE_I2C_RECOVERY_CLOCKS &&
                       palReadLine(PROBE_SDA) == PAL_LOW; i++) {
    pull(PROBE_SCL);
    release(PROBE_SCL);
  }
  if (palReadLine(PROBE_SDA) == PAL_LOW)
    return PROBE_ERR_BUS;
  stop();

  start();
  if (!sendByte((uint8_t)(RV3028_I2C_ADDR << 1))) {
    stop();
    return PROBE_ERR_ADDR_W;
  }
  if (!sendByte((uint8_t)first)) {
    stop();
    return PROBE_ERR_REG;
  }
  start();
  if (!sendByte((uint8_t)((RV3028_I2C_ADDR << 1) | 1U))) {
    stop();
    return PROBE_ERR_ADDR_R;
  }
  for (uint32_t i = 0; i < count; i++)
    buf[i] = readByte(i + 1U == count);
  stop();
  return PROBE_OK;
}
