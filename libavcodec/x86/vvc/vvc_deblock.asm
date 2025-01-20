; from hevc_deblock.asm, grap all the tranpose macros

%include "libavutil/x86/x86util.asm"
%include "libavcodec/x86/h26x/h2656_deblock.asm"

%macro LUMA_DEBLOCK_BODY 2
    H2656_LUMA_DEBLOCK_BODY vvc, %1, %2
%endmacro

SECTION_RODATA

cextern pw_1023
%define pw_pixel_max_10 pw_1023
pw_pixel_max_12: times 8 dw ((1 << 12)-1)
pw_2 :           times 8 dw  2
pw_m2:           times 8 dw -2
pw_1 :           times 8 dw  1
pw_5 :           times 8 dw  5
pd_3 :           times 4 dd  3
pd_1 :           times 4 dd 1

cextern pw_4
cextern pw_8
cextern pw_4096
cextern pw_m1

SECTION .text

%macro SHUFFLE_ON_SHIFT 2 ; dst, src
    cmp shiftd, 1
    je        %%no_shift
    punpcklqdq       %2, %2, %2
    pshufhw          %1, %2, q2222
    pshuflw          %1, %1, q0000
    jmp             %%end
%%no_shift:
    pshufhw          %1, %2, q2301
    pshuflw          %1, %1, q2301
%%end:
%endmacro

%macro SHUFFLE_ON_SHIFT2 2
    cmp          shiftd, 1
    je            %%end
    punpcklqdq       %1, %1, %1
    pshufhw          %2, %1, q2222
    pshuflw          %2, %2, q0000
    movu             %1, %2
%%end:
%endmacro

ALIGN 16
%macro WEAK_CHROMA 0
    H2656_CHROMA_DEBLOCK m14, m15, m2, m3, m4, m5, m8, m9, m12, m13
%endmacro

%macro CHROMA_FILTER 1 ;(dst)
    paddw               m15, m14
    paddw               m15, m13
    H2656_CHROMA_ROUND  m15

    psubw               m15, %1
    CLIPW               m15, m8, m9 ; clip to [-tc, tc]
    paddw               m15, %1

    MASKED_COPY          %1, m15
%endmacro

%macro STRONG_CHROMA 1
    mova            m10, m1          ; save p2
    mova            m12, m2          ; save p1

    paddw           m14, m1, m2      ; p2 + p1
    paddw           m13, m3, m4      ; p0 + q0
    paddw           m13, m14         ; p2 + p1 + p0 + q0

    cmp            no_pq, 0
    je      .end_p_calcs
    pand             m11, [rsp + 16] ; which p

    ; P2
    paddw          m14, m0, m0       ; 2 * p3
    paddw          m15, m0, m1       ; p3 + p2
    CHROMA_FILTER  m1

    ; P1
    paddw          m15, m2, m5       ; p1 + q1
    CHROMA_FILTER  m2

    ; P0
    paddw          m14, m0, m3       ; p3 + p0
    paddw          m15, m5, m6       ; q1 + q2
    CHROMA_FILTER  m3
.end_p_calcs:

    cmp            no_qq, 0
    je      .end_q_calcs
    movu             m11, [rsp + 32] ; strong
    pand             m11, [rsp ]     ; strong & q

    ; Q0
    paddw          m14, m4, m5       ; q0 + q1
    paddw          m15, m6, m7       ; q2 + q3
    CHROMA_FILTER  m4

    ; Q1
    psubw          m10, m5           ; p2 - q1
    psubw          m13, m10          ; p1 + p0 + q0 + q1
    paddw          m14, m7, m7       ; 2 * q3
    paddw          m15, m5, m6       ; q1 + q2
    CHROMA_FILTER  m5

    ; Q2
    psubw          m12, m6           ; p1 - q2
    psubw          m13, m12          ; p0 + q0 + q1 + q2
    paddw          m15, m6, m7       ; q2 + q3
    CHROMA_FILTER  m6

.end_q_calcs:
%endmacro

; m11 strong mask, m8/m9 -tc, tc
%macro SPATIAL_ACTIVITY 1

    ; if p == 1, then p3, p2 are p1 for spatial calc
    pxor              m10, m10
    movd              m11, [max_len_pq]
    punpcklbw         m11, m11, m10
    punpcklwd         m11, m11, m10

    pcmpeqd           m11, [pd_1]

    SHUFFLE_ON_SHIFT2 m11, m13

    movu       [rsp + 48], m0
    movu       [rsp + 64], m1

    movu              m12, m2
    movu              m13, m2
    MASKED_COPY        m0, m12
    MASKED_COPY        m1, m13

; load tc
.load_tc:
    movu               m8, [tcq]
%if %1 == 8
    paddw              m8, [pw_2]
    psrlw              m8, 2
%elif %1 == 12
    psllw              m8, %1 - 10;
%endif
    cmp             shiftd, 1
    je      .tc_load_shift

    punpcklqdq          m8, m8, m8
    pshufhw             m8, m8, q2222
    pshuflw             m8, m8, q0000
    jmp       .end_tc_load
.tc_load_shift:
    pshufhw             m8, m8,  q2200
    pshuflw             m8, m8,  q2200
.end_tc_load:
    movu          [tcptrq], m8

    ; if max_len_q == 3, compute spatial activity to determine final length
    pxor               m10, m10
    movd               m11, [max_len_qq]
    punpcklbw          m11, m11, m10
    punpcklwd          m11, m11, m10

    pcmpeqd            m11, [pd_3];
    SHUFFLE_ON_SHIFT2  m11, m13

    movu   [spatial_maskq], m11

    movmskps           r14, m11
    cmp                r14, 0
    je         .final_mask

.tc25_calculation:
    pmullw              m8, [pw_5]
    paddw               m8, [pw_1]
    psrlw               m8, 1          ; ((tc * 5 + 1) >> 1);

    psubw              m12, m3, m4     ;      p0 - q0
    ABS1               m12, m14        ; abs(p0 - q0)

    cmp             shiftd, 1
    je  .tc25_mask

    pshufhw            m12, m12, q3300
    pshuflw            m12, m12, q3300

.tc25_mask:
    pcmpgtw            m15, m8, m12
    pand               m11, m15

; dsam
    psllw               m9, m2, 1
    psubw              m10, m1, m9
    paddw              m10, m3
    ABS1               m10, m12

    psllw               m9, m5, 1
    psubw              m12, m6, m9
    paddw              m12, m4
    ABS1               m12, m13

    paddw               m9, m10, m12  ; m9 spatial activity sum for all lines
; end dsam

; Load beta
    movu               m12, [betaq]
%if %1 > 8
    psllw              m12, %1 - 8   ; replace with bit_depth
%endif

    cmp             shiftd, 1
    je    .beta_load_shift

    punpcklqdq         m12,  m12, m12
    pshufhw            m13,  m12, q2222
    pshuflw            m13, m13, q0000

; dsam calcs
    pshufhw            m14,  m9, q0033
    pshuflw            m14, m14, q0033
    pshufhw             m9,  m9, q3300
    pshuflw             m9,  m9, q3300

    jmp  .spatial_activity

.beta_load_shift:
    pshufhw            m13, m12,  q2200
    pshuflw            m13, m13,  q2200

    movu               m14, m9
    pshufhw             m9,  m9, q2301
    pshuflw             m9,  m9, q2301

.spatial_activity:
    paddw               m14, m9          ; d0 + d3, d0 + d3, d0 + d3, ...
    pcmpgtw             m15, m13, m14    ; beta > d0 + d3, d0 + d3 (next block)
    pand                m11, m15         ; save filtering and or at the end

    ; beta_2
    psraw               m15, m13, 2   ; beta >> 2
    psllw                m8, m9, 1    ;  d0, d1, d2, d3, ...

    pcmpgtw             m15, m8       ; d0 ..  < beta_2, d0... < beta_2, d3... <
    pand                m11, m15

.beta3_comparison:
    psubw               m12, m0, m3  ; p3 - p0
    ABS1                m12, m14     ; abs(p3 - p0)

    psubw               m15, m7, m4  ; q3 - q0
    ABS1                m15, m14     ; abs(q3 - q0)

    paddw               m12, m15     ; abs(p3 - p0) + abs(q3 - q0)

    psraw               m13, 3       ; beta >> 3

    cmp              shiftd, 1
    je    .beta3_no_first_shuffle

    pshufhw             m12, m12, q3300
    pshuflw             m12, m12, q3300
.beta3_no_first_shuffle:
    pcmpgtw             m13, m12
    pand                m11, m13

.final_mask:
    movu                m15, m11
    cmp              shiftd, 1
    je    .final_shift_mask

    pshufhw             m15, m15, q0033
    pshuflw             m15, m15, q0033
    pand                m11, m15
    jmp     .final_mask_end

.final_shift_mask:
    pshufhw             m15, m15, q2301
    pshuflw             m15, m15, q2301
    pand                m11, m15
.final_mask_end:

.prep_clipping_masks:
    movu         [spatial_maskq], m11
    movu                      m9, [tcptrq]
    psignw                    m8, m9, [pw_m1];

    movu             m0, [rsp + 48]
    movu             m1, [rsp + 64]

%endmacro

%macro ONE_SIDE_CHROMA 1
    pand       m11, [rsp + 16]      ; no_p

    paddw          m12, m3, m4      ; p0 + q0
    paddw          m13, m5, m6      ; q1 + q2
    paddw          m13, m12         ; p0 + q0 + q1 + q2

    ; P0
    paddw          m14, m2, m2      ; 2 * p1
    paddw          m15, m2, m3      ; p1 + p0
    CHROMA_FILTER  m3

    movu         m11, [rsp + 32]    ; strong mask
    pand         m11, [rsp]         ; no_q

    ; Q0
    paddw          m15, m4, m7      ; q0 + q3
    CHROMA_FILTER  m4

    ; Q1
    paddw          m14, m7, m7      ; 2 * q3
    paddw          m15, m2, m5      ; p1 + q1
    CHROMA_FILTER  m5

    ; Q2
    paddw          m15, m6, m7      ; q2 + q3
    CHROMA_FILTER  m6
%endmacro

; CHROMA_DEBLOCK_BODY(bit_depth)
%macro CHROMA_DEBLOCK_BODY 1
    sub  rsp, 16
    mov spatial_maskq, rsp
    sub rsp, 16
    mov tcptrq, rsp
    sub rsp, 16  ; rsp + 64 = spatial activity storage
    sub rsp, 16  ; rsp + 48 = spatial_activity storage
    sub rsp, 16  ; rsp + 32 = strong mask
    sub rsp, 16  ; rsp + 16 = no_p
    sub rsp, 16  ; rsp      = no_q

    ; no_p
    pxor            m10, m10
    movd            m11, [no_pq]  ; 1 means skip
    punpcklbw       m11, m11, m10
    punpcklwd       m11, m11, m10

    pcmpeqd         m11, m10      ; calculate p mask
    movmskps      no_pq, m11

    SHUFFLE_ON_SHIFT m13, m11
    movu      [rsp + 16], m13

    ; load max_len_q
    pxor            m10, m10
    movd            m11, [max_len_pq]
    punpcklbw       m11, m11, m10
    punpcklwd       m11, m11, m10

    pcmpeqd         m11, m10 ; which are 0
    pcmpeqd         m12, m12, m12
    pxor            m11, m11, m12

    SHUFFLE_ON_SHIFT m13, m11

    pand             m13, [rsp + 16]
    movu             [rsp + 16], m13
    movmskps         no_pq, m13

    ; no_q
    pxor            m10, m10
    movd            m11, [no_qq]
    punpcklbw       m11, m11, m10
    punpcklwd       m11, m11, m10

    pcmpeqd         m11, m10;
    movmskps      no_qq, m11;

    SHUFFLE_ON_SHIFT m13, m11
    movu           [rsp], m13

    pxor            m10, m10
    movd            m11, [max_len_qq]
    punpcklbw       m11, m11, m10
    punpcklwd       m11, m11, m10

    pcmpeqd         m11, m10 ; which are 0
    pcmpeqd         m12, m12, m12
    pcmpeqd         m11, m10

    SHUFFLE_ON_SHIFT   m13, m11
    pand               m13, [rsp]
    movu             [rsp], m13
    movmskps         no_qq, m13
; end q

    SPATIAL_ACTIVITY %1

    movmskps         r14, m11
    cmp              r14, 0
    je              .chroma_weak

    pxor            m10, m10
    movd            m11, [max_len_pq]
    punpcklbw       m11, m11, m10
    punpcklwd       m11, m11, m10

    pcmpeqd         m11, [pd_3]

    SHUFFLE_ON_SHIFT2 m11, m13

    pand             m11, [spatial_maskq] ; p = 3 & spatial mask
    movmskps         r14, m11
    cmp              r14, 0
    je              .end_strong_chroma

    movu       [rsp + 32], m11
    STRONG_CHROMA %1

.end_strong_chroma:

.one_side_chroma:
    ; invert mask & all strong mask, to get only one-sided mask
    pcmpeqd  m12, m12, m12
    pxor     m11, m11, m12
    pand     m11, [spatial_maskq]

    movmskps         r14, m11
    cmp              r14, 0
    je              .end_one_side_chroma

    movu     [rsp + 32], m11
    ONE_SIDE_CHROMA %1
.end_one_side_chroma:

.chroma_weak:
    movu     m11, [spatial_maskq]
    pcmpeqd  m12, m12, m12
    pxor     m11, m11, m12
    movu             m9, [tcptrq]
    psignw           m8, m9, [pw_m1];

    movmskps         r14, m11
    cmp              r14, 0
    je              .end_weak_chroma

    WEAK_CHROMA

    movu             m12, m11
    pand             m11, [rsp + 16]
    MASKED_COPY       m3, m14   ; need to mask the copy since we can have a mix of weak + others

    movu             m11, m12
    pand             m11, [rsp]
    MASKED_COPY       m4, m15 ; need to mask the copy since we can have a mix of weak + others
.end_weak_chroma:
    add rsp, 112
%endmacro

%macro LOOP_FILTER_CHROMA 0
cglobal vvc_v_loop_filter_chroma_8, 9, 15, 16, 112, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift , pix0, q_len, r3stride, spatial_mask, tcptr
    sub            pixq, 4
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE8x8B_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 8
    TRANSPOSE8x8B_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    RET

cglobal vvc_v_loop_filter_chroma_10, 9, 15, 16, 112, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift , pix0, q_len, r3stride, spatial_mask, tcptr
    sub            pixq, 8
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE8x8W_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 10
    TRANSPOSE8x8W_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq), [pw_pixel_max_10]
    RET

cglobal vvc_v_loop_filter_chroma_12, 9, 15, 16, 112, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift , pix0, q_len, r3stride, spatial_mask, tcptr
    sub            pixq, 8
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE8x8W_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 12
    TRANSPOSE8x8W_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq), [pw_pixel_max_12]
    RET

cglobal vvc_h_loop_filter_chroma_8, 9, 15, 16, 112, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift , pix0, q_len, src3stride, spatial_mask, tcptr
    lea     src3strideq, [3 * strideq]
    mov           pix0q, pixq
    sub           pix0q, src3strideq
    sub           pix0q, strideq

    movq             m0, [pix0q             ]  ;  p3
    movq             m1, [pix0q +   strideq]   ;  p2
    movq             m2, [pix0q + 2 * strideq] ;  p1
    movq             m3, [pix0q + src3strideq] ;  p0
    movq             m4, [pixq]                ;  q0
    movq             m5, [pixq +     strideq]  ;  q1
    movq             m6, [pixq + 2 * strideq]  ;  q2
    movq             m7, [pixq + src3strideq]  ;  q3

    pxor            m12, m12 ; zeros reg
    punpcklbw        m0, m12
    punpcklbw        m1, m12
    punpcklbw        m2, m12
    punpcklbw        m3, m12
    punpcklbw        m4, m12
    punpcklbw        m5, m12
    punpcklbw        m6, m12
    punpcklbw        m7, m12

    CHROMA_DEBLOCK_BODY 8

    packuswb          m0, m1
    packuswb          m2, m3
    packuswb          m4, m5
    packuswb          m6, m7

    movh     [pix0q              ], m0
    movhps   [pix0q +   strideq  ], m0
    movh     [pix0q + 2 * strideq], m2
    movhps   [pix0q + src3strideq], m2
    movh     [pixq]               , m4
    movhps   [pixq +     strideq] , m4
    movh     [pixq + 2 * strideq] , m6
    movhps   [pixq + src3strideq] , m6

RET

cglobal vvc_h_loop_filter_chroma_10, 9, 15, 16, 112, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift , pix0, q_len, src3stride, spatial_mask, tcptr
    lea    src3strideq, [3 * strideq]
    mov           pix0q, pixq
    sub           pix0q, src3strideq
    sub           pix0q, strideq

    movu             m0, [pix0q]               ;  p3
    movu             m1, [pix0q + strideq]     ;  p2
    movu             m2, [pix0q + 2 * strideq] ;  p1
    movu             m3, [pix0q + src3strideq] ;  p0
    movu             m4, [pixq]                ;  q0
    movu             m5, [pixq +     strideq]  ;  q1
    movu             m6, [pixq + 2 * strideq]  ;  q2
    movu             m7, [pixq + src3strideq]  ;  q3

    CHROMA_DEBLOCK_BODY 10

    pxor           m12, m12
    CLIPW           m1, m12, [pw_pixel_max_10] ; p2
    CLIPW           m2, m12, [pw_pixel_max_10] ; p1
    CLIPW           m3, m12, [pw_pixel_max_10] ; p0
    CLIPW           m4, m12, [pw_pixel_max_10] ; q0
    CLIPW           m5, m12, [pw_pixel_max_10] ; q1
    CLIPW           m6, m12, [pw_pixel_max_10] ; q2

    movu   [pix0q +     strideq], m1
    movu   [pix0q +   2*strideq], m2
    movu   [pix0q + src3strideq], m3

    movu                  [pixq], m4
    movu    [pixq +     strideq], m5
    movu    [pixq + 2 * strideq], m6

    RET

cglobal vvc_h_loop_filter_chroma_12, 9, 15, 16, 112, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift , pix0, q_len, src3stride, spatial_mask, tcptr
    lea    src3strideq, [3 * strideq]
    mov           pix0q, pixq
    sub           pix0q, src3strideq
    sub           pix0q, strideq

    movu             m0, [pix0q]               ;  p3
    movu             m1, [pix0q + strideq]     ;  p2
    movu             m2, [pix0q + 2 * strideq] ;  p1
    movu             m3, [pix0q + src3strideq] ;  p0
    movu             m4, [pixq]                ;  q0
    movu             m5, [pixq +     strideq]  ;  q1
    movu             m6, [pixq + 2 * strideq]  ;  q2
    movu             m7, [pixq + src3strideq]  ;  q3

    CHROMA_DEBLOCK_BODY 12

    pxor           m12, m12
    CLIPW           m1, m12, [pw_pixel_max_12] ; p2
    CLIPW           m2, m12, [pw_pixel_max_12] ; p1
    CLIPW           m3, m12, [pw_pixel_max_12] ; p0
    CLIPW           m4, m12, [pw_pixel_max_12] ; q0
    CLIPW           m5, m12, [pw_pixel_max_12] ; p0
    CLIPW           m6, m12, [pw_pixel_max_12] ; p0

    movu   [pix0q +     strideq], m1
    movu   [pix0q +   2*strideq], m2
    movu   [pix0q + src3strideq], m3
    movu                  [pixq], m4
    movu    [pixq +     strideq], m5
    movu    [pixq + 2 * strideq], m6

    RET

%endmacro

INIT_XMM avx
LOOP_FILTER_CHROMA

INIT_XMM avx2
LOOP_FILTER_CHROMA


%if ARCH_X86_64
%macro LOOP_FILTER_LUMA 0
;-----------------------------------------------------------------------------
; void ff_vvc_v_loop_filter_luma(uint8_t *_pix, ptrdiff_t _stride, int32_t *beta, int32_t *tc,
;     uint8_t *_no_p, uint8_t *_no_q, uint8_t *max_len_p, uint8_t *max_len_q, int hor_ctu_edge);
;-----------------------------------------------------------------------------
cglobal vvc_v_loop_filter_luma_8, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    sub            pixq, 4
    lea           pix0q, [3 * r1]
    mov     src3strideq, pixq
    add            pixq, pix0q
    TRANSPOSE8x8B_LOAD  PASS8ROWS(src3strideq, pixq, r1, pix0q)
    LUMA_DEBLOCK_BODY 8, v
.store:
    TRANSPOSE8x8B_STORE PASS8ROWS(src3strideq, pixq, r1, pix0q)
.bypassluma:
    RET

cglobal vvc_v_loop_filter_luma_10, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    sub            pixq, 8
    lea           pix0q, [3 * strideq]
    mov     src3strideq, pixq
    add            pixq, pix0q
    TRANSPOSE8x8W_LOAD  PASS8ROWS(src3strideq, pixq, strideq, pix0q)
    LUMA_DEBLOCK_BODY 10, v
.store:
    TRANSPOSE8x8W_STORE PASS8ROWS(src3strideq, pixq, r1, pix0q), [pw_pixel_max_10]
.bypassluma:
    RET

cglobal vvc_v_loop_filter_luma_12, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    sub            pixq, 8
    lea           pix0q, [3 * strideq]
    mov     src3strideq, pixq
    add            pixq, pix0q
    TRANSPOSE8x8W_LOAD  PASS8ROWS(src3strideq, pixq, strideq, pix0q)
    LUMA_DEBLOCK_BODY 12, v
.store:
    TRANSPOSE8x8W_STORE PASS8ROWS(src3strideq, pixq, r1, pix0q), [pw_pixel_max_12]
.bypassluma:
    RET

;-----------------------------------------------------------------------------
; void ff_vvc_h_loop_filter_luma(uint8_t *_pix, ptrdiff_t _stride, int32_t *beta, int32_t *tc,
;     uint8_t *_no_p, uint8_t *_no_q, uint8_t *max_len_p, uint8_t *max_len_q, int hor_ctu_edge);
;-----------------------------------------------------------------------------
cglobal vvc_h_loop_filter_luma_8, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    lea     src3strideq, [3 * strideq]
    mov           pix0q, pixq
    sub           pix0q, src3strideq
    sub           pix0q, strideq
    movq             m0, [pix0q];               p3
    movq             m1, [pix0q +     strideq]; p2
    movq             m2, [pix0q + 2 * strideq]; p1
    movq             m3, [pix0q + src3strideq]; p0
    movq             m4, [pixq];                q0
    movq             m5, [pixq +     strideq];  q1
    movq             m6, [pixq + 2 * strideq];  q2
    movq             m7, [pixq + src3strideq];  q3
    pxor             m8, m8
    punpcklbw        m0, m8
    punpcklbw        m1, m8
    punpcklbw        m2, m8
    punpcklbw        m3, m8
    punpcklbw        m4, m8
    punpcklbw        m5, m8
    punpcklbw        m6, m8
    punpcklbw        m7, m8
    LUMA_DEBLOCK_BODY 8, h
.store:
    packuswb          m1, m2
    packuswb          m3, m4
    packuswb          m5, m6
    movh   [pix0q +     strideq], m1
    movhps [pix0q + 2 * strideq], m1
    movh   [pix0q + src3strideq], m3
    movhps [pixq               ], m3
    movh   [pixq  +     strideq], m5
    movhps [pixq  + 2 * strideq], m5
.bypassluma:
    RET

cglobal vvc_h_loop_filter_luma_10, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    lea                  src3strideq, [3 * strideq]
    mov                        pix0q, pixq
    sub                        pix0q, src3strideq
    sub                        pix0q, strideq
    movdqu                        m0, [pix0q];               p3
    movdqu                        m1, [pix0q +     strideq]; p2
    movdqu                        m2, [pix0q + 2 * strideq]; p1
    movdqu                        m3, [pix0q + src3strideq]; p0
    movdqu                        m4, [pixq];                q0
    movdqu                        m5, [pixq  +     strideq]; q1
    movdqu                        m6, [pixq  + 2 * strideq]; q2
    movdqu                        m7, [pixq  + src3strideq]; q3
    LUMA_DEBLOCK_BODY             10, h
.store:
    pxor                          m8, m8; zeros reg
    CLIPW                         m1, m8, [pw_pixel_max_10]
    CLIPW                         m2, m8, [pw_pixel_max_10]
    CLIPW                         m3, m8, [pw_pixel_max_10]
    CLIPW                         m4, m8, [pw_pixel_max_10]
    CLIPW                         m5, m8, [pw_pixel_max_10]
    CLIPW                         m6, m8, [pw_pixel_max_10]
    movdqu     [pix0q +     strideq], m1;  p2
    movdqu     [pix0q + 2 * strideq], m2;  p1
    movdqu     [pix0q + src3strideq], m3;  p0
    movdqu     [pixq               ], m4;  q0
    movdqu     [pixq  +     strideq], m5;  q1
    movdqu     [pixq  + 2 * strideq], m6;  q2
.bypassluma:
    RET

cglobal vvc_h_loop_filter_luma_12, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    lea                  src3strideq, [3 * strideq]
    mov                        pix0q, pixq
    sub                        pix0q, src3strideq
    sub                        pix0q, strideq
    movdqu                        m0, [pix0q];               p3
    movdqu                        m1, [pix0q +     strideq]; p2
    movdqu                        m2, [pix0q + 2 * strideq]; p1
    movdqu                        m3, [pix0q + src3strideq]; p0
    movdqu                        m4, [pixq];                q0
    movdqu                        m5, [pixq  +     strideq]; q1
    movdqu                        m6, [pixq  + 2 * strideq]; q2
    movdqu                        m7, [pixq  + src3strideq]; q3
    LUMA_DEBLOCK_BODY             12, h
.store:
    pxor                          m8, m8; zeros reg
    CLIPW                         m1, m8, [pw_pixel_max_12]
    CLIPW                         m2, m8, [pw_pixel_max_12]
    CLIPW                         m3, m8, [pw_pixel_max_12]
    CLIPW                         m4, m8, [pw_pixel_max_12]
    CLIPW                         m5, m8, [pw_pixel_max_12]
    CLIPW                         m6, m8, [pw_pixel_max_12]
    movdqu     [pix0q +     strideq], m1;  p2
    movdqu     [pix0q + 2 * strideq], m2;  p1
    movdqu     [pix0q + src3strideq], m3;  p0
    movdqu     [pixq               ], m4;  q0
    movdqu     [pixq  +     strideq], m5;  q1
    movdqu     [pixq  + 2 * strideq], m6;  q2
.bypassluma:
    RET

%endmacro

INIT_XMM sse2
LOOP_FILTER_LUMA
INIT_XMM ssse3
LOOP_FILTER_LUMA
INIT_XMM avx
LOOP_FILTER_LUMA
%endif
