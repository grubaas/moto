/* Linker script for i.MX RT1062 (Teensy 4.1) — Renode simulation layout
 *
 * FLASH = ITCM (Instruction Tightly Coupled Memory) — code executes here
 * RAM   = DTCM (Data Tightly Coupled Memory)         — stack + data live here
 *
 * On real hardware the boot ROM copies from external flash (0x6000_0000)
 * into ITCM.  For Renode simulation we load the ELF directly into ITCM.
 */
MEMORY
{
    FLASH : ORIGIN = 0x00000000, LENGTH = 512K
    RAM   : ORIGIN = 0x20000000, LENGTH = 512K
}
