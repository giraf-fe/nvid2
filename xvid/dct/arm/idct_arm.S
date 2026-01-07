@*****************************************************************************
@*
@*  XVID MPEG-4 VIDEO CODEC
@*  - Inverse DCT for ARM926EJ-S (ARMv5TEJ) -
@*
@*  Hand-optimized assembly for 5-stage integer pipeline
@*
@*****************************************************************************

	.text
	.align 2

@ =============================================================================
@ Constants
@ =============================================================================

	.equ ROW_SHIFT, 11
	.equ COL_SHIFT, 6

@ Note: These are UNSIGNED constants, Tan3 > 32767 so can't use smulbb
	.equ Tan1,  0x32ec   @ tan(pi/16) * 65536 = 13036
	.equ Tan2,  0x6a0a   @ tan(2*pi/16) * 65536 = 27146
	.equ Tan3,  0xab0e   @ tan(3*pi/16) * 65536 = 43790 (> 32767!)
	.equ Sqrt2, 0x5a82   @ sqrt(2) * 32768 = 23170

@ =============================================================================
@ idct_int32_arm - Main IDCT entry point
@ =============================================================================
@ void idct_int32_arm(short *const In)
@ r0: pointer to 8x8 block of shorts (128 bytes)
@ =============================================================================

	.global idct_int32_arm
	.type idct_int32_arm, %function

idct_int32_arm:
	stmfd sp!, {r4-r11, lr}
	sub   sp, sp, #4        @ Local storage

	mov   r4, r0            @ r4 = block pointer (preserved)
	mov   r5, #0x07         @ Rows = 0x07 (first 3 rows always processed)

	@ === Row 0 ===
	mov   r0, r4
	adr   r1, Tab04
	mov   r2, #0x10000      @ Rnd0 = 65536
	bl    Idct_Row_arm

	@ === Row 1 ===
	add   r0, r4, #16
	adr   r1, Tab17
	ldr   r2, .L_Rnd1
	bl    Idct_Row_arm

	@ === Row 2 ===
	add   r0, r4, #32
	adr   r1, Tab26
	ldr   r2, .L_Rnd2
	bl    Idct_Row_arm

	@ === Row 3 ===
	add   r0, r4, #48
	adr   r1, Tab35
	ldr   r2, .L_Rnd3
	bl    Idct_Row_arm
	cmp   r0, #0
	orrne r5, r5, #0x08

	@ === Row 4 ===
	add   r0, r4, #64
	adr   r1, Tab04
	mov   r2, #0            @ Rnd4 = 0
	bl    Idct_Row_arm
	cmp   r0, #0
	orrne r5, r5, #0x10

	@ === Row 5 ===
	add   r0, r4, #80
	adr   r1, Tab35
	mov   r2, #120          @ Rnd5 = 120
	bl    Idct_Row_arm
	cmp   r0, #0
	orrne r5, r5, #0x20

	@ === Row 6 ===
	add   r0, r4, #96
	adr   r1, Tab26
	mov   r2, #512          @ Rnd6 = 512
	bl    Idct_Row_arm
	cmp   r0, #0
	orrne r5, r5, #0x40

	@ === Row 7 ===
	add   r0, r4, #112
	adr   r1, Tab17
	mov   r2, #512          @ Rnd7 = 512
	bl    Idct_Row_arm
	cmp   r0, #0
	orrne r5, r5, #0x80

	@ === Column Transforms ===
	@ Decide which column function based on Rows
	tst   r5, #0xf0
	bne   .L_col8_loop

	tst   r5, #0x08
	bne   .L_col4_loop

	@ Fall through to col3

.L_col3_loop:
	mov   r6, #8            @ Loop counter
.L_col3_iter:
	mov   r0, r4
	bl    Idct_Col_3_arm
	add   r4, r4, #2        @ Next column
	subs  r6, r6, #1
	bne   .L_col3_iter
	b     .L_idct_done

.L_col4_loop:
	mov   r6, #8
.L_col4_iter:
	mov   r0, r4
	bl    Idct_Col_4_arm
	add   r4, r4, #2
	subs  r6, r6, #1
	bne   .L_col4_iter
	b     .L_idct_done

.L_col8_loop:
	mov   r6, #8
.L_col8_iter:
	mov   r0, r4
	bl    Idct_Col_8_arm
	add   r4, r4, #2
	subs  r6, r6, #1
	bne   .L_col8_iter

.L_idct_done:
	add   sp, sp, #4
	ldmfd sp!, {r4-r11, pc}

@ Literal pool for main function
.L_Rnd1:
	.word 3597
.L_Rnd2:
	.word 2260
.L_Rnd3:
	.word 1203

@ =============================================================================
@ Lookup Tables (placed right after main for adr access)
@ =============================================================================

	.align 4
Tab04:
	.word 22725   @ C1
	.word 21407   @ C2
	.word 19266   @ C3
	.word 16384   @ C4
	.word 12873   @ C5
	.word 8867    @ C6
	.word 4520    @ C7

Tab17:
	.word 31521
	.word 29692
	.word 26722
	.word 22725
	.word 17855
	.word 12299
	.word 6270

Tab26:
	.word 29692
	.word 27969
	.word 25172
	.word 21407
	.word 16819
	.word 11585
	.word 5906

Tab35:
	.word 26722
	.word 25172
	.word 22654
	.word 19266
	.word 15137
	.word 10426
	.word 5315

@ =============================================================================
@ Idct_Row_arm - Row transform
@ =============================================================================
@ int Idct_Row_arm(short *In, const int *Tab, int Rnd)
@ Returns: 0 if row is all zeros, 1 otherwise
@ =============================================================================

Idct_Row_arm:
	stmfd sp!, {r4-r11, lr}

	@ Load all 8 coefficients
	ldrsh r3, [r0]          @ In[0]
	ldrsh r4, [r0, #2]      @ In[1]
	ldrsh r5, [r0, #4]      @ In[2]
	ldrsh r6, [r0, #6]      @ In[3]
	ldrsh r7, [r0, #8]      @ In[4]
	ldrsh r8, [r0, #10]     @ In[5]
	ldrsh r9, [r0, #12]     @ In[6]
	ldrsh r10, [r0, #14]    @ In[7]

	@ Right = In[5] | In[6] | In[7]
	orr   r11, r8, r9
	orr   r11, r11, r10

	@ Left = In[1] | In[2] | In[3]
	orr   r12, r4, r5
	orr   r12, r12, r6

	@ Check Right | In[4]
	orr   lr, r11, r7
	orrs  lr, lr, r12       @ Check if any non-DC coefficients

	bne   .L_row_full       @ Has non-zero coeffs, do full calculation

	@ === Sparse case: only In[0] and possibly In[2] ===
	@ K = C4*In[0] + Rnd
	ldr   r11, [r1, #12]    @ C4
	mul   lr, r3, r11       @ C4 * In[0]
	add   lr, lr, r2        @ K = C4*In[0] + Rnd

	@ Check if Left is non-zero (already in r12)
	cmp   r12, #0
	bne   .L_row_full

	@ Only In[0] is non-zero
	mov   r11, lr, asr #ROW_SHIFT
	cmp   r11, #0
	beq   .L_row_zero       @ All zeros, return 0

	@ Sign extend to 16-bit
	mov   r11, r11, lsl #16
	mov   r11, r11, asr #16

	strh  r11, [r0]
	strh  r11, [r0, #2]
	strh  r11, [r0, #4]
	strh  r11, [r0, #6]
	strh  r11, [r0, #8]
	strh  r11, [r0, #10]
	strh  r11, [r0, #12]
	strh  r11, [r0, #14]
	b     .L_row_done

.L_row_zero:
	mov   r0, #0
	ldmfd sp!, {r4-r11, pc}

.L_row_full:
	@ Full row calculation with all coefficients
	@ Load table values
	ldr   r11, [r1, #12]    @ C4
	mul   lr, r3, r11       @ C4*In[0]
	add   lr, lr, r2        @ K = C4*In[0] + Rnd

	@ a0 = K + C2*In[2] + C4*In[4] + C6*In[6]
	@ a1 = K + C6*In[2] - C4*In[4] - C2*In[6]
	@ a2 = K - C6*In[2] - C4*In[4] + C2*In[6]
	@ a3 = K - C2*In[2] + C4*In[4] - C6*In[6]

	ldr   r3, [r1, #4]      @ C2
	ldr   r11, [r1, #20]    @ C6
	ldr   r12, [r1, #12]    @ C4

	@ Save K to stack
	str   lr, [sp, #-4]!

	mul   r2, r5, r3        @ C2*In[2]
	mul   r4, r5, r11       @ C6*In[2]
	mul   r6, r7, r12       @ C4*In[4]
	mul   lr, r9, r11       @ C6*In[6]
	mul   r11, r9, r3       @ C2*In[6]

	ldr   r3, [sp], #4      @ Restore K

	@ a0 = K + C2*In[2] + C4*In[4] + C6*In[6]
	add   r5, r3, r2        @ K + C2*In[2]
	add   r5, r5, r6        @ + C4*In[4]
	add   r5, r5, lr        @ + C6*In[6] = a0

	@ a1 = K + C6*In[2] - C4*In[4] - C2*In[6]
	add   r7, r3, r4        @ K + C6*In[2]
	sub   r7, r7, r6        @ - C4*In[4]
	sub   r7, r7, r11       @ - C2*In[6] = a1

	@ a2 = K - C6*In[2] - C4*In[4] + C2*In[6]
	sub   r9, r3, r4        @ K - C6*In[2]
	sub   r9, r9, r6        @ - C4*In[4]
	add   r9, r9, r11       @ + C2*In[6] = a2

	@ a3 = K - C2*In[2] + C4*In[4] - C6*In[6]
	sub   r3, r3, r2        @ K - C2*In[2]
	add   r3, r3, r6        @ + C4*In[4]
	sub   r3, r3, lr        @ - C6*In[6] = a3

	@ Now compute b0-b3 from odd coefficients
	@ b0 = C1*In[1] + C3*In[3] + C5*In[5] + C7*In[7]
	@ b1 = C3*In[1] - C7*In[3] - C1*In[5] - C5*In[7]
	@ b2 = C5*In[1] - C1*In[3] + C7*In[5] + C3*In[7]
	@ b3 = C7*In[1] - C5*In[3] + C3*In[5] - C1*In[7]

	@ Reload odd coefficients
	ldrsh r2, [r0, #2]      @ In[1]
	ldrsh r4, [r0, #6]      @ In[3]
	ldrsh r6, [r0, #10]     @ In[5]
	ldrsh r11, [r0, #14]    @ In[7]

	@ Load C1, C3, C5, C7
	ldr   lr, [r1, #0]      @ C1
	ldr   r12, [r1, #8]     @ C3

	@ b0 calculation
	mul   r8, r2, lr        @ C1*In[1]
	mla   r8, r4, r12, r8   @ + C3*In[3]
	ldr   lr, [r1, #16]     @ C5
	mla   r8, r6, lr, r8    @ + C5*In[5]
	ldr   lr, [r1, #24]     @ C7
	mla   r8, r11, lr, r8   @ + C7*In[7] = b0

	@ b1 calculation
	ldr   lr, [r1, #8]      @ C3
	mul   r10, r2, lr       @ C3*In[1]
	ldr   lr, [r1, #24]     @ C7
	mul   r12, r4, lr       @ C7*In[3]
	sub   r10, r10, r12     @ - C7*In[3]
	ldr   lr, [r1, #0]      @ C1
	mul   r12, r6, lr       @ C1*In[5]
	sub   r10, r10, r12     @ - C1*In[5]
	ldr   lr, [r1, #16]     @ C5
	mul   r12, r11, lr      @ C5*In[7]
	sub   r10, r10, r12     @ - C5*In[7] = b1

	@ Save a values to stack temporarily
	stmfd sp!, {r3, r5, r7, r9}

	@ b2 calculation
	ldr   lr, [r1, #16]     @ C5
	mul   r3, r2, lr        @ C5*In[1]
	ldr   lr, [r1, #0]      @ C1
	mul   r12, r4, lr       @ C1*In[3]
	sub   r3, r3, r12       @ - C1*In[3]
	ldr   lr, [r1, #24]     @ C7
	mla   r3, r6, lr, r3    @ + C7*In[5]
	ldr   lr, [r1, #8]      @ C3
	mla   r3, r11, lr, r3   @ + C3*In[7] = b2

	@ b3 calculation
	ldr   lr, [r1, #24]     @ C7
	mul   r5, r2, lr        @ C7*In[1]
	ldr   lr, [r1, #16]     @ C5
	mul   r12, r4, lr       @ C5*In[3]
	sub   r5, r5, r12       @ - C5*In[3]
	ldr   lr, [r1, #8]      @ C3
	mla   r5, r6, lr, r5    @ + C3*In[5]
	ldr   lr, [r1, #0]      @ C1
	mul   r12, r11, lr      @ C1*In[7]
	sub   r5, r5, r12       @ - C1*In[7] = b3

	@ b2 in r3, b3 in r5
	@ b0 in r8, b1 in r10
	@ Restore a values: a3, a0, a1, a2
	ldmfd sp!, {r7, r9, r11, r12}
	@ r7=a3, r9=a0, r11=a1, r12=a2

	@ Output calculations
	@ In[0] = (a0 + b0) >> ROW_SHIFT
	add   r2, r9, r8
	mov   r2, r2, asr #ROW_SHIFT
	strh  r2, [r0]

	@ In[1] = (a1 + b1) >> ROW_SHIFT
	add   r2, r11, r10
	mov   r2, r2, asr #ROW_SHIFT
	strh  r2, [r0, #2]

	@ In[2] = (a2 + b2) >> ROW_SHIFT
	add   r2, r12, r3
	mov   r2, r2, asr #ROW_SHIFT
	strh  r2, [r0, #4]

	@ In[3] = (a3 + b3) >> ROW_SHIFT
	add   r2, r7, r5
	mov   r2, r2, asr #ROW_SHIFT
	strh  r2, [r0, #6]

	@ In[4] = (a3 - b3) >> ROW_SHIFT
	sub   r2, r7, r5
	mov   r2, r2, asr #ROW_SHIFT
	strh  r2, [r0, #8]

	@ In[5] = (a2 - b2) >> ROW_SHIFT
	sub   r2, r12, r3
	mov   r2, r2, asr #ROW_SHIFT
	strh  r2, [r0, #10]

	@ In[6] = (a1 - b1) >> ROW_SHIFT
	sub   r2, r11, r10
	mov   r2, r2, asr #ROW_SHIFT
	strh  r2, [r0, #12]

	@ In[7] = (a0 - b0) >> ROW_SHIFT
	sub   r2, r9, r8
	mov   r2, r2, asr #ROW_SHIFT
	strh  r2, [r0, #14]

.L_row_done:
	mov   r0, #1
	ldmfd sp!, {r4-r11, pc}

@ =============================================================================
@ Idct_Col_8_arm - Full column transform (8 rows)
@ =============================================================================
@ void Idct_Col_8_arm(short *const In)
@ Processes one column with 16-byte stride
@
@ Uses regular mul instead of smulbb because Tan3=0xab0e > 32767
@ =============================================================================

Idct_Col_8_arm:
	stmfd sp!, {r4-r11, lr}

	@ Load odd rows: In[7*8], In[5*8], In[3*8], In[1*8]
	ldrsh r4, [r0, #112]    @ mm4 = In[7*8]
	ldrsh r5, [r0, #80]     @ mm5 = In[5*8]
	ldrsh r6, [r0, #48]     @ mm6 = In[3*8]
	ldrsh r7, [r0, #16]     @ mm7 = In[1*8]

	@ Load constants from literal pool
	ldr   r12, .L_Tan1
	ldr   lr, .L_Tan3

	@ mm0 = MULT(Tan1, mm4, 16) + mm7 = ((Tan1 * mm4) >> 16) + mm7
	mul   r8, r12, r4       @ Tan1 * In[7*8]
	mov   r8, r8, asr #16
	add   r8, r7, r8        @ mm0 = mm7 + (Tan1*mm4 >> 16)

	@ mm1 = MULT(Tan1, mm7, 16) - mm4
	mul   r9, r12, r7       @ Tan1 * In[1*8]
	mov   r9, r9, asr #16
	sub   r9, r9, r4        @ mm1 = (Tan1*mm7 >> 16) - mm4

	@ mm2 = MULT(Tan3, mm5, 16) + mm6
	mul   r3, lr, r5        @ Tan3 * In[5*8]
	mov   r3, r3, asr #16
	add   r3, r6, r3        @ mm2 = mm6 + (Tan3*mm5 >> 16)

	@ mm3 = MULT(Tan3, mm6, 16) - mm5
	mul   r2, lr, r6        @ Tan3 * In[3*8]
	mov   r2, r2, asr #16
	sub   r2, r2, r5        @ mm3 = (Tan3*mm6 >> 16) - mm5

	@ mm7 = mm0 + mm2
	add   r7, r8, r3

	@ mm4 = mm1 - mm3
	sub   r4, r9, r2

	@ mm0 = mm0 - mm2
	sub   r8, r8, r3

	@ mm1 = mm1 + mm3
	add   r9, r9, r2

	@ mm6 = mm0 + mm1, mm5 = mm0 - mm1
	add   r6, r8, r9
	sub   r5, r8, r9

	@ Load Sqrt2 constant
	ldr   r12, .L_Sqrt2

	@ mm5 = 2*MULT(Sqrt2, mm5, 16) = 2 * ((Sqrt2 * mm5) >> 16)
	mul   r5, r12, r5
	mov   r5, r5, asr #15   @ >> 16 then *2 = >> 15

	@ mm6 = 2*MULT(Sqrt2, mm6, 16)
	mul   r6, r12, r6
	mov   r6, r6, asr #15

	@ === Even part ===
	@ mm1 = In[2*8], mm2 = In[6*8]
	ldrsh r9, [r0, #32]     @ mm1 = In[2*8]
	ldrsh r2, [r0, #96]     @ mm2 = In[6*8]

	@ Load Tan2 constant
	ldr   r12, .L_Tan2

	@ mm3 = MULT(Tan2, mm2, 16) + mm1
	mul   r3, r12, r2       @ Tan2 * In[6*8]
	mov   r3, r3, asr #16
	add   r3, r9, r3        @ mm3 = mm1 + (Tan2*mm2 >> 16)

	@ mm2 = MULT(Tan2, mm1, 16) - mm2
	mul   r8, r12, r9       @ Tan2 * In[2*8]
	mov   r8, r8, asr #16
	sub   r2, r8, r2        @ mm2 = (Tan2*mm1 >> 16) - mm2

	@ LOAD_BUTF(mm0, mm1, 0*8, 4*8)
	ldrsh r8, [r0]          @ In[0*8]
	ldrsh r9, [r0, #64]     @ In[4*8]
	add   r11, r8, r9       @ mm0 = In[0] + In[4]
	sub   r9, r8, r9        @ mm1 = In[0] - In[4]

	@ BUTF(mm0, mm3): tmp=mm0+mm3; mm3=mm0-mm3; mm0=tmp
	add   r8, r11, r3
	sub   r3, r11, r3
	mov   r11, r8           @ mm0 = mm0 + mm3, mm3 = old_mm0 - mm3

	@ BUTF(mm0, mm7): tmp=mm0+mm7; mm7=mm0-mm7; mm0=tmp
	add   r8, r11, r7
	sub   r7, r11, r7
	mov   r11, r8           @ mm0 = mm0 + mm7, mm7 = old_mm0 - mm7

	@ Store In[0] and In[7*8]
	mov   r8, r11, asr #COL_SHIFT
	strh  r8, [r0]
	mov   r8, r7, asr #COL_SHIFT
	strh  r8, [r0, #112]

	@ BUTF(mm3, mm4)
	add   r8, r3, r4
	sub   r4, r3, r4
	mov   r3, r8

	@ Store In[3*8] and In[4*8]
	mov   r8, r3, asr #COL_SHIFT
	strh  r8, [r0, #48]
	mov   r8, r4, asr #COL_SHIFT
	strh  r8, [r0, #64]

	@ BUTF(mm1, mm2)
	add   r8, r9, r2
	sub   r2, r9, r2
	mov   r9, r8

	@ BUTF(mm1, mm6)
	add   r8, r9, r6
	sub   r6, r9, r6
	mov   r9, r8

	@ Store In[1*8] and In[6*8]
	mov   r8, r9, asr #COL_SHIFT
	strh  r8, [r0, #16]
	mov   r8, r6, asr #COL_SHIFT
	strh  r8, [r0, #96]

	@ BUTF(mm2, mm5)
	add   r8, r2, r5
	sub   r5, r2, r5
	mov   r2, r8

	@ Store In[2*8] and In[5*8]
	mov   r8, r2, asr #COL_SHIFT
	strh  r8, [r0, #32]
	mov   r8, r5, asr #COL_SHIFT
	strh  r8, [r0, #80]

	ldmfd sp!, {r4-r11, pc}

@ Literal pool for column functions
.L_Tan1:
	.word Tan1
.L_Tan2:
	.word Tan2
.L_Tan3:
	.word Tan3
.L_Sqrt2:
	.word Sqrt2

@ =============================================================================
@ Idct_Col_4_arm - Sparse column transform (rows 0-3 only)
@ =============================================================================

Idct_Col_4_arm:
	stmfd sp!, {r4-r11, lr}

	@ Load constants
	ldr   r12, .L_Tan1
	ldr   lr, .L_Tan3

	@ Load: In[1*8], In[3*8] (In[5*8] and In[7*8] are zero)
	ldrsh r8, [r0, #16]     @ mm0 = In[1*8]
	ldrsh r3, [r0, #48]     @ mm2 = In[3*8]

	@ mm1 = MULT(Tan1, mm0, 16)
	mul   r9, r12, r8
	mov   r9, r9, asr #16

	@ mm3 = MULT(Tan3, mm2, 16)
	mul   r2, lr, r3
	mov   r2, r2, asr #16

	@ mm7 = mm0 + mm2
	add   r7, r8, r3

	@ mm4 = mm1 - mm3
	sub   r4, r9, r2

	@ mm0 = mm0 - mm2
	sub   r8, r8, r3

	@ mm1 = mm1 + mm3
	add   r9, r9, r2

	@ mm6 = mm0 + mm1, mm5 = mm0 - mm1
	add   r6, r8, r9
	sub   r5, r8, r9

	@ Load Sqrt2
	ldr   r12, .L_Sqrt2

	@ mm6 = 2*MULT(Sqrt2, mm6, 16)
	mul   r6, r12, r6
	mov   r6, r6, asr #15

	@ mm5 = 2*MULT(Sqrt2, mm5, 16)
	mul   r5, r12, r5
	mov   r5, r5, asr #15

	@ Even part
	ldrsh r8, [r0]          @ mm0 = mm1 = In[0*8]
	mov   r9, r8            @ mm1 = In[0*8]
	ldrsh r3, [r0, #32]     @ mm3 = In[2*8]

	@ Load Tan2
	ldr   r12, .L_Tan2

	@ mm2 = MULT(Tan2, mm3, 16)
	mul   r2, r12, r3
	mov   r2, r2, asr #16

	@ BUTF(mm0, mm3)
	add   r12, r8, r3
	sub   r3, r8, r3
	mov   r8, r12

	@ BUTF(mm0, mm7)
	add   r12, r8, r7
	sub   r7, r8, r7
	mov   r8, r12

	mov   r12, r8, asr #COL_SHIFT
	strh  r12, [r0]
	mov   r12, r7, asr #COL_SHIFT
	strh  r12, [r0, #112]

	@ BUTF(mm3, mm4)
	add   r12, r3, r4
	sub   r4, r3, r4
	mov   r3, r12

	mov   r12, r3, asr #COL_SHIFT
	strh  r12, [r0, #48]
	mov   r12, r4, asr #COL_SHIFT
	strh  r12, [r0, #64]

	@ BUTF(mm1, mm2)
	add   r12, r9, r2
	sub   r2, r9, r2
	mov   r9, r12

	@ BUTF(mm1, mm6)
	add   r12, r9, r6
	sub   r6, r9, r6
	mov   r9, r12

	mov   r12, r9, asr #COL_SHIFT
	strh  r12, [r0, #16]
	mov   r12, r6, asr #COL_SHIFT
	strh  r12, [r0, #96]

	@ BUTF(mm2, mm5)
	add   r12, r2, r5
	sub   r5, r2, r5
	mov   r2, r12

	mov   r12, r2, asr #COL_SHIFT
	strh  r12, [r0, #32]
	mov   r12, r5, asr #COL_SHIFT
	strh  r12, [r0, #80]

	ldmfd sp!, {r4-r11, pc}

@ =============================================================================
@ Idct_Col_3_arm - Very sparse column transform (rows 0-2 only)
@ =============================================================================

Idct_Col_3_arm:
	stmfd sp!, {r4-r11, lr}

	@ Load constants
	ldr   r12, .L_Tan1
	ldr   r10, .L_Sqrt2

	@ Only In[1*8] needed for odd part (In[3*8], [5*8], [7*8] are zero)
	ldrsh r7, [r0, #16]     @ mm7 = In[1*8]

	@ mm4 = MULT(Tan1, mm7, 16)
	mul   r4, r12, r7
	mov   r4, r4, asr #16

	@ mm6 = mm7 + mm4, mm5 = mm7 - mm4
	add   r6, r7, r4
	sub   r5, r7, r4

	@ mm6 = 2*MULT(Sqrt2, mm6, 16)
	mul   r6, r10, r6
	mov   r6, r6, asr #15

	@ mm5 = 2*MULT(Sqrt2, mm5, 16)
	mul   r5, r10, r5
	mov   r5, r5, asr #15

	@ Even part
	ldrsh r8, [r0]          @ mm0 = mm1 = In[0*8]
	mov   r9, r8
	ldrsh r3, [r0, #32]     @ mm3 = In[2*8]

	@ Load Tan2
	ldr   r12, .L_Tan2

	@ mm2 = MULT(Tan2, mm3, 16)
	mul   r2, r12, r3
	mov   r2, r2, asr #16

	@ BUTF(mm0, mm3)
	add   r12, r8, r3
	sub   r3, r8, r3
	mov   r8, r12

	@ BUTF(mm0, mm7)
	add   r12, r8, r7
	sub   r7, r8, r7
	mov   r8, r12

	mov   r12, r8, asr #COL_SHIFT
	strh  r12, [r0]
	mov   r12, r7, asr #COL_SHIFT
	strh  r12, [r0, #112]

	@ BUTF(mm3, mm4)
	add   r12, r3, r4
	sub   r4, r3, r4
	mov   r3, r12

	mov   r12, r3, asr #COL_SHIFT
	strh  r12, [r0, #48]
	mov   r12, r4, asr #COL_SHIFT
	strh  r12, [r0, #64]

	@ BUTF(mm1, mm2)
	add   r12, r9, r2
	sub   r2, r9, r2
	mov   r9, r12

	@ BUTF(mm1, mm6)
	add   r12, r9, r6
	sub   r6, r9, r6
	mov   r9, r12

	mov   r12, r9, asr #COL_SHIFT
	strh  r12, [r0, #16]
	mov   r12, r6, asr #COL_SHIFT
	strh  r12, [r0, #96]

	@ BUTF(mm2, mm5)
	add   r12, r2, r5
	sub   r5, r2, r5
	mov   r2, r12

	mov   r12, r2, asr #COL_SHIFT
	strh  r12, [r0, #32]
	mov   r12, r5, asr #COL_SHIFT
	strh  r12, [r0, #80]

	ldmfd sp!, {r4-r11, pc}
