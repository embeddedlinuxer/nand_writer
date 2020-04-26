-lrts64plus_elf.lib
-stack          0x00000800 /* Stack Size */  
-heap           0x00000800 /* Heap Size */

MEMORY
{
  DRAM        org=0xC0000000 len=0x04000000 /* SDRAM */
  L2RAM       org=0x11800000 len=0x00040000 /* DSP L2RAM */  
  SHARED_RAM  org=0x80000000 len=0x00020000 /* DDR for program */
  AEMIF       org=0x60000000 len=0x02000000 /* AEMIF CS2 region */
  AEMIF_CS3   org=0x62000000 len=0x02000000 /* AEMIF CS3 region */
}

SECTIONS
{
  .text :
  {
  } > SHARED_RAM
  .const :
  {
  } > SHARED_RAM
  .bss :
  {
  } > SHARED_RAM
  .far :
  {
  } > SHARED_RAM
  .stack :
  {
  } > SHARED_RAM
  .data :
  {
  } > SHARED_RAM
  .cinit :
  {
  } > SHARED_RAM
  .sysmem :
  {
  } > SHARED_RAM
  .cio :
  {
  } > SHARED_RAM
  .switch :
  {
  } > SHARED_RAM
  .aemif_mem :
  {
  } > AEMIF_CS3, RUN_START(_NANDStart)
  .ddrram	 :
  {
    . += 0x04000000;
  } > DRAM, type=DSECT, RUN_START(_EXTERNAL_RAM_START), RUN_END(_EXTERNAL_RAM_END)
}

EXTERNAL_RAM_START = 0xc0020000;

EXTERNAL_RAM_END	= 0xc0040000;

