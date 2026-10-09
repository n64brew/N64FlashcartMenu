/* jconfig.h — libjpeg-turbo 2.0.7 configuration for mips64-elf / N64 (libdragon)
 * Hand-written for a freestanding C99 environment.  No cmake required.
 */

#define JPEG_LIB_VERSION  62
#define LIBJPEG_TURBO_VERSION  "2.0.7"
#define LIBJPEG_TURBO_VERSION_NUMBER  020007

/* Support arithmetic decoding */
#define D_ARITH_CODING_SUPPORTED  1

/* Support in-memory source/destination managers */
#define MEM_SRCDST_SUPPORTED  1

/* No SIMD on VR4300 (MIPS III, no MSA/DSPr2/MMI) */
/* #undef WITH_SIMD */

#ifndef BITS_IN_JSAMPLE
#define BITS_IN_JSAMPLE  8
#endif

#define HAVE_LOCALE_H  1
#define HAVE_STDDEF_H  1
#define HAVE_STDLIB_H  1

#undef NEED_SYS_TYPES_H
#undef NEED_BSD_STRINGS

#define HAVE_UNSIGNED_CHAR  1
#define HAVE_UNSIGNED_SHORT  1

#undef INCOMPLETE_TYPES_BROKEN
#undef RIGHT_SHIFT_IS_UNSIGNED
#undef __CHAR_UNSIGNED__
