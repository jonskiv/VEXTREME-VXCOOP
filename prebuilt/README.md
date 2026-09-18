Prebuilt binaries
===

Built from the current source in this fork, so you can try the toolkit without
installing Docker/asm6809/arm-none-eabi-gcc first. If you're modifying any of
`code/stm32/` or `code/app6809/vxt/`, rebuild these yourself instead of trusting
stale copies — see [docs/VX-COOP/](../docs/VX-COOP/) for the build commands.

| File | What it is | Where it goes |
|---|---|---|
| `stm32.bin` | STM32 firmware, built with the calibration rig, VOOM, and VX-COOP all enabled | flash via `dfu-util`, replacing the multicart's STM32 firmware |
| `VXCOOP.bin` | VX-COOP, the toolkit reference cart | copy to `roms/` on the multicart's USB drive |
| `VOOM.bin` | VOOM ported onto the SmartList draw engine | copy to `roms/` |
| `test8_cal.bin` | the calibration/measurement rig — controls in [docs/VX-COOP/VX-COOP_Reference_Guide.md, Section 8.1](../docs/VX-COOP/VX-COOP_Reference_Guide.md#81-how-a-measurement-is-obtained) | copy to `roms/` |
| `test6c.bin` | multi-channel sound test, layered on the scene compositor | copy to `roms/` |
| `test1_led.bin` | RPC smoke test against stock (unmodified) firmware | copy to `roms/` |

`stm32.bin` must be flashed once; the cartridge `.bin` files can be swapped
freely afterward by copying a different one to the USB drive and picking it
from the multicart menu.
