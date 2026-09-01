/*
    ChibiOS - Copyright (C) 2006-2026 Giovanni Di Sirio

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

/*
 * STM32U375xG memory setup.
 */
PROVIDE(TAG_FLASH_SIZE = 1024K);

MEMORY
{
    flash0 (rx) : org = 0x08000000, len = TAG_FLASH_SIZE  /* Flash NS */
    flash1 (rx) : org = 0x0C000000, len = TAG_FLASH_SIZE  /* Flash secure alias */
    flash2 (rx) : org = 0x00000000, len = 0
    flash3 (rx) : org = 0x00000000, len = 0
    flash4 (rx) : org = 0x00000000, len = 0
    flash5 (rx) : org = 0x00000000, len = 0
    flash6 (rx) : org = 0x00000000, len = 0
    flash7 (rx) : org = 0x00000000, len = 0
    /* Reserved for host/tag monitor handoff. Not part of any C runtime
       initialized RAM region; see MONITOR_SHARED_ADDR in monitor.h. */
    ram0   (wx) : org = 0x20000040, len = 256k - 0x40  /* SRAM1+SRAM2 NS */
    ram1   (wx) : org = 0x20000040, len = 192k - 0x40  /* SRAM1 NS */
    ram2   (wx) : org = 0x20030000, len = 64k   /* SRAM2 NS */
    ram3   (wx) : org = 0x00000000, len = 0
    ram4   (wx) : org = 0x30000000, len = 256k  /* SRAM1+SRAM2 secure alias */
    ram5   (wx) : org = 0x30000000, len = 192k  /* SRAM1 secure alias */
    ram6   (wx) : org = 0x30030000, len = 64k   /* SRAM2 secure alias */
    ram7   (wx) : org = 0x00000000, len = 0
}

/* For each data/text section two region are defined, a virtual region
   and a load region (_LMA suffix).*/

/* Flash region to be used for exception vectors.*/
REGION_ALIAS("VECTORS_FLASH", flash0);
REGION_ALIAS("VECTORS_FLASH_LMA", flash0);

/* Flash region to be used for constructors and destructors.*/
REGION_ALIAS("XTORS_FLASH", flash0);
REGION_ALIAS("XTORS_FLASH_LMA", flash0);

/* Flash region to be used for code text.*/
REGION_ALIAS("TEXT_FLASH", flash0);
REGION_ALIAS("TEXT_FLASH_LMA", flash0);

/* Flash region to be used for read only data.*/
REGION_ALIAS("RODATA_FLASH", flash0);
REGION_ALIAS("RODATA_FLASH_LMA", flash0);

/* Flash region to be used for various.*/
REGION_ALIAS("VARIOUS_FLASH", flash0);
REGION_ALIAS("VARIOUS_FLASH_LMA", flash0);

/* Flash region to be used for RAM(n) initialization data.*/
REGION_ALIAS("RAM_INIT_FLASH_LMA", flash0);

/* RAM region to be used for Main stack. This stack accommodates the processing
   of all exceptions and interrupts.*/
REGION_ALIAS("MAIN_STACK_RAM", ram0);

/* RAM region to be used for the process stack. This is the stack used by
   the main() function.*/
REGION_ALIAS("PROCESS_STACK_RAM", ram0);

/* RAM region to be used for data segment.*/
REGION_ALIAS("DATA_RAM", ram0);
REGION_ALIAS("DATA_RAM_LMA", flash0);

/* RAM region to be used for BSS segment.*/
REGION_ALIAS("BSS_RAM", ram0);

/* RAM region to be used for the default heap.*/
REGION_ALIAS("HEAP_RAM", ram0);

/* RAM region to be used for DMA3 link structures.*/
REGION_ALIAS("DMA3_RAM", ram0);

SECTIONS
{
    /* Special section for GPDMA links, it must not exceed 64kB in size.*/
    .dma3 (NOLOAD) : ALIGN(16)
    {
        __dma3_base__ = .;
        *(.dma3)
        *(.dma3.*)
        *(.bss.__dma3_*)
        . = ALIGN(4);
        __dma3_end__ = .;
    } > DMA3_RAM
}

/* Generic rules inclusion.*/
INCLUDE rules.ld

/* Storage for logging and provisioned configuration.
   The final two STM32U375 flash pages are reserved for data that ordinary
   persistent-state erase must preserve:

   - penultimate page: host-provided calibration constants;
   - final page: NAND logical-block map, blank on non-NAND targets.

   Keeping the same layout for all U375 IMUTag members costs one unused page on
   non-NAND targets but gives erase code a single persistent-header boundary.

   The persistent region occupies the top of flash at FIXED addresses, between
   __tag_code_limit__ and the calibration page. Pinning both bounds is a
   correctness requirement, not tidiness:

   - `<tag>-download` passes only `-d <elf>` to STM32_Programmer_CLI, with no
     `-e all`. It therefore erases only the pages it writes, deliberately, so
     that provisioned configuration and calibration survive a firmware update.
   - The region previously began at the location counter, immediately after
     .rodata. Its start address was therefore a function of code size, and any
     change that crossed a page boundary relocated sEpoch, sconfig and the
     vddHeader region wholesale.
   - After such a move the objects are read from addresses the programmer did
     not erase: stale internal-header content when the image grows, or leftover
     code bytes when it shrinks. t_storedconfig carries no magic, version, or
     checksum, so a garbage start/stop schedule is accepted verbatim, and a
     garbage state marker that happens to fall inside the TagState range is
     accepted as a state to recover into.

   Both bounds are page-aligned, so the region size is always a whole number of
   pages, which erasePersistent() requires. */
SECTIONS
{
  __tag_flash_page_size__ = 4096;
  __tag_config_start__ = ORIGIN(flash0) + LENGTH(flash0) -
                         (2 * __tag_flash_page_size__);
  __tag_nand_map_start__ = ORIGIN(flash0) + LENGTH(flash0) -
                           __tag_flash_page_size__;
  __flash0_end__ = ORIGIN(flash0) + LENGTH(flash0);

  /* Fixed ceiling for code and read-only data, and therefore the fixed floor of
     the persistent region. Raising it costs internal-header capacity; lowering
     it costs code space. Either way it must stay page-aligned, and the ASSERTs
     after this block turn an overrun into a build failure rather than silent
     data corruption. */
  __tag_code_limit__ = ORIGIN(flash0) + 128K;

  /* Provisioned configuration owns a flash page to itself.
     sconfig is rewritten on every start command, and STM32 flash programming can
     only clear bits, so a rewrite must erase first. It previously shared a page
     with sEpoch and the head of vddHeader, which cannot be erased without
     destroying the state marker log and the internal checkpoint headers, so the
     write was done without an erase: programming S400 (0x190) over S100 (0x64)
     yielded 0x000, an unspecified ODR that aborts collection. Reaching IDLE does
     not imply an erased region either, because a reflash preserves .persistent
     and SelfTest and reset recovery both reach IDLE without erasePersistent().

     __persistent_start__ deliberately covers this page so erasePersistent(), and
     therefore a reset, still clears the provisioned configuration. */
  __stored_config_start__ = __tag_code_limit__;
  __stored_config_end__ = __tag_code_limit__ + __tag_flash_page_size__;
  __persistent_start__ = __tag_code_limit__;

  .storedconfig __stored_config_start__ (NOLOAD): ALIGN(__tag_flash_page_size__)
  {
   KEEP(*(.storedconfig))
   . = ALIGN(__tag_flash_page_size__);
  } > flash0

  .persistent __stored_config_end__ (NOLOAD): ALIGN(__tag_flash_page_size__)
  {
   *(.persistent)
   . = ALIGN(__tag_flash_page_size__);
  } > flash0
  /* vddHeader[] is declared with a nominal element count but grows upward at
     runtime, bounded only by this address; see readInternalHeader() and
     writeDataHeader(). The region must therefore extend to the calibration
     page, not merely cover the declared objects. */
  __persistent_end__ = __tag_config_start__;

  .calibration __tag_config_start__ (NOLOAD): ALIGN(4096)
  {
    __calibration_start__ = .;
    KEEP(*(.calibration))
    . = ALIGN(16);
    __calibration_data_end__ = .;
  } > flash0
  __calibration_end__ = __tag_nand_map_start__;

  .nand_map __tag_nand_map_start__ (NOLOAD): ALIGN(4096)
  {
   __nand_map_start__ = .;
   KEEP(*(.nand_map))
   . = ALIGN(16);
   __nand_map_data_end__ = .;
  } > flash0
  __nand_map_end__ = __flash0_end__;
}

/* Code must not grow into the persistent region. Before the region was pinned,
   an overrun silently relocated sEpoch/sconfig/vddHeader into flash the
   programmer does not erase; now it fails the build instead. .data is the last
   section with flash contents, so its load address bounds code growth. */
ASSERT(LOADADDR(.data) + SIZEOF(.data) <= __tag_code_limit__,
       "STM32U375 code and rodata exceed __tag_code_limit__: raise it (costs internal-header capacity) or shrink the image. Do NOT let the persistent region float: it holds sEpoch, sconfig and vddHeader, which <tag>-download does not erase.");

/* erasePersistent() walks the region a page at a time, so both bounds must be
   page-aligned and the region must be a whole number of pages. */
ASSERT((__tag_code_limit__ % __tag_flash_page_size__) == 0,
       "STM32U375 __tag_code_limit__ must be flash-page aligned");
ASSERT(((__tag_config_start__ - __tag_code_limit__) % __tag_flash_page_size__) == 0,
       "STM32U375 persistent region size must be a whole number of flash pages");
ASSERT(__tag_code_limit__ < __tag_config_start__,
       "STM32U375 __tag_code_limit__ leaves no room for the persistent region");

ASSERT(SIZEOF(.storedconfig) <= __tag_flash_page_size__,
       "STM32U375 stored configuration exceeds its dedicated flash page; it must \
stay within one page so writeStoredConfig() can erase it without touching sEpoch \
or vddHeader");
ASSERT(ADDR(.persistent) >= __stored_config_end__,
       "STM32U375 persistent section overlaps the stored configuration page");
ASSERT(ADDR(.persistent) + SIZEOF(.persistent) <= __tag_config_start__,
       "STM32U375 persistent section overlaps reserved configuration pages");
ASSERT(__calibration_data_end__ <= __tag_nand_map_start__,
       "STM32U375 calibration section exceeds its reserved flash page");
ASSERT(__nand_map_data_end__ <= __flash0_end__,
       "STM32U375 NAND map section exceeds its reserved flash page");
