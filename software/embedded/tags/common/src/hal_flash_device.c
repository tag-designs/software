/*
    ChibiOS - Copyright (C) 2006..2018 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/**
 * @file    hal_flash_device.c
 * @brief   Macronix MX25Rxx35F serial flash driver header.
 *
 * @addtogroup MACRONIX_MX25Rxx35F
 * @{
 */

#include <string.h>

#include "hal.h"
#include "hal_serial_nor.h"

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

#define PAGE_SIZE 256U
#define PAGE_MASK (PAGE_SIZE - 1U)

#if MX25R_USE_SUB_SECTORS == TRUE
#define SECTOR_SIZE 0x00001000U
#define CMD_SECTOR_ERASE MX25R_CMD_SUBSECTOR_ERASE
#else
#define SECTOR_SIZE 0x00010000U
#define CMD_SECTOR_ERASE MX25R_CMD_SECTOR_ERASE
#endif

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/**
 * @brief   MX25Rxx35F descriptor.
 */

flash_descriptor_t snor_descriptor = {
    .attributes = FLASH_ATTR_ERASED_IS_ONE | FLASH_ATTR_REWRITABLE |
                  FLASH_ATTR_SUSPEND_ERASE_CAPABLE,
    .page_size = 256U,
    .sectors_count = 0U, /* It is overwritten.*/
    .sectors = NULL,
    .sectors_size = SECTOR_SIZE,
    .address = 0U,
    .size = 0U /* It is overwritten.*/
};

#if (SNOR_BUS_DRIVER == SNOR_BUS_DRIVER_WSPI) || defined(__DOXYGEN__)
#if (WSPI_SUPPORTS_MEMMAP == TRUE) || defined(__DOXYGEN__)
/**
 * @brief    read command for memory mapped mode.
 */
const wspi_command_t snor_memmap_read = {
#if MX25R_BUS_MODE == MX25R_BUS_MODE_WSPI1L
    .cmd = MX25R_CMD_READ,
    .addr = 0,
    .dummy = MX25R_BUS_DUMMY_1L,
    .cfg = WSPI_CFG_ADDR_SIZE_24 | WSPI_CFG_CMD_MODE_ONE_LINE |
           WSPI_CFG_ADDR_MODE_ONE_LINE | WSPI_CFG_DATA_MODE_ONE_LINE,
    .alt = 0
#elif MX25R_BUS_MODE == MX25R_BUS_MODE_WSPI2L
    .cmd = MX25R_CMD_DUAL_INOUT_READ,
    .addr = 0,
    .dummy = MX25R_BUS_DUMMY_2L,
    .cfg = WSPI_CFG_ADDR_SIZE_24 | WSPI_CFG_CMD_MODE_ONE_LINE |
           WSPI_CFG_ADDR_MODE_TWO_LINES | WSPI_CFG_DATA_MODE_TWO_LINES,
    .alt = 0
#else
    .cmd = MX25R_CMD_QUAD_INOUT_READ,
    .addr = 0,
    .dummy = MX25R_BUS_DUMMY_4L,
    .cfg = WSPI_CFG_ADDR_SIZE_24 | WSPI_CFG_CMD_MODE_ONE_LINE |
           WSPI_CFG_ADDR_MODE_FOUR_LINES | WSPI_CFG_DATA_MODE_FOUR_LINES,
    .alt = 0
#endif
};
#endif /* WSPI_SUPPORTS_MEMMAP == TRUE */
#endif /* SNOR_BUS_DRIVER == SNOR_BUS_DRIVER_WSPI */

/*===========================================================================*/
/* Driver local variables and types.                                         */
/*===========================================================================*/

static const uint8_t mx25r_manufacturer_ids[] = MX25R_SUPPORTED_MANUFACTURE_IDS;
static const uint8_t mx25r_memory_type_ids[] = MX25R_SUPPORTED_MEMORY_TYPE_IDS;

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

static bool mx25r_find_id(const uint8_t *set, size_t size, uint8_t element)
{
  size_t i;

  for (i = 0; i < size; i++)
  {
    if (set[i] == element)
    {
      return true;
    }
  }
  return false;
}


static flash_error_t mx25r_poll_status(SNORDriver *devp)
{
  uint8_t sts;
  uint8_t secr;

  /* poll status register */
  do 
  {
    if ( MX25R_NICE_WAITING) {
      osalThreadSleepMilliseconds(1);
    }
    bus_cmd_receive(devp->config->busp, MX25R_CMD_READ_STATUS_REG, 1, &sts);
  } while ((sts & MX25R_FLAGS_SR_WIP) != 0U);

  /* check for errors */

  bus_cmd_receive(devp->config->busp, MX25R_CMD_READ_SEC_REG, 1, &secr);

  if (secr & (MX25R_FLAGS_ALL_ERRORS))
  {
    return FLASH_ERROR_PROGRAM;
  }
  return FLASH_NO_ERROR;
}

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

void snor_device_init(SNORDriver *devp)
{

  uint8_t sts[3];
  bool changed = false;

snor_device_power_up(devp);
#if MX25R_RESET_ON_INIT == TRUE
  bus_cmd_send(devp->config->busp, MX25R_CMD_RESET_ENABLE);
  bus_cmd_send(devp->config->busp, MX25R_CMD_RESET_MEMORY);
  osalThreadSleepMicroseconds(50);
#endif

  /* read device id  */

  bus_cmd_receive(devp->config->busp, MX25R_CMD_READ_ID, sizeof devp->device_id,
                  devp->device_id);

  /* Checking if the device is white listed  */

  osalDbgAssert(mx25r_find_id(mx25r_manufacturer_ids,
                              sizeof mx25r_manufacturer_ids,
                              devp->device_id[0]),
                "invalid manufacturer id");
  osalDbgAssert(mx25r_find_id(mx25r_memory_type_ids,
                              sizeof mx25r_memory_type_ids, devp->device_id[1]),
                "invalid memory type id");

  /* Set up the descriptor              */

  snor_descriptor.size = 1 << (devp->device_id[2]);
  snor_descriptor.sectors_count = snor_descriptor.size / SECTOR_SIZE;

  /* configure bus width and power mode */

  bus_cmd_receive(devp->config->busp, MX25R_CMD_READ_STATUS_REG, 1, sts);

#if (SNOR_BUS_DRIVER == SNOR_BUS_DRIVER_WSPI) && (MX25R_SWITCH_WIDTH == TRUE)
#if (SNOR_BUS_DRIVER != SNOR_BUS_DRIVER_WSPI) || (MX25R_BUS_MODE == MX25R_BUS_MODE_WSPI1L)
  if (sts[0] & MX25R_FLAGS_SR_QE)
  {
    sts[0] = sts[0] & ~MX25R_FLAGS_SR_QE;
    changed = true;
  }
#else
  if (!(sts[0] & MX25R_FLAGS_SR_QE))
  {
    sts[0] = sts[0] | MX25R_FLAGS_SR_QE;
    changed = true;
  }
#endif
#endif
  bus_cmd_receive(devp->config->busp, MX25R_CMD_READ_CFG_REG, 2, &sts[1]);
  if (((sts[2] & MX25R_FLAGS_CR2_LH_SWITCH) != 0U) != MX25R_HIGH_PERFORMANCE_MODE)
  {
    sts[2] = sts[2] ^ MX25R_FLAGS_CR2_LH_SWITCH;
    changed = true;
  }
  if (changed)
  {
    bus_cmd(devp->config->busp, MX25R_CMD_WRITE_ENABLE);
    bus_cmd_send(devp->config->busp, MX25R_CMD_WRITE_STATUS_CFG_REG, 3, sts);
    mx25r_poll_status(devp);
    /* validate by reading status back. */
    bus_cmd_receive(devp->config->busp, MX25R_CMD_READ_STATUS_REG, 1, &sts[1]);
    osalDbgAssert((sts[0] & MX25R_FLAGS_SR_QE) == (sts[1] & MX25R_FLAGS_SR_QE),
                  "failed to set wspi bus width");
  }
}

flash_error_t snor_device_read(SNORDriver *devp, flash_offset_t offset,
                               size_t n, uint8_t *rp)
{
#if (SNOR_BUS_DRIVER == SNOR_BUS_DRIVER_WSPI) && (MX25R_BUS_MODE == MX25R_BUS_MODE_WSPI2L)
  wspi_command_t read_cmd = {
      .cmd = MX25R_CMD_DUAL_INOUT_READ,
      .addr = offset,
      .dummy = MX25R_BUS_DUMMY_2L,
      .cfg = WSPI_CFG_ADDR_SIZE_24 | WSPI_CFG_CMD_MODE_ONE_LINE |
             WSPI_CFG_ADDR_MODE_TWO_LINES | WSPI_CFG_DATA_MODE_TWO_LINES,
      .alt = 0};
  wspiReceive(devp->config->busp, &read_cmd, n, rp);
#elif (SNOR_BUS_DRIVER == SNOR_BUS_DRIVER_WSPI) && (MX25R_BUS_MODE == MX25R_BUS_MODE_WSPI4L)
  wspi_command_t read_cmd = {
      .cmd = MX25R_CMD_QUAD_INOUT_READ,
      .addr = offset,
      .dummy = MX25R_BUS_DUMMY_4L,
      .cfg = WSPI_CFG_ADDR_SIZE_24 | WSPI_CFG_CMD_MODE_ONE_LINE |
             WSPI_CFG_ADDR_MODE_FOUR_LINES | WSPI_CFG_DATA_MODE_FOUR_LINES,
      .alt = 0};
  wspiReceive(devp->config->busp, &read_cmd, n, rp);
#else
  bus_cmd_addr_receive(devp->config->busp, MX25R_CMD_READ, offset, n, rp);
#endif
  return FLASH_NO_ERROR;
}

flash_error_t snor_device_program(SNORDriver *devp, flash_offset_t offset,
                                  size_t n, const uint8_t *pp)
{

#if (SNOR_BUS_DRIVER == SNOR_BUS_DRIVER_WSPI) && (MX25R_BUS_MODE == MX25R_BUS_MODE_WSPI4L)
  wspi_command_t write_cmd = {
      .cmd = MX25R_CMD_QUAD_PAGE_PROG,
      .addr = 0,
      .dummy = 0,
      .cfg = WSPI_CFG_ADDR_SIZE_24 | WSPI_CFG_CMD_MODE_ONE_LINE |
             WSPI_CFG_ADDR_MODE_FOUR_LINES | WSPI_CFG_DATA_MODE_FOUR_LINES,
      .alt = 0};
#endif

  /* Data is programmed page by page.*/
  while (n > 0U)
  {
    flash_error_t err;

    /* Data size that can be written in a single program page operation.*/
    size_t chunk = (size_t)(((offset | PAGE_MASK) + 1U) - offset);
    if (chunk > n)
    {
      chunk = n;
    }

    /* Enabling write operation.*/
    bus_cmd(devp->config->busp, MX25R_CMD_WRITE_ENABLE);
    /* check write enable */
    /*
    uint8_t sts;
    do
    {
      bus_cmd_receive(devp->config->busp, MX25R_CMD_READ_STATUS_REG, 1, &sts);
    } while ((sts & MX25R_FLAGS_SR_WEL) == 0U);
    */

#if (SNOR_BUS_DRIVER == SNOR_BUS_DRIVER_WSPI) && (MX25R_BUS_MODE == MX25R_BUS_MODE_WSPI4L)
    write_cmd.addr = offset;
    wspiSend(devp->config->busp, &write_cmd, chunk, pp);
#else
    bus_cmd_addr_send(devp->config->busp, MX25R_CMD_PAGE_PROG, offset, chunk, pp);
#endif

    /* Wait for status and check errors.*/

    err = mx25r_poll_status(devp);
    if (err != FLASH_NO_ERROR)
    {
      return err;
    }

    /* Next page.*/
    offset += chunk;
    pp += chunk;
    n -= chunk;
  }

  return FLASH_NO_ERROR;
}

flash_error_t snor_device_start_erase_all(SNORDriver *devp)
{

  bus_cmd(devp->config->busp, MX25R_CMD_WRITE_ENABLE);
  bus_cmd(devp->config->busp, MX25R_CMD_BLOCK_ERASE);

  return FLASH_NO_ERROR;
}

flash_error_t snor_device_start_erase_sector(SNORDriver *devp,
                                             flash_sector_t sector)
{
  flash_offset_t offset = (flash_offset_t)(sector * SECTOR_SIZE);

  bus_cmd(devp->config->busp, MX25R_CMD_WRITE_ENABLE);
  bus_cmd_addr(devp->config->busp, MX25R_CMD_SECTOR_ERASE, offset);

  return FLASH_NO_ERROR;
}

flash_error_t snor_device_verify_erase(SNORDriver *devp,
                                       flash_sector_t sector)
{
  uint8_t cmpbuf[MX25R_COMPARE_BUFFER_SIZE];
  flash_offset_t offset;
  size_t n;

  /* Read command.*/
  offset = (flash_offset_t)(sector * SECTOR_SIZE);
  n = SECTOR_SIZE;
  while (n > 0U)
  {
    uint8_t *p;

    /* May as well use the slow read operation since 
     * we are only reading a few bytes at a time
    */ 

    bus_cmd_addr_receive(devp->config->busp, MX25R_CMD_READ, offset,
                         sizeof cmpbuf, cmpbuf);

    /* Checking for erased state of current buffer.*/
    for (p = cmpbuf; p < &cmpbuf[MX25R_COMPARE_BUFFER_SIZE]; p++)
    {
      if (*p != 0xFFU)
      {
        /* Ready state again.*/
        devp->state = FLASH_READY;
        return FLASH_ERROR_VERIFY;
      }
    }

    offset += sizeof cmpbuf;
    n -= sizeof cmpbuf;
  }

  return FLASH_NO_ERROR;
}

flash_error_t snor_device_query_erase(SNORDriver *devp, uint32_t *msec)
{
  uint8_t sts;
  uint8_t sec;

  bus_cmd_receive(devp->config->busp, MX25R_CMD_READ_STATUS_REG, 1, &sts);
  bus_cmd_receive(devp->config->busp, MX25R_CMD_READ_SEC_REG, 1, &sec);

  /* if the erase cycle is in progress or if it is suspended */

  if ((sts & MX25R_FLAGS_SR_WIP) || (sec & MX25R_FLAGS_SECR_PSB))
  {
    /* Recommended time before polling again, this is a simplified
       implementation.*/
    if (msec != NULL)
    {
      *msec = 1U;
    }
    return FLASH_BUSY_ERASING;
  }

  /* check for errors */

  if (sts & (MX25R_FLAGS_ALL_ERRORS))
  {
    return FLASH_ERROR_ERASE;
  }
  return FLASH_NO_ERROR;
}

flash_error_t snor_device_read_sfdp(SNORDriver *devp, flash_offset_t offset,
                                    size_t n, uint8_t *rp)
{

  (void)devp;
  (void)rp;
  (void)offset;
  (void)n;

  return FLASH_NO_ERROR;
}


flash_error_t snor_device_power_down(SNORDriver *devp){
  bus_cmd(devp->config->busp, MX25R_CMD_DEEP_POWER_DOWN);
  chThdSleepMicroseconds(30);
  return FLASH_NO_ERROR; 
}
flash_error_t snor_device_power_up(SNORDriver *devp){
  bus_cmd(devp->config->busp, MX25R_CMD_NO_OPERATION);
  chThdSleepMicroseconds(35);
  return FLASH_NO_ERROR;
}
/** @} */
