# The Calibration Rig — Operator's Manual

A practical guide to running `test8_cal`, the toolkit's measurement rig, on
real hardware. For the engineering rationale behind it (why it's a
measurement-only tool, how corrections are derived and applied, the log
file's contract), see the [reference guide](VX-COOP_Reference_Guide.md),
Section 8.

> **Nota bene.** This reference application is a demo only.  The process
> seeks to collect data measurements and apply corrective actions in the draw
> routines.  The corrective measure are only illustrative and not complete
>  — see the reference guide's own *nota bene* in Section 0.

---

## 1. What it is

An analog Vectrex does not draw exactly what the arithmetic asks for: a
drawn line and a blanked reposition of the same nominal length land at
slightly different physical distances, timing measured on hardware differs
from datasheet nominals, and both effects vary from one physical unit to
the next. The rig exists to measure those differences on a specific
machine, rather than guess at them once and bake a guess into firmware.

It is a **standalone toolkit-level cartridge**, not a mode inside any one
game — every application built on VXT needs the same corrections, so the
rig lives on its own cart (`code/app6809/test8_cal/`) rather than being
duplicated per game.

It has **no adjustable values, no persistence, and no settings UI of its
own.** Every screen only measures and records. Offering an adjustment
before the underlying measurement exists would let a slider null out a
defect that is actually a bug in a firmware constant — on one machine,
permanently — and the screen would have no way to tell the two cases
apart. Measure first, adjust only from a written-down number.

## 2. Where the code lives

| Piece | File |
|---|---|
| 6809 cartridge | `code/app6809/test8_cal/test8_cal.asm` |
| STM32 handler — the rig itself (screens, measurement logic, RPC 75/76) | `code/stm32/vxt/vxt_cal.c` / `vxt_cal.h` |
| STM32 loader/saver — reading and writing `/calmeas.csv` | `code/stm32/vxt/vxt_cal_load.c` / `vxt_cal_load.h` |
| Prebuilt binary | `prebuilt/test8_cal.bin` |

Build it yourself with `cd code/app6809 && docker run --rm -v "$PWD":/build
-w /build -u $(id -u):$(id -g) asm6809 asm6809 -B -o
test8_cal/test8_cal.bin test8_cal/test8_cal.asm`, or just copy
`prebuilt/test8_cal.bin` to the multicart's `roms/` folder. It needs the
toolkit-enabled `stm32.bin` (`VXT_ENABLE_CAL=1`, the default) already
flashed.

## 3. How a measurement is taken

Every screen answers the same question: **where did the beam actually
land?** The STM32 cannot see the screen — only the operator can. Each
screen therefore draws a test figure by some draw path, together with an
independently positioned **reference mark** at that figure's ideal
endpoint. The gap between them is the error. The joystick drives a
**measuring caret**, which the operator parks on where the beam visibly
landed; the readout reports the caret's offset from the nearest reference
point, in physical units.

This turns a subjective impression ("that looks a bit off") into a number
that can be recorded, photographed, and compared across sessions or
machines.

## 4. Controls

The controls are the same on every screen:

| Control | Action |
|---|---|
| Joystick | Move the measuring caret onto where the beam actually landed. |
| Button 1, short tap | Cycle to the next screen. |
| Button 1, held + joystick left/right | Cycle the current screen's variant instead of the screen. This also toggles the reference card on and off — every geometry variant is paired with a card-on twin, so the two are always logged as a matched comparison. |
| Button 2 | Toggle whether the accumulated correction is applied to the test figure (raw vs. corrected). Screens with no correction-bearing figure of their own (CENTRE, PRIME) treat this as a no-op. |
| Button 3 | Skip to the next reference item on the current screen. |
| Button 4 | Record the caret's current reading to the log. |

## 5. The screens

| Screen | Measures |
|---|---|
| CENTRE | The true screen center / geometry test card itself |
| LADDER | Length sweep — proportional vs. fixed error across a range of line lengths |
| SCALE | Scale-band probe — comparing the four draw paths (`Draw`, `Draw32`, `DrawBig`, `DrawHuge`) against each other |
| CHAIN | Chain accumulation — whether a closed N-gon actually closes |
| SHARED | Two shapes sharing a vertex (does a shared point land in the same place from both figures?) |
| ANGLE | Direction sweep — error as a function of the angle a line is drawn at |
| DEFLECT | Absolute-deflection sweep — error as a function of distance from center |
| TEXT H | Long strings, drawn horizontally |
| TEXT V | Long strings, with the console physically rotated onto its side |
| PRIME | How many beam-priming cycles it takes to remove the first-drawn-element anomaly |
| REPOS | Reposition-distance sweep across SmartList record boundaries |
| ACCUM | Chain accumulation isolated from whole-figure displacement (fixed-per-record vs. length-proportional error) |
| CHORD | A closed-arc topology: one long chord, then a multi-segment arc back onto its own start |

## 6. The measurement log

Every Button 4 press appends (or updates) a row in `/calmeas.csv` on the
multicart's SD card. This file is what `vxt_cal_load.c` reads at boot to
derive the corrections an application applies via
`gamelibBeamSetDrawGain()` and friends — see the reference guide, Sections
8.2–8.3, for exactly how a row becomes a correction, and why a missing
file or a "no data yet" row must never be treated the same as "measured
zero."

The log is per-machine: a CSV pulled from one cartridge describes that
specific unit's analog behavior and should not be copied onto another
one's SD card as a shortcut.
