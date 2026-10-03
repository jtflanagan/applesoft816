ENTRY(__cc_entry)
/* The compiler on the 65816: two banks, any two consecutive ones.
 *
 *   code bank  $0000  header and entry (crt0.s), code, then the data image
 *   data bank  $0200  .rodata and .data (copied from the image on entry),
 *                     .bss and .noinit after them; the soft stack from $FF00 down
 *
 * The compiler addresses every global with a 16-bit operand, so globals are
 * found through DBR = the data bank; code is JSR/JMP within the code bank.
 * Nothing carries a bank byte, so the pair loads anywhere.  Page zero
 * holds only the imaginary registers, $00-$1F, with D = 0 (crt0 saves the
 * caller's). */
__rc0 = 0x00;
INCLUDE imag-regs.ld
ASSERT(__rc31 == 0x1f, "the imaginary registers must be $00-$1F")

MEMORY {
  zp   : ORIGIN = 0x0020,   LENGTH = 0
  code : ORIGIN = 0x010000, LENGTH = 0x10000
  ram  : ORIGIN = 0x020200, LENGTH = 0xDE00
}

REGION_ALIAS("c_readonly", ram)
REGION_ALIAS("c_writeable", ram)

SECTIONS {
  .entry : { KEEP(*(.entry)) } >code
  .text  : { *(.text .text.*) } >code
  .rodata : { INCLUDE rodata-sections.ld } >ram AT>code
  .data   : { INCLUDE data-sections.ld }   >ram AT>code
  .bss (NOLOAD)    : { INCLUDE bss-sections.ld }    >ram
  .noinit (NOLOAD) : { INCLUDE noinit-sections.ld } >ram
  INCLUDE zp.ld
}

__copy_lma  = LOADADDR(.rodata) & 0xFFFF;
__copy_lma_lo = __copy_lma & 0xFF;
__copy_lma_hi = (__copy_lma >> 8) & 0xFF;
__copy_vma  = ADDR(.rodata) & 0xFFFF;
__copy_vma_lo = __copy_vma & 0xFF;
__copy_vma_hi = (__copy_vma >> 8) & 0xFF;
__copy_size = SIZEOF(.rodata) + SIZEOF(.data);
__bss_off   = ADDR(.bss) & 0xFFFF;
__bss_off_lo = __bss_off & 0xFF;
__bss_off_hi = (__bss_off >> 8) & 0xFF;
__bss_size  = SIZEOF(.bss);              /* (.noinit keeps crt0's save of page zero) */
__image_end = (LOADADDR(.data) + SIZEOF(.data)) & 0xFFFF;
__stack_lo  = 0x00;
__stack_hi  = 0xFF;

ASSERT((ADDR(.noinit) + SIZEOF(.noinit)) <= 0x02F000, "the compiler's data runs into its stack")

OUTPUT_FORMAT { TRIM(code) }
