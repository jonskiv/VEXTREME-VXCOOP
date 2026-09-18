
#ifndef VOOM_H
#define VOOM_H

#include <stdint.h>

#define SIZEX 256
#define SIZEY 256
#define FOV (M_PI/2)
#define FARPT 250
#define NEARPT 1
#define PLHEIGHT 34
#define PLMAXJUMP 24
#define PLWIDTH 17

#define VOOM_KEY_UP 1
#define VOOM_KEY_DOWN 2
#define VOOM_KEY_LEFT 4
#define VOOM_KEY_RIGHT 8

/* Doom level-data record layouts. Originally private to VOOM.c; moved here
 * because voom_smart.c's on-demand SD cache (see its header comment) now
 * needs to name these same three types (DoomNode/DoomLine/DoomSeg) to
 * declare its cache slots and fetch functions below. DoomSec/DoomSsec/
 * DoomSide stay private to VOOM.c - voom_smart.c never interprets their
 * contents, it just loads them resident as opaque bytes. */
typedef struct __attribute__((packed)) {
	int16_t x;
	int16_t y;
} DoomVertex;

typedef struct __attribute__((packed)) {
	int16_t vFrom;	//start vertex
	int16_t vTo;	//end vertex
	int16_t flags;
	int16_t types;
	int16_t tag;
	int16_t sdRight;	//right side sidedef index
	int16_t sdLeft;		//left side sidedef index
} DoomLine;

typedef struct __attribute__((packed)) {
	int16_t vStart;	//vertex start
	int16_t vEnd;	//vertex end
	int16_t angle;
	int16_t linedef; //linedef index
	int16_t dir;
	int16_t offset;
} DoomSeg;

typedef struct __attribute__((packed)) {
	int16_t yhi;
	int16_t ylo;
	int16_t xlo;
	int16_t xhi;
} DoomBbox;

typedef struct __attribute__((packed)) {
	DoomVertex lineStart;
	DoomVertex lineD;
	DoomBbox bbRight;
	DoomBbox bbLeft;
	int16_t rChild;
	int16_t lChild;
} DoomNode;

/* REMOVED: voomGetNode()/voomGetLine()/voomGetSegRun() and
 * VOOM_SEG_RUN_MAX. Those were the SD-streaming LRU-cache accessors; all
 * seven lumps now live in flash (game/voom_level_data.S) and VOOM.c indexes
 * them directly again, exactly as Sprite_tm's original does. See
 * voom_smart.c's header for the measurement that drove the change. */

/* VOOM's z-buffer (SIZEX*SIZEY/32 words = 8,192 bytes, plus a 256-byte
 * full-column shortcut row). Backed by caller-supplied memory rather than
 * bss, because this build does not have 8.4KB of bss to give: as plain
 * static arrays, VXT_ENABLE_VOOM=1 overflowed `ram` by 8 bytes and did not
 * link at all. voom_smart.c points these into the unserved part of the
 * running cart's own image (not menuData, which a held reset serves
 * again - see voom_smart.c).
 *
 * MUST be called before voomInit()/voomDraw() - the pointers are NULL until
 * it is, and nothing checks. */
void voomBindArena(unsigned int *zbmBuf, char *zbmxBuf);

void voomHandleKeys(int keys);
void voomInit(char** lvlData, int *chunkSz);
void voomDraw();



#endif
