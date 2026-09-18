;*******************************************************************************
; VOOM.asm - 6809 side of the vxt_smart port of VOOM (Sprite_tm's Doom E1M1
; Copyright (C) 2026 Caelotronics.
; wireframe renderer).
;
; This file is small because:
;   All of VOOM's logic (Doom BSP renderer + line-chaining walk) runs on the
;   STM32 (code/stm32/game/VOOM.c + voom_smart.c) - the toolkit's VOOM model,
;   same as the original. This file is the standard toolkit cart structure
;   (address-publish handshake + frame loop) with no sound region, since
;   VOOM has none.
;
; INPUT
;   Joy_Digital, matching VOOM's original digital direction reading (jx>0 ->
;   RIGHT, jy>0 -> UP) - see code/stm32/vxt/voom_smart_handler.c.
;
; FRAME PACING
;   Wait_Recal (50Hz beam recalibrate). The RPC blocks until the STM32 has
;   finished computing the frame, so VOOM's per-frame compute sets the real
;   cadence.
;*******************************************************************************
        include "vectrex.i"

        ORG     0
        fcb     "g GCE 2026", $80
        fdb     music1
        fcb     $F8, $50, $20, -$56
        fcb     "VOOM SMART", $80
        fcb     0

        jmp     main

        setdp   #$d0

        include "vxt/vxt_rpc_macros.asm"
        include "vxt/vxt_input.asm"
        include "vxt/vxt_smart.asm"

VXT_RPC_ID_SMART_ADDRS EQU 69           ; engine-level handshake, same ID
                                        ; every vxt_smart-using app publishes
                                        ; its routine addresses against
VXT_RPC_ID_VOOM_SMART  EQU 77           ; this game's own ID, inside the
                                        ; toolkit's reserved block

;-------------------------------------------------------------------------------
main
        VXT_RPC_INIT
        VXT_INPUT_INIT

        ; --- one-shot address publish - ALL 12 entries, following the
        ; standard toolkit cart's proven sequence.
        ;
        ; Publish all 12 addresses, not just the entries a "plain Move/Draw
        ; at scale 12 covers every delta in the source's small 256x256
        ; screen space in one record" argument seems to need. That argument
        ; confuses the source's RAW screen units with PHYSICAL beam units:
        ; beam deflection is scale x rate, and the reference renderer drew
        ; every source unit at 96 physical units, not 1. Drawing at scale 12
        ; with a rate of raw/12 makes the whole picture ~1/96 size - an
        ; invisible dot, which reads as a blank screen. See voom_smart.c's
        ; header for the full derivation and the host measurement.
        ;
        ; voom_smart.c now emits through vxtSmartMoveBig() (SM_startMoveBig_d,
        ; parm[14]/[15]) and vxtSmartDrawHuge() (SM_startDrawHuge_d,
        ; parm[18]/[19]). Unpublished entries are NOT zero - the handler
        ; reads whatever stale parmRam bytes sit there - so calling one of
        ; those without publishing it jumps the 6809 to a garbage address.
        ; Publishing the full 12 also leaves the Huge16 path available if a
        ; future change ever needs a ramp longer than 8 bits.
        ldd     #SM_setScale
        std     $7f00
        ldd     #SM_setIntensity
        std     $7f02
        ldd     #SM_recenter
        std     $7f04
        ldd     #SM_startMove_d
        std     $7f06
        ldd     #SM_startDraw_d
        std     $7f08
        ldd     #SM_continue_d
        std     $7f0a
        ldd     #SM_end
        std     $7f0c
        ldd     #SM_startMoveBig_d      ; scale-32 blanked reposition -
        std     $7f0e                   ; VOOM's every move goes here now
        ldd     #SM_startDrawBig_d
        std     $7f10
        ldd     #SM_startDrawHuge_d     ; genuine Timer 1 poll, correct at
        std     $7f12                   ; ANY scale - VOOM's every line
        ldd     #SM_setScaleHi          ; draw goes here now
        std     $7f14
        ldd     #SM_startDrawHuge16_d
        std     $7f16
        ; startDraw32 MUST be published even though this cart may never
        ; choose it: the STM32 handler reads all 13 slots, and the
        ; dispatcher - not this cart - selects Draw32 for short lines.
        ; Left unpublished, $7F18 holds whatever the PREVIOUS cart wrote,
        ; which passes vxtSmartHasDraw32()'s range check because every
        ; cart links the same engine, and short lines then jump into
        ; unrelated code - an invisible measuring caret, depending only
        ; on which cart ran before this one.
        ldd     #SM_startDraw32_d
        std     $7f18
        VXT_RPC #VXT_RPC_ID_SMART_ADDRS   ; blocks until the STM32 has stored them

frame_loop
        jsr     Wait_Recal

        VXT_INPUT_READ  Joy_Digital     ; signed axes -> parmRam[254]/[253]

        VXT_RPC #VXT_RPC_ID_VOOM_SMART   ; STM32 runs one VOOM frame ->
                                        ; vxt_smart list at $0800

        ldu     #$0800
        jsr     vxt_smart

        bra     frame_loop

        include "vxt/vxt_rpc_stub.asm"  ; RAM-resident stub - MUST be last
