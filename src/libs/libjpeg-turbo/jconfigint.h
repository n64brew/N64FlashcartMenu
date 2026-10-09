/* jconfigint.h — libjpeg-turbo 2.0.7 internal config for N64 (libdragon) */

#define BUILD  "0"
#define INLINE  __inline__ __attribute__((always_inline))
#define THREAD_LOCAL
#define PACKAGE_NAME  "libjpeg-turbo"
#define VERSION  "2.0.7"
#define SIZEOF_SIZE_T  4

#define HAVE_BUILTIN_CTZL

#if defined(__has_attribute)
#if __has_attribute(fallthrough)
#define FALLTHROUGH  __attribute__((fallthrough));
#else
#define FALLTHROUGH
#endif
#else
#define FALLTHROUGH
#endif
