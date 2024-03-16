#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/avcodec.h"
#include "checkasm.h"
#include "libavcodec/vvc/vvc_ctu.h"

static const uint32_t pixel_mask[] = { 0xffffffff, 0x03ff03ff, 0x0fff0fff };

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)

#define randomize_buffers(buf0, buf1, size, mask)           \
    do {                                                    \
        int k;                                              \
        for (k = 0; k < size; k += 2) {                     \
            uint32_t r = rnd() & mask;                      \
            AV_WN32A(buf0 + k, r);                          \
            AV_WN32A(buf1 + k, r);                          \
        }                                                   \
    } while (0)

#define randomize_pixels(buf0, buf1, size)                  \
    do {                                                    \
        uint32_t mask = pixel_mask[(bit_depth - 8) >> 1];   \
        randomize_buffers(buf0, buf1, size, mask);          \
    } while (0)

void checkasm_check_vvc_lmcs(void)
{
}
