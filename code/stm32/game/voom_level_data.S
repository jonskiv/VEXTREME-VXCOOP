/*
 * voom_level_data.S - VOOM's e1l1 (Doom E1M1) level lumps, linked into STM32
 * FLASH as read-only data.
 *
 * WHY THIS EXISTS. The previous design streamed LINEDEFS/SEGS/
 * NODES from the FatFS volume on demand through an LRU cache, because 28KB of
 * level data does not fit in RAM. Measured on the host against the real lumps,
 * that cost 217 f_open/f_read pairs PER FRAME in steady state - blocking I/O
 * on the frame path, and at ~100ms per f_open it is ~20 seconds per frame,
 * i.e. a permanently blank screen, not merely flicker.
 *
 * The caches did not help at all: a BSP walk touches each node exactly once
 * per frame, so the access pattern is a CYCLE of ~67 distinct nodes / ~104
 * linedefs / ~46 subsectors. An LRU cache smaller than a cyclic working set
 * has a 0% hit rate - the textbook worst case - and the slot counts (64/96/24)
 * sat just under those working sets. Measured hits: zero, every frame.
 *
 * Flash is the right home for this data: it is fixed (one level), the STM32
 * maps it directly, reads are single-cycle, it costs ZERO RAM, and it removes
 * both the per-frame I/O and the "is e1l1/ on the card?" failure mode that
 * cost a whole debugging session. `.incbin` keeps the bytes byte-identical to
 * the files in code/stm32/e1l1/ - no transcription step, no generated source
 * to drift.
 *
 * COST: 28,364 bytes of flash, and ONLY in a VXT_ENABLE_VOOM=1 build - this
 * object is in $(VOOM_OBJS), so a default build without VOOM links none of it.
 *
 * The lumps here are the VEXTREME-preprocessed set (code/stm32/e1l1/), not
 * raw Doom lumps: SIDEDEFS in particular is 1296 bytes, not the WAD's 19440,
 * because VOOM's DoomSide struct keeps only the 2-byte sector index. Do not
 * substitute a raw WAD lump.
 *
 * Sizes are derived by the linker from the _end symbols below, never from a
 * hardcoded constant - a stated count has already disagreed with its own data
 * elsewhere in this project.
 */

	.section .rodata.voom_level_data, "a"

	.balign 4
	.global voom_lump_vertexes
voom_lump_vertexes:
	.incbin "e1l1/VERTEXES"
	.global voom_lump_vertexes_end
voom_lump_vertexes_end:

	.balign 4
	.global voom_lump_linedefs
voom_lump_linedefs:
	.incbin "e1l1/LINEDEFS"
	.global voom_lump_linedefs_end
voom_lump_linedefs_end:

	.balign 4
	.global voom_lump_sectors
voom_lump_sectors:
	.incbin "e1l1/SECTORS"
	.global voom_lump_sectors_end
voom_lump_sectors_end:

	.balign 4
	.global voom_lump_ssectors
voom_lump_ssectors:
	.incbin "e1l1/SSECTORS"
	.global voom_lump_ssectors_end
voom_lump_ssectors_end:

	.balign 4
	.global voom_lump_segs
voom_lump_segs:
	.incbin "e1l1/SEGS"
	.global voom_lump_segs_end
voom_lump_segs_end:

	.balign 4
	.global voom_lump_sidedefs
voom_lump_sidedefs:
	.incbin "e1l1/SIDEDEFS"
	.global voom_lump_sidedefs_end
voom_lump_sidedefs_end:

	.balign 4
	.global voom_lump_nodes
voom_lump_nodes:
	.incbin "e1l1/NODES"
	.global voom_lump_nodes_end
voom_lump_nodes_end:

	.balign 4
