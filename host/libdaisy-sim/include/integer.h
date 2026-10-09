/* FatFs basic integer types, fixed-width so the host behaves like the 32-bit MCU. */
#ifndef _FF_INTEGER
#define _FF_INTEGER
#include <stdint.h>
typedef int                INT;
typedef unsigned int       UINT;
typedef unsigned char      BYTE;
typedef int16_t            SHORT;
typedef uint16_t           WORD;
typedef uint16_t           WCHAR;
typedef int32_t            LONG;
typedef uint32_t           DWORD;
typedef uint64_t           QWORD;
#endif
