# Appendix A — The VXT Library Modules

A map of the toolkit: each module, the processor it runs on, and its purpose.
Return to the [reference guide](VX-COOP_Reference_Guide.md).

> See the reference guide's *nota bene* (Section 0): descriptions of prior
> work here are derived from reading its code, not from its authors, and
> should be independently validated.

---

## A.0 Tree organization

```
code/app6809/vxt/        VXT toolkit, 6809 side        — assembly, cartridge-resident
code/stm32/vxt/          VXT toolkit, STM32 side       — the cross-processor protocol
code/stm32/gamelib/      reusable STM32 drawing math   — not part of the protocol
code/stm32/vxcoop/       the reference application     — an application, not toolkit
code/app6809/<app>/      one directory per 6809 cartridge
```

The distinction between `vxt/` and `gamelib/` determines where a change belongs.

- **`vxt/` is the protocol:** wire formats, the memory map, RPC dispatch, the
  6809 engines and their STM32 emitters. A change here normally requires a
  corresponding change on the other processor, and several modules state that
  requirement in their own headers.
- **`gamelib/` is STM32-only mathematics built on top of `vxt_smart`.** Nothing
  in it crosses to the 6809, and any of it may be replaced without modifying an
  assembly file.

One rule spans both: `vxt_draw` and `vxt_smart` are different wire formats and
cannot share a single served-image region. A frame using both requires two
separate regions, each serviced by its own engine call on the 6809 side.

---

## A.1 The 6809 side — `code/app6809/vxt/`

All of this is ordinary cartridge-resident code except the RPC stub body, which
exists to be copied into RAM. All of it requires `DP = $D0` at runtime and
`setdp #$d0` earlier in the source.

### `vxt_rpc_macros.asm` — equates and macros; emits no bytes
Place this include early, before `setdp` and before `main`. It contains only
equates and macro definitions, and the assembler does not emit any of it until a
macro is invoked, so early placement is safe.

| Provides                                                         |                                                                                        |
|------------------------------------------------------------------|----------------------------------------------------------------------------------------|
| `VXT_RPC_INIT`                                                   | copies the stub to `$C880` and sets the runtime `DP`                                   |
| `VXT_RPC #id`                                                    | triggers by immediate identifier; returns when the STM32 completes                     |
| `VXT_RPC_A`                                                      | as above, with the identifier already in `A`                                           |
| `RPC_ARG_ADDR` `$7F00`, `RPC_ID_ADDR` `$7FFF`, `VXT_ARG` `$7FFE` | protocol addresses                                                                     |
| `VXT_RAM_BASE` `$C880`                                           | an independently verified free-RAM boundary, not inherited from VOOM's `$ca00`/`$cb00` |

### `vxt_rpc_stub.asm` — the RAM-resident stub body; include LAST
**19 bytes** of instructions; `VXT_RAM_END` resolves to `$C893`, that is
`$C880 + 19`. The body is `sta >$7FFF`, a poll of `$0000`/`$0001` for `'g'` and
`' '`, and `jmp ,x`. It uses forced extended addressing, so correctness does not
depend on any `setdp` state.

> This file must be physically last in the source, after every branch target. See
> the reference guide, Section 9.2, for the power-on failure that established the rule.

### `vxt_rpc.asm` — the original monolithic version
Retained because it was already proven in use, and left unmodified. Superseded by
the macros/stub division. Do not include it together with the divided pair.

### `vxt_input.asm` — controller state to `parmRam`
`VXT_INPUT_INIT`, once, and
`VXT_INPUT_READ <Joy_Digital|Joy_Analog>`, per frame. Reports both axes signed and
raw, four per-button edge bytes, the held bitmask, and the EDGE byte. Restores
`VIA_DDR_a` after `Read_Btns`; that restore is required, not redundant (reference guide,
Section 4.2).

### `vxt_smart.asm` — the SmartList draw engine
The primary draw engine: `ldu #$0800 / jsr vxt_smart`. Records are
`(A, B, routine-address)` triples dispatched by `pulu a,b,pc`, at approximately 35
cycles per vector against the BIOS's approximately 130.

| Routine                             | Role                                                                 |
|-------------------------------------|----------------------------------------------------------------------|
| `SM_setScale` / `SM_setScaleHi`     | Timer 1 low byte, and staged high byte                               |
| `SM_setIntensity`                   | full 0–127; the only costly routine, being a BIOS call               |
| `SM_recenter`                       | zero the integrators                                                 |
| `SM_startMove_d` / `SM_startDraw_d` | scale 12, 1 NOP                                                      |
| `SM_startMoveBig_d`                 | scale 32, 12 NOPs; hardware-proven                                   |
| `SM_startDraw32_d`                  | scale 32, 12 NOPs; the ladder's fast tier                            |
| `SM_startDrawBig_d`                 | scale 64, 27 NOPs; formula-derived, not independently confirmed      |
| `SM_startDrawHuge_d`                | any scale; polls Timer 1, at approximately 2.2× cost                 |
| `SM_startDrawHuge16_d`              | any 16-bit duration; reach to approximately 6.5M units in one record |
| `SM_continue_d`                     | continues draw or move; the least expensive record available         |
| `SM_end`                            | terminator; blanks the beam and returns                              |

### `vxt_sound.asm` — PSG command-block service
`VXT_SOUND_INIT`, once; `jsr vxt_sound`, per frame, after the RPC and before the
draw; `vxt_sound_panic` to silence everything. Sequence-gated, costing
approximately 12 cycles when nothing has changed. Refuses to write any register at
or above `$0E`, register 14 being the buttons.

### `vxt_draw.asm` — the earlier interpretive engine
The VOOM-format loop: 4-byte records of `intensity, scale, coord1, coord2`,
terminated by `$FF`. Approximately 77 to 98 cycles per record, that is
approximately 2.2 times the cost of SmartList. Its revised version introduced two
ideas of general value:

- a **blank-move fast path**, in which intensity `$00` skips the BIOS intensity
  call and runs the ramp with the beam off, and
- an **intensity cache**, so that a scene drawn at one brightness makes one BIOS
  intensity call per frame rather than hundreds.

The original paid approximately 125 to 130 cycles of fixed overhead per record,
including an unconditional `jsr Intensity_a` on every record, so approximately 280
records exhausted the entire 30,000-cycle frame budget before a line was drawn.
That analysis led to the adoption of SmartList.

**New code should use `vxt_smart`.** `vxt_draw` remains for existing applications
and as a known-good fallback.

---

## A.2 The STM32 side — `code/stm32/vxt/`

### `vxt_rpc.c/h` — dispatch
`vxtRpcInit()`, `vxtRpcRegister(id, fn)`, `vxtRpcDispatch(data)`. A table lookup,
bounds-checked to the reserved range, refusing collisions, with errors reported
over the existing serial debug path. It imposes no cost on the hot path, dispatch
occurring only inside an RPC when ROM serving is already suspended.

### `vxt_frame.c/h` — the `vxt_draw`-format writer, and `vxtImgPut()`
`vxtFrameBegin`, `Vector`, `VectorAt`, `MoveBig`, `End`, `vxtFrameOverflowed()`.

**One function here is required even by applications that use SmartList
exclusively.** `vxtImgPut(pos, byte)` is the sole implementation of the
bank-mirroring rule: runtime writes into the 64K `cartData` buffer must be
mirrored to `pos ^ 0x8000`, because `romemu.S` inverts PB6/A15, while writes into
the 20K `menuData` buffer must not be, mirroring overflowing it. Both
`vxt_smart` and `vxt_sound` route through this single implementation.

Also defines `VXT_FRAME_OFFSET` `$0800` and the recenter-flag and intensity-level
constants.

### `vxt_smart.c/h` — the SmartList emitter
`vxtSmartBegin`, `Scale`, `ScaleHi`, `Intensity`, `Recenter`, `Move`, `Draw`,
`Draw32`, `Cont`, `End`; the chaining emitters `vxtSmartMoveBig`, `DrawBig`,
`DrawHuge`, `DrawHuge16`; and `vxtSmartRecordCount()`,
`vxtSmartOverflowed()`, `vxtSmartReady()`, `vxtSmartHasDraw32()` and
`vxtSmartSetAddrs()` for the RPC 69 handshake.

`vxtSmartIntensity()` is change-gated, so call it unconditionally. An ungated
call costs a full BIOS intensity call per object — approximately 15 percent of the
frame budget in a loop over many objects.
`vxtSmartScale()` is **not** gated; use the held-scale helpers in `gamelib_beam`
for a run of draws at one scale.

### `vxt_sound.c/h` — the AY-3-8912 emitter
`vxtSoundBegin`, `Reg`, `Tone`, `Noise`, `Mixer`, `Envelope`, `Silence`, `End`.
Provides the decimal register map constants, positive-logic mixer masks with the
inversion and bit-6 clearing performed internally, and the envelope-shape
constants. Defines `VXT_SND_OFFSET` `$2000`.

### `vxt_music.c/h` — three-channel player
Built on `vxt_sound`, standing in the same relation to it as `gamelib_beam` does
to `vxt_smart`. Songs are converted offline into per-channel delta-encoded
`{frameDelta, period, amp}` arrays; this module only plays them. One event list
per channel makes playback O(1) per channel per frame with no per-frame search,
which matches the AY's own structure of three independent monophonic oscillators.
The choice was verified against real source data beforehand: the worst case
contained 3 conflicting notes out of 723.

`vxtMusicPlay`, `SwitchSong`, `Stop`, `Update`, `IsPlaying`, `LoopCount`,
`ReassertChannel`, `SetRate`.

Three constraints:
- **`vxtMusicUpdate()` does not open its own sound span**; the caller must already
  be inside one.
- **Use `vxtMusicSwitchSong()` rather than `vxtMusicPlay()`** for a track change
  during play while effects may be active; `Play()` re-arms the mixer claim.
- **Use `vxtMusicLoopCount()`** rather than deriving a pass count from a frame
  counter. Playback advances on real elapsed time, so a caller comparing its own
  frame count against `totalFrames` drifts whenever the frame rate departs from a
  stable 50Hz. A caller expecting exactly two passes measured closer to 2.5.

### `vxt_smart_text.c/h` and its companion font table
`vxtSmartTextBegin`, `Char`, `Str`, `Number`, `Number32`, together with
`SetScale`, `SetIntensity`, `SetOrientation`, `SetFont`, `SetSkewComp` and
`vxtSmartTextWidthPhys()`. Four orientations, two selectable font tables, and a
per-character skew compensation hook supplied by the calibration system.

Ported from `vxt_text` for a measured reason: text on the earlier engine cost
approximately 10,400 of approximately 29,800 cycles at the flicker threshold, or
over one third of the frame for four short strings.

A known gap: `vxtSmartTextBegin()` does not route through
`gamelibRepositionAbs()`, so the global center-offset calibration does not reach
it, and text can therefore disagree slightly with corrected geometry.

### `vxt_text.c/h` and `vxt_digits.c/h` — earlier text and number renderers
`vxt_draw` format, superseded by `vxt_smart_text` and retained for existing
callers. Both headers carry an explicit statement that their geometry is
unverified on hardware, their grid constants being initial estimates.

Their shared technique: each glyph traverses a path whose net beam displacement
is identical regardless of which segments are lit, only the intensity byte
varying per edge. That lets an entire string render as one continuous pen path
from a single recenter.

### `vxt_cal.c/h` — the calibration and measurement rig
Toolkit-level rather than application-level: a standalone cartridge rather than a
mode within an application, so any application built on VXT can use it. Handlers
on RPC 75 and 76. Gated by `VXT_ENABLE_CAL`, default 1.

Its first phase is measurement only: no adjustable values, no persistence, no
settings interface. Offer an adjustment before the corresponding measurement
exists and it can null out a
defect residing in the software's own constants, on one machine, permanently, and
the screen cannot distinguish the two cases.

### `vxt_cal_load.c/h` — reading a unit's calibration at boot
`vxtCalLoadDrawGain`, `ClosureComp`, `Offset`, `TextComp`, and the write side
`vxtCalSaveRow()`. Every loader follows the contract in the reference guide, Section 8.3,
and every loader requires provenance, accepting only rows recorded at identity
draw gain.

`vxtCalSaveRow()` performs an upsert by streaming: it copies the existing CSV to a
temporary file, replaces the single row matching `(screen, variant, ref)`, passes
all other lines through unmodified, and exchanges the files. No in-RAM table of
prior rows is held at any point.

> Blocking SD I/O. One-shot paths only.

### `vxt_bounds.c/h` — screen-edge behavior
Answers one question, so that each application need not re-derive four boundary
tests: what occurs when a moving object crosses the screen edge. Carries the
confirmed extents as `VXT_BOUNDS_HALF_X` (13,500) and `VXT_BOUNDS_HALF_Y`
(18,000).

---

## A.2a `code/stm32/vxcoop/` — the reference application

An application rather than toolkit, and a sibling of the toolkit directories for
that reason. Gated by `VXT_ENABLE_VXCOOP`, default 1 in this fork's Makefile.

| File                       | Contents                                                                       |
|----------------------------|--------------------------------------------------------------------------------|
| `vxcoop_handler.c/h`       | the handler on RPC 78 and 79, with the example techniques the reference guide describes |
| `vxcoop_music_example.c/h` | generated song data, one symbol: `const VxtMusicSong vxcMusic_example`         |

The music data is produced offline and must not be hand-edited:

```bash
python3 tools/vpy_to_music.py docs/VX-COOP/music_example.vpy \
    code/stm32/vxcoop/vxcoop_music_example.c \
    code/stm32/vxcoop/vxcoop_music_example.h example vxc
```

The final argument is the symbol prefix, and passing `vxc` yields the
`vxcMusic_*` symbols and the `VXC_MUSIC_*_H` guard used here. The argument is
optional: omitting it selects the tool's original prefix, so every pre-existing
four-argument invocation still produces byte-identical output.

As generated: 536 events across the three channels (212 / 201 / 123), 855 frames
at 50Hz, approximately 17.1 seconds, looping — roughly 2.1KB of flash and no RAM
beyond the player's own scalars.

---

## A.3 `code/stm32/gamelib/`

STM32-only, built on `vxt_smart`, and not part of the wire protocol.

### `gamelib_beam.c/h` — beam positioning, chaining and calibration
The module carrying the accumulated drawing knowledge. It covers:

- **absolute repositioning** that avoids the artifact a bare recenter leaves
  after an open draw run;
- **chaining** with an internal run-mode tracker, so that a multi-segment path
  remains one continuation chain across multiple calls;
- the **escalation dispatcher**, which selects the least expensive correct
  technique per line, and the **draw-scale ladder**;
- all five **calibration corrections**, each costing no 6809 records;
- **beam priming** and **run closing**.

See [Appendix B](Appendix_B_Reusable_Functions.md) for the function-level guide.

### `gamelib_proj3d.c/h` — rotation and projection
`gamelibProject3D` (weak perspective, no division; shallow volumes),
`gamelibProject3DDeep` (true division; deep volumes), and `gamelibLerpEdge`
(interpolation of a point onto an already-projected chord). Pure mathematics, with
no shared state and no `vxt_smart` dependency.

---

## A.4 Build gates

All in `code/stm32/Makefile`. **Changing any of them requires `make clean`:**
`.SECONDARY:` marks objects intermediate and nothing tracks `CFLAGS` as a
dependency, so a stale relink produces an image matching neither configuration.

| Gate                | Default            | Effect                                                                                           |
|---------------------|--------------------|--------------------------------------------------------------------------------------------------|
| `USE_HW`            | **none; required** | `v0.1`, `v0.2` or `v0.3`. Compile-time; the wrong value waits on a signal that is never asserted |
| `VXT_ENABLE_VXCOOP` | 1                  | link the reference handler (RPC 78/79)                                                           |
| `VXT_ENABLE_CAL`    | 1                  | link the calibration rig (RPC 75/76)                                                             |
| `VXT_ENABLE_VOOM`   | 1                  | link the VOOM-on-SmartList port                                                                  |
| `USE_UF2`           | 0                  | UF2 rather than DFU link layout                                                                  |

This fork has no application layer to gate (that is what `VXT_ENABLE_GAME`
controls in the working repository this toolkit was extracted from), and
does not ship `vxt_scene.c` (an experimental element-pool compositor) or
`sha256.c` (a private-repo unlock-file hash) - both are working-repo-only
and neither has a counterpart here. A plain `make all USE_HW=v0.3`
therefore already builds every module this fork contains: VX-COOP, VOOM,
and the calibration rig, all at once.

Convenience target bundling the required clean:

```bash
make vxcoop USE_HW=v0.3     # clean, then make all VXT_ENABLE_VXCOOP=1
```

**A note on RAM, since it constrains what can be built.** **The stack
consists only of what remains between `_ebss` and the top of RAM**, so
adding a shared scratch array is not free, and the margin must be
re-measured after any change to `.bss`:

```bash
grep -E '_ebss' stm32.map                              # compare against RAM top
arm-none-eabi-objdump -d <your>.o | grep 'sub.*sp, #'  # per-function frame sizes
```

Measured on this fork's own two configurations (STM32F411, 128KB RAM):

| Build | `text` | `data` | `bss` | Margin to `_stack` |
|---|---|---|---|---|
| Bare toolkit (`VXT_ENABLE_VXCOOP=0 VXT_ENABLE_VOOM=0 VXT_ENABLE_CAL=0`) | 65,599 | 2,572 | 97,453 | ~29.3 KB |
| Everything linked (`make all`, this fork's defaults) | 160,399 | 2,628 | 111,445 | ~15.6 KB, ~88% of RAM used |

Unlike the working repository this toolkit was extracted from, where a full
application build leaves only a few hundred bytes of stack, this fork's
"everything linked" configuration still has real headroom. Where the
margin is narrow regardless, prefer the narrowest type that suffices — an
`int16_t` in units of eight rather than a `float`, where the additional
precision is not required.

**Two further Makefile notes.** `flash-release:` is the first target, so a bare
`make` invokes it and fails; use `make all USE_HW=v0.3`. And all gates are
forwarded into the build container explicitly, which they were not originally,
with the result that a flag passed on the command line was silently replaced by
the Makefile's default.

---

## A.5 Module selection

| Requirement                          | Module                                                        |
|--------------------------------------|---------------------------------------------------------------|
| draw anything                        | `vxt_smart` with `gamelib_beam`                               |
| draw one line at least cost          | `gamelibDrawAutoLine()`                                       |
| draw a multi-segment path            | `gamelibChainBigGrouped()`                                    |
| draw a closed figure                 | `gamelibChainBigClosedPerimeter()`                            |
| display text                         | `vxt_smart_text`                                              |
| produce a sound effect               | `vxt_sound`                                                   |
| play music                           | `vxt_music`, with the offline converter                       |
| read the controller                  | `vxt_input.asm` with `vxt_input.h`                            |
| project 3D geometry                  | `gamelib_proj3d`                                              |
| remove an object's own back faces    | reference guide, Section 7.1, with the hull tool                       |
| show geometry through an aperture    | reference guide, Section 7.2                                           |
| hide geometry behind a solid         | reference guide, Section 7.3                                           |
| correct for a unit's analog behavior | `vxt_cal_load` with the `gamelibBeamSet*` setters             |
| persist a setting                    | an application-owned CSV; reference guide, Section 8.5                 |
| handle an object leaving the screen  | `vxt_bounds`                                                  |
| add an RPC                           | `vxtRpcRegister()`, after checking which identifiers are free |

---

**License:** GPLv3. Copyright (C) 2026 Caelotronics.
