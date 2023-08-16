; Copyright © 2023, Frank Plowman
; Copyright © 2018-2021, VideoLAN and dav1d authors
; Copyright © 2018, Two Orioles, LLC
; All rights reserved.
;
; Redistribution and use in source and binary forms, with or without
; modification, are permitted provided that the following conditions are met:
;
; 1. Redistributions of source code must retain the above copyright notice, this
;    list of conditions and the following disclaimer.
;
; 2. Redistributions in binary form must reproduce the above copyright notice,
;    this list of conditions and the following disclaimer in the documentation
;    and/or other materials provided with the distribution.
;
; THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
; ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
; WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
; DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
; ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
; (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
; ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
; (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
; SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

%include "libavutil/x86/x86util.asm"

%if ARCH_X86_64

SECTION_RODATA 16

; Note: The order of (at least some of) those constants matter!

const deint_shuf, db  0,  1,  4,  5,  8,  9, 12, 13,  2,  3,  6,  7, 10, 11, 14, 15

%macro COEF_PAIR 2
vvc_pw_%1_%2:  dw  %1, %2
vvc_pw_m%2_%1: dw -%2, %1
%endmacro

const vvc_pw_64_64,  dw  64, 64
const vvc_pw_64_m64, dw  64, -64
const vvc_pw_m64_64, dw -64, 64
const vvc_pw_36_83,  dw  36, 83
const vvc_pw_m83_36, dw -83, 36
const vvc_pw_64_64,  dw  64, 64
const vvc_pw_m64_64, dw -64, 64
const vvc_pw_512,    times 2 dw 512

const vvc_pd_64,     dd 64
const vvc_pd_512,    dd 512

%endif ; ARCH_X86_64
