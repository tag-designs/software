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

#define USEEPRINTF 1
#include <stdint.h>
#include <dp_swd.h>
#include <debug_cm.h>
#include "hal.h"
#include "usbcfg.h"
#include "app.h"
#include "board.h"
#ifdef USEEPRINTF
#include "ch.h"
#include "chprintf.h"
#endif

#define REGRETRIES 20
#define DELCNT 1

static const int AUTO_INCREMENT_PAGE_SIZE = 1024;
static const int CSW_VALUE = (CSW_RESERVED | CSW_MSTRDBG | CSW_HPROT | CSW_DBGSTAT | CSW_SADDRINC);
uint32_t CoreID = 0;

static bool isOutput = true;
static unsigned int  spiSize = 0;

static void _SetSWPinsIdle(void)
{
  EPRINTF("_SetSWPinsIdle\r\n");
  palClearLine(LINE_TGT_SWCLK);
  palClearLine(LINE_TGT_SWDIO);

  isOutput = true;
  spiSize = 0;

  toOutput(LINE_TGT_SWDIO);
  //toOutput(LINE_TGT_SWCLK);
}

static void _ResetDebugPins(void)
{
   //EPRINTF("_ResetDebugPins\r\n");
  palClearLine(LINE_TGT_SWCLK);
  palClearLine(LINE_TGT_SWDIO);
  palClearLine(LINE_TGT_SWDIO_IN);
  toInput(LINE_TGT_SWCLK);
  toInput(LINE_TGT_SWDIO);
  toInput(LINE_TGT_SWDIO_IN);

  rccResetSPI1();
  rccDisableSPI1();
  //toAnalog(LINE_TGT_SWDIO); //was input
 // toAnalog(LINE_TGT_SWCLK); //was output
}


static void spiInit(void)
{
  EPRINTF("spiInit\r\n");

  uint32_t br = 6<<3;
  palClearLine(LINE_TGT_SWCLK);
  palClearLine(LINE_TGT_SWDIO);
  palClearLine(LINE_TGT_SWDIO_IN);

  rccEnableSPI1(true);
  rccResetSPI1(); 

  toAlternate(LINE_TGT_SWCLK);
  toAlternate(LINE_TGT_SWDIO);
  toAlternate(LINE_TGT_SWDIO_IN);

  SPI1->CR1  = 0;
  SPI1->CR1  = SPI_CR1_LSBFIRST | br | SPI_CR1_MSTR;
  SPI1->CR2  = SPI_CR2_SSOE | (7<<8);
  SPI1->CR1 |= SPI_CR1_SPE;
  //spiSize = 8;
  //isOutput = true;
  EPRINTF("CR1 %x CR2 %x\n\r",SPI1->CR1, SPI1->CR2);
}

static void spiSwitch(bool output, unsigned int size)
{

  if (output == isOutput && size == spiSize)
    return;

  if ((size < 4) || (size > 16))
    return;

  unsigned int ds = (size - 1) << 8;
  unsigned int frxth = size > 8 ? 0 : 1 << 12;

  SPI1->CR1 &= ~SPI_CR1_SPE;

  // fifo threshold, 8-bit data size

  SPI1->CR2 = frxth | SPI_CR2_SSOE | ds;

  // master, enable spi device

  
  if (output) { 
    toAlternate(LINE_TGT_SWDIO);
    //SPI1->CR1 &= ~SPI_CR1_CPHA;
  } else {
   toAnalog(LINE_TGT_SWDIO);
    //SPI1->CR1 |= SPI_CR1_CPHA;
  }

  SPI1->CR1 |= SPI_CR1_SPE;
  spiSize = size;
  isOutput = output;
 // EPRINTF("CR1 %x CR2 %x\n\r",SPI1->CR1, SPI1->CR2);
}

static void spiDisable(void)
{
  SPI1->CR1 = 0;
  SPI1->CR2 = 0;
  spiSize = 0;
  _SetSWPinsIdle();
}


static inline void _SetSWDIOasOutput(uint32_t size)
{
  spiSwitch(true,size);
}
static inline void _SetSWDIOasInput(uint32_t size)
{
  spiSwitch(false,size);
}

static inline uint32_t SW_ShiftIn(uint8_t bits)
{
  //EPRINTF("SW_ShiftIn(%d)\r\n",bits);
  uint16_t tmp16;
  uint8_t tmp8;
  _SetSWDIOasInput(bits);

  //uint16_t junk = *((volatile uint16_t *) &SPI1->DR);
  if (bits > 8) {
    *(volatile uint16_t *) &SPI1->DR = (uint16_t) 0;
    while (!(SPI1->SR & SPI_SR_RXNE));
    tmp16 =  *((volatile uint16_t *) &SPI1->DR);
    return tmp16;
  } else {
    *(volatile uint8_t *) &SPI1->DR = (uint8_t) 0;
    while (!(SPI1->SR & SPI_SR_RXNE));
    tmp8 =  *((volatile uint8_t *) &SPI1->DR); 
    //EPRINTF("Shiftin %d bits 0x%x\n\r",bits,tmp8);
    return tmp8;
  }

}

static inline uint32_t SW_ShiftInBytes(uint8_t bytes)
{
  int i;
  uint32_t tmp = 0;
  uint8_t *buf =  (uint8_t *) &tmp;
  //EPRINTF("SW_ShiftInBytes(%d)\r\n",bytes);
  
  if (bytes > 4)
    return 0;
  _SetSWDIOasInput(8);
  
  for (i = 0; i < bytes; i++)
  {
    *(volatile uint8_t*) &SPI1->DR = 0 ;
  }
  //EPRINTF("zeros sent\r\n");
  for (i = 0; i < bytes; i++){
    while (!(SPI1->SR & SPI_SR_RXNE));
    buf[i] = *((volatile uint8_t *) &SPI1->DR);
  }
  //EPRINTF("data received %d\r\n",tmp);
  return tmp;
}

static inline void SW_ShiftOutBytes(uint32_t data, uint8_t bytes)
{
  int i;
  uint8_t tmp;
  uint8_t *buf = (uint8_t *) &data;
  //EPRINTF("_SW_ShiftOutBytes(%x,%d)\r\n",data,bytes);
  //EPRINTF("CR1 %x CR2 %x\n\r",SPI1->CR1, SPI1->CR2);
  if (bytes > 4)
    return;
  _SetSWDIOasOutput(8);
 

 for (i = 0; i < bytes; i++)
  {
    *(volatile uint8_t *) &SPI1->DR = buf[i];
  }
  //EPRINTF("Data sent\r\n");
  
  for (i = 0; i < bytes; i++){
    while (!(SPI1->SR & SPI_SR_RXNE));
    tmp = *((volatile uint8_t *) &SPI1->DR);
  }
  //while((SPI1->SR & SPI_SR_BSY));
  //EPRINTF("loopback received \r\n");
  return tmp;

}

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
  uint32_t ack_bits;
  uint32_t pbit;
  uint32_t d0_bit;
  uint32_t tmp;   
  //uint8_t ack_reverse[] = {0,4,2,6,1,5,3,7};

  SW_ShiftOutBytes(req, 1);     // Send header  
  //chThdSleepMilliseconds(4);
  ack_bits = SW_ShiftIn(5);     // read trn,ack,(trn|data0)
  ack = (ack_bits >> 1) & 7;    // ACK, toss the turnaround bit and possible first data
  d0_bit = (ack_bits >> 4) & 1; // Save possible data bit 0
 // ack = ack_reverse[ack];

 // EPRINTF("Transaction req: %x data: %x ack %x ack_bits %x\r\n",req,*data,ack,ack_bits);
  //chThdSleepMilliseconds(4);

  switch (ack)
  {
  case SW_ACK_OK: // good to go
    if (req & SW_REQ_RnW)
    {        
      *data = SW_ShiftInBytes(3); // get 24 bits data
       tmp = SW_ShiftIn(9);        // get 8 bits data + trn
      // combine data, parity
      pbit = (tmp >> 8) & 1;
      *data = (tmp << 24) | (*data & 0xffffff);    //combine
      *data = (*data << 1) | d0_bit;

  

      if (0 &&(pbit ^ Parity(*data)))
      { // parity check
        ack = SW_ACK_PARITY_ERR;
        EPRINTF("parity error data 0x%x pbit %x dataparity %x\r\n", *data, pbit, Parity(*data));
      }
      //EPRINTF("received data %x\r\n", *data);
    }
    else
    {        
      SW_ShiftOutBytes(*data,3); // send 24 bits
      tmp = ((*data >> 24)&0xff)|(Parity(*data)<<8);
      SW_ShiftOutBytes(tmp,2);  // send 8 bits + parity + plus extra zeros
                                // per STM32 documentation for writes
                                // to ensure they are completed internally
    }
    break;

    // check out this error recovery
    // may need to finish transaction for overrun

  case SW_ACK_FAULT:
  case SW_ACK_WAIT:
  default:             
  }
   SW_ShiftOutBytes(0,1); // drive line to idle state
 // _SetSWDIOasOutput(8); // Set pin direction  
  //chThdSleepMilliseconds(1);
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
  SW_ShiftOutBytes(0xffffffff, 4);
  SW_ShiftOutBytes(0xffffffff, 3);
}

uint32_t SWD_LineReset(uint32_t *idcode)
{
  uint32_t ack;
  *idcode = 0;

  // SWD reset sequence:
  //          56 1's, 8 0's, Read IDcode
  //          SW_IDCODE_RD not allowed to wait or fault
  //_SetSWDIOasOutput();
  SW_ShiftReset();
  SW_ShiftOutBytes(0, 1);
  ack = SWD_Transaction(SW_IDCODE_RD, idcode, 0);
  //EPRINTF("line reset idcode: 0x%x ack: %d\r\n",*idcode,ack);
  return ack;
}

static uint32_t SWD_Connect(uint32_t *idcode)
{
  // Init Pins
  //_SetSWPinsIdle();
  spiInit();
  //EPRINTF("Connect\r\n");
  
  // Select SWD Port
  //_SetSWDIOasOutput();
  SW_ShiftReset();
  SW_ShiftOutBytes(0xE79E, 2);


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
  SW_ShiftOutBytes(0xE73C, 2); 
  SW_ShiftReset();
  // Release pins (except nReset)
  _SetSWPinsIdle();
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
      // let it fail once
    if ((err = _SWD_writeMem32(address, data, len)))
      if ((err = _SWD_writeMem32(address, data, len)))
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
      // let it fail once
    if ((err = _SWD_readMem32(address, data, len)))
      if ((err = _SWD_readMem32(address, data, len)))
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
    // let it fail once
    if ((err = SWD_writeHalf(address + 2 * i, *data)))
      if ((err = SWD_writeHalf(address + 2 * i, *data)))
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
    // let it fail once
    if ((err = SWD_readHalf(address + 2 * i, data + i)))
      if ((err = SWD_readHalf(address + 2 * i, data + i)))
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
    // let it fail once
    if ((err = SWD_writeByte(address + i, *data)))
      if ((err = SWD_writeByte(address + i, *data)))
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
    // let it fail once
    if ((err = SWD_readByte(address + i, data + i)))
      if ((err = SWD_readByte(address + i, data + i)))
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

  EPRINTF("Open\r\n");

  //
  if (SWD_Connect(&CoreID) != SW_ACK_OK)
  {
    SW_ShiftReset();
    if (SWD_Connect(&CoreID) != SW_ACK_OK)
    {
      EPRINTF("Open failed\r\n");
      return 1;
    }
  }



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
