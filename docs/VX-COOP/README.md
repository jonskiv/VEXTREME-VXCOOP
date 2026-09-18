# VX-COOP

**Cooperative dual-CPU application development for the Vectrex, using the VXT
toolkit.**

The VEXTREME cartridge places an STM32F411 on the Vectrex cartridge bus, where
it presents itself as a ROM. VXT is the toolkit that makes that processor usable
as a general-purpose coprocessor: the STM32 computes each frame's geometry,
state, arithmetic and sound, while the 6809 — which is never halted — renders
the result, reads the controller, and services the sound chip.

The architecture originates with Sprite_tm's VOOM (2015). This documentation set
covers the toolkit that generalizes it and the constraints on its use. It is a
starting reference, not a finished or authoritative one, and it will need
correction and extension as it sees more use.

## Reading order

| Document                                                                 | Contents                                                                                                                   |
|--------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------|
| **[VX-COOP_Reference_Guide.md](VX-COOP_Reference_Guide.md)**             | The reference guide: the protocol, the draw engine, sound, input, projection, occlusion, calibration, and a consolidated rule list. |
| **[Appendix_A_VXT_Libraries.md](Appendix_A_VXT_Libraries.md)**           | Every toolkit module, the processor it runs on, and its purpose.                                                           |
| **[Appendix_B_Reusable_Functions.md](Appendix_B_Reusable_Functions.md)** | Function-level reference, each entry with its constraints.                                                                 |

## The reference application

Working code for the example techniques the reference guide describes, containing no
application content and depending on no application layer:

|                |                                                                                                                                           |
|----------------|-------------------------------------------------------------------------------------------------------------------------------------------|
| 6809 side      | [`code/app6809/VXCOOP/VXCOOP.asm`](../../code/app6809/VXCOOP/VXCOOP.asm) — the complete 6809 half, approximately sixty instructions       |
| STM32 side     | [`code/stm32/vxcoop/`](../../code/stm32/vxcoop/) — the handler on RPC 78 (per frame) and 79 (boot init), plus the generated music example |
| Example assets | a three-channel song (`music_example.vpy`, converted offline), an explosion effect, and the toolkit's stroke font                         |

```bash
# firmware: toolkit and reference application, no application layer linked
cd code/stm32
make vxcoop USE_HW=v0.3
dfu-util -a 0 -d 0483:df11 -s 0x08000000 -D stm32.bin

# the cartridge
cd ../app6809
docker run --rm -v "$PWD":/build -w /build -u $(id -u):$(id -g) \
       asm6809 asm6809 -B -o VXCOOP/VXCOOP.bin VXCOOP/VXCOOP.asm
cp VXCOOP/VXCOOP.bin /Volumes/VEXTREME/roms/
```

Button 4 advances the page. Button 3 toggles within a page. Button 2 toggles the
on-screen text, except on `SOUND` where it triggers an effect. Every page prints
`R` for the records its own geometry used and `T` for the whole frame.

| Page      | Demonstrates                                                        |
|-----------|---------------------------------------------------------------------|
| `LADDER`  | six lines drawn by two techniques, with the record count for each   |
| `PROJECT` | 3D-to-2D projection under joystick yaw and pitch                    |
| `CULL`    | backface culling, and the records it saves                          |
| `WINDOW`  | per-edge clipping against a convex aperture                         |
| `SOUND`   | a three-channel song, plus two effects sharing one borrowed channel |
| `INPUT`   | the raw input block, byte by byte                                   |
| `CAL`     | calibration loaded at boot, applied per frame, saved on a press     |

Both source files are written to be read in sequence. Each non-obvious statement
records the constraint it satisfies.

## Two results that govern the design

**The two processors are serialized.** The STM32 is the ROM while the
6809 draws, so the two cannot overlap. One millisecond of STM32 time therefore
costs approximately 38 records of 6809 drawing, and measurement places the STM32
at 7 to 11 percent of a frame. STM32 cycles should be spent freely and 6809
records conserved.

**There are two record budgets, and they differ by a factor of roughly three.**
1536 records fit in memory, that figure being the distance to the sound block.
Between 510 and 730 fit in the time available at 50Hz. Exceeding the second
produces flicker rather than an incorrect image.

## Status

The reference application builds without warnings and the 6809 side assembles,
but **it has not yet been run on hardware.** No simulator exists for
ROM-emulation bus timing on this platform, so a clean build does not confirm
correct behavior — run it on a real unit before relying on any of it.

**License:** GPLv3. Derived from Sprite_tm's `veccart` and VOOM; the SmartList
draw technique is documented by Malban and attributed there to Kristof.
VXT toolkit additions: Copyright (C) 2026 Caelotronics.
