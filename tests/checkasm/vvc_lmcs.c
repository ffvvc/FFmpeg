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

static void check_vvc_lmcs_16bpc_8pixels(void)
{
    const int bit_depth = 10;
    VVCDSPContext c;
    ff_vvc_dsp_init(&c, bit_depth);

    LOCAL_ALIGNED_32(uint16_t, src0, [MAX_CTU_SIZE * MAX_CTU_SIZE * 2]);
    LOCAL_ALIGNED_32(uint16_t, src1, [MAX_CTU_SIZE * MAX_CTU_SIZE * 2]);
    LOCAL_ALIGNED_32(uint16_t, lut0,  [1024 * 2]);
    LOCAL_ALIGNED_32(uint16_t, lut1,  [1024 * 2]);

    memset(src0, 0, MAX_CTU_SIZE * MAX_CTU_SIZE * 2);
    memset(src1, 0, MAX_CTU_SIZE * MAX_CTU_SIZE * 2);
    memset(lut0, 0, 1024*2);
    memset(lut0, 0, 1024*2);

    randomize_pixels(src0, src1, MAX_CTU_SIZE * MAX_CTU_SIZE * 2);
    randomize_pixels(lut0, lut1, 1024*2);

    static const int ctu_sizes[] = { 8 };

    declare_func_emms(AV_CPU_FLAG_AVX, void, uint16_t *_dst, ptrdiff_t dst_stride, const int width, const int height, const uint16_t *_lut);

    if(check_func(c.lmcs.filter[1], "ff_16bpc_8_pixels_test"))
    {
        for(int ctu_size_idx = 0; ctu_size_idx < 1; ++ctu_size_idx)
        {
            for(int i = 0; i < MAX_CTU_SIZE; i+=ctu_sizes[ctu_size_idx])
            {
                const int random_height = (rnd() % MAX_CTU_SIZE + 1) * 2;
                call_new(src1 + i, MAX_CTU_SIZE*2, ctu_sizes[ctu_size_idx], random_height, lut0);
                call_ref(src0 + i, MAX_CTU_SIZE*2, ctu_sizes[ctu_size_idx], random_height, lut0);
            }
            if(memcmp(src0, src1, MAX_CTU_SIZE * MAX_CTU_SIZE * 2 ))
            {
                fail();
            }
        }
    }
}

static void check_vvc_lmcs_16bpc(void)
{
    const int bit_depth = 10;
    VVCDSPContext c;
    ff_vvc_dsp_init(&c, bit_depth);

    LOCAL_ALIGNED_32(uint16_t, src0, [MAX_CTU_SIZE * MAX_CTU_SIZE * 2]);
    LOCAL_ALIGNED_32(uint16_t, src1, [MAX_CTU_SIZE * MAX_CTU_SIZE * 2]);
    LOCAL_ALIGNED_32(uint16_t, lut0,  [1024 * 2]);
    LOCAL_ALIGNED_32(uint16_t, lut1,  [1024 * 2]);

    memset(src0, 0, MAX_CTU_SIZE * MAX_CTU_SIZE * 2);
    memset(src1, 0, MAX_CTU_SIZE * MAX_CTU_SIZE * 2);
    memset(lut0, 0, 1024 * 2);
    memset(lut0, 0, 1024 * 2);

    randomize_pixels(src0, src1, MAX_CTU_SIZE * MAX_CTU_SIZE * 2);
    randomize_pixels(lut0, lut1, 1024 * 2);

    static const int ctu_sizes[] = { 16, 32, 64, 128 };

    declare_func_emms(AV_CPU_FLAG_AVX, void, uint16_t *_dst, ptrdiff_t dst_stride, const int width, const int height, const uint16_t *_lut);

    if(check_func(c.lmcs.filter[2], "ff_16bpc_test"))
    {
        for(int ctu_size_idx = 0; ctu_size_idx < 4; ++ctu_size_idx)
        {
            for(int i = 0; i < MAX_CTU_SIZE; i+=ctu_sizes[ctu_size_idx])
            {
                const int random_height = (rnd() % MAX_CTU_SIZE + 1) * 2;
                call_new(src1 + i, MAX_CTU_SIZE*2, ctu_sizes[ctu_size_idx], random_height, lut0);
                call_ref(src0 + i, MAX_CTU_SIZE*2, ctu_sizes[ctu_size_idx], random_height, lut0);
            }
            if(memcmp(src0, src1, MAX_CTU_SIZE * MAX_CTU_SIZE * 2 ))
            {
                fail();
            }
        }
    }
}

void checkasm_check_vvc_lmcs(void)
{
    check_vvc_lmcs_16bpc_8pixels();
    check_vvc_lmcs_16bpc();
}
