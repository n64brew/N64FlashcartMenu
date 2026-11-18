/* Writable disk staging is used only by the Pro host tests. */
#ifndef ED64_PRO_HOST_COMPAT_H
#define ED64_PRO_HOST_COMPAT_H
#include <fatfs/ff.h>
#ifndef FA_WRITE
#define FA_WRITE 2
#endif
#endif
