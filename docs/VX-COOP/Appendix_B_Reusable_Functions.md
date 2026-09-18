# Appendix B — Function-Level Reference

Each entry states the function's purpose and the constraints governing its use.

Return to the [reference guide](VX-COOP_Reference_Guide.md) ·
[Appendix A](Appendix_A_VXT_Libraries.md)

**⚠** marks a documented constraint. Read the note before calling.

> See the reference guide's *nota bene* (Section 0): descriptions of prior
> work here are derived from reading its code, not from its authors, and
> should be independently validated.

---

## B.0 Status notation

| Notation      | Meaning                                                                                                                                                 |
|---------------|---------------------------------------------------------------------------------------------------------------------------------------------------------|
| **TOOLKIT**   | available in `vxt/` or `gamelib/` and callable directly                                                                                                 |
| **REFERENCE** | implemented in `vxcoop/vxcoop_handler.c` as a self-contained example to be copied, the toolkit providing the method rather than an exported entry point |

The occlusion routines in Sections B.5 and B.6 are **REFERENCE** rather than
**TOOLKIT**. The geometry is short — the aperture classification, point test and
segment clip together occupy approximately sixty lines — and the derivations are
given in full in the reference guide, Sections 7.1 to 7.3. Promoting them into `gamelib/`
is identified but not undertaken.

---

## B.1 Frame skeleton

### `vxtRpcInit(void)`, `vxtRpcRegister(uint8_t id, fn)`, `vxtRpcDispatch(int)`
**TOOLKIT** — `vxt/vxt_rpc.h`

```c
vxtRpcInit();
vxtRpcRegister(69, vxt_smart_addrs_handler);   /* shared engine handshake */
vxtRpcRegister(78, my_frame_handler);
vxtRpcRegister(79, my_init_handler);
```

The handler signature is `void (*)(uint8_t id, volatile uint8_t *parm)`.
Registration refuses identifiers at or below 18, and refuses an identifier already
claimed.

⚠ **Only identifiers 64 to 79 reach the dispatcher**, `main.c`'s hook
range-checking before the call. Currently free: 64, 65, 78, 79. Any other
identifier requires that one line to be widened.

⚠ **Register an initialization handler in every case.** A 6809 warm reset does not
reset the STM32, so without one the handler resumes in its prior state.

### `vxtImgPut(uint16_t pos, uint8_t b)`
**TOOLKIT** — `vxt/vxt_frame.h`

The only correct means of writing a byte into the served ROM image. It implements
the bank-mirroring rule: mirror to `pos ^ 0x8000` for the 64K `cartData` buffer,
because `romemu.S` inverts PB6/A15, and do not mirror for the 20K `menuData`
buffer, mirroring overflowing it. Every toolkit writer routes through this single
implementation, and it should not be reimplemented.

---

## B.2 Drawing — the SmartList emitter

All from `vxt/vxt_smart.h`; all **TOOLKIT**.

### `vxtSmartBegin(uint16_t offset, int maxRecords)`
```c
vxtSmartBegin(VXT_SMART_OFFSET, MY_REGION_RECORDS);
```
⚠ **`maxRecords` must be the sub-region's own capacity and never the outer
full-image bound.** Too large a bound permits one region to overwrite an adjacent
one. The call reserves one slot for the terminator, so that `vxtSmartEnd()` can
never be the record dropped on overflow.

### `vxtSmartEnd(void)`
Writes the terminator into the reserved slot, deliberately bypassing the budget
check. An unterminated list causes the 6809's walker to run past the end of the
list and execute stale bytes as routine addresses.

### `vxtSmartRecordCount(void)` and `vxtSmartOverflowed(void)`
No simulator exists for 6809 draw timing, so measurement on the device is the only
means of sizing a frame. Both budgets apply: 1536 records of memory and
approximately 510 to 730 of time at 50Hz.

### `vxtSmartIntensity(uint8_t i)`
Full 0 to 127 range. Change-gated, so call it unconditionally and let it decide.
Without that gate a loop over objects pays a full BIOS intensity call per object,
which is approximately 15 percent of the frame budget spent setting a value that is
already correct.

### `vxtSmartScale(uint8_t scale)`
⚠ **Not** change-gated, unlike intensity; it emits on every call. Do not leave a
dispatcher wrapping every draw in `scale … scale`, which makes a one-record edge
occupy three and can reach 54 percent of an object's total record count. Use the
held-scale helpers in Section B.3 for a run of draws at one scale.

### `vxtSmartMove/Draw/Cont(int8_t y, int8_t x)`
One record each. `y` and `x` are rate units, that is physical units divided by the
scale, and ±100 is safe. `Cont` continues whichever mode is open and is the least
expensive record available.

### `vxtSmartDraw32(int8_t y, int8_t x)`
The scale-32 single-record draw. ⚠ **Test `vxtSmartHasDraw32()` first.** Unlike the
other optional routines this one is selected by the dispatcher rather than the
application, so new firmware running an older cartridge would emit records pointing
at stale `parmRam` contents and `pulu a,b,pc` would jump into them.

### `vxtSmartMoveBig/DrawBig/DrawHuge(int32_t physY, int32_t physX, uint8_t scale)`
The arguments are physical units; the functions divide by `scale` internally with
round-to-nearest and chain steps of at most ±100 rate units.

⚠ **`scale` must be the scale already loaded on the 6809 side.** Emit
`vxtSmartScale(scale)` first, or the displacement is wrong by the ratio of the two.

Steps are distributed proportionally rather than clamped per axis. A per-axis
clamp renders `(200,100)` as `(100,100)` followed by `(100,0)`, which is a dogleg
rather than a chord.

Selection by cost: `DrawBig` uses a fixed scale of 64 and is least expensive;
`DrawHuge` polls Timer 1 and is correct at any scale, at approximately 2.2 times
the cost; `DrawHuge16` uses a 16-bit duration, reaching approximately 6.5 million
units in one record.

---

## B.3 Drawing — `gamelib_beam`

All **TOOLKIT**, from `gamelib/gamelib_beam.h`.

### `gamelibBeamBegin(uint8_t posScale, uint8_t drawScale)`
Once per frame, alongside `vxtSmartBegin()`. Resets the internal run-mode tracker
and records the two resting scales.

### `gamelibBeamPrime(int cycles)`
Immediately after `Begin` and before any real geometry. The frame's first element
lands with 2.5 to 4.8 times the mean error of everything else (reference
guide, Section 3.8). Approximately 6 blanked records per cycle, with 2 as the default. A
structural correction rather than a calibration value.

### `gamelibRepositionAbs(int32_t physY, int32_t physX)`
Absolute reposition: a closing blanked move, which is unconditional and must not be
skipped even from a cold state, then a recenter, then a `MoveBig` at `posScale`.
Leaves the scale at `drawScale` and the run-mode cleared.

### `gamelibDrawAutoLine(absY, absX, dy, dx, intensity, preferredScale)`
The default for a single independent line. It repositions, sets intensity, and then
selects the least expensive correct technique: one record at `preferredScale` where
that reaches, escalating to the polling path otherwise, and dropping to the
ladder's fast tier where that suffices. It never chains at a fixed scale
implicitly.

### `gamelibDispatchAutoDraw(dy, dx, preferredScale, int32_t *outDy, int32_t *outDx)`
The same escalation decision without a reposition and without an intensity change,
for callers managing their own position tracking.

⚠ **Add `*outDy` and `*outDx` to the tracked position; never an estimate based on
`preferredScale`.** The scale actually used may differ, escalation being possible.
Assuming `preferredScale` makes the tracked position diverge from the position
reached, and the divergence compounds on every subsequent segment rather than
staying bounded.

### `gamelibChainDelta(int32_t dy, int32_t dx, int wantDraw)`
Chains a delta of any magnitude as a draw run (`wantDraw` non-zero) or a move run,
continuing an existing run of the same kind across multiple calls, so that a
multi-segment path remains one continuation chain rather than paying a fresh
dispatch per segment. It switches to a fresh start automatically when the mode
changes.

### `gamelibChainBigGrouped(dy, dx, bigScale, *actualY, *actualX, *segCounter, groupSize)`
The default for a multi-segment figure. Forces a genuine recenter every
`groupSize` segments, bounding drift to that many segments' worth and reducing
recenters by approximately the same factor. A group size of 1 is the safe,
expensive fallback, and 0 never recenters.

⚠ **Use it only for geometry whose segments need to connect to their own neighbors.**
A figure's tracked position is a digital prediction and does not reliably follow real
analog drift. Grouped chaining re-grounds it with a genuine recenter every
`groupSize` segments, which is what makes it a safe target for another figure; do not
aim anything at an unbounded chain's tracked position.

### `gamelibChainBigClosedPerimeter(py[], px[], n, bigScale, intensity)`
A closed n-point perimeter from a single reposition. Its closing segment targets the
position actually reached, computed via `gamelibRoundToScale()`, and not the ideal
point 0 — per-segment rounding drift accumulates around the loop and the closing
segment inherits all of it, which shows as a corner that fails to meet. Sets
intensity and leaves the run marked open. Use only for a self-contained closed
figure.

### `gamelibDrawBigLine(...)` and `gamelibDrawHugeLine(...)`
The explicit-technique forms underlying `DrawAutoLine`. Use them only when one
specific technique is required; otherwise `DrawAutoLine` selects better than a
fixed constant.

### Held-scale chained draws
```c
gamelibBeamSetDrawScale(BIG_DRAW_SCALE);   /* repositions now restore to 64 */
vxtSmartScale(BIG_DRAW_SCALE);             /* once, before the run */
    /* ... gamelibChainBigHeld(...) per edge ... */
vxtSmartScale(MY_DRAW_SCALE);              /* once, after */
gamelibBeamSetDrawScale(MY_DRAW_SCALE);
```
- **`gamelibBeamSetDrawScale(uint8_t)`** changes the value the next restore writes
  and emits nothing itself. ⚠ **Always restore it**, or an unrelated later element
  inherits it.
- **`gamelibChainBigHeld(dy, dx, bigScale, *actualY, *actualX)`** draws one chained
  edge with no surrounding scale records.
- **`gamelibChainAccurateHeld(dy, dx, *actualY, *actualX)`** uses the scale-search
  path at a cost of two records. Use this form where a chained figure's vertices
  are targeted independently: fixed-scale quantization measured 48 units mean and
  96 worst against an independent anchor, while the scale search measured 4 mean
  and 37 worst.

⚠ Do not omit the paired `SetDrawScale` calls; they are what maintains one record
per edge. Without them `gamelibRepositionAbs()`'s trailing restore resets the scale
and every subsequent edge has to set it again.

### `gamelibBeamCloseRun(void)`
Call at the end of any renderer that ends on a draw. One blanked move, and
idempotent, so calling it when the run is already closed is harmless. Omit it and the
beam remains lit at that point for the duration of whatever expensive setup follows,
rendering visibly brighter than anything else in the frame.

### `gamelibBeamMarkOpenDraw(void)`
Bookkeeping only, for callers emitting raw `vxtSmartScale`/`DrawBig` chains, so
that the next `RepositionAbs()` still emits its closing move correctly.

### `gamelibRoundToScale(int32_t val, int32_t scale)`
Returns the physical delta the emitter will actually produce for a given value and
scale, after its own round-to-nearest chaining.

⚠ **Target this value, never a raw ideal coordinate.** Repositions land only on
multiples of `posScale`, so a chained figure and an independently repositioned one
aim at different points by construction unless both target the quantized value.

### `gamelibBeamSetFastDraw(int enable)`
A per-renderer opt-out of the short-draw fast path; bracket a renderer with `(0)`
and `(1)`. Reset to 1 by `gamelibBeamBegin()` each frame, so an omitted restore
cannot propagate. Preferred over requesting the accurate path when only the ladder
is to be disabled, the accurate path also moving the caller onto the polling
dispatch at approximately 2.2 times the cost. Costs no records.

---

## B.4 3D to 2D projection

**TOOLKIT** — `gamelib/gamelib_proj3d.h`.

### `gamelibProject3DDeep(u, v, faceX, cy, sy, cp, sp, focalLength, *outY, *outX)`
Yaw about the vertical axis, then pitch about the horizontal, then a true
perspective division, `scale = focalLength/(focalLength + z)`. For deep volumes,
with world depths to approximately 200,000 units.

⚠ **Axis naming:** `faceX` is across, `v` is vertical, `u` is depth.

⚠ **The eye lies at `u = -focalLength`.** Clip against a near plane before any
window or occlusion test: beyond the eye, geometry projects mirrored and greatly
magnified, with coordinates in the hundreds of thousands, and every downstream test
is then meaningless. Reference guide, Section 6.1.

⚠ **Sine and cosine are passed in precomputed**, so that a per-vertex loop does not
re-derive them. When vertices have already been rotated by the caller, pass
identity angles; rotating twice is a silent error.

### `gamelibProject3D(u, v, faceX, cy, sy, cp, sp, perspD, *outY, *outX)`
Weak perspective, `scale = 1 - z/perspD`, with no division and therefore less
expensive. Appropriate for shallow volumes. It has a documented cosmetic
left/right curvature asymmetry.

⚠ For deep volumes its quadratic term can become negative or non-monotonic; use
the `Deep` form there. The two are deliberately separate functions rather than one
function with a flag.

### `gamelibLerpEdge(int32_t a, int32_t b, int32_t coord, int32_t halfExtent)`
Interpolates a point onto the straight chord between two already-projected
endpoints. Use it for any interior point required to lie exactly on a border
already drawn: projection along an edge is quadratic while a drawn edge is a
straight chord, and re-projecting the interior point through the full 3D
mathematics causes it to miss the line it should touch.

### Rotation for culling
Culling requires normals rotated by exactly the convention the projection uses. No
helper is exported; copy the rotation from `gamelib_proj3d.c` rather than
re-deriving it. `vxcRotate()` in `vxcoop_handler.c` is that copy, with the
reasoning attached. Culling computed under a different rotation convention from the
projection produces a result that is wrong but plausible.

---

## B.5 Backface culling

Derivation: reference guide, Section 7.1. Reference implementation: `vxcCullFaces()` and
`vxcEdgeVisible()` in `vxcoop_handler.c`, together approximately 25 lines. Hull
generation and validation: `tools/obj_cull_tool.py`.

### The front-facing test — **REFERENCE**
```
    dot(n_rot, objectPos) + modelScale*d + focalLength*n_rot.u  <  0
```
The per-face inputs are the unit outward normal and `d = dot(n, faceCentre)`. The
cost is a few multiply-adds per face, which is no greater than the cost of the
approximation it replaces.

⚠ **`rotZ < someConstant` is the orthographic form and is incorrect**, measured at
4.63 percent of edge decisions wrong.
⚠ **Store the raw geometric `d`, never a threshold with the model scale already
applied.** The baked form is algebraically identical and becomes stale silently the
next time a scale constant is retuned.

### Edge visibility
Not a function but a single rule:

```
    an edge is visible  iff  EITHER of its two bordering faces is front-facing
```

This handles the silhouette case by drawing the edge whole, so no partial-edge
splitting is required.

### `tools/obj_cull_tool.py` — **TOOLKIT**
Given a vertex list, computes the exact convex hull and emits faces, outward
normals, `d` values and edge-to-face adjacency, then validates by ray casting.

⚠ **Use it as a check on supplied face data, not only as a generator.** An export
that divides one quadrilateral along the wrong diagonal creates a false crease, and
one wrong edge out of 45 is enough to make the cull visibly wrong while every other
edge behaves correctly.

⚠ **Validate by ray casting, never by a self-consistency check.**
Centroid-direction and topological-winding tests pass on data that is still wrong.
Expect to need the measurement to locate each remaining error; a mesh can read 5
percent wrong, then 4.63 percent, then 0.00 percent over 1,620 tests, and none of
that is visible by inspection.

⚠ **The method is exact only for a convex closed mesh.** On geometry with parts
mounted on struts, a face may be front-facing and nevertheless occluded, and a flat
single-sided panel is legitimately visible from both sides.

---

## B.6 Occlusion — aperture and shadow volume

### `vxcWindowClassify(int32_t px, int32_t py, int32_t r)` → `INSIDE`/`STRADDLE`/`CULL`
**REFERENCE.** Classifies a projected bounding circle against a convex aperture.
For a convex polygon, a center at least `r` inside every edge places the whole disc
inside, and at least `r` outside any one edge places it wholly outside.

⚠ **`STRADDLE` does not mean that every edge crosses.** The test uses a
conservative bounding circle, so objects are routinely classified `STRADDLE` with
no edge actually crossing. Buying the clipping path for an entire model in order to
correct two edges is a common way to exceed the record budget.

### `vxcPointInWindow(int32_t py, int32_t px)` → `0`/`1`
**REFERENCE.** The per-edge pre-test that makes the above affordable. An edge with
both endpoints inside requires no clipping, and most edges qualify.

### `vxcClipToWindow(y0, x0, y1, x1, *oy0, *ox0, *oy1, *ox1)` → `0`/`1`
**REFERENCE.** Cyrus-Beck clip against the convex aperture, returning the clipped
endpoints or 0 when the segment lies wholly outside.

⚠ **Derive the inward normal per edge and sign-check it against a known interior
point** rather than assuming a winding direction. One additional dot product
removes an entire class of error.
⚠ **Classification and clipping must read the same geometry definition**, so that
they cannot disagree about the aperture's position.
⚠ A clipped edge no longer begins where the previous one ended and must therefore
be drawn independently. That is the cost of clipping.

### Near-plane clipping — **REFERENCE**
`vxcProjectCube()` returns 0 when any vertex lies at or beyond the near plane, and
the caller then emits nothing.

⚠ **This must precede every window test.** Those tests are meaningless when applied
to the coordinates a past-the-eye vertex projects to. Reference guide, Section 6.1.

For a production model, clip the individual crossing edge rather than dropping the
whole object: interpolate the crossing point in world space between the two
endpoints and draw the surviving portion.

### Shadow-volume clipping — **REFERENCE**
Derivation: reference guide, Section 7.3. The construction reduces to a Liang-Barsky segment
clip against a plane list derived from the occluder.

⚠ **Classify every face every frame from its own `b`.** Do not hardcode an
orientation: a near cap inverts as soon as the camera passes it, and the resulting
failure suppresses most of the occlusion while the image still looks plausible —
on the order of 6.5 million wrongly-drawn sample points.
⚠ **`b_i < 0` is precisely the front-facing condition** — one sign test serves both
this and Section B.5, and the two should not be computed separately.
⚠ **Only adjacent back-facing/front-facing pairs produce a genuine bounding
plane**, being the silhouette edges: approximately 15 planes rather than 30 for an
eight-sided prism.
⚠ **On plane-list overflow, disable occlusion for that object entirely** rather
than clipping against the planes that fit; a short list silently over-occludes.
⚠ **Guard the case in which the eye lies inside the solid.** The construction then
correctly reports that everything is hidden and blanks the display, which is
geometrically correct and indistinguishable from a failure.

---

## B.7 Sound

All **TOOLKIT**, from `vxt/vxt_sound.h` and `vxt/vxt_music.h`.

### `vxtSoundBegin(void)` and `vxtSoundEnd(void)`
⚠ **Exactly one span per frame, shared by every sound source.** `Begin()` resets
the pending-pair buffer, so a second call within one frame silently discards
whatever the first caller queued. `End()` is a no-op when nothing was queued: the
sequence byte is left unchanged and the 6809 skips servicing in approximately 12
cycles.

### `vxtSoundTone(vxtSndChan ch, uint16_t period12, uint8_t amp)`
A 12-bit period, smaller being higher, and an amplitude of 0 to 15, or
`VXT_AMP_ENV` to transfer the level to the envelope generator.

⚠ **Queue only changes.** The AY latches and sustains; re-emitting an unchanged
tone each frame discards the design.

### `vxtSoundMixer(uint8_t toneEnableMask, uint8_t noiseEnableMask)`
Accepts positive-logic enable masks and performs the inversion, the AY's own logic
being inverted such that 0 enables, and force-clears bit 6.

⚠ **Always use this wrapper rather than `vxtSoundReg(7, x)`.** Bit 6 is the I/O
port direction; setting it causes the AY to drive the button lines and input
reporting to fail.
⚠ **One register controls all six routings, so compute the whole byte in one
place** from the current state of all three channels. Two callers each writing an
independent conception of the mixer is the characteristic failure.

### `vxtSoundNoise(uint8_t period5)` and `vxtSoundEnvelope(uint16_t period16, uint8_t shape)`
A 5-bit noise period, and the envelope shapes `VXT_ENV_DECAY`, `ATTACK`, `SAW`,
`TRIANGLE` and `ATK_HOLD`.

### `vxtSoundReg(uint8_t reg, uint8_t val)`
The raw interface. ⚠ **It discards any register number above 13** rather than
clamping, clamping being liable to write into register 13, the envelope shape.
Register 14 is the buttons.

### `vxtSoundSilence(void)`
Sets all three amplitudes to 0. ⚠ **Silence must be requested.** The AY sustains,
so leaving a screen during an effect leaves a tone running. An unbounded level
trigger is the first sound that will survive a transition and continue over a menu.

### `vxtMusicPlay/SwitchSong/Stop/Update/IsPlaying/LoopCount/ReassertChannel/SetRate`
⚠ **`vxtMusicUpdate()` must be called inside an already-open span**, and must run
before the effects queue, so that it consumes its one-shot mixer claim before
anything can contend with it.
⚠ **Use `vxtMusicSwitchSong()` rather than `Play()`** for a track change during play
while effects may be active; `Play()` re-arms that claim.
⚠ **Call `vxtMusicReassertChannel(ch)`** at the moment a borrowed channel is
returned, and **after** restoring its mixer routing, not before: the call writes
only the tone period and amplitude registers and never the mixer, so re-emitting a
tone into a channel still routed to the noise generator is inaudible. Without the
call the channel holds the effect's final register values until the song's next
scheduled event for it, which may be many frames away and is heard as that channel
falling out of time with the other two. Note that this restores *register state*,
not playback position — the sequencer advanced normally throughout the borrow, so
there is nothing to seek. It is a no-op when no song is assigned, so that case needs
an explicit amplitude-zero instead. Reference guide, Section 5.5.1.
⚠ **Use `vxtMusicLoopCount()`** rather than a frame counter divided by
`totalFrames`. Playback advances on real elapsed time and diverges from a frame
count whenever the rate departs from a stable 50Hz; a caller expecting two passes
measured approximately 2.5.

---

## B.8 Input

### `VXT_INPUT_INIT` and `VXT_INPUT_READ <Joy_Digital|Joy_Analog>`
**TOOLKIT** — `app6809/vxt/vxt_input.asm`. Once at startup, once per frame.
⚠ For analog response also `clr Vec_Joy_Resltn`; the default POTRES of `$80` is too
coarse.

### `vxtInJoyX/Y(parm)`, `vxtInJoy2X/Y(parm)`, `vxtInDir(v)`
**TOOLKIT** — `vxt/vxt_input.h`. Signed accessors over the unsigned `parmRam`.
`vxtInDir()` reduces an axis to −1, 0 or +1 and functions whichever BIOS routine
populated it, which is the purpose of reporting raw values.

### Buttons
| Requirement                                         | Read                                                             |
|-----------------------------------------------------|------------------------------------------------------------------|
| **Discrete** — page change, toggle, menu navigation | `parm[VXT_IN_BTN1_1..4]`, the raw edge byte, one frame per press |
| **Continuous** — adjustment while held              | the `VXT_IN_BTNS` held bitmask                                   |

⚠ Using the held pattern for a discrete action causes it to fire for every frame of
a press. Using the edge byte for a held action causes it to fire once and then
cease.
⚠ **No documentation states which bit of `VXT_IN_BTNS` corresponds to which
button.** Determine it empirically: on the frame an edge byte asserts, the single
bit set in the held mask belongs to that button. Latch it once, guard against
multi-button presses, and use the edge byte until then.

---

## B.9 Calibration and persistence

### `vxtCalLoadDrawGain/ClosureComp/Offset/TextComp(...)`
**TOOLKIT** — `vxt/vxt_cal_load.h`. Each returns 1 and fills its out-parameter only
when usable data was found, and otherwise returns 0 leaving it untouched.

⚠ **The caller supplies the fallback.** "No data" and "measured zero" must remain
distinguishable; substituting another machine's fitted constant is the error the
mechanism exists to prevent.
⚠ **Blocking SD I/O; one-shot initialization only, cached in a static.**
Approximately 100ms per call is five lost frames at 50Hz, per frame. The symptom is
flicker rather than an incorrect image.
⚠ **Audit by walking the call graph; a grep of one function body is not enough.**
A call one level down does not appear — in the reference application a body-only grep
of the frame handler reports no hits while `vxtCalSaveRow()` sits below it in
`vxcPageCal()`. Reference guide, Section 8.4.

### `vxtCalSaveRow(screen, variant, ref, refY, refX, dy, dx, compY, compX, drawGain)`
**TOOLKIT.** An upsert by streaming, holding no in-RAM table of prior rows.
⚠ **Blocking I/O; an actual button press only.** Never a timer, never per frame.
⚠ **`drawGain` must reflect what was active for this reading**, 1000 denoting a raw
measurement. Every loader inspects that column to decide whether to accept the row.

### `gamelibBeamSetDrawGain/MoveGain/MoveSettle/Offset(...)`
**TOOLKIT.** The inexpensive per-frame setters. **None costs any 6809 records**,
each altering values within records already being emitted.
⚠ Apply the center offset to absolute positions only and never to deltas: a pure
translation cancels out of any difference between two already-offset points.
⚠ Re-apply the gain per frame where a calibration rig may have forced it to
identity.

### `gamelibChainCloseOffset(dy, dx, k, n, *outY, *outX)`
**TOOLKIT.** Distributes a measured closure error across a chain's segments.

⚠ **The offset applies to the TARGET and never to the DELTA.** Chained draws are
self-correcting, each segment aiming from the beam's tracked position at that
vertex's absolute target, so a correction added to a delta is canceled exactly by
the following segment. `target[k] += D*k/n` moves the landing point.
⚠ **A uniform gain cannot alter a closed figure's closure**: it scales every delta,
and a closed figure's deltas sum to zero. Only an asymmetric correction has any
effect.

### Application settings
A pattern rather than an interface. A header row, one data row, a full rewrite on
save — the complete state always being known, so no load-before-write step is
required — and the convention that a missing file leaves the caller's default in
place. Reference guide, Section 8.5, states why the multicart's own `SettingsRecord` is not
used.

---

## B.10 Text

**TOOLKIT** — `vxt/vxt_smart_text.h`.

`vxtSmartTextBegin(originY, originX)`, `Char`, `Str`, `Number`, `Number32`,
`SetScale`, `SetIntensity`, `SetOrientation` (four rotations), `SetFont` (two),
`SetSkewComp`, `WidthPhys(nchars)`, `GlyphStrokes()`.

⚠ **Text consumes records, at roughly 6 per upper-case character and 125 per
30-character line.** Keep labels short, count them before laying out a screen, and
watch `vxtSmartRecordCount()`. Reference guide, Section 3.4.1.
⚠ **`Begin()` computes `originY / POS_SCALE` as a truncating division**, so an
origin that is not a whole number of reposition steps is partly discarded. Make
adjustments multiples of the reposition scale.
⚠ **Text does not route through `gamelibRepositionAbs()`**, so the global center
offset does not reach it; text can therefore disagree slightly with corrected
geometry.
⚠ **It sets its own scale** and therefore conflicts with a held-scale bracket. End
such a run before drawing text.

---

## B.11 Additional

### `vxtBoundsStep(vxtMovingBody *b, int32_t halfSize, vxtBoundMode mode)`
**TOOLKIT** — `vxt/vxt_bounds.h`. Determines what occurs when a moving object
crosses the screen edge, so that four boundary tests need not be re-derived per
application. Modes: `VXT_BOUND_WRAP`, `VXT_BOUND_BOUNCE`, `VXT_BOUND_VANISH`
(returning 0 once fully off screen) and `VXT_BOUND_CLAMP`. Carries the confirmed
extents `VXT_BOUNDS_HALF_X` (13,500) and `VXT_BOUNDS_HALF_Y` (18,000).

### Host-side tooling
| Tool                           | Function                                                                          |
|--------------------------------|-----------------------------------------------------------------------------------|
| `tools/obj_cull_tool.py`       | convex hull to faces, normals, `d` values and adjacency, with ray-cast validation |
| `tools/calib_analyze.py`       | fits corrections from a logged calibration CSV                                    |
| `tools/vpy_to_music.py`        | offline conversion to `vxt_music` event arrays                                    |
| a host record-stream simulator | counts the records a renderer will emit, without hardware                         |

⚠ **Simulate before spending records.** A host-side simulation of the emitted
stream will usually show that a proposed accuracy fix does not need extra records
at all.

⚠ For any bulk vertex or edge data: **copy the values verbatim, derive counts by
counting the actual entries, and prefer a script to retyping.** A stated count in a
comment has disagreed with its own array.

---

**License:** GPLv3. Copyright (C) 2026 Caelotronics.
