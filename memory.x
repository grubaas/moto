/* Linker script for ESP32-C5 — Renode simulation layout
 *
 * The ESP32-C5 has 384 KB of unified HP SRAM at 0x4080_0000 – 0x4086_0000.
 * For simulation we split it into a code region (FLASH) and a data region (RAM).
 * Renode loads the ELF directly into SRAM — no flash or boot ROM is involved.
 */
MEMORY
{
    FLASH : ORIGIN = 0x40800000, LENGTH = 256K
    RAM   : ORIGIN = 0x40840000, LENGTH = 128K
}

REGION_ALIAS("REGION_TEXT",   FLASH);
REGION_ALIAS("REGION_RODATA", FLASH);
REGION_ALIAS("REGION_DATA",   RAM);
REGION_ALIAS("REGION_BSS",    RAM);
REGION_ALIAS("REGION_HEAP",   RAM);
REGION_ALIAS("REGION_STACK",  RAM);
