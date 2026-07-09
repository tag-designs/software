/**************************************************************************
*      Copyright 2018  Geoffrey Brown                                     *
*                                                                         *
*                                                                         *
*                                                                         *
* Licensed under the Apache License, Version 2.0 (the "License");         *
* you may not use this file except in compliance with the License.        *
* You may obtain a copy of the License at                                 *
*                                                                         *
*     http://www.apache.org/licenses/LICENSE-2.0                          *
*                                                                         *
* Unless required by applicable law or agreed to in writing, software     *
* distributed under the License is distributed on an "AS IS" BASIS,       *
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*
* See the License for the specific language governing permissions and     *
* limitations under the License.                                          *
**************************************************************************/
// Rewrite to use bitwidths compatible with SPI
// specifically, arrays of bytes, 5-bit words, and 9-bit words

#define UNDEF USEEPRINTF
#include <stdint.h>
#include <dp_swd.h>
#include <debug_cm.h>
#include "hal.h"
#include "usbcfg.h"
#include "app.h"
#include "board.h"
#if defined(USEEPRINTF) && USEEPRINTF
#include "ch.h"
#include "chprintf.h"
#endif

#define REGRETRIES 20
#define DELCNT 2

static const int AUTO_INCREMENT_PAGE_SIZE = 1024;
static const int CSW_VALUE = (CSW_RESERVED | CSW_MSTRDBG | CSW_HPROT | CSW_DBGSTAT | CSW_SADDRINC);
uint32_t CoreID = 0;

static bool isOutput = true;
static int  spiSize = 8;

static void SW_ShiftReset(void);

#define SWCLK_PORT PAL_PORT(LINE_TGT_SWCLK)
#define SWCLK_MASK (1U << PAL_PAD(LINE_TGT_SWCLK))
#define SWDIO_PORT PAL_PORT(LINE_TGT_SWDIO)
#define SWDIO_MASK (1U << PAL_PAD(LINE_TGT_SWDIO))
#define SWDIO_IN_PORT PAL_PORT(LINE_TGT_SWDIO_IN)
#define SWDIO_IN_MASK (1U << PAL_PAD(LINE_TGT_SWDIO_IN))

static inline void gpioSet(stm32_gpio_t *port, uint32_t mask)
{
  pal_lld_setport(port, mask);
}

static inline void gpioClear(stm32_gpio_t *port, uint32_t mask)
{
  pal_lld_clearport(port, mask);
}

static inline void swclkHigh(void)
{
  gpioSet(SWCLK_PORT, SWCLK_MASK);
}

static inline void swclkLow(void)
{
  gpioClear(SWCLK_PORT, SWCLK_MASK);
}

static inline void swdioHigh(void)
{
  gpioSet(SWDIO_PORT, SWDIO_MASK);
}

static inline void swdioLow(void)
{
  gpioClear(SWDIO_PORT, SWDIO_MASK);
}

static inline uint32_t swdioRead(void)
{
  return (SWDIO_IN_PORT->IDR & SWDIO_IN_MASK) != 0;
}

#if defined(__GNUC__)
#define SWD_DO_PRAGMA(x) _Pragma(#x)
#define SWD_PRAGMA(x) SWD_DO_PRAGMA(x)
#define SWD_UNROLL(count) SWD_PRAGMA(GCC unroll count)
#else
#define SWD_UNROLL(count)
#endif

static inline void delay(int i)
{
  for (; i > 0; i--)
  {
    __asm volatile("nop");
  }
}


static void _SetSWPinsIdle(void)
{
  swclkLow();
  swdioLow();
  palClearLine(LINE_SWDIO_DIR);

  isOutput = false;
  spiSize = 8;

  toOutput(LINE_TGT_SWDIO);
  toInput(LINE_TGT_SWDIO_IN);
  toOutput(LINE_TGT_SWCLK);
}

static void _ResetDebugPins(void)
{
  toAnalog(LINE_TGT_SWDIO); //was input
  toAnalog(LINE_TGT_SWDIO_IN);
  toAnalog(LINE_TGT_SWCLK); //was output
  palClearLine(LINE_SWDIO_DIR);
  isOutput = false;
}

static inline void _SetSWDIOasOutput(uint32_t size)
{
  spiSize =  size;
  if (!isOutput){
    palSetLine(LINE_SWDIO_DIR);
    delay(DELCNT);
    toOutput(LINE_TGT_SWDIO);
    delay(DELCNT);
    isOutput = true;
  }
}
static inline void _SetSWDIOasInput(uint32_t size)
{
  spiSize = size;
  if (isOutput){
    toAnalog(LINE_TGT_SWDIO);
    toInput(LINE_TGT_SWDIO_IN);
    palClearLine(LINE_SWDIO_DIR);
    delay(DELCNT);
    isOutput = false;
  }
}

static inline uint32_t SWDIO_IN(void)
{
  uint32_t b = swdioRead();

  swclkHigh();
  delay(DELCNT);
  swclkLow();
  delay(DELCNT);

  return b;
}

static inline uint32_t SW_ShiftIn(uint8_t bits)
{
  int i;
  uint32_t in = 0;

  _SetSWDIOasInput(bits);
  for (i = 0; i < bits; i++)
  {
    //in = (in >> 1) | ((SWDIO_IN() & 1) << (bits - 1));
    in |= ((SWDIO_IN() & 1) << i);
  }
  return in;
}

static inline uint32_t SW_ShiftInBytes(uint8_t bytes)
{
  int i;
  uint32_t tmp = 0;
  _SetSWDIOasInput(8);
  if (bytes > 4)
    return 0;

  for (i = 0; i < bytes; i++)
  {
    ((uint8_t *)&tmp)[i] = SW_ShiftIn(8);
  }
  return tmp;
}

static inline void SW_ShiftOut(uint64_t data, uint8_t bits)
{
  _SetSWDIOasOutput(bits);
  for (int i = 0; i < bits; i++){
    if (data & 1)
      swdioHigh();
    else
      swdioLow();
    swclkHigh();
    delay(DELCNT);
    data = data >> 1;
    swclkLow();
    delay(DELCNT);
  }
}

static inline void SW_ShiftOutBytes(uint32_t data, uint8_t bytes)
{
  int i;
  _SetSWDIOasOutput(8);
  if (bytes > 4)
    return;

  for (i = 0; i < bytes * 8; i++)
  {
    if (data & 1)
      swdioHigh();
    else
      swdioLow();
    swclkHigh();
    delay(DELCNT);
    data = data >> 1;
    swclkLow();
    delay(DELCNT);
  }
}

#define DEFINE_SW_SHIFT_IN_FIXED(name, bit_count)              \
  static inline uint32_t name(void)                            \
  {                                                            \
    uint32_t in = 0;                                           \
    _SetSWDIOasInput(bit_count);                               \
    SWD_UNROLL(bit_count)                                      \
    for (int i = 0; i < bit_count; i++)                        \
    {                                                          \
      in |= (SWDIO_IN() & 1) << i;                             \
    }                                                          \
    return in;                                                 \
  }

#define DEFINE_SW_SHIFT_OUT_FIXED(name, bit_count)             \
  static inline void name(uint32_t data)                       \
  {                                                            \
    _SetSWDIOasOutput(bit_count);                              \
    SWD_UNROLL(bit_count)                                      \
    for (int i = 0; i < bit_count; i++)                        \
    {                                                          \
      if (data & 1)                                            \
        swdioHigh();                                           \
      else                                                     \
        swdioLow();                                            \
      swclkHigh();                                             \
      delay(DELCNT);                                           \
      data = data >> 1;                                        \
      swclkLow();                                              \
      delay(DELCNT);                                           \
    }                                                          \
  }

DEFINE_SW_SHIFT_IN_FIXED(SW_ShiftIn5, 5)
DEFINE_SW_SHIFT_IN_FIXED(SW_ShiftIn9, 9)
DEFINE_SW_SHIFT_IN_FIXED(SW_ShiftIn24, 24)

DEFINE_SW_SHIFT_OUT_FIXED(SW_ShiftOut8, 8)
DEFINE_SW_SHIFT_OUT_FIXED(SW_ShiftOut16, 16)
DEFINE_SW_SHIFT_OUT_FIXED(SW_ShiftOut24, 24)
DEFINE_SW_SHIFT_OUT_FIXED(SW_ShiftOut32, 32)

static const bool ParityTable256[256] = 
{
#   define P2(n) n, n^1, n^1, n
#   define P4(n) P2(n), P2(n^1), P2(n^1), P2(n)
#   define P6(n) P4(n), P4(n^1), P4(n^1), P4(n)
    P6(0), P6(1), P6(1), P6(0)
};


static inline uint32_t Parity(uint32_t x)
{
  /*
  y = x ^ (x >> 1);
  y = y ^ (y >> 2);
  y = y ^ (y >> 4);
  y = y ^ (y >> 8);
  y = y ^ (y >> 16);
  return y & 1;
  */
  x ^= x>>16;
  x ^= x>>8;
  return ParityTable256[x&0xff];
}

static uint32_t SWD_TransactionBB(uint32_t req, uint32_t *data)
{

  uint32_t ack;
  uint32_t pbit;
  SW_ShiftOut8(req);            // Send header
  ack = (SW_ShiftIn(4) >> 1) & 7; // Read turnaround + ACK

  switch (ack)
  {
  case SW_ACK_OK: // good to go
    if (req & SW_REQ_RnW)
    {
      *data = SW_ShiftInBytes(4);
      pbit = SW_ShiftIn(2) & 1; // parity + turnaround
      if (pbit ^ Parity(*data))
      { // parity check
        ack = SW_ACK_PARITY_ERR;
        EPRINTF("parity error req %02x data 0x%x pbit %x\r\n", req, *data, pbit);
      }
    }
    else
    {
      SW_ShiftIn(1); // turnaround
      SW_ShiftOutBytes(*data, 4);
      SW_ShiftOut(Parity(*data), 1);
    }
    break;

    // check out this error recovery
    // may need to finish transaction for overrun

  case SW_ACK_WAIT:
  case SW_ACK_FAULT:
    SW_ShiftIn(1); // turnaround
    break;
  default:
    SW_ShiftInBytes(4);
    SW_ShiftIn(2);
    break;
  }
  SW_ShiftOut8(0); // drive line to idle state
  return ack;
}

static uint32_t SWD_Transaction(uint32_t req, uint32_t *data, uint32_t retry)
{
  uint32_t ack = 0;
  // try transaction  (always at least once)
  do
  {
    ack = SWD_TransactionBB(req, data);
    if ((ack == SW_ACK_WAIT) && retry--)
      continue;
    else
      break;
  } while (retry);
  return ack; // errors handled up call chain
}

static void SW_ShiftReset(void)
{
  SW_ShiftOut32(0xffffffff);
  SW_ShiftOut24(0xffffffff);
}

uint32_t SWD_LineReset(uint32_t *idcode)
{
  uint32_t ack;

  // SWD reset sequence:
  //          56 1's, 8 0's, Read IDcode
  //          SW_IDCODE_RD not allowed to wait or fault
  //_SetSWDIOasOutput();
  SW_ShiftReset();
  SW_ShiftOut8(0);
  ack = SWD_Transaction(SW_IDCODE_RD, idcode, 0);
  return ack;
}

static uint32_t SWD_Connect(uint32_t *idcode)
{
  // Init Pins
  _SetSWPinsIdle();
  // Select SWD Port
  //_SetSWDIOasOutput();
  SW_ShiftReset();
  SW_ShiftOut16(0xE79E);
  // Finish with Line reset
  return SWD_LineReset(idcode);
}

static void SWD_Disconnect(void)
{
  // set pins to idle state
  //_SetSWPinsIdle();
  // select JTAG port
 // _SetSWDIOasOutput();
  SW_ShiftReset();
  SW_ShiftOut16(0xE73C);
  SW_ShiftReset();
  // Release pins (except nReset)
  _ResetDebugPins();
}

/*
 *   Public Debug Port access functions
 *     TRANSACTION macro used to catch errors and
 *     clear error condition.   Passes errors back up
 *     to stlink protocol
 */

#define TRANSACTION(reg, op)                                       \
  do                                                               \
  {                                                                \
    int ack = errorClear(SWD_Transaction(reg, op, MAX_SWD_RETRY),__LINE__); \
    if (ack)                                                       \
    {                                                              \
      EPRINTF("trans fail %d reg %x op %x line %d\r\n", ack, reg, op,__LINE__);     \
      return ack;                                                  \
    }                                                              \
  } while (0)

static uint32_t errorClear(uint32_t ack, int line)
{
  (void)line;
  uint32_t tmp;
  if (ack == SW_ACK_OK)
    return 0;

  // parity error requires no special clearing

  if (ack == SW_ACK_PARITY_ERR)
    return ack;

  // Read CSR if this fails, we need to do a line reset

  if (ack == 7)
  { // nothing came back !
    SWD_LineReset(&tmp);
    return ack;
  }

  EPRINTF("Error clear %d line %d\r\n", ack,line);
  if (SW_ACK_OK != SWD_Transaction(SW_CTRLSTAT_RD, &tmp, 0))
  {
    EPRINTF("Error clear line reset line %d\r\n",line);
    SWD_LineReset(&tmp);
  }

  // clear error/sticky bits in abort reg
  // if this fails, we're truly wedged

  tmp = (STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR);
  if (SW_ACK_OK != SWD_Transaction(SW_ABORT_WR, &tmp, 0))
  {
    EPRINTF("Clear sticky bits failed !\r\n");
    return 2;
  }

  // set CSW to 32-bit, autoinc
  // this better not fail !

  tmp = CSW_VALUE | CSW_SIZE32;
  if (SW_ACK_OK != SWD_Transaction(SW_CSW_WR, &tmp, MAX_SWD_RETRY))
    return 2;
  return 1;
}

static bool shouldRetryMemError(uint32_t err)
{
  /*
   * errorClear() returns 1 after a target WAIT/FAULT was acknowledged and the
   * sticky bits were cleared.  Repeating that transfer just re-faults invalid
   * address probes from host tools, so only retry heavier link-level errors.
   */
  return err && (err != 1);
}

static uint32_t _SWD_writeMem32(uint32_t address, uint32_t *data,
                                uint32_t size)
{
  uint32_t i;
  uint32_t tmp;

  tmp = CSW_VALUE | CSW_SIZE32;
  TRANSACTION(SW_CSW_WR, &tmp);
  // Write TAR register
  TRANSACTION(SW_TAR_WR, &address);
  // Write data
  for (i = 0; i < size / 4; i++)
    TRANSACTION(SW_DRW_WR, data++);
  // dummy read to flush transaction
  TRANSACTION(SW_RDBUFF_RD, &tmp);
  return 0;
}

uint32_t SWD_writeMem32(uint32_t address, uint32_t *data,
                        uint32_t size)
{
  uint32_t len;
  while (size)
  {
    int err;
    len = AUTO_INCREMENT_PAGE_SIZE -
          (address & (AUTO_INCREMENT_PAGE_SIZE - 1));
    if (size < len)
      len = size;
    if ((err = _SWD_writeMem32(address, data, len)) && shouldRetryMemError(err))
      if ((err = _SWD_writeMem32(address, data, len)))
        return err;
    if (err)
      return err;
    address += len;
    data += len / 4;
    size -= len;
  }
  return 0;
}

static uint32_t _SWD_readMem32(uint32_t address, uint32_t *data,
                               uint32_t size)
{
  uint32_t i;
  uint32_t tmp;

  tmp = CSW_VALUE | CSW_SIZE32;
  TRANSACTION(SW_CSW_WR, &tmp);
  // Write TAR register
  TRANSACTION(SW_TAR_WR, &address);
  // Read first word, discard return value
  TRANSACTION(SW_DRW_RD, data);
  // Read data
  for (i = 1; i < size / 4; i++)
    TRANSACTION(SW_DRW_RD, data++);
  // complete the transaction (last data)
  TRANSACTION(SW_RDBUFF_RD, data);
  return 0;
}

uint32_t SWD_readMem32(uint32_t address, uint32_t *data,
                       uint32_t size)
{
  uint32_t len;
  while (size)
  {
    int err;
    len = AUTO_INCREMENT_PAGE_SIZE -
          (address & (AUTO_INCREMENT_PAGE_SIZE - 1));
    if (size < len)
      len = size;
    if ((err = _SWD_readMem32(address, data, len)) && shouldRetryMemError(err))
      if ((err = _SWD_readMem32(address, data, len)))
        return err;
    if (err)
      return err;
    address += len;
    data += len / 4;
    size -= len;
  }
  return 0;
}

// To Do:
//    SWD_writeMem16
//    SWD_readMem16

static uint32_t SWD_writeHalf(uint32_t address, uint16_t data)
{
  uint32_t tmp;
  TRANSACTION(SW_TAR_WR, &address);
  tmp = address & 2 ? (data << 16) : data;
  TRANSACTION(SW_DRW_WR, &tmp);
  return 0;
}

uint32_t SWD_writeMem16(uint32_t address, uint16_t *data, uint32_t size)
{
  uint32_t tmp;
  int err = 0;
  // Write CSW register

  tmp = CSW_VALUE | CSW_SIZE16;
  TRANSACTION(SW_CSW_WR, &tmp);

  for (uint32_t i = 0; i < size / 2; i++)
  {
    if ((err = SWD_writeHalf(address + 2 * i, *data)) && shouldRetryMemError(err))
      if ((err = SWD_writeHalf(address + 2 * i, *data)))
        break;
    if (err)
      break;
    data++;
  }

  // reset CSW
  if (err)
    EPRINTF("SWD_writeMem16 failed\r\n");
  tmp = CSW_VALUE | CSW_SIZE32;
  TRANSACTION(SW_CSW_WR, &tmp);
  return err;
}

static uint32_t SWD_readHalf(uint32_t address, uint16_t *data)
{
  uint32_t tmp;
  TRANSACTION(SW_TAR_WR, &address);
  TRANSACTION(SW_DRW_RD, &tmp);
  TRANSACTION(SW_RDBUFF_RD, &tmp);
  *data = address & 2 ? (tmp >> 16) : tmp;
  return 0;
}

uint32_t SWD_readMem16(uint32_t address, uint16_t *data, uint32_t size)
{
  uint32_t tmp;
  uint32_t i;
  int err = 0;
  // Write CSW register
  tmp = CSW_VALUE | CSW_SIZE16;
  TRANSACTION(SW_CSW_WR, &tmp);
  for (i = 0; i < size / 2; i++){
    if ((err = SWD_readHalf(address + 2 * i, data + i)) && shouldRetryMemError(err))
      if ((err = SWD_readHalf(address + 2 * i, data + i)))
        break;
    if (err)
      break;
  }
  if (err)
    EPRINTF("SWD_readMem8 failed\r\n");
  tmp = CSW_VALUE | CSW_SIZE32;
  TRANSACTION(SW_CSW_WR, &tmp);
  return err;
}

static uint32_t SWD_writeByte(uint32_t address, uint8_t data)
{
  uint32_t tmp;
  TRANSACTION(SW_TAR_WR, &address);
  tmp = (data) << ((address & 3) << 3);
  TRANSACTION(SW_DRW_WR, &tmp);
  return 0;
}

uint32_t SWD_writeMem8(uint32_t address, uint8_t *data, uint32_t size)
{
  uint32_t tmp;
  int err = 0;
  // Write CSW register

  tmp = CSW_VALUE | CSW_SIZE8;
  TRANSACTION(SW_CSW_WR, &tmp);

  for (uint32_t i = 0; i < size; i++)
  {
    if ((err = SWD_writeByte(address + i, *data)) && shouldRetryMemError(err))
      if ((err = SWD_writeByte(address + i, *data)))
        break;
    if (err)
      break;
    data++;
  }

  // reset CSW
  if (err)
    EPRINTF("SWD_writeMem8 failed\r\n");
  tmp = CSW_VALUE | CSW_SIZE32;
  TRANSACTION(SW_CSW_WR, &tmp);
  return err;
}

static uint32_t SWD_readByte(uint32_t address, uint8_t *data)
{
  uint32_t tmp;
  TRANSACTION(SW_TAR_WR, &address);
  TRANSACTION(SW_DRW_RD, &tmp);
  TRANSACTION(SW_RDBUFF_RD, &tmp);
  *data = (tmp >> ((address & 3) << 3));
  return 0;
}

uint32_t SWD_readMem8(uint32_t address, uint8_t *data, uint32_t size)
{
  uint32_t tmp;
  uint32_t i;
  int err = 0;
  // Write CSW register
  tmp = CSW_VALUE | CSW_SIZE8;
  TRANSACTION(SW_CSW_WR, &tmp);
  for (i = 0; i < size; i++){
    if ((err = SWD_readByte(address + i, data + i)) && shouldRetryMemError(err))
      if ((err = SWD_readByte(address + i, data + i)))
        break;
    if (err)
      break;
  }
  if (err)
    EPRINTF("SWD_readMem8 failed\r\n");
  tmp = CSW_VALUE | CSW_SIZE32;
  TRANSACTION(SW_CSW_WR, &tmp);
  return err;
}

int32_t SWD_Close()
{
  uint32_t tmp;
  CoreID = 0;
  // release debug mode
  SWD_writeWord(DBG_HCSR, DBGKEY);
  // release power up
  tmp = 0;
  SWD_Transaction(SW_CTRLSTAT_WR, &tmp, 0);
  SWD_Disconnect();
  return 0;
}

int32_t SWD_Open()
{
  uint32_t tmp;
  int tries;

  EPRINTF("SWD_Open start\r\n");

  //
  if (SWD_Connect(&CoreID) != SW_ACK_OK)
  {
    EPRINTF("SWD_Connect retry\r\n");
    SW_ShiftReset();
    if (SWD_Connect(&CoreID) != SW_ACK_OK)
    {
      EPRINTF("SWD_Connect failed\r\n");
      return 1;
    }
  }
  EPRINTF("SWD CoreID %08x\r\n", CoreID);

  // clear any pending errors
  //   EPRINTF("write clear ok\r\n");
  tmp = (STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR);
  TRANSACTION(SW_ABORT_WR, &tmp);
  // Make sure DP bank select is 0
  tmp = 0;
  TRANSACTION(SW_SELECT_WR, &tmp);
  // Power up !
  //  EPRINTF("write ctrlstat\r\n");
  tmp = CSYSPWRUPREQ | CDBGPWRUPREQ;
  TRANSACTION(SW_CTRLSTAT_WR, &tmp);
  // delay ?
  //  EPRINTF("read ctrlstat\r\n");
  tries = 10;
  while (1)
  {
    TRANSACTION(SW_CTRLSTAT_RD, &tmp);
    // Check power up
    if ((tmp & 0xF0000000) == 0xF0000000)
      break;
    if (tries--)
      chThdSleepMilliseconds(1);
    else
    {
      //  EPRINTF("power up failed 0x%x\r\n", tmp);
      EPRINTF("SWD power up failed %08x\r\n", tmp);
      return 1;
    }
  };
  // make sure everything is normal
  tmp = CSYSPWRUPREQ | CDBGPWRUPREQ | TRNNORMAL | MASKLANE;
  TRANSACTION(SW_CTRLSTAT_WR, &tmp);
  // Read ID register -- set bank register to 0xF, read 0xFC (same command as DRW_RD)
  tmp = 0xF0;
  TRANSACTION(SW_SELECT_WR, &tmp);
  TRANSACTION(SW_IDR_RD, &tmp);    // discard result
  TRANSACTION(SW_RDBUFF_RD, &tmp); // now read the result
  //  EPRINTF("ID register 0x%x\r\n",tmp);
  //  EPRINTF("reset apsel\r\n");
  // reset APSEL to 0
  tmp = 0;
  TRANSACTION(SW_SELECT_WR, &tmp);
  // set CSW to 32-bit, autoinc
  tmp = CSW_VALUE | CSW_SIZE32;
  TRANSACTION(SW_CSW_WR, &tmp);
  // enable debugging

  return SWD_writeWord(DBG_HCSR, (DBGKEY | C_DEBUGEN));
}

uint32_t SWD_writeWord(uint32_t address, uint32_t data)
{
  return _SWD_writeMem32(address, &data, 4);
}

uint32_t SWD_readWord(uint32_t address, uint32_t *data)
{
  return _SWD_readMem32(address, data, 4);
}

uint32_t SWD_readReg(uint32_t idx, uint32_t *value)
{
  int i;
  // request read
  if (SWD_writeWord(DBG_CRSR, idx))
    return 1;
  // wait for data

  for (i = 0; i < REGRETRIES; i++)
  {
    if (SWD_readWord(DBG_HCSR, value))
      return 1;
    if (*value & S_REGRDY)
      return SWD_readWord(DBG_CRDR, value);
  }
  return 1;
}

uint32_t SWD_writeReg(uint32_t idx, uint32_t value)
{
  int i;
  // write register value
  if (SWD_writeWord(DBG_CRDR, value))
    return 1;
  // request write
  if (SWD_writeWord(DBG_CRSR, idx | REGWnR))
    return 1;
  // wait for acknowledgement
  for (i = 0; i < REGRETRIES; i++)
  {
    if (SWD_readWord(DBG_HCSR, &value))
      return 1;
    if (value & S_REGRDY)
    {
      return 0;
    }
  }
  return 1;
}

// The following two routines aren't fully functional
//

uint32_t SWD_ReadDAPReg(uint16_t dap, uint16_t reg, uint32_t *data)
{
  uint32_t tmp;
  uint32_t req;
  // Register  bbbbnn00
  // Bank is four bits, register number is 2 bits (shifted)
  if (dap > 0xff)
  { // read debug port
    tmp = (reg & 0xf);
    TRANSACTION(SW_SELECT_WR, &tmp);
    req = SW_REQ_RnW | SW_REQ_PARK_START | ((reg & 0xC) << 1);
    req |= Parity(req) ? SW_REQ_PARITY : 0;
    TRANSACTION(req, data);
    TRANSACTION(SW_RDBUFF_RD, data);
  }
  else
  {
    // write select register
    tmp = (dap << 24) | (reg & 0xf0);
    TRANSACTION(SW_SELECT_WR, &tmp);

    req = SW_REQ_APnDP | SW_REQ_RnW | SW_REQ_PARK_START | ((reg & 0xC) << 1);
    req |= Parity(req) ? SW_REQ_PARITY : 0;
    TRANSACTION(req, data);
    // discard result and read from rdbuff
    TRANSACTION(SW_RDBUFF_RD, data);
    // restore select register
  }
  tmp = 0;
  TRANSACTION(SW_SELECT_WR, &tmp);
  return 0;
}

uint32_t SWD_WriteDAPReg(uint16_t dap, uint16_t reg, uint32_t data)
{
  uint32_t tmp;
  uint32_t req;
  if (dap > 0xff)
  { // Write debug port
    tmp = reg & 0xf;
    TRANSACTION(SW_SELECT_WR, &tmp);  
    req = SW_REQ_PARK_START | ((reg & 0xC) << 1);
    req |= Parity(req) ? SW_REQ_PARITY : 0;
    TRANSACTION(req, &data);
  }
  else
  {
    // write select register
    tmp = (dap << 24) | (reg & 0xf0);
    TRANSACTION(SW_SELECT_WR, &tmp);

    req = SW_REQ_APnDP | SW_REQ_PARK_START | ((reg & 0xC) << 1);
    req |= Parity(req) ? SW_REQ_PARITY : 0;
    TRANSACTION(req, &data);
    
  }
  // restore select register
  tmp = 0;
  TRANSACTION(SW_SELECT_WR, &tmp);
  return 0;
}
