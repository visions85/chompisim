/** Pre-included (via -include) in every DaisySP and firmware translation unit.
 *
 *  The firmware and DaisySP were written for arm-none-eabi-gcc, where common C
 *  names such as size_t arrive as a side effect of <stdint.h> or <math.h>.
 *  Newer C++ standard libraries (libc++ 19 on macOS, for example) declare
 *  exactly what each header promises and nothing more, so a file that uses
 *  size_t with only <stdint.h> included no longer compiles. Pulling in the
 *  basic C and C++ headers up front keeps the upstream sources untouched.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#endif
