;*******************************************************************************
; test6c.asm — 6809 side of TEST 6c: multi-channel sound (A4/C5/E5, joystick
; Copyright (C) 2026 Caelotronics.
; select + tune) layered on the vxt_scene compositor (STM32 side).
;
; CONTROLS: joystick X selects the channel to tune (CH1/CH2/CH3, one step per
; press); joystick Y raises or lowers the selected channel's tone while held;
; buttons 3 and 4 decrease and increase the number of boxes drawn.
;
; Text draws via vxt_smart, unifying box+text into ONE region at $0800.
; Do not call vxt_draw against a region nothing writes to: the 6809 reads
; whatever stale or uninitialized bytes happen to sit there and draws
; them as vectors, producing flashing lines from the center. If a file
; does not call vxt_draw, drop its include and VXT_DRAW_INIT entirely
; rather than leaving them wired to a dead region.
;
; This file is the standard toolkit cart structure (address-publish handshake
; + frame loop): all of Test 6c's own logic (channel/frequency state, sound
; emission, box+text drawing) runs on the STM32 in test6c_handler.c. No
; 6809-side logic is specific to this test.
;
; ADDRESS HANDSHAKE: the one-shot publish below is a property of
; vxt_smart.asm itself, not of any one test or application - every cart using
; the SmartList engine publishes the same 8 routine addresses this way.
;*******************************************************************************
        include "vectrex.i"

        ORG     0
        fcb     "g GCE 2026", $80
        fdb     music1
        fcb     $F8, $50, $20, -$56
        fcb     "VXT TEST 6C", $80
        fcb     0

        jmp     main

        setdp   #$d0

        include "vxt/vxt_rpc_macros.asm"
        include "vxt/vxt_input.asm"
        include "vxt/vxt_smart.asm"     ; box region
        include "vxt/vxt_sound.asm"

VXT_RPC_ID_SMART_ADDRS EQU 69           ; the shared SmartList engine handshake,
                                        ; not specific to this test
VXT_RPC_ID_6C          EQU 71           ; this test's own per-frame RPC

;-------------------------------------------------------------------------------
main
        VXT_RPC_INIT
        VXT_INPUT_INIT
        VXT_SOUND_INIT

        ; --- one-shot address publish - the standard SmartList handshake ---
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
        ldd     #SM_startMoveBig_d
        std     $7f0e
        VXT_RPC #VXT_RPC_ID_SMART_ADDRS   ; blocks until the STM32 has stored them

frame_loop
        jsr     Wait_Recal

        VXT_INPUT_READ  Joy_Digital      ; buttons 3/4 (box count) + joystick
                                        ; X/Y (channel select + tune) - all
                                        ; read via this one macro. Joy_Digital's
                                        ; -1/0/1 axis values are sufficient for
                                        ; test6c_handler's sign-only (held
                                        ; direction) logic

        VXT_RPC #VXT_RPC_ID_6C          ; STM32 runs test6c_handler(): reads
                                        ; input, updates channel/frequency
                                        ; state, emits sound, fills $0800
                                        ; with ONE unified SmartList (boxes
                                        ; AND text - see main.c's fix note)

        jsr     vxt_sound

        ldu     #$0800                  ; boxes + text, one list, one call
        jsr     vxt_smart

        bra     frame_loop

        include "vxt/vxt_rpc_stub.asm"
