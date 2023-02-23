#ifndef __HIREDIS_FMACRO_H
#define __HIREDIS_FMACRO_H

#define _XOPEN_SOURCE 600
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#if defined(__APPLE__) && defined(__MACH__)
/* Enable TCP_KEEPALIVE */
#define _DARWIN_C_SOURCE
#endif

#endif
