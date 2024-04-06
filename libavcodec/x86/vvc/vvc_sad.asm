; sad but specifically for dmvr which restricts applicable PU sizes and only does even rows to reduce complexity
; w >= 8, h >= 8 and w*h >= 128

%include "libavutil/x86/x86util.asm"


SECTION .text

%macro MIN_MAX_SAD 3 ; 
    vpminuw           %1, %2, %3
    vpmaxuw           %3, %2, %3
    vpsubusw          %3, %3, %1
%endmacro

%macro HORIZONTAL_ADD 3  ; xm0, xm1, m1
    vextracti128      %1, %3 , q0001 ;        3        2      1          0
    vpaddd            %1, %2         ; xm0 (7 + 3) (6 + 2) (5 + 1)   (4 + 0)
    vpshufd           %2, %1, q0032  ; xm1    -      -     (7 + 3)   (6 + 2)
    vpaddd            %1, %1, %2     ; xm0    _      _     (5 1 7 3) (4 0 6 2)
    vpshufd           %2, %1, q0001  ; xm1    _      _     (5 1 7 3) (5 1 7 3)
    vpaddd            %1, %1, %2     ;                               (01234567)
%endmacro

%macro INIT_OFFSET 6 ; src1, src2, dxq, dyq, off1, off2

    sub             %3, 2
    sub             %4, 2

    mov             %5, 2
    mov             %6, 2

    add             %5, %4   
    sub             %6, %4

    imul            %5, 128
    imul            %6, 128

    add             %5, 2
    add             %6, 2
    
    add             %5, %3
    sub             %6, %3

    lea             %1, [%1 + %5 * 2]
    lea             %2, [%2 + %6 * 2]

%endmacro


INIT_YMM avx2
; technically not valid since 8x8 = 64 < 128 8x16 is valid though
cglobal vvc_sad_8_16bpc, 6, 8, 13, src1, src2, dx, dy, block_w, block_h, off1, off2

    INIT_OFFSET src1q, src2q, dxq, dyq, off1q, off2q
    pxor               m3, m3

    .loop_height:
        movu              xm0, [src1q]
        movu              xm1, [src2q]
        MIN_MAX_SAD xm2, xm0, xm1
        vpmovzxwd          m1, xm1
        vpaddd             m3, m1

        movu              xm5, [src1q + 128 * 2 * 2]
        movu              xm6, [src2q + 128 * 2 * 2]
        MIN_MAX_SAD xm7, xm5, xm6
        vpmovzxwd          m6, xm6
        vpaddd             m3, m6

        movu              xm8, [src1q + 128 * 4 * 2]
        movu              xm9, [src2q + 128 * 4 * 2]
        MIN_MAX_SAD xm10, xm8, xm9
        vpmovzxwd          m9, xm9
        vpaddd             m3, m9

        movu              xm11, [src1q + 128 * 6 * 2]
        movu              xm12, [src2q + 128 * 6 * 2]
        MIN_MAX_SAD       xm13, xm11, xm12
        vpmovzxwd          m12, xm12

        vpaddd              m3, m12

        add             src1q, 8 * 128 * 2 
        add             src2q, 8 * 128 * 2

        sub          block_hd, 8
        jg  .loop_height

        HORIZONTAL_ADD xm0, xm3, m3
        movd              eax, xm0
    RET


cglobal vvc_sad_16_16bpc, 6, 9, 13, src1, src2, dx, dy, block_w, block_h, off1, off2, sad
    INIT_OFFSET src1q, src2q, dxq, dyq, off1q, off2q
    pxor               m8, m8
.load_pixels:
        movu              xm0, [src1q]
        movu              xm1, [src2q]
        MIN_MAX_SAD       xm2, xm0, xm1
        vpmovzxwd          m1, xm1
        vpaddd             m8, m1

        movu              xm5, [src1q + 16]
        movu              xm6, [src2q + 16]
        MIN_MAX_SAD       xm7, xm5, xm6
        vpmovzxwd          m6, xm6
        vpaddd             m8, m6

        add             src1q, (2 * 128) * 2 
        add             src2q, (2 * 128) * 2

        sub  block_hd, 2
        jg  .load_pixels

        HORIZONTAL_ADD xm0, xm8, m8
        movd              eax, xm0

    RET

cglobal vvc_sad_32_16bpc, 6, 9, 13, src1, src2, dx, dy, block_w, block_h, off1, off2, row_idx
    INIT_OFFSET src1q, src2q, dxq, dyq, off1q, off2q
    pxor                 m3, m3

.loop_height:

    mov               off1q, src1q
    mov               off2q, src2q

    .loop_row:
        movu              xm0, [src1q]
        movu              xm1, [src2q]
        MIN_MAX_SAD       xm2, xm0, xm1
        vpmovzxwd          m1, xm1
        vpaddd             m3, m1

        movu              xm5, [src1q + 16]
        movu              xm6, [src2q + 16]
        MIN_MAX_SAD       xm7, xm5, xm6
        vpmovzxwd          m6, xm6
        vpaddd             m3, m6

        movu              xm8, [src1q + 32]
        movu              xm9, [src2q + 32]
        MIN_MAX_SAD       xm10, xm8, xm9
        vpmovzxwd          m9, xm9
        vpaddd             m3, m9

        movu              xm11, [src1q + 48]
        movu              xm12, [src2q + 48]
        MIN_MAX_SAD       xm13, xm11, xm12
        vpmovzxwd          m12, xm12
        vpaddd             m3, m12

    lea             src1q, [off1q + 2 * 128 * 2] 
    lea             src2q, [off2q + 2 * 128 * 2]

    sub block_hq, 2
    jg .loop_height

    HORIZONTAL_ADD xm0, xm3, m3
    movd              eax, xm0

    RET

cglobal vvc_sad_64_16bpc, 6, 9, 13, src1, src2, dx, dy, block_w, block_h, off1, off2, row_idx
    INIT_OFFSET src1q, src2q, dxq, dyq, off1q, off2q
    pxor                 m3, m3

.loop_height:

    mov               off1q, src1q
    mov               off2q, src2q
    mov            row_idxd, 2

    .loop_row:
        movu              xm0, [src1q]
        movu              xm1, [src2q]
        MIN_MAX_SAD       xm2, xm0, xm1
        vpmovzxwd          m1, xm1
        vpaddd             m3, m1

        movu              xm5, [src1q + 16]
        movu              xm6, [src2q + 16]
        MIN_MAX_SAD       xm7, xm5, xm6
        vpmovzxwd          m6, xm6
        vpaddd             m3, m6

        movu              xm8, [src1q + 32]
        movu              xm9, [src2q + 32]
        MIN_MAX_SAD      xm10, xm8, xm9
        vpmovzxwd          m9, xm9
        vpaddd             m3, m9

        movu              xm11, [src1q + 48]
        movu              xm12, [src2q + 48]
        MIN_MAX_SAD       xm13, xm11, xm12
        vpmovzxwd          m12, xm12
        vpaddd              m3, m12

        add              src1q, 64
        add              src2q, 64
        dec           row_idxd
        jg  .loop_row

    lea             src1q, [off1q + 2 * 128 * 2] 
    lea             src2q, [off2q + 2 * 128 * 2]

    sub block_hq, 2
    jg .loop_height

    HORIZONTAL_ADD xm0, xm3, m3
    movd              eax, xm0

    RET

cglobal vvc_sad_128_16bpc, 6, 9, 13, src1, src2, dx, dy, block_w, block_h, off1, off2, row_idx
    INIT_OFFSET src1q, src2q, dxq, dyq, off1q, off2q
    pxor                 m3, m3
.loop_height:

    mov               off1q, src1q
    mov               off2q, src2q
    mov            row_idxd, 4

    .loop_row:
        movu              xm0, [src1q]
        movu              xm1, [src2q]
        MIN_MAX_SAD       xm2, xm0, xm1
        vpmovzxwd          m1, xm1
        vpaddd             m3, m1

        movu              xm5, [src1q + 16]
        movu              xm6, [src2q + 16]
        MIN_MAX_SAD       xm7, xm5, xm6
        vpmovzxwd          m6, xm6
        vpaddd             m3, m6

        movu              xm8, [src1q + 32]
        movu              xm9, [src2q + 32]
        MIN_MAX_SAD       xm10, xm8, xm9
        vpmovzxwd          m9, xm9
        vpaddd             m3, m9

        movu              xm11, [src1q + 48]
        movu              xm12, [src2q + 48]
        MIN_MAX_SAD       xm13, xm11, xm12
        vpmovzxwd          m12, xm12
        vpaddd             m3, m12

        add             src1q, 64
        add             src2q, 64
        dec          row_idxd
        jg  .loop_row

    lea             src1q, [off1q + 2 * 128 * 2] 
    lea             src2q, [off2q + 2 * 128 * 2]

    sub block_hq, 2
    jg .loop_height

    HORIZONTAL_ADD    xm0, xm3, m3
    movd              eax, xm0

    RET
