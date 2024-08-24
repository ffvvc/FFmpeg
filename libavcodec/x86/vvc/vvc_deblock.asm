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

    punpckldq        m1, m3, m9 ;  0, 1
    punpckhdq        m3, m9     ;  2, 3

    punpckldq        m5, m7, m4 ;  4, 5
    punpckhdq        m7, m4     ;  6, 7

    pxor            m13, m13

    punpcklbw        m0, m1, m13 ; 0 in 16 bit
    punpckhbw        m1, m13     ; 1 in 16 bit

    punpcklbw        m2, m3, m13 ; 2
    punpckhbw        m3, m13     ; 3

    punpcklbw        m4, m5, m13 ; 4
    punpckhbw        m5, m13     ; 5

    punpcklbw        m6, m7, m13 ; 6
    punpckhbw        m7, m13     ; 7
%endmacro

; in: 8 rows of 8 words in m0..m7
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
    movu             m0, %1
    movu             m1, %2
    movu             m2, %3
    movu             m3, %4
    movu             m4, %5
    movu             m5, %6
    movu             m6, %7
    movu             m7, %8
    TRANSPOSE8x8W     0, 1, 2, 3, 4, 5, 6, 7, 8
%endmacro

; in: 8 rows of 8 words in m0..m7
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

    movu             %1, m0
    movu             %2, m1
    movu             %3, m2
    movu             %4, m3
    movu             %5, m4
    movu             %6, m5
    movu             %7, m6
    movu             %8, m7
%endmacro


; in: %2 clobbered
; out: %1
; mask in m11
%macro MASKED_COPY 2
%ifnum sizeof%1
    PBLENDVB         %1, %2, m11
%else
    vpmaskmovd       %1, m11, %2
%endif
%endmacro

; in: %2 clobbered
; out: %1
; mask in %3, will be clobbered
%macro MASKED_COPY2 3
    pand             %2, %3 ; and mask
    pandn            %3, %1 ; and -mask
    por              %2, %3
    movu             %1, %2
%endmacro

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

    movu             m12, m11
    pand             m11, [rsp + 16]
    MASKED_COPY       m3, m14

    movu             m11, m12
    pand             m11, [rsp]
    MASKED_COPY       m4, m15
%endmacro

%macro CLIP_RESTORE 4  ; toclip, value, -tc, +tc
    paddw             %3, %2
    paddw             %4, %2
    CLIPW             %1, %3, %4
    psubw             %3, %2
    psubw             %4, %2
%endmacro

%macro STRONG_CHROMA 1
    cmp            no_pq, 0
    je      .end_p_calcs
    pand             m11, [rsp + 16] ; which p

    ; p0
    paddw            m12, m0, m1
    paddw            m12, m2
    paddw            m12, m3
    paddw            m12, m4
    paddw            m12, [pw_4]
    movu             m15, m12      ; p3 +  p2 + p1 +  p0 + q0 + 4
    paddw            m12, m3
    paddw            m12, m5       ; q1
    paddw            m12, m6       ; q2
    psraw            m12, 3

    ; p1
    paddw            m13, m15, m0     ; + p3
    paddw            m13, m2          ; + p1
    paddw            m13, m5          ; + q1
    psraw            m13, 3
    CLIP_RESTORE     m13, m2, m8, m9

    ; p2
    psllw            m14, m0, 1       ; 2*p3
    paddw            m14, m15
    paddw            m14, m1          ; + p2
    psraw            m14, 3
.end_p_calcs:

    ; q0
    cmp            no_qq, 0
    je      .end_q_calcs
    movu             m11, [rsp + 32]; strong
    pand             m11, [rsp ]    ; strong & q

    paddw            m15, m3, m4    ; p0 + q0

    CLIP_RESTORE     m12, m3, m8, m9
    MASKED_COPY       m3, m12        ; p0

    paddw            m15, m5        ; + q1
    paddw            m15, m6        ; + q2
    paddw            m15, m7        ; + q3
    paddw            m15, [pw_4]
    movu             m12, m15       ; p0 + q0 + q1 + q2 + q3 + 4
    paddw            m12, m1        ; + p2  -- p2 is unused after this point

    CLIP_RESTORE     m14, m1, m8, m9
    MASKED_COPY       m1, m14       ; p2

    paddw            m12, m2        ; + p1
    paddw            m12, m4        ; + q0
    psraw            m12, 3
    CLIP_RESTORE     m12, m4, m8, m9

    ; q1
    paddw            m14, m2, m15; p0 + ...   + p1
    paddw            m14, m5     ; + q1
    paddw            m14, m7     ; + q3
    psraw            m14, 3
    CLIP_RESTORE     m14, m5, m8, m9

    ; q2
    ; clobber m15 - sum is fully used
    paddw            m15, m7  ; + q3
    paddw            m15, m7  ; + q3
    paddw            m15, m6  ; + q2
    psraw            m15, 3
    CLIP_RESTORE     m15, m6, m8, m9

    MASKED_COPY       m2, m13  ; p1
    MASKED_COPY       m4, m12  ; q0
    MASKED_COPY       m5, m14  ; q1
    MASKED_COPY       m6, m15  ; q2
.end_q_calcs:
%endmacro

; m11 strong mask, m8/m9 -tc, tc
%macro SPATIAL_ACTIVITY 2
%if %2 == 0
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
%endif
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

%if %2 == 0
    movu             m0, [rsp + 48]
    movu             m1, [rsp + 64]
%endif

%endmacro

%macro ONE_SIDE_CHROMA 1
    pand       m11, [rsp + 16] ; no_p

    ; p0
    paddw          m12, m3, m4 ;      p0 + q0
    paddw          m12, m5     ;      p0 + q0 + q1
    paddw          m12, m6     ;      p0 + q0 + q1 + q2
    paddw          m12, [pw_4] ;      p0 + q0 + q1 + q2 + 4
    paddw          m12, m2     ; p1 + p0 + q0 + q1 + q2 + 4
    movu           m15, m12
    paddw          m12, m2     ; + p1
    paddw          m12, m2     ; + p1
    paddw          m12, m3     ; + p0
    psrlw          m12, 3

    CLIP_RESTORE   m12, m3, m8, m9
    MASKED_COPY    m3, m12  ; m2

    ; q0
    movu         m11, [rsp + 32] ; strong mask
    pand         m11, [rsp]      ; no_q

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

    MASKED_COPY   m4, m12 ; m3
    MASKED_COPY   m5, m13 ; m4
    MASKED_COPY   m6, m14 ; m5
%endmacro

; CHROMA_DEBLOCK_BODY(bit_depth)
%macro CHROMA_DEBLOCK_BODY 2
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

    SPATIAL_ACTIVITY %1, %2

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

%if %2 == 0
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
%endif

.chroma_weak:
    movu     m11, [spatial_maskq]
    pcmpeqd  m12, m12, m12
    pxor     m11, m11, m12
    movu             m9, [tcptrq]
    psignw           m8, m9, [pw_m1];

    movmskps         r14, m11
    cmp              r14, 0
    je              .end_weak_chroma

    WEAK_CHROMA     %1
.end_weak_chroma:
    add rsp, 112
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
    movq            m13, [betaq];
    punpcklwd       m13, m13
    vpermilps       m13, m13, q2200
%if %1 > 8
    psllw          m13, %1 - 8
%endif
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
%if %1 > 10
    shl             r11, %1 - 10
%elif %1 < 10
    add             r11, 1 << (9 - %1)
    shr             r11, 10 - %1
%endif
    movd             m8, r11d; tc0
    mov             r3d, [tcq+4];
%if %1 >= 10
    shl              r3, %1 - 10
%elif %1 < 10
    add              r3, 1 << (9 - %1)
    shr              r3, 10 - %1
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
    ;----max_len comparison end---
    mov            r11q, r6mp
    pinsrw          m12, [r11q], 0; max_len_p
    mov            r11q, r7mp
    pinsrw          m12, [r11q], 1; max_len_q
    pxor            m13, m13
    punpcklbw       m12, m13
    pshuflw         m12, m12, q3120
    punpcklwd       m12, m12
    pcmpgtw         m12, [pw_2]; max_len comparisons
    movmskps        r11, m12
    and              r6, r11; strong mask, beta_2, beta_3 and tc25 and max_len_{q, p} comparisons
    ;----max_len comparison end---
    mov             r11, r6;
    shr             r11, 1;
    and             r6, r11; strong mask, bits 2 and 0

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

    mova            m15, m9
    psllw            m9, 2;          4*tc
    psubw           m14, m15, m9;   -3*tc
    psubw            m9, m15;        3*tc

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
    CLIPW           m12, m14, m9; av_clip( , -3 * tc, 3 * tc)
    paddw           m12, m3; p0'

    paddw           m14, m15;  -2*tc
    psubw            m9, m15;   2*tc

    paddw           m15, m1, m10; p2 + p1 + p0 + q0
    psrlw           m13, 1; 2 in every cell
    paddw           m15, m13; p2 + p1 + p0 + q0 + 2
    psraw           m15, 2;  (p2 + p1 + p0 + q0 + 2) >> 2
    psubw           m15, m2;((p2 + p1 + p0 + q0 + 2) >> 2) - p1
    CLIPW           m15, m14, m9; av_clip( , -2 * tc, 2 * tc)
    paddw           m15, m2; p1'

    psraw            m9, 1;      tc
    psraw           m14, 1;     -tc

    paddw            m8, m1, m0;     p3 +   p2
    paddw            m8, m8;   2*p3 + 2*p2
    paddw            m8, m1;   2*p3 + 3*p2
    paddw            m8, m10;  2*p3 + 3*p2 + p1 + p0 + q0
    paddw           m13, m13
    paddw            m8, m13;  2*p3 + 3*p2 + p1 + p0 + q0 + 4
    psraw            m8, 3;   (2*p3 + 3*p2 + p1 + p0 + q0 + 4) >> 3
    psubw            m8, m1; ((2*p3 + 3*p2 + p1 + p0 + q0 + 4) >> 3) - p2
    CLIPW            m8, m14, m9; av_clip( , -2 * tc, 2 * tc)
    paddw            m8, m1; p2'
    MASKED_COPY      m1, m8

    mova            m10, m9
    psllw            m9, 2;          4*tc
    psubw           m14, m10, m9;   -3*tc
    psubw            m9, m10;        3*tc

    paddw            m8, m3, m4;         p0 +   q0
    paddw            m8, m5;         p0 +   q0 +   q1
    paddw            m8, m8;       2*p0 + 2*q0 + 2*q1
    paddw            m8, m2;  p1 + 2*p0 + 2*q0 + 2*q1
    paddw            m8, m6;  p1 + 2*p0 + 2*q0 + 2*q1 + q2
    paddw            m8, m13; p1 + 2*p0 + 2*q0 + 2*q1 + q2 + 4
    psraw            m8, 3;  (p1 + 2*p0 + 2*q0 + 2*q1 + q2 + 4) >>3
    psubw            m8, m4;
    CLIPW            m8, m14, m9; av_clip( , -3 * tc, 3 * tc)
    paddw            m8, m4; q0'
    MASKED_COPY      m2, m15

    paddw           m14, m10;  -2*tc
    psubw            m9, m10;   2*tc

    paddw           m15, m3, m4;   p0 + q0
    paddw           m15, m5;   p0 + q0 + q1
    mova            m10, m15;
    paddw           m15, m6;   p0 + q0 + q1 + q2
    psrlw           m13, 1; 2 in every cell
    paddw           m15, m13;  p0 + q0 + q1 + q2 + 2
    psraw           m15, 2;   (p0 + q0 + q1 + q2 + 2) >> 2
    psubw           m15, m5; ((p0 + q0 + q1 + q2 + 2) >> 2) - q1
    CLIPW           m15, m14, m9; av_clip( , -2 * tc, 2 * tc)
    paddw           m15, m5; q1'

    psraw            m9, 1;      tc
    psraw           m14, 1;     -tc

    paddw           m13, m7;      q3 + 2
    paddw           m13, m6;      q3 +  q2 + 2
    paddw           m13, m13;   2*q3 + 2*q2 + 4
    paddw           m13, m6;    2*q3 + 3*q2 + 4
    paddw           m13, m10;   2*q3 + 3*q2 + q1 + q0 + p0 + 4
    psraw           m13, 3;    (2*q3 + 3*q2 + q1 + q0 + p0 + 4) >> 3
    psubw           m13, m6;  ((2*q3 + 3*q2 + q1 + q0 + p0 + 4) >> 3) - q2
    CLIPW           m13, m14, m9; av_clip( , -2 * tc, 2 * tc)
    paddw           m13, m6; q2'

    MASKED_COPY      m6, m13
    MASKED_COPY      m5, m15
    MASKED_COPY      m4, m8
    MASKED_COPY      m3, m12

.weakfilter:
    pmullw          m14, m9, [pw_m2]; -tc * 2
    paddw            m9, m9

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

    CLIPW           m12, m14, m9;  av_clip(delta0, -tc, tc)

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
    CLIPW           m15, m14, m9; av_clip(deltap1, -tc/2, tc/2)
    paddw           m15, m2; p1'

    ;beta calculations
    movq            m10, [betaq]
    punpcklwd       m10, m10
    vpermilps       m10, m10, q2200
%if %1 > 8
    psllw           m10, %1 - 8
%endif
    psrlw           m13, m10, 1
    paddw           m10, m13
    psrlw           m10, m10, 3

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
    CLIPW            m8, m14, m9; av_clip(deltaq1, -tc/2, tc/2)
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
cglobal vvc_v_loop_filter_chroma_8, 9, 15, 16, 112, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift , pix0, q_len, src3stride, spatial_mask, tcptr
    mov          pix0q, pixq
    sub           pix0q, 4
    lea     src3strideq, [3 * strideq]
    lea            pixq, [pix0q + 4 * strideq]

    TRANSPOSE8x8B_LOAD  [pix0q], [pix0q + strideq], [pix0q + 2 * strideq], [pix0q + src3strideq], [pixq], [pixq + strideq], [pixq + 2 * strideq], [pixq + src3strideq]
    CHROMA_DEBLOCK_BODY 8, 1
    TRANSPOSE8x8B_STORE [pix0q], [pix0q + strideq], [pix0q + 2 * strideq], [pix0q + src3strideq], [pixq], [pixq + strideq], [pixq + 2 * strideq], [pixq + src3strideq]

    RET

cglobal vvc_v_loop_filter_chroma_10, 9, 15, 16, 112, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift , pix0, q_len, src3stride, spatial_mask, tcptr
    mov          pix0q, pixq
    sub           pix0q, 8
    lea     src3strideq, [3 * strideq]
    lea            pixq, [pix0q + 4 * strideq]

    TRANSPOSE8x8W_LOAD  [pix0q], [pix0q + strideq], [pix0q + 2 * strideq], [pix0q + src3strideq], [pixq], [pixq + strideq], [pixq + 2 * strideq], [pixq + src3strideq]
    CHROMA_DEBLOCK_BODY 10, 1
    TRANSPOSE8x8W_STORE [pix0q], [pix0q + strideq], [pix0q + 2 * strideq], [pix0q + src3strideq], [pixq], [pixq + strideq], [pixq + 2 * strideq], [pixq + src3strideq], [pw_pixel_max_10]

    RET

cglobal vvc_v_loop_filter_chroma_12, 9, 15, 16, 112, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift , pix0, q_len, src3stride, spatial_mask, tcptr
    mov          pix0q, pixq
    sub           pix0q, 8
    lea     src3strideq, [3 * strideq]
    lea            pixq, [pix0q + 4 * strideq]
    TRANSPOSE8x8W_LOAD  [pix0q], [pix0q + strideq], [pix0q + 2 * strideq], [pix0q + src3strideq], [pixq], [pixq + strideq], [pixq + 2 * strideq], [pixq + src3strideq]
    CHROMA_DEBLOCK_BODY 12, 1
    TRANSPOSE8x8W_STORE [pix0q], [pix0q + strideq], [pix0q + 2 * strideq], [pix0q + src3strideq], [pixq], [pixq + strideq], [pixq + 2 * strideq], [pixq + src3strideq], [pw_pixel_max_12]

    RET

cglobal vvc_h_loop_filter_chroma_8, 9, 15, 16, 112, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift , pix0, q_len, src3stride, spatial_mask, tcptr
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

    CHROMA_DEBLOCK_BODY 8, 0

    packuswb          m0, m1
    packuswb          m2, m3
    packuswb          m4, m5
    packuswb          m6, m7

    movh     [pix0q             ], m0
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

    CHROMA_DEBLOCK_BODY 10, 0

    pxor           m12, m12
    CLIPW           m1, m12, [pw_pixel_max_10] ; p2
    CLIPW           m2, m12, [pw_pixel_max_10] ; p1
    CLIPW           m3, m12, [pw_pixel_max_10] ; p0
    CLIPW           m4, m12, [pw_pixel_max_10] ; q0
    CLIPW           m5, m12, [pw_pixel_max_10] ; q1
    CLIPW           m6, m12, [pw_pixel_max_10] ; q2

    movu   [pix0q]              , m0
    movu   [pix0q +     strideq], m1
    movu   [pix0q +   2*strideq], m2
    movu   [pix0q + src3strideq], m3

    movu                  [pixq], m4
    movu    [pixq +     strideq], m5 ;
    movu    [pixq + 2 * strideq], m6 ;
    movu    [pixq + src3strideq], m7  ;  q3

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

    CHROMA_DEBLOCK_BODY 12, 0

    pxor           m12, m12
    CLIPW           m1, m12, [pw_pixel_max_12] ; p2
    CLIPW           m2, m12, [pw_pixel_max_12] ; p1
    CLIPW           m3, m12, [pw_pixel_max_12] ; p0
    CLIPW           m4, m12, [pw_pixel_max_12] ; q0
    CLIPW           m5, m12, [pw_pixel_max_12] ; p0
    CLIPW           m6, m12, [pw_pixel_max_12] ; p0

    movu   [pix0q]              , m0
    movu   [pix0q +     strideq], m1
    movu   [pix0q +   2*strideq], m2
    movu   [pix0q + src3strideq], m3
    movu                  [pixq], m4
    movu    [pixq +     strideq], m5 ;
    movu    [pixq + 2 * strideq], m6 ;
    movu    [pixq + src3strideq], m7  ;  q3
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
