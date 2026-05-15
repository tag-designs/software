/*

BSD 3-Clause License

Copyright (c) 2020, stlink-org
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef STLINK_H_
#define STLINK_H_

enum stlink_debug_start {
  STLINK_GET_VERSION                 = 0xF1,
  STLINK_DEBUG_COMMAND               = 0xF2,
  STLINK_DFU_COMMAND                 = 0xF3,
  STLINK_SWIM_COMMAND                = 0xF4,
  STLINK_GET_CURRENT_MODE            = 0xF5,
  STLINK_GET_TARGET_VOLTAGE          = 0xF7,

  STLINK_SWIM_ENTER                  = 0x00,
  STLINK_SWIM_EXIT                   = 0x01,
  STLINK_DFU_EXIT                    = 0x07
};

enum stlink_debug_commands {
    STLINK_DEBUG_ENTER_JTAG              = 0x00,
    STLINK_DEBUG_GETSTATUS               = 0x01,
    STLINK_DEBUG_FORCEDEBUG              = 0x02,
    STLINK_DEBUG_APIV1_RESETSYS          = 0x03,
    STLINK_DEBUG_APIV1_READALLREGS       = 0x04,
    STLINK_DEBUG_APIV1_READREG           = 0x05,
    STLINK_DEBUG_APIV1_WRITEREG          = 0x06,
    STLINK_DEBUG_READMEM_32BIT           = 0x07,
    STLINK_DEBUG_WRITEMEM_32BIT          = 0x08,
    STLINK_DEBUG_RUNCORE                 = 0x09,
    STLINK_DEBUG_STEPCORE                = 0x0a,
    STLINK_DEBUG_APIV1_SETFP             = 0x0b,
    STLINK_DEBUG_READMEM_8BIT            = 0x0c,
    STLINK_DEBUG_WRITEMEM_8BIT           = 0x0d,
    STLINK_DEBUG_APIV1_CLEARFP           = 0x0e,
    STLINK_DEBUG_APIV1_WRITEDEBUGREG     = 0x0f,
    STLINK_DEBUG_APIV1_ENTER             = 0x20,
    STLINK_DEBUG_EXIT                    = 0x21,
    STLINK_DEBUG_READCOREID              = 0x22,
    STLINK_DEBUG_APIV2_ENTER             = 0x30,
    STLINK_DEBUG_APIV2_READ_IDCODES      = 0x31,
    STLINK_DEBUG_APIV2_RESETSYS          = 0x32,
    STLINK_DEBUG_APIV2_READREG           = 0x33,
    STLINK_DEBUG_APIV2_WRITEREG          = 0x34,
    STLINK_DEBUG_APIV2_WRITEDEBUGREG     = 0x35,
    STLINK_DEBUG_APIV2_READDEBUGREG      = 0x36,
    STLINK_DEBUG_APIV2_READALLREGS       = 0x3A,
    STLINK_DEBUG_APIV2_GETLASTRWSTATUS   = 0x3B,
    STLINK_DEBUG_APIV2_DRIVE_NRST        = 0x3C,
    STLINK_DEBUG_APIV2_GETLASTRWSTATUS2  = 0x3E,
    STLINK_DEBUG_APIV2_SWD_SET_FREQ      = 0x43,
    STLINK_DEBUG_APIV2_READ_DAP_REG      = 0x45,
    STLINK_DEBUG_APIV2_WRITE_DAP_REG     = 0x46,
    STLINK_DEBUG_APIV2_READMEM_16BIT     = 0x47,
    STLINK_DEBUG_APIV2_WRITEMEM_16BIT    = 0x48,

    STLINK_DEBUG_APIV2_INIT_AP           = 0x4B,
    STLINK_DEBUG_APIV2_CLOSE_AP_DBG      = 0x4C,
    STLINK_DEBUG_ENTER_SWD               = 0xa3
};

// These versions chosen to keep stm32 programmer happy

#define VJTAG 0x25
#define VSWIM 7
#define VSTLINK 2
#define BESWAP(x) ((((x)&0xff)<<8)|(((x)>>8)&0xff))
#define STLINK_VERSION BESWAP(((VJTAG<<6)|(VSTLINK<<12)|VSWIM))

#define STLINK_DEV_DFU_MODE            0x00
#define STLINK_DEV_MASS_MODE           0x01
#define STLINK_DEV_DEBUG_MODE          0x02
#define STLINK_DEV_SWIM_MODE           0x03
#define STLINK_DEV_BOOTLOADER_MODE     0x04
#define STLINK_DEV_UNKNOWN_MODE        -1

#define STLINK_DEBUG_ERR_OK             0x80
#define STLINK_DEBUG_ERR_FAULT          0x81
#define STLINK_JTAG_WRITE_ERROR         0x0c
#define STLINK_JTAG_WRITE_VERIF_ERROR   0x0d
#define STLINK_SWD_AP_WAIT              0x10
#define STLINK_SWD_DP_WAIT              0x14

#endif
