; from hevc_deblock.asm, grap all the tranpose macros

%include "libavutil/x86/x86util.asm"

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
cextern pw_m1

SECTION .text
INIT_XMM sse2
; INIT_YMM avx2


; in: 8 rows of 4 bytes in %4..%11
; out: 4 rows of 8 words in m0..m3
%macro TRANSPOSE4x8B_LOAD 8
    movd             m0, %1
    movd             m2, %2
    movd             m1, %3
    movd             m3, %4

    punpcklbw        m0, m2
    punpcklbw        m1, m3
    punpcklwd        m0, m1

    movd             m4, %5
    movd             m6, %6
    movd             m5, %7
    movd             m3, %8

    punpcklbw        m4, m6
    punpcklbw        m5, m3
    punpcklwd        m4, m5

    punpckhdq        m2, m0, m4
    punpckldq        m0, m4

    pxor             m5, m5
    punpckhbw        m1, m0, m5
    punpcklbw        m0, m5
    punpckhbw        m3, m2, m5
    punpcklbw        m2, m5
%endmacro

; in: 4 rows of 8 words in m0..m3
; out: 8 rows of 4 bytes in %1..%8
%macro TRANSPOSE8x4B_STORE 8
    packuswb         m0, m2
    packuswb         m1, m3
    SBUTTERFLY bw, 0, 1, 2
    SBUTTERFLY wd, 0, 1, 2

    movd             %1, m0
    pshufd           m0, m0, 0x39
    movd             %2, m0
    pshufd           m0, m0, 0x39
    movd             %3, m0
    pshufd           m0, m0, 0x39
    movd             %4, m0

    movd             %5, m1
    pshufd           m1, m1, 0x39
    movd             %6, m1
    pshufd           m1, m1, 0x39
    movd             %7, m1
    pshufd           m1, m1, 0x39
    movd             %8, m1
%endmacro

; in: 8 rows of 4 words in %4..%11
; out: 4 rows of 8 words in m0..m3
%macro TRANSPOSE4x8W_LOAD 8
    movq             m0, %1
    movq             m2, %2
    movq             m1, %3
    movq             m3, %4

    punpcklwd        m0, m2
    punpcklwd        m1, m3
    punpckhdq        m2, m0, m1
    punpckldq        m0, m1

    movq             m4, %5
    movq             m6, %6
    movq             m5, %7
    movq             m3, %8

    punpcklwd        m4, m6
    punpcklwd        m5, m3
    punpckhdq        m6, m4, m5
    punpckldq        m4, m5

    punpckhqdq       m1, m0, m4
    punpcklqdq       m0, m4
    punpckhqdq       m3, m2, m6
    punpcklqdq       m2, m6

%endmacro

; in: 4 rows of 8 words in m0..m3
; out: 8 rows of 4 words in %1..%8
%macro TRANSPOSE8x4W_STORE 9
    TRANSPOSE4x4W     0, 1, 2, 3, 4

    pxor             m5, m5; zeros reg
    CLIPW            m0, m5, %9
    CLIPW            m1, m5, %9
    CLIPW            m2, m5, %9
    CLIPW            m3, m5, %9

    movq             %1, m0
    movhps           %2, m0
    movq             %3, m1
    movhps           %4, m1
    movq             %5, m2
    movhps           %6, m2
    movq             %7, m3
    movhps           %8, m3
%endmacro

; in: 8 rows of 8 bytes in %1..%8
; out: 8 rows of 8 words in m0..m7
%macro TRANSPOSE8x8B_LOAD 8
    movq             m7, %1
    movq             m2, %2
    movq             m1, %3
    movq             m3, %4

    punpcklbw        m7, m2
    punpcklbw        m1, m3
    punpcklwd        m3, m7, m1
    punpckhwd        m7, m1

    movq             m4, %5
    movq             m6, %6
    movq             m5, %7
    movq            m15, %8

    punpcklbw        m4, m6
    punpcklbw        m5, m15
    punpcklwd        m9, m4, m5
    punpckhwd        m4, m5

    punpckldq        m1, m3, m9;  0, 1
    punpckhdq        m3, m9;  2, 3

    punpckldq        m5, m7, m4;  4, 5
    punpckhdq        m7, m4;  6, 7

    pxor            m13, m13

    punpcklbw        m0, m1, m13; 0 in 16 bit
    punpckhbw        m1, m13; 1 in 16 bit

    punpcklbw        m2, m3, m13; 2
    punpckhbw        m3, m13; 3

    punpcklbw        m4, m5, m13; 4
    punpckhbw        m5, m13; 5

    punpcklbw        m6, m7, m13; 6
    punpckhbw        m7, m13; 7
%endmacro


; in: 8 rows of 8 words in m0..m8
; out: 8 rows of 8 bytes in %1..%8
%macro TRANSPOSE8x8B_STORE 8
    packuswb         m0, m4
    packuswb         m1, m5
    packuswb         m2, m6
    packuswb         m3, m7
    TRANSPOSE2x4x4B   0, 1, 2, 3, 4

    movq             %1, m0
    movhps           %2, m0
    movq             %3, m1
    movhps           %4, m1
    movq             %5, m2
    movhps           %6, m2
    movq             %7, m3
    movhps           %8, m3
%endmacro

; in: 8 rows of 8 words in %1..%8
; out: 8 rows of 8 words in m0..m7
%macro TRANSPOSE8x8W_LOAD 8
    movdqu           m0, %1
    movdqu           m1, %2
    movdqu           m2, %3
    movdqu           m3, %4
    movdqu           m4, %5
    movdqu           m5, %6
    movdqu           m6, %7
    movdqu           m7, %8
    TRANSPOSE8x8W     0, 1, 2, 3, 4, 5, 6, 7, 8
%endmacro

; in: 8 rows of 8 words in m0..m8
; out: 8 rows of 8 words in %1..%8
%macro TRANSPOSE8x8W_STORE 9
    TRANSPOSE8x8W     0, 1, 2, 3, 4, 5, 6, 7, 8

    pxor             m8, m8
    CLIPW            m0, m8, %9
    CLIPW            m1, m8, %9
    CLIPW            m2, m8, %9
    CLIPW            m3, m8, %9
    CLIPW            m4, m8, %9
    CLIPW            m5, m8, %9
    CLIPW            m6, m8, %9
    CLIPW            m7, m8, %9

    movdqu           %1, m0
    movdqu           %2, m1
    movdqu           %3, m2
    movdqu           %4, m3
    movdqu           %5, m4
    movdqu           %6, m5
    movdqu           %7, m6
    movdqu           %8, m7
%endmacro


; in: %2 clobbered
; out: %1
; mask in m11
; clobbers m10
%macro MASKED_COPY 2
    pand             %2, m11 ; and mask
    pandn           m10, m11, %1; and -mask
    por              %2, m10
    movu             %1, %2
%endmacro

; in: %2 clobbered
; out: %1
; mask in %3, will be clobbered
%macro MASKED_COPY2 3
    pand             %2, %3 ; and mask
    pandn            %3, %1; and -mask
    por              %2, %3
    movu             %1, %2
%endmacro


ALIGN 16
%macro WEAK_CHROMA 1
    psubw            m12, m4, m3 ; q0 - p0
    psubw            m13, m2, m5 ; p1 - q1
    psllw            m12, 2      ; << 2
    paddw            m13, m12    ;

    paddw            m13, [pw_4] ; +4
    psraw            m13, 3      ; >> 3

    CLIPW            m13, m8, m9
    paddw            m14, m3, m13 ; p0 + delta0
    psubw            m15, m4, m13 ; q0 - delta0
    MASKED_COPY       m3, m14
    MASKED_COPY       m4, m15
%endmacro

%macro CLIP_RESTORE 4  ; toclip, value, -tc, +tc
    paddw           %3, %2
    paddw           %4, %2
    CLIPW           %1, %3, %4
    psubw           %3, %2
    psubw           %4, %2

%endmacro

%macro STRONG_CHROMA 0
    ; --------  strong calcs -------
    ; p0
    paddw         m12, m0, m1
    paddw         m12, m2
    paddw         m12, m3
    paddw         m12, m4
    paddw         m12, [pw_4]
    movu          m15, m12      ; p3 +  p2 + p1 +  p0 + q0 + 4
    paddw         m12, m3
    paddw         m12, m5       ; q1
    paddw         m12, m6       ; q2
    psraw         m12, 3
    CLIP_RESTORE  m12, m3, m8, m9

    ; p1
    paddw        m13, m15, m0 ; + p3
    paddw        m13, m2      ; + p1
    paddw        m13, m5      ; + q1
    psraw        m13, 3
    CLIP_RESTORE  m13, m2, m8, m9

    ; p2
    psllw         m14, m0, 1 ; 2*p3
    paddw         m14, m15
    paddw         m14, m1    ; + p2
    psraw         m14, 3
    CLIP_RESTORE  m14, m1, m8, m9

    ; q0
    ; clobber m0 / P3 - not used anymore
    paddw         m0, m3, m4    ; p0 + q0
    paddw         m0, m5        ; + q1
    paddw         m0, m6        ; + q2
    paddw         m0, m7        ; + q3
    paddw         m0, [pw_4]
    movu          m15, m0       ; p0 + q0 + q1 + q2 + q3 + 4
    paddw         m0, m1        ; + p2  -- p2 is unused after this point
    paddw         m0, m2        ; + p1
    paddw         m0, m4        ; + q0
    psraw         m0, 3
    CLIP_RESTORE  m0, m4, m8, m9

    ; q1
    ; clobber m1 / P2 - last use was q0 calc
    paddw         m1, m2, m15; p0 + ...   + p1
    paddw         m1, m5     ; + q1
    paddw         m1, m7     ; + q3
    psraw         m1, 3
    CLIP_RESTORE  m1, m5, m8, m9

    ; q2
    ; clobber m15 - sum is fully used
    paddw         m15, m7  ; + q3
    paddw         m15, m7  ; + q3
    paddw         m15, m6  ; + q2
    psraw         m15, 3
    CLIP_RESTORE  m15, m6, m8, m9

    MASKED_COPY m4, m0  ; q0
    MASKED_COPY m5, m1  ; q1
    MASKED_COPY m6, m15 ; q2
    MASKED_COPY m3, m12 ; p0
    MASKED_COPY m2, m13 ; p1
    MASKED_COPY m1, m14 ; p2
%endmacro

; m11 strong mask, m8/m9 -tc, tc
; p3 to q3 in m0, m7 - clobbers p3
%macro SPATIAL_ACTIVITY 1
; if p == 1, then p3, p2 are p1 for spatial calc
; clobber m10, m12, m13, clobber m11
    pxor            m10, m10
    movd            m11, [max_len_pq]
    punpcklbw       m11, m11, m10
    punpcklwd       m11, m11, m10

    pcmpeqd         m11, [pd_1]

    cmp           shiftd, 1
    je           .max_len_pq_spatial_shift
    punpcklqdq       m11, m11, m11
    pshufhw          m13, m11, q2222
    pshuflw          m13, m13, q0000
    movu             m11, m13
    ; intentional fallthrough

.max_len_pq_spatial_shift:
    movu             m12, m2
    movu             m13, m2
    MASKED_COPY      m0, m12
    MASKED_COPY      m1, m13
.end_len_pq_spatial_shift:

    ; if max_len_q == 3, compute spatial activity to determine final length
    pxor            m10, m10
    movd            m11, [max_len_qq]
    punpcklbw       m11, m11, m10
    punpcklwd       m11, m11, m10

    pcmpeqd         m11, [pd_3];

    cmp           shiftd, 1
    je           .max_len_shift
    punpcklqdq       m11, m11, m11
    pshufhw          m13, m11, q2222
    pshuflw          m13, m13, q0000
    movu             m11, m13
.max_len_shift:

; load tc
.load_tc:
    movu             m8, [tcq]
%if %1 == 8
    paddw            m8, [pw_2]
    psrlw            m8, 2
%elif %1 == 12
    psllw            m8, %1 - 10;
%endif
    cmp           shiftd, 1
    je   .tc_load_shift

    punpcklqdq       m8, m8, m8
    pshufhw          m8, m8, q2222
    pshuflw          m8, m8, q0000
    jmp     .end_tc_load
.tc_load_shift:
    pshufhw          m8, m8,  q2200
    pshuflw          m8, m8,  q2200
.end_tc_load:
    movu         [tcptrq], m8
    movu         [spatial_maskq], m11
    movmskps               r14, m11
    cmp                    r14, 0
    je             .chroma_weak

.tc25_calculation:
    ; movu             m9, m8
    pmullw           m8, [pw_5]
    paddw            m8, [pw_1]
    psrlw            m8, 1          ; ((tc * 5 + 1) >> 1);

    psubw           m12, m3, m4     ;      p0 - q0
    ABS1            m12, m14        ; abs(p0 - q0)

    cmp           shiftd, 1
    je  .tc25_mask

    pshufhw         m12, m12, q3300
    pshuflw         m12, m12, q3300
    ; intentional fall through

.tc25_mask:
    pcmpgtw          m15, m8, m12
    pand             m11, m15

; dsam
    psllw            m9, m2, 1
    psubw           m10, m1, m9
    paddw           m10, m3
    ABS1            m10, m12

    psllw            m9, m5, 1
    psubw           m12, m6, m9
    paddw           m12, m4
    ABS1            m12, m13

    paddw           m9, m10, m12  ; m9 spatial activity sum for all lines
; end dsam

; Load beta
    movu             m12, [betaq]
%if %1 > 8
    psllw            m12, %1 - 8   ; replace with bit_depth
%endif

    cmp           shiftd, 1
    je           .beta_load_shift

    punpcklqdq      m12,  m12, m12
    pshufhw         m13,  m12, q2222
    pshuflw         m13, m13, q0000

; dsam calcs
    pshufhw         m14,  m9, q0033
    pshuflw         m14, m14, q0033
    pshufhw          m9,  m9, q3300
    pshuflw          m9,  m9, q3300

    jmp  .spatial_activity

.beta_load_shift:
    pshufhw         m13, m12,  q2200
    pshuflw         m13, m13, q2200

    movu            m14, m9
    pshufhw          m9,  m9, q2301
    pshuflw          m9,  m9, q2301

.spatial_activity:
    paddw            m14, m9          ; d0 + d3, d0 + d3, d0 + d3, .....
    pcmpgtw          m15, m13, m14    ; beta > d0 + d3, d0 + d3 (next block)
    pand             m11, m15         ; save filtering and or at the end

    ; beta_2
    psraw          m15, m13, 2   ; beta >> 2
    psllw           m8, m9, 1    ;  d0, d1, d2, d3, ...

    pcmpgtw       m15, m8        ; d0 ..  < beta_2, d0... < beta_2, d3... <
    pand          m11, m15

.beta3_comparison:
    ; beta_3
    psubw           m12, m0, m3     ; p3 - p0
    ABS1            m12, m14        ; abs(p3 - p0)

    psubw           m15, m7, m4     ; q3 - q0
    ABS1            m15, m14        ; abs(q3 - q0)

    paddw           m12, m15        ; abs(p3 - p0) + abs(q3 - q0)

    psraw           m13, 3          ; beta >> 3

    cmp           shiftd, 1
    je    .beta3_no_first_shuffle

    pshufhw         m12, m12, q3300
    pshuflw         m12, m12, q3300
.beta3_no_first_shuffle:
    pcmpgtw         m13, m12
    pand            m11, m13


.final_mask:
    ; final shift mask
    movu          m15, m11
    cmp           shiftd, 1
    je  .final_shift_mask

    pshufhw         m15, m15, q0033
    pshuflw         m15, m15, q0033
    pand            m11, m15
    jmp   .final_mask_end

.final_shift_mask:
    pshufhw         m15, m15, q2301
    pshuflw         m15, m15, q2301
    pand            m11, m15

.final_mask_end:

.prep_clipping_masks:
    movu [spatial_maskq], m11
    movu             m9, [tcptrq] 
    psignw           m8, m9, [pw_m1];

%if %1 != 8
    movu             m0, [pix0q]
    movu             m1, [pix0q + strideq]
%else
    movq             m0, [pix0q    ] ;  p2
    movq             m1, [pix0q + strideq   ] ;  p1
    pxor             m12, m12
    punpcklbw        m0, m12
    punpcklbw        m1, m12
%endif
%endmacro

%macro ONE_SIDE_CHROMA 0
    ; strong one-sided
    ; p0   -  clobber p3 again
    paddw          m0, m3, m4 ;      p0 + q0
    paddw          m0, m5     ;      p0 + q0 + q1
    paddw          m0, m6     ;      p0 + q0 + q1 + q2
    paddw          m0, [pw_4] ;      p0 + q0 + q1 + q2 + 4
    paddw          m0, m2     ; p1 + p0 + q0 + q1 + q2 + 4
    movu           m15, m0
    paddw          m0, m2     ; + p1
    paddw          m0, m2     ; + p1
    paddw          m0, m3     ; + p0
    psrlw          m0, 3

    CLIP_RESTORE   m0, m3, m8, m9

    ; q0
    paddw          m12, m2, m15 ; + p1
    paddw          m12, m4      ;  q0
    paddw          m12, m7      ; q3

    psrlw          m12, 3

    CLIP_RESTORE   m12, m4, m8, m9

    ; q1
    psllw          m13, m7, 1 ; 2*q3
    paddw          m13, m15
    paddw          m13, m5   ; q1

    psrlw          m13, 3

    CLIP_RESTORE   m13, m5, m8, m9

    ;q2
    psllw          m14, m7, 1  ;2*q3
    paddw          m14, m7     ;3*q3
    paddw          m14, m15    ;
    paddw          m14, m6  ; q2
    psubw          m14, m2  ; sub p1

    psrlw          m14, 3

    CLIP_RESTORE   m14, m6, m8, m9

    MASKED_COPY   m3, m0  ; m2
    MASKED_COPY   m4, m12 ; m3
    MASKED_COPY   m5, m13 ; m4
    MASKED_COPY   m6, m14 ; m5
%endmacro

; CHROMA_DEBLOCK_BODY(bit_depth)
%macro CHROMA_DEBLOCK_BODY 1
    sub  rsp, 16
    mov spatial_maskq, rsp
    sub rsp, 16
    mov tcptrq, rsp

    SPATIAL_ACTIVITY %1

    movu [spatial_maskq], m11

    movmskps         r14, m11
    cmp              r14, 0
    je              .chroma_weak

    pxor            m10, m10
    movd            m11, [max_len_pq]
    punpcklbw       m11, m11, m10
    punpcklwd       m11, m11, m10

    pcmpeqd         m11, [pd_3]

    cmp           shiftd, 1
    je           .strong_chroma
    punpcklqdq       m11, m11, m11
    pshufhw          m13, m11, q2222
    pshuflw          m13, m13, q0000
    movu             m11, m13


.strong_chroma:
    pand             m11, [spatial_maskq]
    movmskps         r14, m11
    cmp              r14, 0
    je              .one_side_chroma
    STRONG_CHROMA

    ; store strong changes ... will need to adapt to no_p
    pxor           m12, m12
    CLIPW           m1, m12, [pw_pixel_max_%1] ; p0
    CLIPW           m2, m12, [pw_pixel_max_%1] ; p0
    MASKED_COPY   [pix0q +     strideq], m1
    MASKED_COPY   [pix0q +   2*strideq], m2

.one_side_chroma:
    ; invert mask & all strong mask, to get only one-sided mask
    pcmpeqd  m12, m12, m12
    pxor     m11, m11, m12
    pand     m11, [spatial_maskq]

    movmskps         r14, m11
    cmp              r14, 0
    je              .chroma_weak

    ONE_SIDE_CHROMA

.chroma_weak:
    movu     m11, [spatial_maskq]
    pcmpeqd  m12, m12, m12
    pxor     m11, m11, m12
    movu             m9, [tcptrq]
    psignw           m8, m9, [pw_m1];

    WEAK_CHROMA     %1
    pxor           m12, m12
    CLIPW           m3, m12, [pw_pixel_max_%1] ; p0
    CLIPW           m4, m12, [pw_pixel_max_%1] ; q0
    CLIPW           m5, m12, [pw_pixel_max_%1] ; p0
    CLIPW           m6, m12, [pw_pixel_max_%1] ; p0

; no_p
    pxor            m10, m10
    movd            m11, [no_pq]
    punpcklbw       m11, m11, m10
    punpcklwd       m11, m11, m10

    pcmpeqd         m11, m10;

    cmp           shiftd, 1
    je           .no_p_shift
    punpcklqdq       m11, m11, m11
    pshufhw          m13, m11, q2222
    pshuflw          m13, m13, q0000
    jmp      .store_p
.no_p_shift:
    pshufhw          m13, m11, q2301
    pshuflw          m13, m13, q2301
.store_p:
    movu             m11, m13


    MASKED_COPY   [pix0q + src3strideq], m3

; no_q
    pxor            m10, m10
    movd            m11, [no_qq]
    punpcklbw       m11, m11, m10
    punpcklwd       m11, m11, m10

    pcmpeqd         m11, m10;

    cmp           shiftd, 1
    je           .no_q_shift
    punpcklqdq       m11, m11, m11
    pshufhw          m13, m11, q2222
    pshuflw          m13, m13, q0000
    jmp      .store_q
.no_q_shift:
    pshufhw          m13, m11, q2301
    pshuflw          m13, m13, q2301
.store_q:
    movu             m11, m13

%endmacro

; input in m0 ... m7, beta in r2 tcs in r3. Output in m1...m6
%macro LUMA_DEBLOCK_BODY 2
    psllw            m9, m2, 1; *2
    psubw           m10, m1, m9
    paddw           m10, m3
    ABS1            m10, m11 ; 0dp0, 0dp3 , 1dp0, 1dp3

    psllw            m9, m5, 1; *2
    psubw           m11, m6, m9
    paddw           m11, m4
    ABS1            m11, m13 ; 0dq0, 0dq3 , 1dq0, 1dq3

    ;beta calculations
%if %1 > 8
    shl             betaq, %1 - 8
%endif
    xor             betaq, betaq
    movd            m13, betad
    SPLATW          m13, m13, 0
    ;end beta calculations

    paddw            m9, m10, m11;   0d0, 0d3  ,  1d0, 1d3

    pshufhw         m14, m9, 0x0f ;0b00001111;  0d3 0d3 0d0 0d0 in high
    pshuflw         m14, m14, 0x0f ;0b00001111;  1d3 1d3 1d0 1d0 in low

    pshufhw          m9, m9, 0xf0 ;0b11110000; 0d0 0d0 0d3 0d3
    pshuflw          m9, m9, 0xf0 ;0b11110000; 1d0 1d0 1d3 1d3

    paddw           m14, m9; 0d0+0d3, 1d0+1d3

    ;compare
    pcmpgtw         m15, m13, m14
    movmskps        r13, m15 ;filtering mask 0d0 + 0d3 < beta0 (bit 2 or 3) , 1d0 + 1d3 < beta1 (bit 0 or 1)
    test            r13, r13
    je              .bypassluma

    ;weak / strong decision compare to beta_2
    psraw           m15, m13, 2;   beta >> 2
    psllw            m8, m9, 1;
    pcmpgtw         m15, m8; (d0 << 1) < beta_2, (d3 << 1) < beta_2
    movmskps        r6, m15;
    ;end weak / strong decision

    ; weak filter nd_p/q calculation
    pshufd           m8, m10, 0x31
    psrld            m8, 16
    paddw            m8, m10
    movd            r7d, m8
    pshufd           m8, m8, 0x4E
    movd            r8d, m8

    pshufd           m8, m11, 0x31
    psrld            m8, 16
    paddw            m8, m11
    movd            r9d, m8
    pshufd           m8, m8, 0x4E
    movd           r10d, m8
    ; end calc for weak filter

    ; filtering mask
    mov             r11, r13
    shr             r11, 3
    movd            m15, r11d
    and             r13, 1
    movd            m11, r13d
    shufps          m11, m15, 0
    shl             r11, 1
    or              r13, r11

    pcmpeqd         m11, [pd_1]; filtering mask

    ;decide between strong and weak filtering
    ;tc25 calculations
    mov            r11d, [tcq];
%if %1 > 8
    shl             r11, %1 - 8
%endif
    movd             m8, r11d; tc0
    mov             r3d, [tcq+4];
%if %1 > 8
    shl              r3, %1 - 8
%endif
    add            r11d, r3d; tc0 + tc1
    jz             .bypassluma
    movd             m9, r3d; tc1
    punpcklwd        m8, m8
    punpcklwd        m9, m9
    shufps           m8, m9, 0; tc0, tc1
    mova             m9, m8
    psllw            m8, 2; tc << 2
    pavgw            m8, m9; tc25 = ((tc * 5 + 1) >> 1)
    ;end tc25 calculations

    ;----beta_3 comparison-----
    psubw           m12, m0, m3;      p3 - p0
    ABS1            m12, m14; abs(p3 - p0)

    psubw           m15, m7, m4;      q3 - q0
    ABS1            m15, m14; abs(q3 - q0)

    paddw           m12, m15; abs(p3 - p0) + abs(q3 - q0)

    pshufhw         m12, m12, 0xf0 ;0b11110000;
    pshuflw         m12, m12, 0xf0 ;0b11110000;

    psraw           m13, 3; beta >> 3
    pcmpgtw         m13, m12;
    movmskps        r11, m13;
    and             r6, r11; strong mask , beta_2 and beta_3 comparisons
    ;----beta_3 comparison end-----
    ;----tc25 comparison---
    psubw           m12, m3, m4;      p0 - q0
    ABS1            m12, m14; abs(p0 - q0)

    pshufhw         m12, m12, 0xf0 ;0b11110000;
    pshuflw         m12, m12, 0xf0 ;0b11110000;

    pcmpgtw          m8, m12; tc25 comparisons
    movmskps        r11, m8;
    and             r6, r11; strong mask, beta_2, beta_3 and tc25 comparisons
    ;----tc25 comparison end---
    mov             r11, r6;
    shr             r11, 1;
    and             r6, r11; strong mask, bits 2 and 0

    pmullw          m14, m9, [pw_m2]; -tc * 2
    paddw            m9, m9

    and             r6, 5; 0b101
    mov             r11, r6; strong mask
    shr             r6, 2;
    movd            m12, r6d; store to xmm for mask generation
    shl             r6, 1
    and             r11, 1
    movd            m10, r11d; store to xmm for mask generation
    or              r6, r11; final strong mask, bits 1 and 0
    jz      .weakfilter

    shufps          m10, m12, 0
    pcmpeqd         m10, [pd_1]; strong mask

    mova            m13, [pw_4]; 4 in every cell
    pand            m11, m10; combine filtering mask and strong mask
    paddw           m12, m2, m3;          p1 +   p0
    paddw           m12, m4;          p1 +   p0 +   q0
    mova            m10, m12; copy
    paddw           m12, m12;       2*p1 + 2*p0 + 2*q0
    paddw           m12, m1;   p2 + 2*p1 + 2*p0 + 2*q0
    paddw           m12, m5;   p2 + 2*p1 + 2*p0 + 2*q0 + q1
    paddw           m12, m13;  p2 + 2*p1 + 2*p0 + 2*q0 + q1 + 4
    psraw           m12, 3;  ((p2 + 2*p1 + 2*p0 + 2*q0 + q1 + 4) >> 3)
    psubw           m12, m3; ((p2 + 2*p1 + 2*p0 + 2*q0 + q1 + 4) >> 3) - p0
    pmaxsw          m12, m14
    pminsw          m12, m9; av_clip( , -2 * tc, 2 * tc)
    paddw           m12, m3; p0'

    paddw           m15, m1, m10; p2 + p1 + p0 + q0
    psrlw           m13, 1; 2 in every cell
    paddw           m15, m13; p2 + p1 + p0 + q0 + 2
    psraw           m15, 2;  (p2 + p1 + p0 + q0 + 2) >> 2
    psubw           m15, m2;((p2 + p1 + p0 + q0 + 2) >> 2) - p1
    pmaxsw          m15, m14
    pminsw          m15, m9; av_clip( , -2 * tc, 2 * tc)
    paddw           m15, m2; p1'

    paddw            m8, m1, m0;     p3 +   p2
    paddw            m8, m8;   2*p3 + 2*p2
    paddw            m8, m1;   2*p3 + 3*p2
    paddw            m8, m10;  2*p3 + 3*p2 + p1 + p0 + q0
    paddw           m13, m13
    paddw            m8, m13;  2*p3 + 3*p2 + p1 + p0 + q0 + 4
    psraw            m8, 3;   (2*p3 + 3*p2 + p1 + p0 + q0 + 4) >> 3
    psubw            m8, m1; ((2*p3 + 3*p2 + p1 + p0 + q0 + 4) >> 3) - p2
    pmaxsw           m8, m14
    pminsw           m8, m9; av_clip( , -2 * tc, 2 * tc)
    paddw            m8, m1; p2'
    MASKED_COPY      m1, m8

    paddw            m8, m3, m4;         p0 +   q0
    paddw            m8, m5;         p0 +   q0 +   q1
    paddw            m8, m8;       2*p0 + 2*q0 + 2*q1
    paddw            m8, m2;  p1 + 2*p0 + 2*q0 + 2*q1
    paddw            m8, m6;  p1 + 2*p0 + 2*q0 + 2*q1 + q2
    paddw            m8, m13; p1 + 2*p0 + 2*q0 + 2*q1 + q2 + 4
    psraw            m8, 3;  (p1 + 2*p0 + 2*q0 + 2*q1 + q2 + 4) >>3
    psubw            m8, m4;
    pmaxsw           m8, m14
    pminsw           m8, m9; av_clip( , -2 * tc, 2 * tc)
    paddw            m8, m4; q0'
    MASKED_COPY      m2, m15

    paddw           m15, m3, m4;   p0 + q0
    paddw           m15, m5;   p0 + q0 + q1
    mova            m10, m15;
    paddw           m15, m6;   p0 + q0 + q1 + q2
    psrlw           m13, 1; 2 in every cell
    paddw           m15, m13;  p0 + q0 + q1 + q2 + 2
    psraw           m15, 2;   (p0 + q0 + q1 + q2 + 2) >> 2
    psubw           m15, m5; ((p0 + q0 + q1 + q2 + 2) >> 2) - q1
    pmaxsw          m15, m14
    pminsw          m15, m9; av_clip( , -2 * tc, 2 * tc)
    paddw           m15, m5; q1'

    paddw           m13, m7;      q3 + 2
    paddw           m13, m6;      q3 +  q2 + 2
    paddw           m13, m13;   2*q3 + 2*q2 + 4
    paddw           m13, m6;    2*q3 + 3*q2 + 4
    paddw           m13, m10;   2*q3 + 3*q2 + q1 + q0 + p0 + 4
    psraw           m13, 3;    (2*q3 + 3*q2 + q1 + q0 + p0 + 4) >> 3
    psubw           m13, m6;  ((2*q3 + 3*q2 + q1 + q0 + p0 + 4) >> 3) - q2
    pmaxsw          m13, m14
    pminsw          m13, m9; av_clip( , -2 * tc, 2 * tc)
    paddw           m13, m6; q2'

    MASKED_COPY      m6, m13
    MASKED_COPY      m5, m15
    MASKED_COPY      m4, m8
    MASKED_COPY      m3, m12

.weakfilter:
    not             r6; strong mask -> weak mask
    and             r6, r13; final weak filtering mask, bits 0 and 1
    jz             .store

    ; weak filtering mask
    mov             r11, r6
    shr             r11, 1
    movd            m12, r11d
    and             r6, 1
    movd            m11, r6d
    shufps          m11, m12, 0
    pcmpeqd         m11, [pd_1]; filtering mask

    mov             r13, betaq
    shr             r13, 1;
    add             betaq, r13
    shr             betaq, 3; ((beta + (beta >> 1)) >> 3))

    psubw           m12, m4, m3 ; q0 - p0
    paddw           m10, m12, m12
    paddw           m12, m10 ; 3 * (q0 - p0)
    psubw           m10, m5, m2 ; q1 - p1
    psubw           m12, m10 ; 3 * (q0 - p0) - (q1 - p1)
%if %1 < 12
    paddw           m10, m12, m12
    paddw           m12, [pw_8]; + 8
    paddw           m12, m10 ; 9 * (q0 - p0) - 3 * ( q1 - p1 )
    psraw           m12, 4; >> 4 , delta0
    PABSW           m13, m12; abs(delta0)
%elif cpuflag(ssse3)
    pabsw           m13, m12
    paddw           m10, m13, m13
    paddw           m13, [pw_8]
    paddw           m13, m10 ; abs(9 * (q0 - p0) - 3 * ( q1 - p1 ))
    pxor            m10, m10
    pcmpgtw         m10, m12
    paddw           m13, m10
    psrlw           m13, 4; >> 4, abs(delta0)
    psignw          m10, m13, m12
    SWAP             10, 12
%else
    pxor            m10, m10
    pcmpgtw         m10, m12
    pxor            m12, m10
    psubw           m12, m10 ; abs()
    paddw           m13, m12, m12
    paddw           m12, [pw_8]
    paddw           m13, m12 ; 3*abs(m12)
    paddw           m13, m10
    psrlw           m13, 4
    pxor            m12, m13, m10
    psubw           m12, m10
%endif

    psllw           m10, m9, 2; 8 * tc
    paddw           m10, m9; 10 * tc
    pcmpgtw         m10, m13
    pand            m11, m10

    psraw            m9, 1;   tc * 2 -> tc
    psraw           m14, 1; -tc * 2 -> -tc

    pmaxsw          m12, m14
    pminsw          m12, m9;  av_clip(delta0, -tc, tc)

    psraw            m9, 1;   tc -> tc / 2
%if cpuflag(ssse3)
    psignw          m14, m9, [pw_m1]; -tc / 2
%else
    pmullw          m14, m9, [pw_m1]; -tc / 2
%endif

    pavgw           m15, m1, m3;   (p2 + p0 + 1) >> 1
    psubw           m15, m2;  ((p2 + p0 + 1) >> 1) - p1
    paddw           m15, m12; ((p2 + p0 + 1) >> 1) - p1 + delta0
    psraw           m15, 1;   (((p2 + p0 + 1) >> 1) - p1 + delta0) >> 1
    pmaxsw          m15, m14
    pminsw          m15, m9; av_clip(deltap1, -tc/2, tc/2)
    paddw           m15, m2; p1'

    ;beta calculations
    movd            m10, betad
    SPLATW          m10, m10, 0

    movd            m13, r7d; 1dp0 + 1dp3
    movd             m8, r8d; 0dp0 + 0dp3
    punpcklwd        m8, m8
    punpcklwd       m13, m13
    shufps          m13, m8, 0;
    pcmpgtw          m8, m10, m13
    pand             m8, m11
    ;end beta calculations
    MASKED_COPY2     m2, m15, m8; write p1'

    pavgw            m8, m6, m4;   (q2 + q0 + 1) >> 1
    psubw            m8, m5;  ((q2 + q0 + 1) >> 1) - q1
    psubw            m8, m12; ((q2 + q0 + 1) >> 1) - q1 - delta0)
    psraw            m8, 1;   ((q2 + q0 + 1) >> 1) - q1 - delta0) >> 1
    pmaxsw           m8, m14
    pminsw           m8, m9; av_clip(deltaq1, -tc/2, tc/2)
    paddw            m8, m5; q1'

    movd            m13, r9d;
    movd            m15, r10d;
    punpcklwd       m15, m15
    punpcklwd       m13, m13
    shufps          m13, m15, 0; dq0 + dq3

    pcmpgtw         m10, m13; compare to ((beta+(beta>>1))>>3)
    pand            m10, m11
    MASKED_COPY2     m5, m8, m10; write q1'

    paddw           m15, m3, m12 ; p0 + delta0
    MASKED_COPY      m3, m15

    psubw            m8, m4, m12 ; q0 - delta0
    MASKED_COPY      m4, m8
%endmacro

%macro LOOP_FILTER_CHROMA 0
cglobal vvc_v_loop_filter_chroma_8, 4, 6, 7, pix, stride, beta, tc, pix0, r3stride
    sub            pixq, 2
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE4x8B_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    WEAK_CHROMA 8
    TRANSPOSE8x4B_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    RET

cglobal vvc_v_loop_filter_chroma_10, 4, 6, 7, pix, stride, beta, tc, pix0, r3stride
    sub            pixq, 4
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE4x8W_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    WEAK_CHROMA 10
    TRANSPOSE8x4W_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq), [pw_pixel_max_10]
    RET

cglobal vvc_v_loop_filter_chroma_12, 4, 6, 7, pix, stride, beta, tc, pix0, r3stride
    sub            pixq, 4
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE4x8W_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    WEAK_CHROMA 12
    TRANSPOSE8x4W_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq), [pw_pixel_max_12]
    RET

cglobal vvc_h_loop_filter_chroma_8, 9, 15, 16, 32, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift , pix0, q_len, src3stride, spatial_mask, tcptr
    lea     src3strideq, [3 * strideq]
    mov           pix0q, pixq
    sub           pix0q, src3strideq
    sub           pix0q, strideq

    movq             m0, [pix0q             ] ;  p1
    movq             m1, [pix0q +   strideq] ;  p1
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

    sub  rsp, 16
    mov spatial_maskq, rsp
    sub rsp, 16
    mov tcptrq, rsp

    SPATIAL_ACTIVITY 8

    movu [spatial_maskq], m11


    pxor            m10, m10
    movd            m11, [max_len_pq]
    punpcklbw       m11, m11, m10
    punpcklwd       m11, m11, m10

    pcmpeqd         m11, [pd_3]


    cmp           shiftd, 1
    je           .strong_chroma
    punpcklqdq       m11, m11, m11
    pshufhw          m13, m11, q2222
    pshuflw          m13, m13, q0000
    movu             m11, m13

.strong_chroma:
    pand             m11, [spatial_maskq]
    movmskps         r14, m11
    cmp              r14, 0
    je              .one_side_chroma
    STRONG_CHROMA

    movq             m14, [pix0q + strideq   ] ;  p1
    movq             m15, [pix0q + 2 * strideq   ] ;  p2

    pxor             m12, m12
    punpcklbw        m14, m12
    punpcklbw        m15, m12

    MASKED_COPY      m14, m1
    MASKED_COPY      m15, m2
    movu             m1, m14
    movu             m2, m15

    packuswb        m12, m1, m2
    movh            [pix0q + strideq   ], m12 ;  p1
    movhps          [pix0q + 2 * strideq   ], m12 ;  p2

.one_side_chroma:
    pcmpeqd  m12, m12, m12
    pxor     m11, m11, m12
    pand     m11, [spatial_maskq]

    movmskps         r14, m11
    cmp              r14, 0
    je              .chroma_weak

    ONE_SIDE_CHROMA

    ; chroma weak
.chroma_weak
    movu     m11, [spatial_maskq]
    pcmpeqd  m12, m12, m12
    pxor     m11, m11, m12
    movu             m9, [tcptrq]
    psignw           m8, m9, [pw_m1];

    WEAK_CHROMA 10

    movq             m12, [pix0q + src3strideq] ;  p0
    movq             m0,  [pixq]                ;  q0
    movq             m14, [pixq +     strideq]  ;  q1
    movq             m15, [pixq + 2 * strideq]  ;  q2

    pxor             m11, m11
    punpcklbw        m12, m11
    punpcklbw        m0, m11
    punpcklbw        m14, m11
    punpcklbw        m15, m11

; no_p
    pxor            m10, m10
    movd            m11, [no_pq]
    punpcklbw       m11, m11, m10
    punpcklwd       m11, m11, m10

    pcmpeqd         m11, m10;

    cmp           shiftd, 1
    je           .no_p_shift
    punpcklqdq       m11, m11, m11
    pshufhw          m13, m11, q2222
    pshuflw          m13, m13, q0000
    jmp      .store_p
.no_p_shift:
    pshufhw          m13, m11, q2301
    pshuflw          m13, m13, q2301
.store_p:
    movu             m11, m13

    MASKED_COPY   m12, m3

; no_q
    pxor            m10, m10
    movd            m11, [no_qq]
    punpcklbw       m11, m11, m10
    punpcklwd       m11, m11, m10

    pcmpeqd         m11, m10;

    cmp           shiftd, 1
    je           .no_q_shift
    punpcklqdq       m11, m11, m11
    pshufhw          m13, m11, q2222
    pshuflw          m13, m13, q0000
    jmp      .store_q
.no_q_shift:
    pshufhw          m13, m11, q2301
    pshuflw          m13, m13, q2301
.store_q:
    movu             m11, m13

    MASKED_COPY   m0, m4
    MASKED_COPY   m14, m5
    MASKED_COPY   m15, m6

    packuswb         m12, m0
    packuswb         m14, m15

    movh     [pix0q + src3strideq], m12
    movhps                  [pixq], m12
    movh      [pixq +     strideq], m14 ; m4
    movhps    [pixq + 2 * strideq], m14 ; m5

    add rsp, 32
RET

cglobal vvc_h_loop_filter_chroma_10, 9, 15, 16, 32, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift , pix0, q_len, src3stride, spatial_mask, tcptr
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

    MASKED_COPY                  [pixq], m4
    MASKED_COPY    [pixq +     strideq], m5 ;
    MASKED_COPY    [pixq + 2 * strideq], m6 ;

    add rsp, 32
RET

cglobal vvc_h_loop_filter_chroma_12, 9, 15, 16, 32, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift , pix0, q_len, src3stride, spatial_mask, tcptr
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

    MASKED_COPY                  [pixq], m4
    MASKED_COPY    [pixq +     strideq], m5 ;
    MASKED_COPY    [pixq + 2 * strideq], m6 ;

    add rsp, 32
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
