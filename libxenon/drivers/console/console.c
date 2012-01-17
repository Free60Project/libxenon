// Copyright 2009  Georg Lukas <georg@op-co.de>
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF 
// THE POSSIBILITY OF SUCH DAMAGE.
// 
// hacked to fit into libxenon by Felix Domke <tmbinc@elitedvb.net>
// This is a bit ugly, and it's not Georg's fault.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ppc/cache.h>
#include "font_8x16.h"
#include "console.h"
#include <xenos/xenos.h>
#include <ppc/atomic.h>

static int console_width, console_height,
    console_size;

/* Colors in BGRA: background and foreground */
static uint32_t console_color[2] = { 0x00000000, 0xFFA0A000 };

static unsigned char *console_fb = 0LL;

static int cursor_x, cursor_y, max_x, max_y, offset_x, offset_y;

struct ati_info {
	uint32_t unknown1[4];
	uint32_t base;
	uint32_t unknown2[8];
	uint32_t width;
	uint32_t height;
} __attribute__ ((__packed__)) ;


/* set a pixel to RGB values, must call console_init() first */
static inline void console_pset32(int x, int y, int color)
{
#define fbint ((uint32_t*)console_fb)
#define base (((y >> 5)*32*console_width + ((x >> 5)<<10) \
	   + (x&3) + ((y&1)<<2) + (((x&31)>>2)<<3) + (((y&31)>>1)<<6)) ^ ((y&8)<<2))
	fbint[base] = color;
#undef fbint
#undef base
}

inline void console_pset(int x, int y, unsigned char r, unsigned char g, unsigned char b) {
	console_pset32(x, y, (b<<24) + (g<<16) + (r<<8));
	/* little indian:
	 * fbint[base] = b + (g<<8) + (r<<16); */
}

static void console_draw_char(const int x, const int y, const unsigned char c) {
#define font_pixel(ch, x, y) ((fontdata_8x16[ch*16+y]>>(7-x))&1)
	int lx, ly;
	for (ly=0; ly < 16; ly++)
		for (lx=0; lx < 8; lx++) {
			console_pset32(x+lx, y+ly, console_color[font_pixel(c, lx, ly)]);
		}
}

void console_clrscr() {
	unsigned int *fb = (unsigned int*)console_fb;
	int count = console_width * console_height;
	while (count--)
		*fb++ = console_color[0];

	memdcbst(console_fb, console_size*4);

	cursor_x=0;
	cursor_y=0;
}

static void console_scroll32(const unsigned int lines) {
	int l, bs;
	bs = console_width*32*4;
	/* copy all tile blocks to a higher position */
	for (l=lines; l*32 < console_height; l++) {
		memcpy(console_fb + bs*(l-lines),
		       console_fb + bs*l,
		       bs);
	}

	/* fill up last lines with background color */
	uint32_t *fb = (uint32_t*)(console_fb + console_size*4 - bs*lines);
	uint32_t *end = (uint32_t*)(console_fb + console_size*4);
	while (fb != end)
		*fb++ = console_color[0];

	memdcbst(console_fb, console_size*4);
}

void console_newline() {
#if 0
	/* fill up with spaces */
	while (cursor_x*8 < console_width) {
		console_draw_char(cursor_x*8, cursor_y*16, ' ');
		cursor_x++;
	}
#endif

	/* reset to the left */
	cursor_x = 0;
	cursor_y++;
	if (cursor_y >= max_y) {
		/* XXX implement scrolling */
		console_scroll32(1);
		cursor_y -= 2;
	}
}

void console_putch(const char c) {
	if (!console_fb)
		return;
	if (c == '\r') {
		cursor_x = 0;
	} else if (c == '\n') {
		console_newline();
	} else {
		console_draw_char(cursor_x*8 + offset_x, cursor_y*16 + offset_y, c);
		cursor_x++;
		if (cursor_x >= max_x)
			console_newline();
	}
	memdcbst(console_fb, console_size*4);
}

static void console_stdout_hook(const char *buf, int len)
{
	while (len--)
		console_putch(*buf++);
}

extern void (*stdout_hook)(const char *buf, int len);

void console_init(void) {
	struct ati_info *ai = (struct ati_info*)0xec806100ULL;

	console_fb = (unsigned char*)(long)(ai->base | 0x80000000);
	/* round up size to tiles of 32x32 */
	console_width = ((ai->width+31)>>5)<<5;
	console_height = ((ai->height+31)>>5)<<5;
	console_size = console_width*console_height;

	offset_x = offset_y = 0;
	if (xenos_is_overscan())
	{
		offset_x = (ai->width/28); //50;
		offset_y = (ai->height/28); //50;
	}

	cursor_x = cursor_y = 0;
	max_x = (ai->width - offset_x * 2) / 8;
	max_y = (ai->height - offset_y * 2) / 16;
	
	console_clrscr();
	
	stdout_hook = console_stdout_hook;

	printf(" * Xenos FB with %dx%d (%dx%d) at %p initialized.\n",
		max_x, max_y, ai->width, ai->height, console_fb);
}

void console_set_colors(unsigned int background, unsigned int foreground){
	console_color[0]=background;
	console_color[1]=foreground;
}

void console_get_dimensions(unsigned int * width,unsigned int * height){
	if (width) *width=max_x;
	if (height) *height=max_y;
}

void console_close(void)
{
	stdout_hook = 0;
}
