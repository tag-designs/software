/*
    ChibiOS - Copyright (C) 2006..2016 Giovanni Di Sirio

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
 * STM32L432xC memory setup.
 */
PROVIDE(TAG_FLASH_SIZE = 256K);

MEMORY
{
    flash0  : org = 0x08000000, len = TAG_FLASH_SIZE
    flash1  : org = 0x08000000, len = 0
    flash2  : org = 0x00000000, len = 0
    flash3  : org = 0x00000000, len = 0
    flash4  : org = 0x00000000, len = 0
    flash5  : org = 0x00000000, len = 0
    flash6  : org = 0x00000000, len = 0
    flash7  : org = 0x00000000, len = 0
    ram0    : org = 0x20000000, len = 48k  /* SRAM1 */
    ram1    : org = 0x10000000, len = 16k  /* SRAM2 */
    ram2    : org = 0x00000000, len = 0 
    ram3    : org = 0x00000000, len = 0
    ram4    : org = 0x00000000, len = 0
    ram5    : org = 0x00000000, len = 0
    ram6    : org = 0x00000000, len = 0
    ram7    : org = 0x00000000, len = 0
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

/* Generic rules inclusion: ChibiOS's rules.ld, with rules_code.ld replaced by
   the project copy that places the tag identity record after the vectors. */
INCLUDE rules_stacks.ld
INCLUDE ../common/tag_rules_code.ld
INCLUDE rules_data.ld
INCLUDE rules_memory.ld

/* Storage for logging and calibration.

   Calibration constants occupy the top TAG_CALIBRATION_PAGES flash pages, at a
   FIXED address, so that an upgrade which erases every page except those keeps
   a tag's calibration. It previously followed the code at the next page
   boundary, which moved it whenever the image crossed one: a calibrated board
   then read an unwritten page, or leftover code bytes, as its constants.

   Only a family that stores calibration reserves pages; it passes
   --defsym=TAG_CALIBRATION_PAGES=<n> from its family.mk. Every other L432
   target keeps the default of zero, so its layout is unchanged and its
   persistent region still runs to the end of flash.

   .persistent still begins at the first page after the code, so its start
   moves with code size. An upgrade must therefore erase it; see
   embedded/tags/families/CompassTag/README.md. */
PROVIDE(TAG_CALIBRATION_PAGES = 0);

SECTIONS
{
  __tag_flash_page_size__ = 2048;
  __tag_calibration_base__ = ORIGIN(flash0) + LENGTH(flash0) -
                             (TAG_CALIBRATION_PAGES * __tag_flash_page_size__);

  .nand_map (NOLOAD): ALIGN(2048)
  {
   __nand_map_start__ = .;
   KEEP(*(.nand_map))
   . = ALIGN(2048);
   __nand_map_end__ = .;
  } > flash0
  .persistent (NOLOAD): ALIGN(2048)
  {
   __persistent_start__	= .;
   *(.persistent)
   . = ALIGN(2048);
  }  > flash0
  /* Internal headers grow upward at runtime, bounded only by this address, so
     the region extends to the calibration pages, not merely over the declared
     objects. */
  __persistent_end__ = __tag_calibration_base__;
  .calibration __tag_calibration_base__ (NOLOAD):
  {
    __calibration_start__ = .;
    KEEP(*(.calibration))
    __calibration_end__ = .;
  } > flash0
}

/* Calibration storage needs reserved pages: a family that places a
   .calibration table must set TAG_CALIBRATION_PAGES, and the table must fit. */
ASSERT(SIZEOF(.calibration) <= TAG_CALIBRATION_PAGES * __tag_flash_page_size__,
       "STM32L432 .calibration does not fit its reserved pages: set --defsym=TAG_CALIBRATION_PAGES in the family.mk");

/* The image, and the persistent objects placed after it, must stay below the
   calibration pages. Without this the build would fail only once .persistent's
   location counter had to move backwards, with a less helpful message. */
ASSERT(LOADADDR(.data) + SIZEOF(.data) <= __persistent_start__ &&
       ADDR(.persistent) + SIZEOF(.persistent) <= __tag_calibration_base__,
       "STM32L432 code or persistent data reaches the calibration pages");

/* End of the loaded image: the later of flash-resident sections and the .data
   initialisers stored after them. A host hashes up to here. */
__tag_image_end__ = MAX(__flash0_free__, LOADADDR(.data) + SIZEOF(.data));

/* Every target built with the tag core module (modules/tag_core.mk) has a
   record; a program that links this script without it, such as stop1test,
   simply has none. Where there is one, it must directly follow the vectors. */
ASSERT(SIZEOF(.tag_identity) == 0 ||
       ADDR(.tag_identity) == ADDR(.vectors) + SIZEOF(.vectors),
       "the tag identity record must directly follow the vector table");
