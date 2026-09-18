/*
 * voom_smart.c - VOOM's Vectrex output stage, ported from vxt_draw to
 * Copyright (C) 2026 Caelotronics.
 * vxt_smart, the SmartList engine other toolkit applications already use.
 * This is a from-scratch port; VOOM's original vxt_draw-based output stage
 * is not part of this fork.
 *
 * PORT SCOPE: only the two format-specific pieces of the original changed
 * - the record emission, and linesDraw()'s frame brackets. The
 * nearest-neighbor line-chaining walk itself (which line to draw next,
 * when to recenter) is VOOM's own algorithm, has nothing to do with the
 * wire format, and is kept verbatim.
 *
 * ==========================================================================
 * THE SCALE BUG - why the first version of this file drew nothing
 * ==========================================================================
 * The previous revision emitted every VOOM delta through the plain
 * vxtSmartMove()/vxtSmartDraw() path at a FIXED scale of 12, converting a
 * raw VOOM delta `d` into a rate of round(d/12). It picked scale 12 for a
 * real reason - vxt_smart.asm's plain SM_startMove_d/SM_startDraw_d pad
 * their Timer 1 wait with a nop count baked in at assembly time for
 * VXT_SM_SCALE=12, so they are only hardware-correct at that one scale.
 *
 * But it also treated VOOM's raw screen coordinates as if they were
 * PHYSICAL beam units, and they are not. Beam deflection is proportional
 * to (scale x rate) - SM_setScale writes the scale byte straight into
 * VIA_t1_cnt_lo, i.e. it IS the ramp duration. The vxt_draw original's
 * veEmitLine() starts at sp=0x60 and then only ever halves sp while
 * doubling d (or vice versa), so its product is invariant:
 *
 *     physical deflection = 96 x raw_delta          (the original)
 *     physical deflection =  1 x raw_delta          (this file, before)
 *
 * i.e. the port drew VOOM's whole picture at ~1/96 size. Measured on the
 * host against the real e1l1 lumps, running BOTH renderers over the same
 * frame: the original's average emitted step is 4,875 physical units and
 * its longest is 24,384; this file's were 97 and 252. A third of the draws
 * quantized to a rate of literally zero. VOOM's entire map collapsed into a
 * dot a couple of pixels wide at screen center - reported as "the title
 * screen shows, then the screen goes blank."
 *
 * Cross-check on the 96: VOOM's raw space is 256 wide, so a half-span is
 * 128 x 96 = 12,288 physical units. A real game's own cockpit window -
 * which visibly fills the screen - uses a window radius of 12,534. The
 * two agree, independently of VOOM.
 *
 * THE FIX, and why it does not cost 6809 records:
 * work in physical units (VOOM_UNIT below) and pick the ramp scale per
 * operation instead of fixing it at 12.
 *   - DRAWS go through vxtSmartDrawHuge() / SM_startDrawHuge_d, which does
 *     a GENUINE Timer 1 poll and is therefore correct at ANY scale, with no
 *     hand-computed nop pairing to match. One record per line. The scale is
 *     chosen as the smallest that reaches the line in one record and then
 *     SEARCHED upward for the value whose rounding lands closest to the true
 *     endpoint - free precision, exactly as gb_dispatch_huge() does elsewhere.
 *   - MOVES go through vxtSmartMoveBig() / SM_startMoveBig_d at scale 32,
 *     the pairing this project has hardware-proven. Moves are blanked, so
 *     their chaining is invisible.
 * Both are self-describing "start" routines (each sets VIA_shift_reg
 * explicitly), so there is no run-mode state to carry between them.
 *
 * Sign convention (y-arg = -dy, x-arg = dx) is inherited unchanged from
 * VOOM's original hardware-confirmed mapping, matching SM_startMove_d's
 * own A=y,B=x port-write order - do not "correct" it.
 *
 * ==========================================================================
 * LEVEL DATA: FLASH, NOT PER-FRAME SD READS
 * ==========================================================================
 * The e1l1 level data is 28,364 bytes and this build has no RAM for it. The
 * previous revision streamed LINEDEFS/SEGS/NODES from FatFS on demand
 * through three small LRU caches. Measured on the host against the real
 * lumps, that cost 217 f_open/f_read pairs PER FRAME in steady state, with
 * the player standing still - blocking I/O on the per-frame path
 * (~100ms per f_open => ~20 seconds per frame => a blank screen, not merely
 * flicker), and it was reached from inside the recursive BSP walk.
 *
 * The caches contributed nothing: a BSP walk visits each node exactly once
 * per frame, so the access pattern is a CYCLE of ~67 distinct nodes / ~104
 * linedefs / ~46 subsectors, and an LRU cache smaller than a cyclic working
 * set has a 0% hit rate - the textbook worst case. The slot counts
 * (64/96/24) sat just below those working sets. Raising them to 80/112 does
 * take the measured miss count to zero for THIS viewpoint, but that is a
 * cliff, not a margin: a busier view falls straight back off it, and even
 * one miss per frame is five lost frames.
 *
 * So the lumps now live in flash (game/voom_level_data.S, VOOM-build-only)
 * and VOOM.c indexes them directly, exactly as Sprite_tm's original does.
 * No frame-path I/O, no cache, no "is e1l1/ on the card?" failure mode.
 *
 * WHAT IS LEFT IN menuData: only VOOM's z-buffer and 2D line list (10,848
 * bytes). Those are RAM by nature and this build's bss cannot hold them -
 * with them as plain statics, VXT_ENABLE_VOOM=1 overflowed `ram` by 8 bytes
 * and did not link at all. menuData is idle whenever a game cart is running
 * (the same precedent the level-data overlay used to cite), so VOOM's own
 * top-level static RAM is now ~200 bytes.
 */

#include "VOOM.h"
#include "voom_smart.h"
#include "../vxt/vxt_smart.h"
#include "../gamelib/gamelib_beam.h"   /* gamelibRoundToScale() only - the
                                        * pure rounding helper, not gamelib's
                                        * run-mode/gain state machine */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Physical beam units per VOOM raw screen unit. See the scale-bug note in
 * this file's header for the derivation - this is the vxt_draw original's
 * invariant (scale x rate) product, and it puts VOOM's half-span at 12,288,
 * matching a real game's own full-screen cockpit ring. This is THE dial
 * to turn if the picture comes out too large or too small on hardware;
 * nothing else in this file needs to change with it. */
#define VOOM_UNIT 96

/* Scale for blanked repositioning: SM_startMoveBig_d's 11-nop pairing,
 * hardware-proven (vxt_smart.asm). Must stay 32 unless that routine's
 * padding changes with it. */
#define VOOM_POS_SCALE 32

/* This project's established per-record rate limit (vxt_smart.asm), and how
 * far past the minimum scale to search for a better-landing one - same
 * values and same reasoning as gamelib_beam.c's GB_MAX_RATE /
 * GB_SCALE_SEARCH_SPAN. */
#define VOOM_MAX_RATE          100
#define VOOM_SCALE_SEARCH_SPAN  32
/* VIA_t1_cnt_lo is one byte: 255 is the hardware ceiling on a single
 * record's ramp duration. A VOOM delta never needs more than 246
 * (24,576 / 100), so this is a guard, not a live path - and because
 * SM_startDrawHuge_d polls, exceeding it degrades to chained records that
 * are still correct, never to wrong timing. */
#define VOOM_MAX_SCALE 255

/* Region this game owns: the whole $0800-$1FFF SmartList window (VOOM has
 * no sound, so unlike a sound-using game there's no sub-region to share
 * it with) - same OFFSET/RECORDS pattern as other regions in this
 * toolkit (size to the real region, never the outer full-image bound). */
#define VOOM_REGION_OFFSET VXT_SMART_OFFSET
#define VOOM_REGION_RECORDS ((0x2000 - VXT_SMART_OFFSET) / 4)

/* Brightness for VOOM's single, constant draw intensity. Full 0-127 range
 * is available (vxt_draw's original 0x3f was a 6-bit wire-format ceiling,
 * not a real limit); tune on hardware. */
#define VOOM_INTENSITY 100

/* ---------------------------------------------------------------------
 * Level lumps in flash - game/voom_level_data.S. Sizes come from the
 * linker's own _end symbols, never a hardcoded constant - derive counts
 * from the actual data, don't trust a stated total.
 * --------------------------------------------------------------------- */
extern const char voom_lump_vertexes[], voom_lump_vertexes_end[];
extern const char voom_lump_linedefs[], voom_lump_linedefs_end[];
extern const char voom_lump_sectors[],  voom_lump_sectors_end[];
extern const char voom_lump_ssectors[], voom_lump_ssectors_end[];
extern const char voom_lump_segs[],     voom_lump_segs_end[];
extern const char voom_lump_sidedefs[], voom_lump_sidedefs_end[];
extern const char voom_lump_nodes[],    voom_lump_nodes_end[];

/* ---------------------------------------------------------------------
 * menuData overlay - z-buffer + line list only. `extern char
 * menuData[20*1024]` MUST match main.c's real definition exactly - an
 * array-vs-pointer mismatch compiles clean but silently misdirects writes.
 * --------------------------------------------------------------------- */
extern char menuData[20*1024];

typedef struct {
	int x;
	int y;
} Vertex;

typedef struct {
	Vertex p[2];
} Line2d;

#define LINEMAX 150

typedef struct {
	unsigned int zbm[SIZEX*SIZEY/32];   /* 8192 - VOOM.c's z-buffer      */
	char         zbmx[SIZEX];           /*  256 - full-column shortcut   */
	Line2d       lines[LINEMAX];        /* 2400 - this frame's 2D lines  */
} VoomArena;

/* +4 for the alignment round-up in voomArena() below. If this fires, VOOM's
 * arena no longer fits in menuData - shrink LINEMAX or grow menuData.
 * Catches a silent RAM regression at compile time instead of silently
 * corrupting whatever follows menuData. */
_Static_assert(sizeof(VoomArena) + 4 <= sizeof(menuData),
	"VOOM's menuData overlay no longer fits - shrink LINEMAX or grow menuData");

/* menuData is a plain char array, so nothing guarantees the 4-byte
 * alignment VoomArena's `unsigned int zbm[]` needs. It happens to be
 * 4-aligned today; round up anyway rather than depend on that. */
static VoomArena *voomArena(void)
{
	uintptr_t base = ((uintptr_t)menuData + 3u) & ~(uintptr_t)3u;
	return (VoomArena *)base;
}

static int lineIdx;
static int voomInited;

void veLineAdd(int xs, int ys, int xe, int ye) {
	Line2d *lines = voomArena()->lines;
	int t;
	if (lineIdx==LINEMAX) return;
	if (xs>xe) {
		t=xs; xs=xe; xe=t;
		t=ys; ys=ye; ye=t;
	}

	if (xs<0 || xs>SIZEX) return;
	if (ys<0 || ys>SIZEX) return;
	if (xe<0 || xe>SIZEX) return;
	if (ye<0 || ye>SIZEX) return;

	if (xs==xe && ys==ye) return;

	lines[lineIdx].p[0].x=xs;
	lines[lineIdx].p[0].y=ys;
	lines[lineIdx].p[1].x=xe;
	lines[lineIdx].p[1].y=ye;
	lineIdx++;
}

/* ---------------------------------------------------------------------
 * Emission layer. Everything below works in PHYSICAL units relative to
 * screen center, and tracks where the beam ACTUALLY landed (not where it
 * was asked to go), so quantisation cannot accumulate across the up-to-4
 * lines VOOM chains between recenters - aim at points the hardware can
 * actually reach, not the raw ideal coordinate.
 * --------------------------------------------------------------------- */
static int32_t voomBeamY, voomBeamX;   /* actual landed position, 0 = center */
static int     voomCurScale;           /* last scale byte emitted; 0 = none  */

static int32_t voomPhysX(int rawX) { return ((int32_t)rawX - SIZEX/2) * VOOM_UNIT; }
static int32_t voomPhysY(int rawY) { return -(((int32_t)rawY - SIZEY/2) * VOOM_UNIT); }

static int32_t voomAbs32(int32_t v) { return (v < 0) ? -v : v; }

static void voomSetScale(int s) {
	if (s == voomCurScale) return;
	voomCurScale = s;
	vxtSmartScale((uint8_t)s);
}

/* A recenter needs a startMove-family record in front of it - UNCONDITIONALLY,
 * after an open draw run AND from a cold state alike. Skipping it leaves a
 * visible artifact; that is hardware-confirmed elsewhere in this toolkit
 * and is why gamelibRepositionAbs() opens with vxtSmartMove(0, 0). Same
 * closing move here, for the same reason.
 *
 * The scale is forced to VOOM_CLOSE_SCALE first because vxtSmartMove() routes
 * through SM_startMove_d, whose nop padding is baked in at assembly time for
 * VXT_SM_SCALE=12 and is correct at no other scale - and by this point the
 * scale is whatever the last line's search picked (up to 246). This is the
 * one place VOOM still uses the plain scale-12 path, and it is a zero-travel
 * record, so 12 costs nothing but correctness. */
#define VOOM_CLOSE_SCALE 12

static void voomRecenter(void) {
	voomSetScale(VOOM_CLOSE_SCALE);
	vxtSmartMove(0, 0);
	vxtSmartRecenter();
	voomBeamY = 0;
	voomBeamX = 0;
}

/* Blanked reposition to an absolute physical point. */
static void voomMoveTo(int32_t py, int32_t px) {
	int32_t dy = py - voomBeamY;
	int32_t dx = px - voomBeamX;
	if (dy == 0 && dx == 0) return;
	voomSetScale(VOOM_POS_SCALE);
	vxtSmartMoveBig(dy, dx, VOOM_POS_SCALE);
	voomBeamY += gamelibRoundToScale(dy, VOOM_POS_SCALE);
	voomBeamX += gamelibRoundToScale(dx, VOOM_POS_SCALE);
}

/* Lit draw to an absolute physical point, in ONE record wherever the
 * hardware can reach it. The scale search costs STM32 cycles and zero 6809
 * records - see gb_dispatch_huge()'s derivation in gamelib_beam.c. */
static void voomDrawTo(int32_t py, int32_t px) {
	int32_t dy = py - voomBeamY;
	int32_t dx = px - voomBeamX;
	int32_t maxAbs, s, need, bestS, bestErr;

	if (dy == 0 && dx == 0) return;

	maxAbs = voomAbs32(dy);
	if (voomAbs32(dx) > maxAbs) maxAbs = voomAbs32(dx);

	need = (maxAbs + VOOM_MAX_RATE - 1) / VOOM_MAX_RATE;
	if (need < 1) need = 1;
	if (need > VOOM_MAX_SCALE) need = VOOM_MAX_SCALE;   /* guard - see above */

	bestS = need;
	bestErr = -1;
	for (s = need; s <= need + VOOM_SCALE_SEARCH_SPAN && s <= VOOM_MAX_SCALE; s++) {
		int32_t ey = gamelibRoundToScale(dy, s) - dy;
		int32_t ex = gamelibRoundToScale(dx, s) - dx;
		int32_t err = voomAbs32(ey) + voomAbs32(ex);
		if (bestErr < 0 || err < bestErr) { bestErr = err; bestS = s; }
	}

	voomSetScale((int)bestS);
	vxtSmartDrawHuge(dy, dx, (uint8_t)bestS);
	voomBeamY += gamelibRoundToScale(dy, bestS);
	voomBeamX += gamelibRoundToScale(dx, bestS);
}

/* Was linesDraw() in the vxt_draw original: same nearest-neighbor walk,
 * kept verbatim - only the frame brackets and the emission calls changed.
 * The walk still tracks (x, y) in VOOM's raw units exactly as before; the
 * emission layer converts each resulting TARGET to absolute physical
 * coordinates, which is also why a quantized landing can never desync the
 * two. */
static void linesDraw(void) {
	Line2d *lines = voomArena()->lines;
	int i, j, k, d, dx, dy;
	int cd, ci, inv;
	int x=SIZEX/2,y=SIZEY/2;
	int nwr=0;

	vxtSmartBegin(VOOM_REGION_OFFSET, VOOM_REGION_RECORDS);
	voomCurScale = 0;
	voomBeamY = 0;
	voomBeamX = 0;
	vxtSmartIntensity(VOOM_INTENSITY);

	for (i=0; i<lineIdx; i++) {
		nwr++;
		//Find closest line
		cd=99999; ci=0; inv=0;
		for (j=0; j<lineIdx; j++) {
			if (lines[j].p[0].x!=-1) {
				for (k=0; k<2; k++) {
					dx=abs(lines[j].p[k].x-x);
					dy=abs(lines[j].p[k].y-y);
					if (dx>dy) d=dx; else d=dy;
					if (d<cd) {
						cd=d;
						ci=j;
						inv=k;
					}
				}
			}
		}

		dx=lines[ci].p[inv].x-x;
		dy=lines[ci].p[inv].y-y;
		if (dx>SIZEX/2 || dy>SIZEY/2 || nwr>3) {
			//Zero integrators.
			nwr=0;
			x=(SIZEX/2);
			y=(SIZEY/2);
			dx=lines[ci].p[inv].x-x;
			dy=lines[ci].p[inv].y-y;
		}
		if (dx!=0 || dy!=0) {
			if (nwr==0) voomRecenter();
			voomMoveTo(voomPhysY(y+dy), voomPhysX(x+dx));
			x+=dx;
			y+=dy;
		}

		dx=lines[ci].p[inv^1].x-x;
		dy=lines[ci].p[inv^1].y-y;
		voomDrawTo(voomPhysY(y+dy), voomPhysX(x+dx));
		x+=dx;
		y+=dy;

		//Poison line
		lines[ci].p[0].x=-1;
	}

	vxtSmartEnd();
}

/* One-shot. Cheap now that the lumps are in flash - no I/O at all, so
 * there is nothing here that could block the per-frame path. */
static void doInitVoom(void) {
	char *dptr[7];
	int chunkSz[7];
	VoomArena *a = voomArena();

	dptr[0]=(char *)voom_lump_vertexes;
	dptr[1]=(char *)voom_lump_linedefs;
	dptr[2]=(char *)voom_lump_sectors;
	dptr[3]=(char *)voom_lump_ssectors;
	dptr[4]=(char *)voom_lump_segs;
	dptr[5]=(char *)voom_lump_sidedefs;
	dptr[6]=(char *)voom_lump_nodes;

	chunkSz[0]=(int)(voom_lump_vertexes_end - voom_lump_vertexes);
	chunkSz[1]=(int)(voom_lump_linedefs_end - voom_lump_linedefs);
	chunkSz[2]=(int)(voom_lump_sectors_end  - voom_lump_sectors);
	chunkSz[3]=(int)(voom_lump_ssectors_end - voom_lump_ssectors);
	chunkSz[4]=(int)(voom_lump_segs_end     - voom_lump_segs);
	chunkSz[5]=(int)(voom_lump_sidedefs_end - voom_lump_sidedefs);
	chunkSz[6]=(int)(voom_lump_nodes_end    - voom_lump_nodes);

	/* Bind BEFORE voomInit()/voomDraw() - see voomBindArena() in VOOM.h. */
	voomBindArena(a->zbm, a->zbmx);
	voomInit(dptr, chunkSz);
	voomInited=1;
}

void voomSmartFrame(int keys) {
	if (!voomInited) doInitVoom();
	voomHandleKeys(keys);
	lineIdx=0;
	voomDraw();
	linesDraw();
}
