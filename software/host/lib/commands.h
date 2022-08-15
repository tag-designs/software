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

#ifndef STLINK_COMMANDS_H_
#define STLINK_COMMANDS_H_

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
    STLINK_DEBUG_APIV2_GETLASTRWSTATUS2  = 0x3E,
    STLINK_DEBUG_ENTER_SWD               = 0xa3
};

#endif // STLINK_COMMANDS_H_
