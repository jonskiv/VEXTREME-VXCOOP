;*******************************************************************************
; test1_led/main.asm - VXT Test 1: RPC smoke test against STOCK firmware
; Copyright (C) 2026 Caelotronics.
;
; PURPOSE: prove the entire 6809 -> RPC -> STM32 path (RAM stub copy, parmRam
; parameter write, $7FFF trigger, busy-wait, resume) against KNOWN-GOOD,
; UNMODIFIED VEXTREME firmware - before any custom STM32 code exists.
;
; MECHANISM: stock doHandleEvent() case 5 is rainbowStep((int)parmRam[254])
; (main.c dispatch table). We call it once per frame with an incrementing
; step value; the cart's LEDs should animate through the rainbow. No STM32
; rebuild, no reflash - copy this binary to the USB drive and launch it
; from the menu.
;
; FRAME PACING: Wait_Recal (= FRWAIT, $F192, vectrex.i / Vol.2): no entry
; registers required, waits for the frame boundary, RETURNS with DP=$D0.
; Called once per loop so RPCs fire at frame rate (~50/s).
;
; HEADER: standard Vectrex cartridge header (menu.asm org-0 block / Boot doc
; multicart.asm excerpt). Tune tables (ADSR/twang format, note pairs, 0,$80
; terminator) follow menu.asm's vextreme_tune1 structure exactly.
;
; BUILD:   asm6809 -B -o test1.bin main.asm      (menu Makefile convention)
; DEPLOY:  copy test1.bin to the VEXTREME USB drive, launch from menu
; EXPECT:  title screen "VXT TEST1", then LEDs animate continuously.
;          If LEDs freeze or the Vectrex locks: the RPC path is broken.
;*******************************************************************************

  include              "vectrex.i"          ; BIOS equates (Wait_Recal etc.)

;*******************************************************************************
; CARTRIDGE HEADER (org 0)
;*******************************************************************************
  org                  0
  fcb                  "g GCE 2026", $80    ; 'g' is copyright sign
  fdb                  blip_tune            ; header music (BIOS plays at title)
  fcb                  $F8, $50, $20, -$37  ; height, width, rel y, rel x
  fcb                  "VXT TEST1", $80     ; title string
  fcb                  0                    ; end of header

  jmp                  main                 ; explicit entry point - do NOT rely
                                             ; on fall-through, since the include
                                             ; below places real code (the RPC
                                             ; stub, meant only to be copied to
                                             ; RAM and run from there) right
                                             ; after this point in the binary.

;*******************************************************************************
; TOOLKIT
;*******************************************************************************
  include              "vxt/vxt_rpc.asm"

; App RAM variables live after the toolkit's RAM stub:
step_var               equ    VXT_RAM_END   ; 1 byte: rainbow step counter

;*******************************************************************************
; CODE
;*******************************************************************************
main
  VXT_RPC_INIT                              ; copy RPC stub to $C880 RAM

  clr                  step_var

loop
  jsr                  Wait_Recal           ; frame boundary; returns DP=$D0

  lda                  step_var             ; step++
  inca
  sta                  step_var

  sta                  VXT_ARG              ; parmRam[254] = step
  VXT_RPC              5                    ; stock firmware: rainbowStep(step)

  bra                  loop

;*******************************************************************************
; HEADER TUNE - minimal "blip" in the exact vextreme_tune1 structure:
;   fdb ADSR-table, fdb twang-table, (note,duration) pairs, 0,$80 terminator.
; Tables copied from menu.asm (FADE14 / VIBENL definitions).
;*******************************************************************************
FS5                    equ  $23             ; note equates per menu.asm
RST                    equ  $3F

VIBENL                 fcb  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
FADE14                 fdb  $0000,$2DDD,$DDDD,$B000,0,0,0,0

blip_tune
  fdb                  FADE14
  fdb                  VIBENL
  fcb                  FS5,8
  fcb                  RST,8
  fcb                  0,$80                ; end marker
