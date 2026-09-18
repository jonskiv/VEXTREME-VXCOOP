# VX-COOP — Cooperative Dual-CPU Application Development for the Vectrex

A reference guide for the VXT toolkit: how to divide a frame between an STM32 and a
Vectrex 6809, what the communication protocol is, and which routines to call
rather than re-derive.

---

## 0. Scope

The VEXTREME cartridge places an STM32F411 on the Vectrex cartridge bus, where
it presents itself as a ROM. VXT is the toolkit that makes that processor usable
as a general-purpose coprocessor: the STM32 computes each frame's geometry,
state, arithmetic, sound and persistence, while the 6809 — never halted —
renders the result, reads the controller, and services the sound chip.

The architecture originates with Sprite_tm's VOOM (2015). This reference guide documents
the toolkit that generalizes it, and the constraints on its use that were
established subsequently by measurement on hardware.

> **Nota bene.** Anything below describing prior work — VEXTREME's existing
> firmware, Sprite_tm's original VOOM, or similar — is derived from reading
> that code, not from its original authors, and may mischaracterize what it
> actually does or why. Treat those descriptions as one interpretation, not
> an authoritative account: validate independently, and consult the original
> source and its authors where possible, before relying on any of it.

**VX-COOP** is the reference application accompanying this reference guide:

|                |                                                                                                            |
|----------------|------------------------------------------------------------------------------------------------------------|
| 6809 cartridge | `code/app6809/VXCOOP/VXCOOP.asm` — the complete 6809 half, approximately sixty instructions                |
| STM32 handler  | `code/stm32/vxcoop/` — the handler on RPC 78 (per frame) and 79 (boot init), plus its generated music data |
| Build          | `cd code/stm32 && make vxcoop USE_HW=v0.3`                                                                 |
| Appendix A     | [the VXT library modules](Appendix_A_VXT_Libraries.md)                                                     |
| Appendix B     | [function-level reference](Appendix_B_Reusable_Functions.md)                                               |

VX-COOP links no application layer. `make vxcoop` sets `VXT_ENABLE_GAME=0`, so
the resulting image comprises the multicart core, the toolkit, and the reference
handler. Both source files are written to be read in sequence; this reference guide
supplies the derivations.

### Pages of the reference application

Button 4 advances the page; button 3 toggles within a page.

| Page      | Demonstrates                                                                      | Controls               | Section                                   |
|-----------|-----------------------------------------------------------------------------------|------------------------|-------------------------------------------|
| `LADDER`  | single-record long draws against chained draws; the scale ladder and tiered waits | 3 switches technique   | [3](#3-drawing-the-smartlist)             |
| `PROJECT` | 3D-to-2D projection under joystick yaw and pitch                                  | stick rotates          | [6](#6-3d-to-2d-projection)               |
| `CULL`    | backface culling, with the resulting record difference                            | 3 toggles culling      | [7](#7-occlusion-three-distinct-problems) |
| `WINDOW`  | edge occlusion by per-edge clipping against a convex aperture                     | 3 toggles clipping     | [7](#7-occlusion-three-distinct-problems) |
| `SOUND`   | a three-channel song, an explosion effect, and their contention for one channel   | 1 explosion, 2 sweep   | [5](#5-sound)                             |
| `INPUT`   | the raw 6809-to-STM32 input block                                                 | move and press         | [4](#4-input)                             |
| `CAL`     | per-unit calibration: loaded once, applied per frame, saved on a press            | stick adjusts, 1 saves | [8](#8-calibration)                       |

Button 4 advances the page throughout, and button 2 toggles the on-screen text
except on `SOUND`, where it triggers an effect. Every page prints `R`, the records
its own geometry consumed, and `T`, the records for the whole frame including
text. The two are reported separately because the text is a fixed overhead of
approximately 200 records; `R` is the figure to compare between techniques and `T`
the figure to compare against the budget.

### Example assets

The reference application carries three, each labeled in the source with a
block comment naming the data value so that it can be located directly:

| Asset                | Data value                                       | Where                                                                                    |
|----------------------|--------------------------------------------------|------------------------------------------------------------------------------------------|
| A three-channel song | `vxcMusic_example`                               | `vxcoop/vxcoop_music_example.c`, generated offline from `docs/VX-COOP/music_example.vpy` |
| An explosion effect  | `VXC_BOOM_FRAMES`, `_NTYPE`, `_VOL_MAX`, `_CHAN` | `vxcoop/vxcoop_handler.c`, Section 4                                                     |
| A stroke font        | `glyphs[]`                                       | `vxt/vxt_smart_text.c` — a toolkit module, not an asset of this application              |

---

## 1. The machine

```
        ┌───────────────────────┐         cartridge bus        ┌───────────────┐
        │  STM32F411 @ 120MHz   │◄────────────────────────────►│  Vectrex 6809 │
        │                       │  A0-A14   (direct to PC0-15) │    ~1.5 MHz   │
        │  romemu.S IS the ROM  │  D0-D7    (via U4)           │               │
        │  - executes from RAM  │  /CE /OE R/W                 │   6522 VIA    │
        │  - interrupts off     │                              │   → DACs      │
        │  - polls the bus      │                              │   → integrators│
        └───────────────────────┘                              │   → beam      │
                                                               └───────────────┘
```

Three properties of this arrangement determine everything downstream.

**The STM32 is not a peripheral; it is the ROM.** `romemu.S` executes from RAM
with interrupts disabled, polling the address bus and answering reads within the
6809's bus cycle. It uses the DWT cycle counter rather than SysTick precisely
because it cannot tolerate an interrupt. No interrupt may be introduced on this
path.

**The beam is analog.** The VIA drives DACs that charge integrators that deflect
the beam. Physical settling time, rather than processor speed, is the true limit
on how much can be drawn per frame, and it is the reason several of the
corrections in Section 8 exist. An analog integrator does not arrive exactly
where the arithmetic specifies.

**The tube is portrait.** The confirmed extents are half-width ≈ 13,500 and
half-height ≈ 18,000 phys units. A single half-extent constant serving both axes
is incorrect.

### 1.1 The data-bus level shifter

`U4` is an SN74LVC8T245DB, an 8-bit dual-supply bidirectional level-shifting
transceiver between STM32 `GPIOA` and cartridge `D0–D7`.

The address bus requires no translation: `A0–A14` connect directly to
`PC0–PC15`, the STM32F411 having been selected for its 5V-tolerant GPIO inputs.
So the transceiver's role concerns the data bus alone, and only the direction in
which the STM32 must drive 5V logic.

Electrically, the transceiver is not required. TTL input thresholds are 0.8V
low and 2.0V or above high, so a 3.3V CMOS output already satisfies a TTL high
with margin. The board designer states this directly: *"the STM has 5V tolerant
GPIO and TTL is 0.8 low and 2-5 high, so the 0-3.3V output works for Vectrex as
well."* Sprite_tm reached the same conclusion regarding his own earlier design,
having added a converter as a precaution and noting subsequently, after closer
reading of the 6809 datasheet, that it would probably have worked without one.

The transceiver is a stability margin rather than an electrical
necessity. Its propagation delay of approximately 4.3ns is negligible against
both the bus period of 667ns and analog settling times measured in microseconds.

It also performs a second function. U4's `DIR` and
`OE` pins are gated by external hardware rather than by STM32 software. The
designer again: *"It is nice on this slightly slower MCU to
gate the /OE of the data bus with hardware though so the STM doesn't have to
respond to that so quickly. It's handled externally."* That removes bus-timing
work from the STM32.

Removal was proposed and declined. The proposal was to omit the transceiver
and have the STM32 manage `/OE` and `/CE` timing in software, conditional on the
processor being fast enough. The response was to upgrade the processor instead:
*"I think I'd rather spend time to upgrade the MCU than to mess around with
making it more unstable. The buffer doesn't really hurt and not too expensive[,]
just takes up a little space that's all."*

Consequence for an independent cartridge design: a data-bus transceiver can
probably be omitted, and Sprite_tm's original did omit one. What is given up is
not voltage translation but the hardware `/OE` gating and the noise margin, and
both are then paid for in firmware timing budget on a part whose budget is
already constrained.

---

## 2. The RPC protocol

Every other mechanism in the toolkit is built on this one.

### 2.1 The memory map

The 6809 sees a cartridge. Writes to the top page are intercepted.

| Address         | Direction                       | Meaning                                                 |
|-----------------|---------------------------------|---------------------------------------------------------|
| `$0000`–`$0001` | 6809 reads                      | Cartridge header `'g'`, `' '`, and the readiness signal |
| `$0800`–`$1FFF` | STM32 writes, 6809 reads        | The draw list                                           |
| `$2000`–…       | STM32 writes, 6809 reads        | The sound command block                                 |
| `$7F00`–`$7FFE` | 6809 writes → `parmRam[0..254]` | Parameters passed to the STM32                          |
| `$7FFF`         | 6809 writes                     | Invokes `doHandleEvent(byte)` on the STM32              |

**`parmRam` is write-only from the 6809 side.** The 6809 cannot read back what
it wrote there, and the STM32 must not attempt to signal the 6809 through it.
Reads are always served from the ROM image, so **STM32-to-6809 data travels in
the served image**, which is why the draw list and sound block occupy the
addresses they do. This asymmetry is a property of the protocol rather than an
implementation detail.

One further constraint applies to the served image: runtime writes into the 64K
`cartData` buffer must be mirrored into both 32K banks at `pos ^ 0x8000`, because
`romemu.S` inverts PB6/A15. Writes into the 20K `menuData` buffer must **not** be
mirrored, since mirroring overflows it. `vxtImgPut()` is the single
implementation of that rule.

### 2.2 The sequence of an RPC

```
6809                                    STM32
────                                    ─────
sta  >$7FFF   ───────────────────────►  romemu.S observes a write to $7FFF
                                        → leaves the bus-polling loop
                                        → blx doHandleEvent
   ROM IS NOT BEING SERVED               ⋮  the handler executes
   (reads return one frozen              ⋮  (computes, writes $0800 and $2000)
    stale byte)                          ⋮
lda  >$0000  ◄─────  frozen byte         ⋮
cmpa #'g'          not 'g' → loop        ⋮
   ⋮                                     ⋮
                                        → returns
                                        → b initloop (serving resumes)
lda  >$0000  ◄─────  genuine 'g'
cmpa #'g'          matches
lda  >$0001  ◄─────  genuine ' '
cmpa #' '          matches
jmp  ,x            return to caller
```

**The trigger sequence must execute from 6809 RAM.** While the handler runs,
normal ROM bytes are not served, so 6809 code still fetching instructions from
the cartridge would fetch invalid data. `VXT_RPC_INIT` copies a 19-byte stub to
`$C880`, and every RPC jumps through it.

### 2.3 The readiness signal is structural, not a flag

An alternative to polling the header is explicit messaging: have the STM32
write a `FRAME_READY` flag byte into the served image and have the 6809 poll
that flag instead. Such a scheme would be self-documenting and extensible,
able to carry a status code, a sequence number, or an error. It does not work.

While the handler executes, the STM32 has left the bus-servicing
loop entirely and is not answering reads at all. Every 6809 read during that
interval returns one frozen stale byte. A `FRAME_READY` byte in the served image
is not readable-as-false during the handler; it is not readable at all.
No datum on the bus can be inspected, because the bus is not being served.

The header poll works because it does not test a datum; it tests the bus.
"Is correct ROM service being provided?" is the operative question, and reading
two known header bytes asks it directly. The handshake is the absence of
service, and absence of service is not a condition a flag can represent.

The two-byte form is also stronger than its own source comment suggests. The
comment describes the second read as defeating bus noise. Because the entire
handler interval returns a single frozen byte, and the test requires
`$0000 == 'g'` **and** `$0001 == ' '`, and `'g'` differs from `' '`, **no single
frozen value can satisfy both conditions.** The two-byte test is therefore a
provably correct busy detector, given the frozen-byte behavior. The frozen-byte
behavior is the measured premise; the conclusion follows from it.

**Conditions under which explicit messaging would become possible.** A flag
becomes viable only if the STM32 can continue serving the bus while computing,
which requires either interleaving computation into the polling loop in small
increments — the loop's timing budget is already tight, and interrupting it is
prohibited — or a second core, or DMA-driven bus service. None is available on
this part. Consequently:

> The two processors are serialized. `VXT_RPC` is a pure busy-wait and
> `doHandleEvent` is a plain `blx` out of the polling loop. Double-buffering the
> draw list does not help, for the same reason: the STM32 is the ROM while the
> 6809 draws.

### 2.4 The consequence that governs application design

Because execution is serialized:

```
   STM32 compute  +  6809 draw  +  overhead   ≤   20 ms   for a stable 50 Hz
```

and therefore **1 ms of STM32 time corresponds to approximately 1,500 6809
cycles, or about 38 records of drawing.**

Measurements taken with the DWT counter around the frame handler, for three
representative scenes:

| Scene | Frame    | STM32      | 6809 draw | Effective rate |
|-------|----------|------------|-----------|----------------|
| A     | 35–50 ms | 2.4–3.3 ms | 33–47 ms  | 20–28 Hz       |
| B     | 45–50 ms | 4.0–4.9 ms | 41–45 ms  | 20–22 Hz       |
| C     | 35–40 ms | 4.5 ms     | 31–36 ms  | 25–28 Hz       |

**The STM32 accounts for 7 to 11 percent of a frame and is not the bottleneck.**
Every optimization of consequence is an optimization of 6809 drawing, which
yields the governing rule of this reference guide:

> Spend STM32 cycles freely. Conserve 6809 records.

### 2.5 The identifier space

| Identifiers | Owner                                                                  |
|-------------|------------------------------------------------------------------------|
| 0–18        | upstream multicart firmware                                            |
| 19–63       | documented as application-specific                                     |
| **64–79**   | **reserved for the toolkit**; 66 is VOOM's historical frame identifier |
| 80–255      | documented as application-specific                                     |

`vxtRpcRegister(id, fn)` refuses identifiers at or below 18, refuses a second
registration on a claimed identifier, and reports over the serial debug path.

**A discrepancy between that documentation and the code:** `main.c`'s hook reads

```c
default:
    if (data >= VXT_RPC_TOOLKIT_FIRST && data <= VXT_RPC_TOOLKIT_LAST)
        vxtRpcDispatch(data);
    break;
```

so only 64–79 reach the dispatcher, and the documented 19–63 and 80+ ranges are
unreachable until that test is widened. Currently free: **64, 65, 78, 79.**
VX-COOP occupies 78 and 79. The narrow form is deliberate: every unmatched
identifier remains a silent no-op, matching original firmware behavior exactly.

### 2.6 Upstream integration

That `default:` case is the entire difference from upstream firmware; all other
code resides in toolkit modules. Preserving that property is what keeps the
toolkit rebasable.

### 2.7 Differences from VOOM's RPC implementation

Sprite_tm's original, `voom.asm`, is 175 lines and establishes the architecture. This attempt seeks to create generality.

|                         | VOOM (2015)                                                                                                | VXT                                                                                                                                           |
|-------------------------|------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------|
| **Stub invocation**     | `jsr rpcfn` … `rts`, using the hardware stack                                                              | `jmp` with the return address in **X** and `jmp ,x` to return: no stack use, matching the multicart's own call convention                     |
| **Identifiers**         | one hardcoded identifier (66), one handler                                                                 | runtime registration table, bounds-checked, collision-refusing, 16 reserved identifiers                                                       |
| **Stub RAM address**    | `$cb00`, within the menu's own layout                                                                      | `$C880`, an independently verified free-RAM boundary                                                                                          |
| **Fall-through safety** | stub placed last in the file (structural defence only)                                                     | an explicit `jmp main` after the header **and** stub-last placement; macros separated into a byte-free include so they may be placed early    |
| **Draw format**         | 4 bytes per vector — intensity, scale, y, x — interpreted by a loop containing a per-vector spin-wait      | SmartList: 4 bytes per record, `(A, B, routine-address)`, dispatched by one `pulu a,b,pc`, with no spin-waits; approximately 2.2 times faster |
| **Long lines**          | each segment carries its own scale, so a long line is many segments                                        | one record can draw an entire line, and Timer 1's full 16-bit range is reachable                                                              |
| **Intensity**           | packed with a recenter flag in bit 6, so values of 64 and above were misread; an effective ceiling of 0–63 | flag relocated to bit 7; the full 0–127 BIOS range available; change-gated, so re-emission is free                                            |
| **Input**               | four direction bits packed into one nibble at `$7FFE`                                                      | a 10-byte block: both axes signed and raw, four per-button edge bytes, the held bitmask, and the edge byte                                    |
| **Sound**               | none                                                                                                       | three tones, noise and envelope, sequence-gated, costing approximately 12 cycles when unchanged                                               |
| **Frame pacing**        | fixed 30 Hz via Timer 2                                                                                    | `Wait_Recal`, with the achieved rate measured                                                                                                 |
| **Overflow**            | unbounded list                                                                                             | hard-bounded, with the terminator's slot reserved so that overflow drops geometry rather than causing the walker to fail                      |

Two entries merit expansion.

**Do not pack a flag into a field whose full range you will later want.** VOOM
masks the intensity byte with `$3F` before calling the BIOS, bit 6 being its
recenter flag, which confines usable intensity to 0–63. That ceiling is not a
hardware or BIOS limit — the BIOS routine accepts 0–127 — and any value of 64 or
above set the flag and was read as a beam reset, so the geometry vanished rather
than brightening. Put such a flag on bit 7, which a 0–127 range does not use.

**Take a wire format from the code, not from the comment describing it.** VOOM's
header documents `flags count y1 x1 [y2 x2 …]`, a run-length scheme; the
assembled code reads `intensity, scale, y, x` with no count field. Where the two
disagree, only one of them is what the hardware sees.

---

## 3. Drawing: the SmartList

### 3.1 The dispatch mechanism

A SmartList is a flat array of 4-byte records:

```
    db  y, x                 ; → the 6809's A and B
    dw  SM_<routine>         ; → the 6809's PC
    ...
    db  0, 0                 ; terminator
    dw  SM_end
```

Every routine ends with **`pulu a,b,pc`**, a single instruction that loads the
next record's two data bytes and jumps to its routine. That one 9-cycle
instruction replaces an interpreter's entire dispatch sequence: load, compare
against a sentinel, test flag bits, branch. The technique is documented by
Malban and attributed there to Kristof, whose measurements on one 60-vector
figure are:

| Engine              | Cycles   | Per vector |
|---------------------|----------|------------|
| BIOS `Draw_VL_Mode` | 7803     | ~130       |
| `Draw_VLp`          | 4825     | ~80        |
| **SmartList**       | **2122** | **~35**    |

Two further consequences follow, both exploited:

- **The shift register is not modified between records**, so `SM_continue_d`
  continues whichever mode is open, draw or move. It is the least expensive
  record available, and a chained run of them is the least expensive geometry.
- **No spin-wait loops are required.** The timer has always expired before the
  next relevant change is made; Section 3.2 describes the mechanism.

The STM32 side is `vxtSmartBegin`, `Scale`, `Intensity`, `Move`, `Draw`, `Cont`
and `End`. The address of each 6809 routine is unknown to the STM32 at compile
time, so the cartridge publishes the addresses once at boot (Section 3.5).

### 3.2 The tiered wait, and why one routine exists per scale

Timer 1 determines the beam ramp's duration. The requirement is that the timer
be allowed to expire before anything further is done, and Malban's pad is

```
    NOPs = (scale - 9) / 2
```

That count is fixed at assembly time, and a routine has no means of determining
which scale is loaded when it executes. A family of routines therefore exists:

| Routine                             | Scale       | Reach      | NOPs                           | Cost    | Status                  |
|-------------------------------------|-------------|------------|--------------------------------|---------|-------------------------|
| `SM_startMove_d` / `SM_startDraw_d` | 12          | 1,200      | 1                              | ~45 cyc | proven                  |
| `SM_startMoveBig_d`                 | 32          | 3,200      | 12                             | —       | **hardware-proven**     |
| `SM_startDraw32_d`                  | 32          | 3,200      | 12                             | ~67 cyc | same proven pairing     |
| `SM_startDrawBig_d`                 | 64          | 6,400      | 27                             | ~97 cyc | formula, extrapolated   |
| `SM_startDrawHuge_d`                | **any**     | any        | polls Timer 1                  | ~2.2×   | correct by construction |
| `SM_startDrawHuge16_d`              | any, 16-bit | ~6,553,500 | polls, uses the real high byte | ~2.2×   | correct by construction |

**Never dispatch an operation through a routine padded for a different scale.**
Under-padding is not a rounding error: the next VIA write lands before the ramp
has finished and the beam is left somewhere other than the intended point, which
accumulates as positional drift across a figure. Match the routine to the scale.

**The wait is the draw, so do not try to shorten it.** Timer 1 runs for `scale`
ticks because that is the beam's sweep duration for the line; the NOPs are the
beam working, not overhead. Trimming a pad to its arithmetic minimum saves 2 to 4
cycles out of 67 to 97 and reduces the timing margin to a single cycle against a
VIA whose timer reload carries ±1 cycle of ambiguity. The only variable worth
changing is the scale, which is the purpose of the ladder.

**The `Huge` routines exist for scales chosen at runtime.** If the STM32 is to
select whatever scale minimizes work for a given distance, a hand-verified NOP
count cannot exist for every possible scale. Those two routines therefore poll
the timer flag, which is correct at any duration, at approximately 2.2 times the
dispatch cost. They are a targeted exception and not a default.

### 3.3 The scale ladder and its constraint

One record draws `(dy,dx)` as `(sy*s, sx*s)` with `|sy|` and `|sx|` at most 100.
The scale `s` is therefore constrained **only from below**, by
`s ≥ ceil(maxAbs/100)`. Any larger `s` is equally legal and still occupies one
record.

The dispatcher formerly selected the largest fixed tier, paying scale 64 for
every edge. Timer 1, however, counts down from the **scale rather than the
line's length**: a 2,000-unit edge at scale 64 waits exactly as long as a
6,400-unit one, expending 52 of its 97 cycles in NOPs to no purpose.
`gb_dispatch_auto()` therefore selects the smallest calibrated routine that
still reaches the target, so that every edge between 1,200 and 3,200 units saves
30 cycles at the same single record. The change is a strict improvement: never a
coarser routine, only a finer one where it suffices.

> **A draw scale must be an integer multiple of the reposition scale.**

Scale 12 is faster still and is not usable as a third tier, because a draw scale
that is not a multiple of the reposition scale produces two distinct defects:

1. **Convergence.** A draw lands on a multiple of its own scale, and a
   reposition lands on multiples of `POS_SCALE` (32). Both 64 (= 2 × 32) and 32
   land on the reposition grid, but multiples of 12 coincide with it only every
   96 units. A vertex reached by a short edge could therefore differ from the
   same vertex reached by a reposition by approximately 22 units, or 1.2 pixels,
   which renders as two separate points where a junction should appear.
2. **Brightness.** Dwell time is proportional to scale, so scale 12 is 5.3 times
   dimmer than scale 64 at any length, which renders as an incomplete line.

Do not attempt to correct either by tuning a length threshold: reducing a maximum
retains the short edges and excludes the long ones, which is the opposite of what
is required. **Use draw scales that are multiples of the reposition scale.** The
values 16, 20 and 24 reintroduce 16, 26 and 28 units of error respectively in
exchange for 8 to 16 cycles.

**One limit of the argument, stated rather than glossed.** The grid relationship
accounts for a chained edge meeting an independently repositioned one. It does not
account for a failure between two edges within a single tracked chain. Staying
grid-safe avoids that case rather than explaining it, so do not treat the grid
rule as a proof that chained edges cannot disagree.

### 3.4 The two budgets

```
   VXC_REGION_RECORDS = ($2000 - $0800) / 4 = 1536       ← MEMORY bound
   records that fit within a 50Hz frame ≈ 510-730        ← TIMING bound
```

The memory bound is nothing more than the distance to the sound block. Exceeding it causes
the draw list to overwrite the sound commands, after which the 6809 feeds
invalid register/value pairs to the PSG and sound ceases permanently. The list is
consequently hard-bounded.

The timing bound is approximately one third of the memory bound and follows from
the measured 32.6 to 46.6 cycles per record. **A frame that fits in memory but
not in time produces flicker rather than an incorrect image.** Both figures
matter.

Per-primitive costs:

| Primitive                               | Records                                                                 |
|-----------------------------------------|-------------------------------------------------------------------------|
| chained edge (`gamelibChainBigGrouped`) | ~3                                                                      |
| independent edge (`gamelibDrawBigLine`) | ~9–14, comprising a full close, recenter, scale, move and scale restore |
| `MoveBig`/`DrawBig` reposition          | one record per 100 units of scaled travel                               |

Distant or off-center targets therefore cost more, and budget arithmetic is
required rather than optional. Measurement via `vxtSmartRecordCount()` is
preferable to estimation; no simulator exists for this.

Overflow is survivable: `vxtSmartBegin()` reserves the terminator's slot, so
`vxtSmartEnd()` can never be the record dropped and the 6809 always walks a
properly terminated list. Before that provision, a dropped terminator caused the
walker to run past the end of the list and execute stale bytes as routine
addresses. Survivable is not acceptable: it still denotes silently dropped
geometry.

### 3.4.1 Text is the most expensive thing on the screen

This is the budget item most often underestimated. Each glyph stroke costs a blanked move plus a draw, and each glyph adds
a return to its origin and an advance. Counted against the bundled font's own
stroke table:

| Item                             | Records |
|----------------------------------|---------|
| one average upper-case character | ~6      |
| a 30-character line              | ~125    |
| a 7-character page title         | 46      |
| `"OVER 50HZ"`                    | 61      |
| `"!"`                            | 6       |

Two limits follow, and both are worth checking with arithmetic before committing
to a screen layout:

- **Three 30-character lines, a title and a verbose readout cost 641 records.**
  That is at or above the entire 50Hz timing budget *before any geometry*, so such
  a layout flickers unconditionally.
- **A twelve-line page describing every page and control costs 1501 records**,
  which exceeds even the 1536-record memory bound. **Do not plan a full-screen
  help page; it is not affordable at any frame rate.**

What fits alongside geometry is a title, two short lines and a compact readout:
263 to 288 records, leaving 222 to 442. Keep on-screen text terse, and make it
toggleable wherever it shares a frame with geometry.

A rule that generalizes beyond text: **a diagnostic must not perturb its own
measurement in the direction of the fault.** An over-budget warning spelled out as
a word costs 61 records against 6 for a single character, so the verbose form adds
materially to the overrun it is reporting. Use a single-character marker.

### 3.5 The address handshake

The STM32 must write genuine 6809 addresses into the list, and those routines
reside wherever the cartridge's assembler placed them. The cartridge therefore
publishes them into `parmRam` once at boot and fires RPC 69:

```asm
        ldd     #SM_setScale
        std     $7f00
        ldd     #SM_setIntensity
        std     $7f02
        ...
        ldd     #SM_startDraw32_d
        std     $7f18
        VXT_RPC #69              ; blocks until the STM32 has stored them
```

Emitting records before the table is populated is a no-op, which fails as a
blank screen rather than by jumping the 6809 to an arbitrary address. That is the
intended failure mode.

A cartridge may publish a subset: routines it never uses may be omitted, and the
emitters requiring them are never called. **One exception exists.**
`startDraw32` is selected by the dispatcher rather than by the application, so
new firmware running an older cartridge would emit records pointing at stale
`parmRam` contents and `pulu a,b,pc` would jump into them. `vxtSmartHasDraw32()`
therefore range-checks it, every routine being assembled from the same include
and a genuine `startDraw32` consequently residing within a few hundred bytes of
`startDrawBig`. When adding a published routine, consider which side decides to
use it.

### 3.6 Precision

> Drawing precision must not be purchased with additional records or recenters.

The single-record long-line path exists specifically to avoid chaining. Capping
the ramp scale and chaining 2 to 4 records per edge will meet an accuracy target,
and it spends the one resource that is scarce to save the one that is not.
Chaining additional segments, adding recenters and lowering the scale are all the
expensive solution to a problem that has a free one.

**The free solution: the ramp scale carries slack.** Because `s` is constrained
only from below, and endpoint error is ±s/2, *searching* the legal scales for the
value that lands closest costs STM32 cycles and no 6809 records. The worst anchor
gap improved from 61 units to 4 with one record per edge throughout.

Four rules follow:

- **Bound the error before theorizing about it.** Simulate the emitted record
  stream and establish the magnitude. An error bounded at approximately 61 units
  and non-compounding cannot produce a defect measured in thousands, and knowing
  that eliminates a whole class of candidate cause before any code is changed.
- **A correlation is not a mechanism.** "Everything above scale 64 looks wrong"
  fits the evidence and points at the wrong correction; ±s/2 quantization explains
  the same correlation and can be computed. Prefer the explanation you can compute
  over the one you can only assert.
- **Target points the hardware can reach.** Repositions land only on multiples of
  `POS_SCALE`, so aiming at a raw ideal coordinate makes a chained figure and an
  independently repositioned one target different points by construction. Have both
  target the quantized coordinate, via `gamelibRoundToScale()`.
- **Distribute chained steps proportionally; never clamp per axis.** Clamping each
  axis independently to ±100 renders `d = (200,100)` as `(100,100)` followed by
  `(100,0)`, which is a dogleg rather than a chord. The fault is invisible for
  single-record draws and active on every reposition beyond approximately 3,200
  units, so it will not show up in the case you are most likely to test.

### 3.7 Drift and the bounded chain

Neither extreme is satisfactory:

- **One continuous chain from a single recenter** is inexpensive, but analog
  integrator drift accumulates without bound over a long chain.
- **A fresh recenter before every segment** is safe, but costs approximately 9 to
  10 records per segment rather than 2.

`gamelibChainBigGrouped(..., groupSize)` forces a genuine recenter every
`groupSize` segments, bounding drift to that many segments' worth and reducing
recenters by approximately the same factor. A group size of 1 is the safe,
expensive fallback; 0 forms one unbounded chain.

**Do not aim one figure at another figure's tracked position unless that figure is
re-grounded.** A chained figure's tracked position is a digital prediction and does
not reliably follow real analog drift. Grouped chaining re-grounds it with a
genuine recenter every `groupSize` segments, which makes it a safe target; an
unbounded chain's tracked position is not, however carefully it is computed.

### 3.8 Two effects that resemble defects

**Excess brightness at one point.** The beam's physical position does not change
until the next record executes, so if a render function's final operation is a draw
and the following code opens with an expensive multi-record setup, the beam remains
lit at that point for the whole setup and dwells longer than anywhere else in the
frame. Call `gamelibBeamCloseRun()` — one blanked move — at the end of any renderer
that ends on a draw.

**The frame's first element lands displaced.** Measured across several figures,
the first item drawn in a frame carries 2.5 to 4.8 times the mean error of
everything else on the same screen; in the clearest case one spoke of a 12-spoke
starburst read +403 units while the other eleven sat in a coherent 107 to 230 band.
A recenter does not fully zero the integrators in one operation and the residual
depends on where the beam was beforehand, so it does not cancel out of the
arithmetic as a constant offset would — do not expect to correct it with one.
Call `gamelibBeamPrime(2)` before any real geometry; it spends approximately 12
blanked records absorbing the effect. Treat this as a structural correction rather
than a calibration value, since there is no per-unit constant to measure.

---

## 4. Input

The 6809 reports raw controller state; all application logic resides on the
STM32.

| `parmRam`       | Byte    | Contents                                   |
|-----------------|---------|--------------------------------------------|
| `$7FFE`         | 254     | `Vec_Joy_1_X`, signed                      |
| `$7FFD`         | 253     | `Vec_Joy_1_Y`, signed                      |
| `$7FFC`/`$7FFB` | 252/251 | controller 2 axes, when enabled            |
| `$7FFA`–`$7FF7` | 250–247 | `Vec_Button_1_1..4`, per-button edge bytes |
| `$7FF6`         | 246     | `Vec_Btn_State`, the raw held bitmask      |
| `$7FF5`         | 245     | `Read_Btns`' EDGE return value             |

**Raw rather than pre-digitized, which is the central design decision.**
`Joy_Digital` and `Joy_Analog` are drop-in interchangeable: identical entry
values, identical result locations, and `JOYSTK` calls `JOYBIT` internally. Only
the values differ. The toolkit therefore reports the axes verbatim and leaves
their interpretation to the STM32, so that a digital application tests the sign,
an analog application uses the magnitude, and substituting one BIOS routine for
the other is a single-token change on the 6809 side requiring no STM32 edit. An
earlier design packed direction into flag bits, which would have made analog
applications impossible to build on the module.

For analog use, also `clr Vec_Joy_Resltn`: `VXT_INPUT_INIT` sets POTRES to `$80`,
the minimum resolution and fastest read, which is correct for digital use and too
coarse for proportional response.

### 4.1 Discrete against continuous actions

> **Discrete action** — menu navigation, a page change, a toggle: read the raw
> per-button edge byte `parm[VXT_IN_BTN1_n]` directly. It asserts for exactly one
> frame per press.
>
> **Continuous action** — adjusting a value while a button is held: the edge byte
> asserts once, so a held button then produces nothing. Use the held bitmask
> `VXT_IN_BTNS` instead.

Applying the held pattern to a discrete action causes it to fire for every frame
of a press, approximately ten frames for a normal tap.

One complication underlies the held pattern: **no available documentation states
which bit of `Vec_Btn_State` corresponds to which button.** The toolkit therefore
determines it empirically. On the frame that an edge byte asserts, whichever
single bit is set in the held mask must belong to that button. The value is
latched once, guarded so that it is learned only from an unambiguous
single-button press, with the edge byte used as a fallback until then.

### 4.2 `Read_Btns` modifies the DAC's direction register

`Read_Btns` is documented to modify `DDAC`, that is `VIA_DDR_a`, the register
that must configure Port A as an output for the DAC to drive beam coordinates.
`VXT_INPUT_READ` restores it unconditionally afterwards. The restore is not
redundant: VOOM never calls `Read_Btns`, so no precedent establishes that the
BIOS restores the register, and drawing immediately after a button read without
the restore is unsafe.

---

## 5. Sound

### 5.1 Registers rather than samples

The AY-3-8912 provides three independent monophonic tone oscillators, one noise
generator routable to any of them, an envelope generator, and a single mixer
register governing all six routings.

The STM32 writes `[seq][reg][val][reg][val]…$FF` into the served image at
`$2000`. The 6809 calls `vxt_sound` once per frame, which compares the sequence
byte against its shadow and returns in approximately 12 cycles when nothing has
changed.

**The AY's registers are latched.** The datasheet states that once programmed
they generate and sustain the sounds, freeing the system processor for other
tasks. Only changes should therefore be queued; re-emitting an unchanged tone
each frame discards the entire benefit.

By contrast, the alternative approach, streaming PCM through the multiplexer
DAC, requires servicing after nearly every drawn line, and each sample's
timing is subordinate to the draw loop. Such code is not a PSG reference. Writing `VIA_port_b = $06` leaves BC1 and BDIR both low, so the PSG
bus is inactive for the entire write; the value selects multiplexer channel 3,
the analog audio line.

### 5.2 Three constraints

**Register numbering is decimal in the toolkit headers and octal in the
datasheet.** The datasheet's R10, R11 and R12 are decimal 8, 9 and 10
(amplitude), and its R13, R14 and R15 are decimal 11, 12 and 13 (envelope).
Reading its charts literally writes amplitude values into envelope registers.

**Register 14 is the AY's I/O port A, which is the buttons.** It is never
written. `vxtSoundReg()` discards any register number above 13 rather than
clamping, because clamping would write into register 13, the envelope shape.

**Mixer bit 6 must remain clear.** It is the I/O port direction; setting it
causes the AY to drive the button lines and input reporting to fail.
`vxtSoundMixer()` accepts positive-logic enable masks — the AY's own logic being
inverted, such that 0 enables — and force-clears bit 6. A raw
`vxtSoundReg(7, x)` provides no such protection.

### 5.3 Position within the frame

`vxt_sound` runs after the frame RPC and before the draw. `Sound_Byte` modifies
CNTRL and the DAC, displacing Port B's multiplexer state and Port A, so calling
it between vector records would corrupt the beam. The interval between the RPC
and the draw is the only point at which no operation is in flight, and the draw
engine re-asserts `VIA_cntl` at loop entry in any case.

Because the registers latch, discrete sound requires one service point per
frame rather than one per line.

### 5.3.1 The per-frame register-pair budget

There is a second budget in the sound path, separate from the record budget, and
it is easy to overlook because exceeding it is silent.

`vxtSoundReg()` accepts at most `VXT_SND_MAX_PAIRS` — **14** — register/value
pairs per frame, and **discards anything beyond that without an error or a flag.**
The costs are:

| Call                 | Pairs                                     |
|----------------------|-------------------------------------------|
| `vxtSoundTone()`     | 3 — fine period, coarse period, amplitude |
| `vxtSoundMixer()`    | 1                                         |
| `vxtSoundNoise()`    | 1                                         |
| `vxtSoundEnvelope()` | 3                                         |

A three-channel song with effects reaches that limit. Worst case on the reference
application's `SOUND` page:

```
   song firing events on all three channels          3 x 3 = 9
   a channel returned this frame: mixer + re-assert    1 + 3 = 4   -> 13
   an effect granted on the same frame: noise + mixer  1 + 1 = 2   -> 15
```

The two writes past 14 are discarded, and the one most likely to be lost is the
mixer's noise routing — audible as an effect coming out as a tone rather than
noise, which is the same symptom the shared-mixer-register rule above describes
and therefore easy to misdiagnose.

Two mitigations, both worth copying:

- **Change-gate the mixer.** A recomputation that yields the same routing should
  cost nothing. `vxtSmartIntensity()` uses the same technique for the same reason;
  `vxtSoundMixer()` does not do it for you.
- **Display the pair count.** `vxtSoundEnd()` returns it. A value sitting at the
  maximum means the frame saturated and writes were dropped.

If an application needs more, `VXT_SND_MAX_PAIRS` is the constant to
raise. The 6809 service loop terminates on a `$FF` register byte rather than on a
count, so a longer block is serviced correctly with **no assembly change**, at the
cost of one `Sound_Byte` call per additional pair.

### 5.4 One span per frame, shared by all sources

`vxtSoundBegin()` resets the pending-pair buffer. A second call within one frame
silently discards whatever the first caller queued. Therefore:

> Every sound source in a frame — the music player and every effect — must
> operate inside a single `vxtSoundBegin()`…`vxtSoundEnd()` span.
> `vxtMusicUpdate()` deliberately does not open its own.

`vxtSoundEnd()` is a no-op when nothing was queued: the sequence byte is left
unchanged and the 6809 skips servicing entirely.

### 5.5 Conflict resolution

Three channels serve more than three sound sources. The established mechanism is
fixed channel assignment together with call order, under which a later caller
overwrites an earlier one on a shared channel. The order of the calls constitutes
the priority ranking.

The arrangement in practice is that the song occupies all three channels and an
effect **borrows one of them, and only for the frames it is actually sounding.**
Outside a burst it holds nothing and the song has all three back. The two
channels the effect does not use are never written by it.

Two provisions make this work.

### 5.5.1 Re-syncing a returned channel

This is the half of channel borrowing that is easy to omit, and its absence is
audible. It is worth being precise about what is and is not out of step when an
effect finishes.

**Nothing about the song's timeline desynchronizes.** `vxtMusicUpdate()` is called
every frame unconditionally, whichever source owns which channel. Inside it each
channel's countdown is decremented and any event whose delta has elapsed is
fired, which both writes the AY and updates the player's own record of that
channel's current note and amplitude. The sequencer therefore advances through
the borrow exactly as it would have done otherwise. **The song never loses its
place, and nothing needs rewinding or seeking.**

**What desynchronizes is the channel's hardware register state.** While the effect
owns the channel it overwrites that channel's amplitude, and for a noise effect
the mixer routes the channel to the noise generator rather than its tone
oscillator. When the effect expires those registers still hold the effect's final
values, and the player will not correct them, because it writes a channel only
when that channel has an event to fire. The next event may be many frames away.
Until then the channel sits silent or on a stale value while the other two
continue, which is heard as one channel falling out of time with the rest. The
sparser the song's events on that channel, the longer it persists.

**The re-sync is therefore not a resynchronization of time.** It is the act of
bringing the hardware back into agreement with a timeline that was never wrong.
Two writes, in this order:

1. **Restore the routing.** Recompute the mixer from current ownership so the
   returned channel is routed to its tone oscillator again. Recomputing rather
   than patching one bit means the other two channels are restated at the same
   time, and since they were never taken, restating them is a no-op.
2. **Re-assert the channel.** `vxtMusicReassertChannel(ch)` writes the period and
   amplitude the sequencer currently holds for that channel — either a note still
   sounding, or a legitimate silence if the note happened to end during the
   burst. Either way the channel resumes at the position the song is actually at,
   on the frame it comes back, instead of at its next event.

**The order is required, not conventional.** The re-assert writes only the tone
and amplitude registers and never the mixer, so re-emitting a tone into a channel
still routed to the noise generator would be inaudible.

**When no song is playing** there is no sequencer to interrogate and the re-assert
is a no-op by design. That channel's amplitude must then be zeroed explicitly, or
it holds the effect's final value indefinitely.

**One further consequence, for the frames during a burst rather than at its end.**
The player keeps writing the borrowed channel whenever the song has an event for
it. Those writes are harmless only because the effects assert after the player
within the same frame, so the effect's amplitude is the last value written, and
because the mixer is routing noise rather than tone on that channel in any case.
Reverse the two and the song audibly interrupts the effect.

**Where two channels are returned on the same frame,** restore the mixer once and
then re-assert both. Emitting the mixer inside a per-channel loop writes register 7
once per returned channel for no benefit.

**Ordering against the mixer claim.** The music player must update first. It
performs one unconditional claim of the whole mixer register on its first call
after `vxtMusicPlay()`. With music updating after the effects queue, an effect
occurring on that exact frame had its correct noise routing silently reverted,
and music then played the channel's next scheduled tone where the noise should
have been. Music updates first; effects assert last.

A related provision: use `vxtMusicSwitchSong()` rather than `vxtMusicPlay()` for
a track change during gameplay while effects may be active, because `Play()`
re-arms that mixer claim.

**VX-COOP states the same scheme explicitly** in `vxcSndRequest()`: each channel
has an owner, a numeric priority and a countdown; a request is granted only if it
outranks the incumbent; and expiry returns the channel and re-asserts the song's
value for it. The semantics match the call-order convention. Note in particular
that the mixer byte is computed in one function from the ownership of all three
channels simultaneously: because that register is shared, two callers each writing
an independent conception of it is the characteristic failure.

### 5.5.2 The `SOUND` page

The page exercises the whole arrangement against real data. A three-channel song
occupies all three channels. An explosion borrows the second channel for a
43-frame noise burst with a computed amplitude ramp, then restores the routing and
re-syncs it. A second, lower-priority sweep effect is placed on **the same
channel**, so pressing it during an explosion is refused and counted, while an
explosion requested during a sweep preempts it.

**Sharing one channel between two effects is a teaching choice, not normal
practice.** The correct arrangement when channels are available is to assign
effects to different channels so that they never contend and the priority rule
never has to fire. Two effects on one channel is what makes the rule observable —
and sooner or later an application has more effects than spare channels.

The page displays the current owner of each channel (0 the song, 1 the sweep, 2
the explosion) with counts of granted explosions and refused sweeps, so the rule
is observable rather than merely asserted.

Budget its readout as carefully as geometry. Seven readable labeled lines cost
392 records, which with a title, hint and readout totals 689 against a 50Hz budget
of 510 to 730. Abbreviating the labels onto three lines costs 162, totalling 459.
**A page with no geometry at all can exceed the timing budget on text alone**, so
count the characters before laying one out.

The explosion's parameters are four constants — duration, noise period, amplitude
ceiling and which channel is borrowed — and its decay is
`vol = framesRemaining >> 1` clamped to the ceiling, so the volume reaches zero
exactly as the burst expires. No AY hardware envelope register is involved; the
ramp is computed on the STM32, one value per frame. At 43 frames the ceiling holds
for the first 13 frames and the ramp occupies the remaining 30.

Finally, **the AY sustains, so silence must be requested.** Leaving a screen
during an effect without silencing leaves a tone running. An unbounded level
trigger, such as a warning tone, is the first sound that will survive a
transition and continue over a menu.

---

## 6. 3D to 2D projection

Two projections exist as deliberately separate functions:

```c
gamelibProject3D    (u, v, faceX, cy,sy,cp,sp, perspD,      &outY, &outX);
gamelibProject3DDeep(u, v, faceX, cy,sy,cp,sp, focalLength, &outY, &outX);
```

The axis naming is easily mistaken: **`faceX` is across, `v` is vertical, and
`u` is depth.** Both rotate yaw about the vertical axis and then pitch about the
horizontal, and both accept precomputed sine and cosine values so that a
per-vertex loop does not re-derive them.

They differ only in the depth term:

- **`gamelibProject3D`** applies weak perspective, `scale = 1 - z/perspD`, with
  no division. It is appropriate for shallow volumes.
- **`gamelibProject3DDeep`** applies a true division,
  `scale = focalLength/(focalLength + z)`. It is necessary for deep volumes, with
  world depths to approximately 200,000 units, where weak perspective's
  quadratic term can become negative or non-monotonic.

`perspD` and `focalLength` are explicit parameters with no file-scope default, so
that one element's tuning cannot become another's inadvertently.

### 6.1 The near-plane singularity

> `scale = focalLength/(focalLength + z)` places the eye at `z = -focalLength`.

A vertex at or beyond the eye projects mirrored and greatly magnified, with
coordinates in the hundreds of thousands, rather than simply moving off screen.
On hardware this appears as long straight strays extending from a model, and it
requires a model with sufficient depth extent to occur at all.

Two rules follow:

1. Any model with a large depth extent, and any change to a depth or focal-length
   constant, must be checked against the near plane.
2. **Near-plane clipping must precede every window and occlusion test**, those
   tests being meaningless when applied to invalid coordinates.

When investigating a stray artifact apparently at a great distance, check whether
it correlates with geometry immediately behind the camera; the mirroring makes
that a frequent cause.

### 6.2 Interior points: interpolate rather than re-project

A point required to lie exactly on an already-projected straight edge should be
derived by interpolating between that edge's two projected endpoints, using
`gamelibLerpEdge()`, rather than by applying the full 3D projection to its 3D
position. Projection along an edge is quadratic while a drawn edge is a straight
chord; the two do not agree, and the disagreement appears as interior lines that
fail to meet the border they should touch.

---

## 7. Occlusion: three distinct problems

There is no depth buffer on a Vectrex, so occlusion can only mean not emitting
the hidden geometry.

The following are three different problems with three different solutions.

| Problem                                         | Example                                     | Method                                                           | Exact?                                |
|-------------------------------------------------|---------------------------------------------|------------------------------------------------------------------|---------------------------------------|
| **A.** An object's own hidden half              | the far side of a solid                     | backface culling from face normals                               | **Exactly**, for a convex closed mesh |
| **B.** Geometry visible only through an opening | anything seen through a foreground aperture | convex-polygon classification, then per-edge Cyrus-Beck clipping | Exactly                               |
| **C.** Geometry hidden behind a solid           | a wall passing behind an obstacle           | shadow-volume clipping                                           | Exactly, for a convex occluder        |

### 7.1 Problem A — backface culling

**The front-facing test is perspective, not orthographic.** A face is
front-facing if and only if its outward normal points back toward the eye:

```
    dot(n_world, faceCentre_world - eye)  <  0
```

`gamelibProject3DDeep()` places the eye at `(0, 0, -focalLength)` looking along
+z. A model vertex reaches world space as
`objectPos + modelScale * (R * v_local)`, with `R` the object's rotation.
Substituting the face center and expanding:

```
    dot(R*n, objectPos) + modelScale * dot(R*n, R*c) + focalLength * (R*n).z  <  0
```

A rotation preserves dot products, so `dot(R*n, R*c) = dot(n, c) = d`, the face
plane's own constant offset from the model origin. The test therefore reduces to:

```
    dot(n_rot, objectPos)  +  modelScale * d  +  focalLength * n_rot.z   <   0
```

Every term is either a per-face constant, `n` and `d`, or a per-object scalar.
The cost is a few multiply-adds per face, with no per-vertex work and no
divisions, which is no greater than the cost of the approximation it replaces.

The shortcut `rotZ < someSmallConstant` is the orthographic form. It ignores the
perspective spread, approximately 6 degrees for an object of moderate scale at
focal length 17,400, and therefore leaves faces alive beyond the true silhouette.
That error has been measured at **4.63 percent of edge decisions.**

**Edge visibility is a single rule:**

```
    an edge is visible  iff  EITHER of its two bordering faces is front-facing
```

This handles the silhouette case, one front face and one back face, by drawing
the edge whole, so **no partial-edge splitting is required.** An earlier design
assumed that silhouette-crossing lines would need to be divided mid-edge; they do
not, once culling is performed per face rather than per vertex.

Five further rules:

- **A face normal must never be approximated as the direction from the
  centroid.** That approximation is valid only for a convex mesh centered on its
  own origin.
- **For a convex mesh no authored face list is required.** Computing the exact
  convex hull of the vertex list yields faces, outward normals and edge-to-face
  adjacency.
- **Use the hull to check supplied face data, not only to generate it.** An
  export that divides one quadrilateral along the wrong diagonal creates a false
  crease, and a single wrong edge out of 45 is enough to make the cull visibly
  incorrect while every other edge behaves. Where an authored wireframe and a face
  export disagree, the hull settles it.
- **Store the raw geometric `d`, never a threshold with the model scale already
  applied.** Precomputing `-(modelScale*d)/focalLength` is algebraically
  identical and becomes silently invalid the next time a scale constant is
  retuned.
- **Validate by ray casting against the real eye position; do not rely on a
  self-consistency check.** Centroid-direction and topological-winding tests pass
  on data that is still wrong, so they cannot tell you whether the cull is correct.
  Ray casting and the convex hull are independent ground truth. Expect to need the
  measurement to find each remaining error: a mesh can read 5 percent wrong, then
  4.63 percent, then **0.00 percent** over 1,620 tests, and no step of that is
  visible by inspection.

**The limit of the method: it is exact only for a convex closed mesh.** On a
non-convex model — a panel on a strut, a fin on a stalk — a face may be
front-facing and nevertheless occluded by another part, and a flat single-sided
panel is legitimately visible from both sides. Such cases require genuine
occlusion testing.

**Cost note:** culling breaks SmartList chain adjacency, a culled edge meaning
that the next no longer begins where the last ended, so some free continuations
are lost. The net record count nevertheless falls substantially, roughly half the
edges of a convex mesh being removed, so the technique remains a clear gain; the
chaining savings should not be expected to survive alongside it.

### 7.2 Problem B — edge occlusion through an aperture

Two stages, the first of which resolves most frames:

1. **Classify** the object's projected bounding circle: `INSIDE` (draw
   unclipped, at no additional cost), `CULL` (emit nothing), or `STRADDLE`. For a
   convex aperture, a center at least `r` inside every edge places the whole disc
   inside, and a center at least `r` outside any one edge places it wholly
   outside.
2. **For `STRADDLE` only,** clip each edge by Cyrus-Beck.

> A `STRADDLE` classification does not imply that every edge crosses the
> boundary.

The classification tests a conservative bounding circle, so objects are routinely
classified `STRADDLE` with no edge actually crossing. Each edge's own endpoints
must therefore be tested first, and the clipping cost paid only for edges that
genuinely cross. Purchasing the expensive path for an entire model in order to
correct two edges is a common way to exceed the record budget.

Two implementation notes:

- **Derive the inward normal per edge and sign-check it against a known interior
  point**, rather than assuming a winding direction. The cost is one additional
  dot product, and it removes an entire class of sign error.
- **Classification and clipping must read the same geometry definition**, so that
  the two cannot disagree about the aperture's position.

A clipped edge no longer begins where the previous one ended and must therefore
be drawn independently. That is the cost of clipping, and the reason it is
applied per edge and only where required.

### 7.3 Problem C — shadow-volume occlusion

Backface culling removes an object's own hidden half. Removing the surrounding
geometry that passes behind the object is a separate problem.

The central result: **the shadow volume of a convex solid is itself a convex
polyhedron.** Concealing a line behind such a solid is therefore a plain
Liang-Barsky segment clip, requiring no sampling, no ray marching and no
screen-space work. The hidden portion of a segment is a single interval, so a
clipped line yields at most two pieces.

Point `P` is hidden by convex solid `S` as seen from eye `E` if and only if the
segment `[E,P]` meets `S`. Writing `S` as a half-space intersection
`{ n_i·X ≤ d_i }` and setting

```
    a_i = n_i·(P - E)        (linear in P)
    b_i = d_i - n_i·E        (constant for a given eye)
```

the condition becomes `∃ s ≥ 1 : a_i ≤ s·b_i` for every face `i`, which splits on
the **sign of the constant** `b_i` into

```
    max( 1, max_{b_i>0} a_i/b_i )  ≤  min_{b_j<0} a_j/b_j
```

Each of those comparisons clears its denominators into an expression linear in
`P`: the `s ≥ 1` part yields `n_j·P ≤ d_j` for each front-facing face `j`, and
each pair of one `b_i > 0` with one `b_j < 0` yields
`(n_i·b_j - n_j·b_i)·P ≥ (n_i·E)·b_j - (n_j·E)·b_i`.

Two facts make the construction inexpensive rather than merely correct:

1. **`b_i < 0` is precisely the condition "face i is front-facing."** One sign
   test per face serves both this construction and Section 7.1's culling; the two
   should not be computed separately.
2. **Only adjacent back-facing/front-facing pairs produce a genuine bounding
   plane**, those being the silhouette edges. Every non-adjacent pair is
   redundant. For an eight-sided prism that is the difference between
   approximately 30 planes and approximately 15.

Two rules matter more than the algebra:

- **Never assume a face's orientation is fixed. Classify every face, every frame,
  from its own `b`.** Hardcoding "the near cap faces forward, the far cap faces
  away" appears to work and is wrong: the near cap inverts as soon as the camera
  passes it, which happens routinely once an occluder is longer than the visible
  window. The failure mode is the dangerous one — most of the occlusion is
  suppressed while the image still looks plausible. Expect a scale of error like
  6.5 million wrongly-drawn sample points, and segments fully culled rising from
  3,575 to 18,818 of 85,800 once the assumption is removed.
- **Validate with an eroded and a dilated solid, not the exact one.** A point
  hidden by the solid eroded by ε must be reported hidden, and a point not hidden
  by the solid dilated by ε must be reported visible. Points between the two
  straddle the exact boundary, where disagreement is of measure zero and visually
  meaningless. Test against the exact solid instead and tangency alone generates
  millions of false alarms that hide any real defect.

Two further provisions: **assert that the plane list never overflows**, and on
overflow disable occlusion for that object entirely rather than clipping against
the planes that fit, since a short list silently over-occludes. And guard the
degenerate case in which the eye lies inside the solid: every face is then
back-facing, the plane list is empty, and the construction correctly reports that
everything is hidden, blanking the display. That is geometrically correct and
indistinguishable from a failure.

Measured in production use: **22 percent of segments removed entirely and 0.4
percent divided in two**, at 248 plane evaluations per frame on average and 764
at worst. A few thousand STM32 operations to conserve 6809 records is the correct
trade.

---

## 8. Calibration

An analog machine does not draw exactly what the arithmetic specifies.
Measurement on hardware establishes that a **drawn** line covers less physical
distance than a blanked **reposition** for the same nominal delta, by
approximately 4 to 7 percent. Around a figure of any size that is enough to
displace a chained shape from the independently positioned elements meeting its
vertices by hundreds to thousands of units, so it cannot be ignored as a rounding
term.

> A calibration value measured on one unit describes that unit.

Compiling a fitted constant into firmware fixes one machine's analog behavior
permanently, and it is incorrect as soon as the cartridge is moved. Values are
therefore re-derived at each boot from whatever `/calmeas.csv` is present on the
SD card, that file being written by the measurement rig and read by
`vxt_cal_load.c`.

### 8.1 How a measurement is obtained

The STM32 cannot observe the screen; only the operator can. Each rig screen
therefore draws a test figure by some draw path together with an **independently
positioned reference mark** at that figure's ideal endpoint. The separation
between them is the error. The joystick drives a **measuring caret** which the
operator places where the beam visibly landed, and the readout reports the
caret's offset from nominal in physical units.

The procedure converts a subjective impression into a number that can be
recorded, photographed and compared.

The controls are the same on every screen:

- **Joystick** — moves the measuring caret onto where the beam actually
  landed.
- **Button 1** — a short tap cycles to the next screen. Press and hold it
  while moving the joystick left or right to cycle the current screen's
  variant instead. Variant-cycling also toggles the reference card on and
  off: every geometry variant is paired with a card-on twin, so the two are
  always logged as a matched comparison rather than something to remember
  to do separately.
- **Button 2** — toggles whether the accumulated correction is applied to
  the test figure (raw vs. corrected). A screen with no correction-bearing
  test figure of its own treats this as a no-op.
- **Button 3** — skip to the next reference item on the current screen.
- **Button 4** — record the caret's current reading to the log.

### 8.2 The corrections

| Correction    | Corrects                                            | Setter                       |
|---------------|-----------------------------------------------------|------------------------------|
| Draw gain     | drawn deltas falling short of moved ones by ~5%     | `gamelibBeamSetDrawGain()`   |
| Move gain     | reposition overshoot, ~9.9% measured                | `gamelibBeamSetMoveGain()`   |
| Move settle   | landing error proportional to chain step size       | `gamelibBeamSetMoveSettle()` |
| Center offset | true screen center not coinciding with DAC zero     | `gamelibBeamSetOffset()`     |
| Closure       | a chained figure failing to return to its own start | `gamelibChainCloseOffset()`  |

**All of these cost no 6809 records.** They alter the values within records
already being emitted, which is the property that makes them the correct class of
correction.

Two constraints:

**A uniform gain cannot alter a closed figure's closure.** It scales every delta,
and a closed figure's deltas sum to zero, so `g × 0 = 0`. Only an asymmetric
correction, one treating the return path differently from the outbound path, has
any effect on where a closed chain lands. Re-fitting the draw gain accomplishes
nothing for that symptom.

**The closure offset applies to the TARGET, never to the DELTA.** Chained draws
are self-correcting: each segment computes its delta from the beam's tracked
position to that vertex's absolute target, so a correction added to a delta is
canceled exactly by the following segment. Adding it to the targets is what
moves the landing point: `target[k] += D*k/n`.

Apply corrections to absolute positions only, never to deltas, where the physics
requires it. A DAC-zero shift is a fixed offset in deflection space, so a pure
translation cancels out of any difference between two already-offset points;
every figure beginning from a reposition shifts uniformly and a chain's internal
shape is unaffected.

### 8.3 The loaders' contract

Each loader **returns 1 and fills its out-parameter only when usable data was
found, and otherwise returns 0 leaving the parameter untouched, so that the
caller supplies the fallback.**

> "No data" and "measured zero" must remain distinguishable.

Substituting another machine's fitted constant is the error the entire mechanism
exists to prevent.

Loaders also **require provenance**: only rows recorded at identity draw gain
(1000) are accepted. A row measured through a correction is not a measurement of
the hardware, and a row predating those columns cannot establish either case.
Legacy rows are therefore skipped rather than trusted.

### 8.4 The I/O rule

> Blocking SD or flash I/O must never appear on the per-frame path.
> **The symptom is flicker, not an incorrect image.**

The 6809 waits inside its RAM-resident stub while the STM32 computes, so anything
that blocks the STM32 blanks the display for its duration. An `f_open`, `f_gets`
or `f_write` costs approximately 100ms, which at 50Hz is five lost frames per
frame.

- **Load** in the one-shot boot-init handler and cache in a static.
- **Apply** the cached value per frame; the setters are inexpensive.
- **Save** only in response to a button press.

**Verify mechanically, and make the check transitive.** Do not grep a single
function body: a call one level down will not appear. Walk the call graph. For the
reference application that yields:

```
=== vxcoop_handler (transitive) ===
   vxtCalSaveRow      via vxcoop_handler -> vxcPageCal
=== vxcoop_init_handler (transitive) ===
   vxtCalLoadDrawGain via vxcoop_init_handler -> vxcCalLoadOnce
   vxtCalLoadOffset   via vxcoop_init_handler -> vxcCalLoadOnce
```

Both results are acceptable: the loads are confined to initialization, and the
save is reachable from the frame handler but gated on a button edge, which is the
permitted pattern — an occasional single-frame write on a user action is fine, and
per-frame writing is what the rule prohibits. Note that a body-only grep reports no
hits at all here, so it would have certified this code without examining the one
call that matters. **Ask both questions separately:** is the call reachable from the
frame path, and is it gated on a one-shot? Reachable and ungated is the defect.

### 8.5 Application settings

Small persisted state belongs in an application-owned CSV file on the SD card: a
header row, one data row, a full rewrite on save — the complete state always
being known, so no load-before-write step is needed — and the same convention as
the calibration loaders, in which a missing file leaves the caller's default in
place.

Application settings are deliberately **not** placed in the multicart menu's own
`SettingsRecord`, notwithstanding its `reserved[]` field. That structure belongs
to the multicart firmware, and an application loaded by that menu has no RPC path
by which to request a live mid-session write from the menu's settings pipeline.
Owning a separate file keeps the boundary clear and requires no change to shared
code.

If `SettingsRecord` is extended regardless, note that it carries a
`static_assert(sizeof == 1024)`, so `reserved[]` must be reduced by exactly the
number of bytes added. That preserves the on-disk blob size, but a blob written by
earlier firmware holds arbitrary prior contents in what is now a defined field.
Normalize on first load.

---

## 9. Getting started

### 9.1 Building and running the reference application

```bash
# STM32 firmware: toolkit and reference handler, no application layer
cd code/stm32
make vxcoop USE_HW=v0.3
dfu-util -a 0 -d 0483:df11 -s 0x08000000 -D stm32.bin

# the 6809 cartridge
cd ../app6809
docker run --rm -v "$PWD":/build -w /build -u $(id -u):$(id -g) \
       asm6809 asm6809 -B -o VXCOOP/VXCOOP.bin VXCOOP/VXCOOP.asm
cp VXCOOP/VXCOOP.bin /Volumes/VEXTREME/roms/
```

Points to observe:

- **`USE_HW` is compile-time.** Building for the wrong board revision causes the
  firmware to wait on a signal that is never asserted. Confirm the physical
  revision first.
- **A bare `make` does not build.** `flash-release:` is the first target, so
  `make` alone invokes it and fails. Use `make all USE_HW=v0.3`.
- **Changing a build gate requires `make clean`.** `.SECONDARY:` marks objects
  intermediate, so make will not rebuild them when the `.elf` appears current,
  and nothing tracks `CFLAGS` as a dependency. A stale relink produces an image
  matching neither configuration. The `vxcoop` and `dist` targets bundle the
  clean for that reason.
- **Docker clock drift** can cause make to conclude incorrectly that a rebuild is
  required and then fail. Restart Docker, or flash directly with `dfu-util`.
- **zsh expands a bare `*`.** Quote it in `dfu-util` byte-count arguments.

Deployment is asymmetric, and it shapes the development cycle: **a 6809 change is
a file copy to the USB volume, while an STM32 change is a firmware rebuild and
DFU reflash.** Place anything requiring rapid iteration on the STM32 side.

### 9.2 A minimal cartridge

```asm
        include "vectrex.i"
        ORG     0
        fcb     "g GCE 2026", $80
        fdb     silent_music
        fcb     $F8, $50, $20, -$56
        fcb     "MY APP", $80, 0       ; also the high-score lookup key
        jmp     main                   ; defence 1; never omit

silent_music
        fdb     $FEE8
        fdb     $FEB6
        fcb     $00, $80

        setdp   #$d0                   ; assembler directive; emits no bytes
        include "vxt/vxt_rpc_macros.asm"   ; macros only; safe here
        include "vxt/vxt_input.asm"
        include "vxt/vxt_smart.asm"
        include "vxt/vxt_sound.asm"

main
        VXT_RPC_INIT                   ; stub to RAM, DP=$D0. Must be first.
        VXT_SOUND_INIT
        VXT_INPUT_INIT
        clr     Vec_Joy_Resltn         ; for analog response only
        ; ... publish SmartList addresses, then VXT_RPC #69 ...
        VXT_RPC #<your init id>

frame_loop
        jsr     Wait_Recal
        VXT_INPUT_READ  Joy_Analog
        VXT_RPC #<your frame id>
        jsr     vxt_sound
        ldu     #$0800
        jsr     vxt_smart
        bra     frame_loop

        include "vxt/vxt_rpc_stub.asm"   ; defence 2; must be LAST
```

**Include both defences; neither alone is sufficient.** They are an explicit
`jmp main` immediately after the header, and placement of the RAM-resident stub
physically last, after every branch target.

The failure they prevent is a power-on freeze. The Vectrex BIOS falls through to
whatever follows the header's terminating zero byte, so a stub include placed before
`main` is executed in place at power-on with undefined values in A and X. It stores
an undefined RPC identifier to `$7FFF` and executes `jmp ,x` to an undefined
address. Note that the poll loop passes trivially in that state, `$0000` and `$0001`
already containing `'g'` and `' '` from the header itself, so nothing stops it.

Sprite_tm's 2015 source uses the second defence structurally. Keep both.

Note the nature of `setdp #$d0`. It is an **assembler directive** instructing the
assembler to assume DP = `$D0` when selecting addressing modes for subsequent
**source lines**, which is what allows the draw engine's VIA accesses to assemble
compactly. Its effect depends on source position and is entirely distinct from
the runtime `lda #$d0 / tfr a,dp` inside `VXT_RPC_INIT`. **Both are required.**
This is the reason the RPC include is divided in two: a macros-only file, which
emits no bytes and may therefore be placed early, before `setdp`, and the stub
body, which must be placed last.

### 9.3 A minimal STM32 handler

```c
void my_frame_handler(uint8_t id, volatile uint8_t *parm)
{
    (void)id;
    /* 1. read input from parm[], which is write-only from the 6809 side */
    /* 2. advance application state */
    /* 3. one sound span */
    vxtSoundBegin();
        /* ... queue only what changed ... */
    vxtSoundEnd();
    /* 4. one draw span, bounded by THIS region's own capacity */
    vxtSmartBegin(VXT_SMART_OFFSET, MY_REGION_RECORDS);
    gamelibBeamBegin(POS_SCALE, DRAW_SCALE);
    gamelibBeamPrime(2);
        /* ... emit geometry ... */
    vxtSmartEnd();
}
```

> The region bound must be the sub-region's actual capacity and never the outer
> full-image bound. Too large a bound permits one region to overwrite an
> adjacent one.

Register the handler, and an initialization handler alongside it:

```c
vxtRpcInit();
vxtRpcRegister(69, vxt_smart_addrs_handler);   /* shared engine handshake */
vxtRpcRegister(78, my_frame_handler);
vxtRpcRegister(79, my_init_handler);
```

The initialization handler is required rather than merely tidy: **a 6809 warm
reset does not reset the STM32.** Without an initialization fired from the
cartridge's own startup, the STM32 resumes in its prior state, with counters
continuing and stale values retained, and the application misreports its own
condition. It is also the only correct location for blocking SD reads.

---

## 10. Consolidated rules

**Architecture**

1. The two processors are **serialized**. Do not attempt to overlap them
   and do not double-buffer to that end. An explicit readiness flag cannot work,
   the bus not being served at the moment such a flag would need to be read.
2. **1 ms of STM32 time costs approximately 38 records of drawing.** The STM32
   occupies 7 to 11 percent of a frame; optimization belongs on the 6809 side.
3. **`parmRam` is write-only from the 6809 side.** STM32-to-6809 data travels in
   the served image.
4. **No interrupts on the ROM-emulation path.**

**Drawing**

5. Two budgets: **1536 records of memory and approximately 510–730 of time.**
   Exceeding the second produces flicker.
6. **A draw scale must be an integer multiple of the reposition scale.**
   Grid-safe scales are multiples of 32.
7. **Do not purchase precision with records or recenters.** Search the scale
   instead.
8. **Distribute chained steps proportionally**, never by clamping each axis.
9. **Target quantized coordinates**, not raw ideals; repositions can land only on
   the grid.
10. **Independent per-edge drawing corrects drift at 3 to 4 times the cost.**
    Apply it to the edges that require it, not to an entire model.
11. **Close an open draw run** at the end of a renderer, or the beam dwells lit
    at that point and it renders brighter.
12. **Prime the beam**; the frame's first element lands displaced.

**Geometry**

13. **Clip against the near plane first.** The eye lies at `z = -focalLength`,
    and beyond it geometry projects mirrored and magnified. Every test downstream
    of invalid coordinates is meaningless.
14. **A model's scale constant and its bounding radius must change together**,
    and detail tiers should be confirmed reachable.
15. **Backface culling is exact only for a convex closed mesh.** The front-facing
    test is perspective. Store raw `d`. Validate by ray casting.
16. **A `STRADDLE` classification does not imply that every edge crosses.** Test
    each edge's own endpoints.
17. **A clamp is not a gate.** Clamping one end of a segment still draws it.
    Where sibling blocks each carry their own distance gate, the defect lies in
    whichever one does not.

**Sound**

18. **One `vxtSoundBegin()`/`End()` span per frame**, shared by every source.
19. **Queue only changes.** The AY latches; re-emission discards the design.
20. **Compute the shared mixer byte in one place.** Music updates first, effects
    assert last.
21. **Silence must be requested.**

**Process**

22. **No blocking I/O on the frame path.** Load at initialization, apply per
    frame, save on a press. Verify by walking the call graph.
23. **"No data" and "measured zero" are distinct.** Never default silently to
    another machine's constant.
24. **No simulator exists for bus timing.** A clean build is necessary but not
    sufficient; verify on hardware and record the result.
25. **Where a change is mechanically checkable, check it mechanically.** Diff the
    control flow after a rewrite, grep the frame path for I/O, count actual array
    entries rather than trusting a stated total, and exercise a new parser
    against real logged data before relying on it.
26. **Where repeated readings of the code disagree with hardware, stop reading
    and instrument.** Add a readout.
27. **Bound the error before theorizing about it**, and prefer the explanation
    that can be computed over the one that can only be asserted.

---

## Appendices

- **[Appendix A — the VXT library modules](Appendix_A_VXT_Libraries.md)**
- **[Appendix B — function-level reference](Appendix_B_Reusable_Functions.md)**

## External references

| Source                                               | Covers                                                        |
|------------------------------------------------------|---------------------------------------------------------------|
| Sprite_tm, `spritesmods.com/?art=veccart`            | the original cartridge design and its rationale               |
| `voom.asm` (2015)                                    | the original dual-CPU application, 175 lines                  |
| Malban, *VPatrol ingenuity part III*, vide.malban.de | the SmartList dispatch technique, attributed there to Kristof |
| AY-3-8910/8912 datasheet                             | the PSG register map, with register numbering in octal        |
| `vecx.c`                                             | an authoritative behavioral model of the VIA/PSG interconnect |
| Vectrex programmer's manuals, Vol. 1 and Vol. 2      | BIOS entry points, requirements and side effects              |

**License:** GPLv3. Derived from Sprite_tm's `veccart`; the SmartList technique is
Malban's, attributed there to Kristof. VXT toolkit additions: Copyright (C) 2026
Caelotronics.
