/*
 * STM32CubeProgrammer external-loader link map for STM32U375 (256 KB SRAM).
 *
 * The programmer downloads the loadable segments of this ELF into SRAM and
 * calls the entry points it finds by symbol name, so everything -- code,
 * constants and data -- is linked to run from SRAM1, and nothing is placed in
 * internal flash. The image starts at 0x20000004, the origin ST's own loaders
 * use; the first word of SRAM is left to the programmer.
 *
 * StorageInfo goes in its own region at address 0, outside SRAM. That is
 * where STM32CubeProgrammer looks for it: ST's own loaders place it there
 * (the "Info" region of their IAR link maps), and a loader whose descriptor
 * sits anywhere else is ignored without any message -- -el is accepted and
 * reads silently bypass the loader. The programmer reads the descriptor from
 * the file; it is never downloaded to the target.
 *
 * The entry points and StorageInfo are not referenced by any code, so they
 * are kept explicitly; --gc-sections would otherwise discard them.
 *
 * .bss is NOLOAD: the programmer does not write it, and Init() clears it
 * between __loader_bss_start__ and __loader_bss_end__.
 *
 * On the U375 the first 64 bytes of SRAM1 are the firmware's monitor
 * mailbox (tags/common/STM32U375xG.ld). An image here overwrites them, which
 * is harmless: the loader runs only while the firmware is halted, and the
 * firmware's own boot initialises the mailbox.
 *
 * The loader provides no stack. The programmer sets SP before each call.
 * Loader code keeps its stack use small and allocates no large locals.
 */

MEMORY
{
    info (r)  : org = 0x00000000, len = 0x100      /* StorageInfo only */
    ram (rwx) : org = 0x20000004, len = 192k - 4  /* SRAM1 */
}

ENTRY(Init)

SECTIONS
{
    .dev_info : ALIGN(4)
    {
        KEEP(*(.dev_info*))
    } > info

    .text : ALIGN(4)
    {
        KEEP(*(.loader_entry*))
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.glue_7t)
        *(.glue_7)
        . = ALIGN(4);
    } > ram

    .data : ALIGN(4)
    {
        *(.data)
        *(.data.*)
        . = ALIGN(4);
    } > ram

    .bss (NOLOAD) : ALIGN(4)
    {
        __loader_bss_start__ = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        __loader_bss_end__ = .;
    } > ram

    __loader_end__ = .;

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
}
