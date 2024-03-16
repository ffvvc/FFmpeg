%include "libavutil/x86/x86util.asm"

SECTION_RODATA 32

pb_shufmask:   db 0, 1, 4, 5, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1

SECTION .text

; Only handles widths 16 to 128
INIT_YMM avx2

cglobal lmcs_16bpc, 5, 8, 13, src, src_stride, width, height, lut, src_pt, cnt, src2
    pxor             m0, m0 ; 0 register
    movu             m12, [pb_shufmask]

    .loopheight:
        mov             src_ptq, srcq ; saveptr
        lea             src2q, [srcq + src_strideq]

        prefetcht0       [src2q + src_strideq]
        prefetcht0       [src2q + src_strideq * 2]

        mov             cntd, widthd
        shr             cntd, mmsize / 8 ; width loop counter

        .loopwidth:
            prefetcht0       [srcq  + mmsize]  ; arbitrary prefetch location
            prefetcht0       [src2q + mmsize]

            ; first row
            movu             m1, [srcq]
            punpcklwd        m2, m1, m0
            punpckhwd        m3, m1, m0

            vpcmpeqd         m6, m6, m6
            vpgatherdd       m4, [lutq + m2 * 2], m6
            vpcmpeqd         m6, m6, m6              
            vpgatherdd       m5, [lutq + m3 * 2], m6
            ; m4, m5 contain final data
            
            ; second row
            movu             m7, [src2q]
            punpcklwd        m8, m7, m0 
            punpckhwd        m9, m7, m0

            vpcmpeqd         m0, m0, m0
            vpgatherdd       m10, [lutq + m8 * 2], m0
            vpcmpeqd         m0, m0, m0
            vpgatherdd       m11, [lutq + m9 * 2], m0

            ; shuf off garbage data
            pshufb          m4,  m12
            pshufb          m5,  m12
            pshufb          m10, m12
            pshufb          m11, m12

            ; repack data
            punpcklqdq       m4, m5
            punpcklqdq      m10, m11

            ; write out
            movu             [srcq], m4
            movu             [src2q], m10

            ; load next and increment if necessary
            lea              srcq, [srcq + mmsize]
            lea              src2q, [src2q + mmsize]
            dec cntd
            jg .loopwidth

        lea             srcq, [src_ptq + src_strideq * 2]
        sub             heightq, 2
        jg .loopheight

    RET

SECTION .text
INIT_YMM avx2


INIT_YMM avx2
cglobal lmcs_16bpc_8pix, 5, 9, 12, src, src_stride, width, height, lut, src_pt, cnt, src2 ,halfwidth
    pxor             m0, m0 ; 0 register
    movu             m12, [pb_shufmask]

    .loopheight:
        mov             src_ptq, srcq ; saveptr
        lea             src2q, [srcq + src_strideq]

        prefetcht0       [src2q + src_strideq]
        prefetcht0       [src2q + src_strideq * 2]

        mov             cntd, 1

        .loopwidth:
            ; rows 1 & 2
            movu             xm1, [srcq]
            movu             xm2, [src2q]
            vperm2i128       m1, m1, m2, q0200 ; pack two row
            punpcklwd        m2, m1, m0
            punpckhwd        m3, m1, m0

            vpcmpeqd         m6, m6, m6
            vpgatherdd       m4, [lutq + m2 * 2], m6
            vpcmpeqd         m6, m6, m6
            vpgatherdd       m5, [lutq + m3 * 2], m6
            
            ; rows 3 & 4
            movu             xm7, [src2q + src_strideq]
            movu             xm8, [src2q + src_strideq * 2]
            vperm2i128       m7, m7, m8, q0200
            punpcklwd        m8, m7, m0 
            punpckhwd        m9, m7, m0

            vpcmpeqd         m0, m0, m0
            vpgatherdd       m10, [lutq + m8 * 2], m0
            vpcmpeqd         m0, m0, m0
            vpgatherdd       m11, [lutq + m9 * 2], m0

            ; shuf off garbage data
            pshufb          m4, m12
            pshufb          m5, m12
            pshufb          m10,m12
            pshufb          m11,m12

            ; repack data
            punpcklqdq       m4, m5
            punpcklqdq      m10, m11

            ; write out
            movu            [srcq], xm4
            vperm2i128      m4, m4, m4, q1001
            movu            [src2q], xm4

            movu            [src2q + src_strideq], xm10
            vperm2i128      m10, m10, m10, q1001
            movu            [src2q + src_strideq * 2], xm10


        lea             srcq, [src_ptq + src_strideq * 4]
        sub             heightq, 4
        jg .loopheight

    RET


INIT_YMM avx2
cglobal lmcs_8bpc, 5, 7, 13, src, src_stride, width, height, lut, src_pt, cnt
    pxor             m0, m0 ; 0 register
    movu            m12, [pb_shufmask]

    .loopheight:
        mov             src_ptq, srcq ; saveptr

        mov             cntd, widthd
        shr             cntd, mmsize / 4; width 32

        .loopwidth:
            prefetcht0       [srcq  + mmsize]  ; sort of arbitrary...

            movu             m12, [srcq]
            punpcklbw        m1, m12, m0
            punpckhbw        m7, m12, m0

            ; first half
            punpcklwd        m2, m1, m0
            punpckhwd        m3, m1, m0

            vpcmpeqd         m6, m6, m6
            vpgatherdd       m4, [lutq + m2 * 2], m6
            vpcmpeqd         m6, m6, m6
            vpgatherdd       m5, [lutq + m3 * 2], m6
            
            ; second half
            ; movu             m7, [src2q]
            punpcklwd        m8, m7, m0 
            punpckhwd        m9, m7, m0

            vpcmpeqd         m0, m0, m0
            vpgatherdd       m10, [lutq + m8 * 2], m0
            vpcmpeqd         m0, m0, m0
            vpgatherdd       m11, [lutq + m9 * 2], m0

            ; shuf off garbage data
            pshufb          m4,  m12
            pshufb          m5,  m12
            pshufb          m10, m12
            pshufb          m11, m12

            ; repack data
            punpcklqdq       m4, m5
            punpcklqdq      m10, m11

            vperm2i128      m5, m4, m10, q0200

            ; write out
            movu             [srcq], m5

            ; load next and increment if necessary
            lea              srcq, [srcq + mmsize]
            dec cntd
            jg .loopwidth

        lea             srcq, [src_ptq + src_strideq]
        dec             heightq
        jg .loopheight

    RET
