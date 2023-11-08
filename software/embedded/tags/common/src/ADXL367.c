#include "ADXL367.h"
#include "ch.h"
#include "hal.h"
#include "board.h"
#include "custom.h"


static inline void spiSendPolled(uint32_t n, uint8_t *buf) {
  volatile uint8_t *spidr = (volatile uint8_t *)&SPI1->DR;
  while (n--) {
    *spidr = *buf++;
    while ((SPI1->SR & SPI_SR_RXNE) == 0);
    *spidr;
  }
}

static inline void spiReceivePolled(uint32_t n, uint8_t *buf) {
  volatile uint8_t *spidr = (volatile uint8_t *)&SPI1->DR;
  while (n--) {
    *spidr = 0xff;
    while ((SPI1->SR & SPI_SR_RXNE) == 0);
    *buf++ = *spidr;
  }
}



/***************************************************************************//**
 * @brief Writes data into a register.
 *
 * @param registerValue   - Data value to write.
 * @param registerAddress - Address of the register.
 * @param bytesNumber     - Number of bytes. Accepted values: 0 - 1.
 *
 * @return None.
*******************************************************************************/
void ADXL367_SetRegisterValue(unsigned short registerValue,
                              unsigned char  registerAddress,
                              unsigned char  bytesNumber)
{
  unsigned char buffer[4];

  buffer[0] = ADXL367_WRITE_REG;
  buffer[1] = registerAddress;
  buffer[2] = (registerValue & 0x00FF);
  buffer[3] = (registerValue >> 8);
  

  palClearLine(LINE_ACCEL_CS);
  spiSendPolled(bytesNumber + 2, buffer);
  palSetLine(LINE_ACCEL_CS);
 
}

/***************************************************************************//**
 * @brief Performs a burst read of a specified number of registers.
 *
 * @param pReadData       - The read values are stored in this buffer.
 * @param registerAddress - The start address of the burst read.
 * @param bytesNumber     - Number of bytes to read.
 *
 * @return None.
*******************************************************************************/
void ADXL367_GetRegisterValue(unsigned char* pReadData,
                              unsigned char  registerAddress,
                              unsigned char  bytesNumber)
{
    unsigned char buffer[2];
    
    buffer[0] = ADXL367_READ_REG;
    buffer[1] = registerAddress;

    palClearLine(LINE_ACCEL_CS);
    spiSendPolled(2, buffer);
    spiReceivePolled(bytesNumber, pReadData);
    palSetLine(LINE_ACCEL_CS);
}
/*

char ADXL367_Init(void)
{
    unsigned char regValue = 0;
    char          status   = -1;

    ADXL367_GetRegisterValue(&regValue, ADXL367_REG_PARTID, 1);
    if((regValue != ADXL367_PART_ID))
    {
        status = -1;
    }

    return status;
}
*/

void ADXL367_SoftwareReset(void) {
  ADXL367_SetRegisterValue(ADXL367_RESET_KEY,ADXL367_REG_SOFT_RESET,1);
}

static void ADXL367_RegisterWriteMask(uint8_t reg_addr,uint8_t data, uint8_t mask)
{
	uint8_t reg_data;

	ADXL367_GetRegisterValue(&reg_data, reg_addr, 1);
	reg_data &= ~mask;
	reg_data |= data;
	ADXL367_SetRegisterValue(reg_data, reg_addr,1);
}

/***************************************************************************//**
 * @brief Places the device into standby/measure mode.
 *
 * @param pwrMode - Power mode.
 *                  Example: 0 - standby mode.
 *                  1 - measure mode.
 *
 * @return None.
*******************************************************************************/
void ADXL367_SetPowerMode(enum adxl367_op_mode mode)
{
  ADXL367_RegisterWriteMask(ADXL367_REG_POWER_CTL,mode,ADXL367_POWER_CTL_MEASURE_MSK);
}