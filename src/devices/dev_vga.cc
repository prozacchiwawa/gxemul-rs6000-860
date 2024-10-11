/*
 *  Copyright (C) 2004-2009  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *   
 *
 *  COMMENT: VGA framebuffer device (charcell and graphics modes)
 *
 *  It should work with 80x25 and 40x25 text modes, and with a few graphics
 *  modes as long as no fancy VGA features are used.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "bus_isa.h"

#include "vga.h"

/*  These are generated from binary font files:  */
#include "fonts/font8x8.cc"
#include "fonts/font8x10.cc"
#include "fonts/font8x16.cc"

#define VGA_DEBUG 0

#if VGA_DEBUG
#define L(x) x
#else
#define L(x) do { } while(0)
#endif

/*  For videomem -> framebuffer updates:  */
#define	VGA_TICK_SHIFT		18

#define	MAX_RETRACE_SCANLINES	420
#define	N_IS1_READ_THRESHOLD	50

#define	GFX_ADDR_WINDOW		(16 * 1024 * 1024)

#define	VGA_FB_ADDR	0x1c00000000ULL

#define	MODE_CHARCELL		1
#define	MODE_GRAPHICS		2

#define	GRAPHICS_MODE_8BIT	1
#define	GRAPHICS_MODE_4BIT	2

#define S3_PIO_SEQ_LEN 11

struct vga_data {
	uint64_t	videomem_base;
	uint64_t	control_base;

	struct vfb_data *fb;
	uint32_t	fb_size;

	int		fb_max_x;		/*  pixels  */
	int		fb_max_y;		/*  pixels  */
	int		max_x;			/*  charcells or pixels  */
	int		max_y;			/*  charcells or pixels  */

	/*  Selects charcell mode or graphics mode:  */
	int		cur_mode;

	/*  Common for text and graphics modes:  */
	int		pixel_repx, pixel_repy;

	/*  Textmode:  */
	int		font_width;
	int		font_height;
	unsigned char	*font;
	size_t		charcells_size;
	unsigned char	*charcells;		/*  2 bytes per char  */
	unsigned char	*charcells_outputed;	/*  text  */
	unsigned char	*charcells_drawn;	/*  framebuffer  */

	/*  Graphics:  */
	int		graphics_mode;
	int		bits_per_pixel;
	unsigned char	*gfx_mem;
	uint32_t	gfx_mem_size;

	/*  Registers:  */
	int		attribute_state;	/*  0 or 1  */
	unsigned char	attribute_reg_select;
	unsigned char	attribute_reg[256];

	unsigned char	misc_output_reg;

	unsigned char	sequencer_reg_select;
	unsigned char	sequencer_reg[256];

	unsigned char	graphcontr_reg_select;
	unsigned char	graphcontr_reg[256];

	unsigned char	crtc_reg_select;
	unsigned char	crtc_reg[256];

	unsigned char	palette_read_index;
	char		palette_read_subindex;
	unsigned char	palette_write_index;
	char		palette_write_subindex;

	int		current_retrace_line;
	int		input_status_1;

	/*  Palette per scanline during retrace:  */
	unsigned char	*retrace_palette;
	int		use_palette_per_line;
	int64_t		n_is1_reads;

	/*  Misc.:  */
	int		console_handle;

	int		cursor_x;
	int		cursor_y;

	int		modified;
	int		palette_modified;
	int		update_x1;
	int		update_y1;
	int		update_x2;
	int		update_y2;

	/* S3 */
  int   s3_pio_select;
	int   s3_cur_x, s3_cur_y, s3_pix_x, s3_pix_y, s3_draw_width;
  int   s3_fg_color, s3_bg_color;
  int   s3_fg_color_mix, s3_bg_color_mix;
  int   s3_v_dir;
  int   s3_rem_height;
  int   s3_destx, s3_desty;
  uint32_t s3_color_compare;

  /* BEE8H */
  uint16_t bee8_regs[16];

  /* Command */
  int s3_cmd_mx, s3_cmd_bus_size, s3_cmd_swap, s3_cmd_pxtrans;
};


inline uint16_t get_le_16(uint64_t idata) {
  return ((idata >> 8) & 0xff) | ((idata << 8) & 0xff00);
}


inline uint32_t get_le_32(uint64_t idata) {
  return get_le_16(idata >> 16) | (get_le_16(idata) << 16);
}


/*
 *  recalc_cursor_position():
 *
 *  Should be called whenever the cursor location _or_ the display
 *  base has been changed.
 */
static void recalc_cursor_position(struct vga_data *d)
{
	int base = (d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] << 8)
	    + d->crtc_reg[VGA_CRTC_START_ADDR_LOW];
	int ofs = d->crtc_reg[VGA_CRTC_CURSOR_LOCATION_HIGH] * 256 +
	    d->crtc_reg[VGA_CRTC_CURSOR_LOCATION_LOW];
	ofs -= base;
	d->cursor_x = ofs % d->max_x;
	d->cursor_y = ofs / d->max_x;
}


/*
 *  register_reset():
 *
 *  Resets many registers to sane values.
 */
static void register_reset(struct vga_data *d)
{
	/*  Home cursor and start at the top:  */
	d->crtc_reg[VGA_CRTC_CURSOR_LOCATION_HIGH] =
	    d->crtc_reg[VGA_CRTC_CURSOR_LOCATION_LOW] = 0;
	d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] =
	    d->crtc_reg[VGA_CRTC_START_ADDR_LOW] = 0;

	recalc_cursor_position(d);

	/*  Reset cursor scanline stuff:  */
	d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START] = d->font_height - 2;
	d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_END] = d->font_height - 1;

	d->sequencer_reg[VGA_SEQ_MAP_MASK] = 0x0f;
	d->graphcontr_reg[VGA_GRAPHCONTR_MASK] = 0xff;

	d->misc_output_reg = VGA_MISC_OUTPUT_IOAS;
	d->n_is1_reads = 0;
}


static void c_putstr(struct vga_data *d, const char *s)
{
	while (*s)
		console_putchar(d->console_handle, *s++);
}


/*
 *  reset_palette():
 */
static void reset_palette(struct vga_data *d, int grayscale)
{
	int i, r, g, b;

	/*  TODO: default values for entry 16..255?  */
	for (i=16; i<256; i++)
		d->fb->rgb_palette[i*3 + 0] = d->fb->rgb_palette[i*3 + 1] =
		    d->fb->rgb_palette[i*3 + 2] = (i & 15) * 4;
	d->palette_modified = 1;
	i = 0;

	if (grayscale) {
		for (r=0; r<2; r++)
		    for (g=0; g<2; g++)
			for (b=0; b<2; b++) {
				d->fb->rgb_palette[i + 0] =
				    d->fb->rgb_palette[i + 1] =
				    d->fb->rgb_palette[i + 2] =
				    (r+g+b) * 0xaa / 3;
				d->fb->rgb_palette[i + 8*3 + 0] =
				    d->fb->rgb_palette[i + 8*3 + 1] =
				    d->fb->rgb_palette[i + 8*3 + 2] =
				    (r+g+b) * 0xaa / 3 + 0x55;
				i+=3;
			}
		return;
	}

	for (r=0; r<2; r++)
		for (g=0; g<2; g++)
			for (b=0; b<2; b++) {
				d->fb->rgb_palette[i + 0] = r * 0xaa;
				d->fb->rgb_palette[i + 1] = g * 0xaa;
				d->fb->rgb_palette[i + 2] = b * 0xaa;
				i+=3;
			}
	for (r=0; r<2; r++)
		for (g=0; g<2; g++)
			for (b=0; b<2; b++) {
				d->fb->rgb_palette[i + 0] = r * 0xaa + 0x55;
				d->fb->rgb_palette[i + 1] = g * 0xaa + 0x55;
				d->fb->rgb_palette[i + 2] = b * 0xaa + 0x55;
				i+=3;
			}
}


/*
 *  vga_update_textmode():
 *
 *  Called from vga_update() when x11 in_use is false. This causes modified
 *  character cells to be "simulated" by outputing ANSI escape sequences
 *  that draw the characters in a terminal window instead.
 */
static void vga_update_textmode(struct machine *machine,
	struct vga_data *d, int base, int start, int end)
{
	char s[50];
	int i, oldcolor = -1, printed_last = 0;

	for (i=start; i<=end; i+=2) {
		unsigned char ch = d->charcells[base+i];
		int fg = d->charcells[base+i+1] & 15;
		int bg = (d->charcells[base+i+1] >> 4) & 15;
			/*  top bit of bg = blink  */
		int x = (i/2) % d->max_x;
		int y = (i/2) / d->max_x;

		if (d->charcells[base+i] == d->charcells_outputed[i] &&
		    d->charcells[base+i+1] == d->charcells_outputed[i+1]) {
			printed_last = 0;
			continue;
		}

		d->charcells_outputed[i] = d->charcells[base+i];
		d->charcells_outputed[i+1] = d->charcells[base+i+1];

		if (!printed_last || x == 0) {
			snprintf(s, sizeof(s), "\033[%i;%iH", y + 1, x + 1);
			c_putstr(d, s);
		}
		if (oldcolor < 0 || (bg<<4)+fg != oldcolor || !printed_last) {
			snprintf(s, sizeof(s), "\033[0;"); c_putstr(d, s);

			switch (fg & 7) {
			case 0:	c_putstr(d, "30"); break;
			case 1:	c_putstr(d, "34"); break;
			case 2:	c_putstr(d, "32"); break;
			case 3:	c_putstr(d, "36"); break;
			case 4:	c_putstr(d, "31"); break;
			case 5:	c_putstr(d, "35"); break;
			case 6:	c_putstr(d, "33"); break;
			case 7:	c_putstr(d, "37"); break;
			}
			if (fg & 8)
				c_putstr(d, ";1");
			c_putstr(d, ";");
			switch (bg & 7) {
			case 0:	c_putstr(d, "40"); break;
			case 1:	c_putstr(d, "44"); break;
			case 2:	c_putstr(d, "42"); break;
			case 3:	c_putstr(d, "46"); break;
			case 4:	c_putstr(d, "41"); break;
			case 5:	c_putstr(d, "45"); break;
			case 6:	c_putstr(d, "43"); break;
			case 7:	c_putstr(d, "47"); break;
			}
			/*  TODO: blink  */
			c_putstr(d, "m");
		}

		if (ch >= 0x20 && ch != 127)
			console_putchar(d->console_handle, ch);

		oldcolor = (bg << 4) + fg;
		printed_last = 1;
	}

	/*  Restore the terminal's cursor position:  */
	snprintf(s, sizeof(s), "\033[%i;%iH", d->cursor_y + 1, d->cursor_x + 1);
	c_putstr(d, s);
}


/*
 *  vga_update_graphics():
 *
 *  This function should be called whenever any part of d->gfx_mem[] has
 *  been written to. It will redraw all pixels within the range x1,y1
 *  .. x2,y2 using the right palette.
 */
static void vga_update_graphics(struct machine *machine, struct vga_data *d,
	int x1, int y1, int x2, int y2)
{
	int x, y, ix, iy, c, rx = d->pixel_repx, ry = d->pixel_repy;
	unsigned char pixel[3];

	for (y=y1; y<=y2; y++)
		for (x=x1; x<=x2; x++) {
			/*  addr is where to read from VGA memory, addr2 is
			    where to write on the 24-bit framebuffer device  */
			int addr = (y * d->max_x + x) * d->bits_per_pixel;
			switch (d->bits_per_pixel) {
			case 8:	addr >>= 3;
				c = d->gfx_mem[addr];
				pixel[0] = d->fb->rgb_palette[c*3+0];
				pixel[1] = d->fb->rgb_palette[c*3+1];
				pixel[2] = d->fb->rgb_palette[c*3+2];
				break;
			case 4:	addr >>= 2;
				if (addr & 1)
					c = d->gfx_mem[addr >> 1] >> 4;
				else
					c = d->gfx_mem[addr >> 1] & 0xf;
				pixel[0] = d->fb->rgb_palette[c*3+0];
				pixel[1] = d->fb->rgb_palette[c*3+1];
				pixel[2] = d->fb->rgb_palette[c*3+2];
				break;
			}
			for (iy=y*ry; iy<(y+1)*ry; iy++)
				for (ix=x*rx; ix<(x+1)*rx; ix++) {
					uint32_t addr2 = (d->fb_max_x * iy
					    + ix) * 3;
					if (addr2 < d->fb_size)
						dev_fb_access(machine->cpus[0],
						    machine->memory, addr2,
						    pixel, sizeof(pixel),
						    MEM_WRITE, d->fb);
				}
		}
}


/*
 *  vga_update_text():
 *
 *  This function should be called whenever any part of d->charcells[] has
 *  been written to. It will redraw all characters within the range x1,y1
 *  .. x2,y2 using the right palette.
 */
static void vga_update_text(struct machine *machine, struct vga_data *d,
	int x1, int y1, int x2, int y2)
{
	int fg, bg, x,y, subx, line;
	size_t i, start, end, base;
	int font_size = d->font_height;
	int font_width = d->font_width;
	unsigned char *pal = d->fb->rgb_palette;

	if (d->pixel_repx * font_width > 8*8) {
		fatal("[ too large font ]\n");
		return;
	}

	/*  Hm... I'm still using the old start..end code:  */
	start = (d->max_x * y1 + x1) * 2;
	end   = (d->max_x * y2 + x2) * 2;

	start &= ~1;
	end |= 1;

	if (end >= d->charcells_size)
		end = d->charcells_size - 1;

	base = ((d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] << 8)
	    + d->crtc_reg[VGA_CRTC_START_ADDR_LOW]) * 2;

	if (!machine->x11_md.in_use)
		vga_update_textmode(machine, d, base, start, end);

	for (i=start; i<=end; i+=2) {
		unsigned char ch = d->charcells[i + base];

		if (!d->palette_modified && d->charcells_drawn[i] == ch &&
		    d->charcells_drawn[i+1] == d->charcells[i+base+1])
			continue;

		d->charcells_drawn[i] = ch;
		d->charcells_drawn[i+1] = d->charcells[i + base + 1];

		fg = d->charcells[i+base + 1] & 15;
		bg = (d->charcells[i+base + 1] >> 4) & 7;

		/*  Blink is hard to do :-), but inversion might be ok too:  */
		if (d->charcells[i+base + 1] & 128) {
			int tmp = fg; fg = bg; bg = tmp;
		}

		x = (i/2) % d->max_x; x *= font_width;
		y = (i/2) / d->max_x; y *= font_size;

		/*  Draw the character:  */
		for (line = 0; line < font_size; line++) {
			/*  hardcoded for max 8 scaleup... :-)  */
			unsigned char rgb_line[3 * 8 * 8];
			int iy;

			for (subx = 0; subx < font_width; subx++) {
				int ix, color_index;

				if (d->use_palette_per_line) {
					int sline = d->pixel_repy * (line+y);
					if (sline < MAX_RETRACE_SCANLINES)
						pal = d->retrace_palette
						    + sline * 256*3;
					else
						pal = d->fb->rgb_palette;
				}

				if (d->font[ch * font_size + line] &
				    (128 >> subx))
					color_index = fg;
				else
					color_index = bg;

				for (ix=0; ix<d->pixel_repx; ix++)
					memcpy(rgb_line + (subx*d->pixel_repx +
					    ix) * 3, &pal[color_index * 3], 3);
			}

			for (iy=0; iy<d->pixel_repy; iy++) {
				uint32_t addr = (d->fb_max_x * (d->pixel_repy *
				    (line+y) + iy) + x * d->pixel_repx) * 3;
				if (addr >= d->fb_size)
					continue;
				dev_fb_access(machine->cpus[0],
				    machine->memory, addr, rgb_line,
				    3 * machine->x11_md.scaleup * font_width,
				    MEM_WRITE, d->fb);
			}
		}
	}
}


/*
 *  vga_update_cursor():
 */
static void vga_update_cursor(struct machine *machine, struct vga_data *d)
{
	int onoff = 1, height = d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_END]
	    - d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START] + 1;

	if (d->cur_mode != MODE_CHARCELL)
		onoff = 0;

	if (d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START] >
	    d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_END]) {
		onoff = 0;
		height = 1;
	}

	if (d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START] >= d->font_height)
		onoff = 0;

	dev_fb_setcursor(d->fb,
	    d->cursor_x * d->font_width * d->pixel_repx, (d->cursor_y *
	    d->font_height + d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START]) *
	    d->pixel_repy, onoff, d->font_width * d->pixel_repx, height *
	    d->pixel_repy);
}


DEVICE_TICK(vga)
{
	struct vga_data *d = (struct vga_data *) extra;
	int64_t low = -1, high;

	vga_update_cursor(cpu->machine, d);

	/*  TODO: text vs graphics tick?  */
	memory_device_dyntrans_access(cpu, cpu->mem, extra,
	    (uint64_t *) &low, (uint64_t *) &high);

	if (low != -1) {
		int base = ((d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] << 8)
		    + d->crtc_reg[VGA_CRTC_START_ADDR_LOW]) * 2;
		int new_u_y1, new_u_y2;
		debug("[ dev_vga_tick: dyntrans access, %" PRIx64" .. %"
		    PRIx64" ]\n", (uint64_t) low, (uint64_t) high);
		low -= base;
		high -= base;
		d->update_x1 = 0;
		d->update_x2 = d->max_x - 1;
		new_u_y1 = (low/2) / d->max_x;
		new_u_y2 = ((high/2) / d->max_x) + 1;
		if (new_u_y1 < d->update_y1)
			d->update_y1 = new_u_y1;
		if (new_u_y2 > d->update_y2)
			d->update_y2 = new_u_y2;
		if (d->update_y1 < 0)
			d->update_y1 = 0;
		if (d->update_y2 >= d->max_y)
			d->update_y2 = d->max_y - 1;
		d->modified = 1;
	}

	if (d->n_is1_reads > N_IS1_READ_THRESHOLD &&
	    d->retrace_palette != NULL) {
		d->use_palette_per_line = 1;
		d->update_x1 = 0;
		d->update_x2 = d->max_x - 1;
		d->update_y1 = 0;
		d->update_y2 = d->max_y - 1;
		d->modified = 1;
	} else {
		if (d->use_palette_per_line) {
			d->use_palette_per_line = 0;
			d->update_x1 = 0;
			d->update_x2 = d->max_x - 1;
			d->update_y1 = 0;
			d->update_y2 = d->max_y - 1;
			d->modified = 1;
		}
	}

	if (!cpu->machine->x11_md.in_use) {
		/*  NOTE: 2 > 0, so this only updates the cursor, no
		    character cells.  */
		vga_update_textmode(cpu->machine, d, 0, 2, 0);
	}

	if (d->modified) {
		if (d->cur_mode == MODE_CHARCELL)
			vga_update_text(cpu->machine, d, d->update_x1,
			    d->update_y1, d->update_x2, d->update_y2);
		else
			vga_update_graphics(cpu->machine, d, d->update_x1,
                          d->update_y1, d->update_x2, d->update_y2);

		d->palette_modified = 0;
		d->modified = 0;
		d->update_x1 = 999999;
		d->update_x2 = -1;
		d->update_y1 = 999999;
		d->update_y2 = -1;
	}

	if (d->n_is1_reads > N_IS1_READ_THRESHOLD)
		d->n_is1_reads = 0;
}


/*
 *  Reads and writes to the VGA video memory (pixels).
 */
DEVICE_ACCESS(vga_graphics)
{
	struct vga_data *d = (struct vga_data *) extra;
	int j, x=0, y=0, x2=0, y2=0, modified = 0;
	size_t i;

	L(fprintf(stderr, "VGA: mode %d gfx %d relative_addr %08" PRIx64" len %08" PRIx64"\n", d->cur_mode, d->graphics_mode, relative_addr, len));

	if (relative_addr + len >= d->gfx_mem_size)
		return 0;

	if (d->cur_mode != MODE_GRAPHICS)
		return 1;

	switch (d->graphics_mode) {
	case GRAPHICS_MODE_8BIT:
		y = relative_addr / d->max_x;
		x = relative_addr % d->max_x;
		y2 = (relative_addr+len-1) / d->max_x;
		x2 = (relative_addr+len-1) % d->max_x;

		if (writeflag == MEM_WRITE) {
			memcpy(d->gfx_mem + relative_addr, data, len);
			modified = 1;
		} else
			memcpy(data, d->gfx_mem + relative_addr, len);
		break;
	case GRAPHICS_MODE_4BIT:
		y = relative_addr * 8 / d->max_x;
		x = relative_addr * 8 % d->max_x;
		y2 = ((relative_addr+len)*8-1) / d->max_x;
		x2 = ((relative_addr+len)*8-1) % d->max_x;
		/*  TODO: color stuff  */

		/*  Read/write d->gfx_mem in 4-bit color:  */
		if (writeflag == MEM_WRITE) {
			/*  i is byte index to write, j is bit index  */
			for (i=0; i<len; i++)
				for (j=0; j<8; j++) {
					int pixelmask = 1 << (7-j);
					int b = data[i] & pixelmask;
					int m = d->sequencer_reg[
					    VGA_SEQ_MAP_MASK] & 0x0f;
					uint32_t addr = (y * d->max_x + x +
					    i*8 + j) * d->bits_per_pixel / 8;
					unsigned char byte;
					if (!(d->graphcontr_reg[
					    VGA_GRAPHCONTR_MASK] & pixelmask))
						continue;
					if (addr >= d->gfx_mem_size)
						continue;
					byte = d->gfx_mem[addr];
					if (b && j&1)
						byte |= m << 4;
					if (b && !(j&1))
						byte |= m;
					if (!b && j&1)
						byte &= ~(m << 4);
					if (!b && !(j&1))
						byte &= ~m;
					d->gfx_mem[addr] = byte;
				}
			modified = 1;
		} else {
			fatal("TODO: 4 bit graphics read, mask=0x%02x\n",
			    d->sequencer_reg[VGA_SEQ_MAP_MASK]);
			for (i=0; i<len; i++)
				data[i] = random();
		}
		break;
	default:fatal("dev_vga: Unimplemented graphics mode %i\n",
		    d->graphics_mode);
		cpu->running = 0;
	}

	if (modified) {
		d->modified = 1;
		if (x < d->update_x1)  d->update_x1 = x;
		if (x > d->update_x2)  d->update_x2 = x;
		if (y < d->update_y1)  d->update_y1 = y;
		if (y > d->update_y2)  d->update_y2 = y;
		if (x2 < d->update_x1)  d->update_x1 = x2;
		if (x2 > d->update_x2)  d->update_x2 = x2;
		if (y2 < d->update_y1)  d->update_y1 = y2;
		if (y2 > d->update_y2)  d->update_y2 = y2;
		if (y != y2) {
			d->update_x1 = 0;
			d->update_x2 = d->max_x - 1;
		}
	}
	return 1;
}


/*
 *  Reads and writes the VGA video memory (charcells).
 */
DEVICE_ACCESS(vga)
{
	struct vga_data *d = (struct vga_data *) extra;
	uint64_t idata = 0, odata = 0;
	int x, y, x2, y2, r, base;
	size_t i;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	base = ((d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] << 8)
	    + d->crtc_reg[VGA_CRTC_START_ADDR_LOW]) * 2;
	r = relative_addr - base;
	y = r / (d->max_x * 2);
	x = (r/2) % d->max_x;
	y2 = (r+len-1) / (d->max_x * 2);
	x2 = ((r+len-1)/2) % d->max_x;

	if (relative_addr + len - 1 < d->charcells_size) {
		if (writeflag == MEM_WRITE) {
			for (i=0; i<len; i++) {
				int old = d->charcells[relative_addr + i];
				if (old != data[i]) {
					d->charcells[relative_addr + i] =
					    data[i];
					d->modified = 1;
				}
			}

			if (d->modified) {
				if (x < d->update_x1)  d->update_x1 = x;
				if (x > d->update_x2)  d->update_x2 = x;
				if (y < d->update_y1)  d->update_y1 = y;
				if (y > d->update_y2)  d->update_y2 = y;
				if (x2 < d->update_x1)  d->update_x1 = x2;
				if (x2 > d->update_x2)  d->update_x2 = x2;
				if (y2 < d->update_y1)  d->update_y1 = y2;
				if (y2 > d->update_y2)  d->update_y2 = y2;

				if (y != y2) {
					d->update_x1 = 0;
					d->update_x2 = d->max_x - 1;
				}
			}
		} else
			memcpy(data, d->charcells + relative_addr, len);
		return 1;
	}

	switch (relative_addr) {
	default:
		if (writeflag==MEM_READ) {
			debug("[ vga: read from 0x%08lx ]\n",
			    (long)relative_addr);
		} else {
			debug("[ vga: write to  0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


void vga_hack_start(struct vga_data *d) {
  if (d->cur_mode != MODE_GRAPHICS) {
    d->cur_mode = MODE_GRAPHICS;
    d->max_x = 800; d->max_y = 600;
    d->graphics_mode = GRAPHICS_MODE_8BIT;
    d->bits_per_pixel = 8;
    d->pixel_repx = d->pixel_repy = 1;

    d->gfx_mem_size = 2 * 1024 * 1024; /*d->max_x * d->max_y /
      (d->graphics_mode == GRAPHICS_MODE_8BIT? 1 : 2);*/

    CHECK_ALLOCATION(d->gfx_mem = (unsigned char *) malloc(d->gfx_mem_size));

    /*  Clear screen and reset the palette:  */
    memset(d->charcells_outputed, 0, d->charcells_size);
    memset(d->charcells_drawn, 0, d->charcells_size);
    memset(d->gfx_mem, 0, d->gfx_mem_size);

    reset_palette(d, 0);
    register_reset(d);

    d->bee8_regs[3] = 600;
    d->bee8_regs[4] = 800;
    d->s3_fg_color_mix = 0x27;
  }
}


void pixel_transfer(cpu *cpu, struct vga_data *d, int fg_color_mix, uint8_t *pixel_p, int write_len) {
  uint64_t target = 0;
  int do_color_compare = d->bee8_regs[0xe] & 0x100;
  int copy_if_equal = !!(d->bee8_regs[0xe] & 0x80);
  int prev_pixel;
  int nowrite;
  uint8_t pixel;
  int pix;

  /* Compute write target */
  d->update_x1 = d->s3_pix_x;
  d->update_x2 = d->s3_pix_x + 1;
  d->update_y1 = d->s3_pix_y;
  d->update_y2 = d->s3_pix_y + 1;
  d->modified = 1;

  if (d->s3_rem_height == 0) {
    L(fprintf(stderr, "[ s3: out of copy height (%d) ]\n", d->bee8_regs[0]));
    return;
  }

  if (d->s3_pix_x == d->s3_cur_x + d->s3_draw_width - 1) {
    write_len = 1;
  }

  for (pix = 0; pix < write_len; pix++) {
    target = (d->s3_pix_y * 800) + d->s3_pix_x;

    nowrite =
      ((d->s3_pix_y < d->bee8_regs[1]) &&
       (d->s3_pix_y > d->bee8_regs[3]) &&
       (d->s3_pix_x < d->bee8_regs[2]) &&
       (d->s3_pix_x > d->bee8_regs[4])) ||
      (target >= d->gfx_mem_size);

    d->s3_pix_x++;

    switch (fg_color_mix) {
    case 0x27:
      // Use color register
      pixel = d->s3_fg_color;
      break;

    case 0x47:
      // Use display memory
      prev_pixel = d->gfx_mem[target];
      if (do_color_compare && ((pixel_p[pix] == d->s3_color_compare) != copy_if_equal)) {
        pixel = prev_pixel;
      } else {
        pixel = pixel_p[pix];
      }
      break;

    case 0x67:
      // Use pixel
      pixel = pixel_p[pix];
      d->update_x2++;
      break;

    default:
      L(fprintf(stderr, "[ vga copy pixel: unknown mix type %02x ]\n", d->s3_fg_color_mix));
      break;
    }

    if (!nowrite) {
      d->gfx_mem[target] = pixel;
    }
    L(fprintf(stderr, "[ vga write (%d,%d) %08" PRIx64 " = %04x clip %d,%d,%d,%d h %d rem %d mix %04x ]\n", d->s3_pix_x, d->s3_pix_y, target, pixel, d->bee8_regs[1], d->bee8_regs[2], d->bee8_regs[3], d->bee8_regs[4], d->bee8_regs[0], d->s3_rem_height, d->s3_fg_color_mix));
  }

  if (d->s3_pix_x >= d->s3_cur_x + d->s3_draw_width) {
    d->s3_pix_x = d->s3_cur_x;
    d->s3_pix_y += d->s3_v_dir;
    d->s3_rem_height -= 1;
  }

  vga_update_graphics(cpu->machine, d, d->update_x1,
                      d->update_y1, d->update_x2, d->update_y2);
}


void bitblt(cpu *cpu, struct vga_data *d) {
  int rectangle_height = d->bee8_regs[0];
  int clipping_top = d->bee8_regs[1];
  int clipping_left = d->bee8_regs[2];
  int clipping_bottom = d->bee8_regs[3];
  int clipping_right = d->bee8_regs[4];
  int width_of_copy = std::min(clipping_right - clipping_left, clipping_right - d->s3_destx);
  int rows = std::min(clipping_bottom - clipping_top, d->s3_rem_height);
  int target_row = d->s3_desty;

  L(fprintf(stderr, "[ s3: BITBLT: R(%d,%d,%d,%d) SRC (%d,%d) ]\n", d->s3_destx, d->s3_desty, clipping_right, clipping_top + rows, d->s3_cur_x, d->s3_cur_y));

  for (; rows > 0; d->s3_cur_y++, rows--, target_row++) {
    uint8_t *source = &d->gfx_mem[d->s3_cur_y * 800 + d->s3_cur_x];
    uint8_t *target = &d->gfx_mem[target_row * 800 + d->s3_destx];
    memmove(target, source, width_of_copy);
  }
  vga_update_graphics
    (cpu->machine, d,
     clipping_left, clipping_top, clipping_right, clipping_bottom
     );
}


void fillrect(cpu *cpu, struct vga_data *d, uint16_t command) {
  int leftmost_allowed = d->bee8_regs[0];
  uint8_t transfer_color[2];
  int color_source = (d->s3_fg_color_mix >> 5) & 3;
  int mix_op = d->s3_fg_color_mix & 0xf;
  int lines_written = 0;

  switch (color_source) {
  case 1:
    memset(transfer_color, d->s3_fg_color, sizeof(transfer_color));
    break;

  default:
    L(fprintf(stderr, "[ s3: unknown color source %d ]\n", color_source));
    break;
  }

  if (command & 0x10) {
    // Actually draw
    d->s3_v_dir = 1;
    while (d->s3_rem_height > 0) {
      pixel_transfer(cpu, d, d->s3_fg_color_mix, transfer_color, sizeof(transfer_color));
    }
  }
}


DEVICE_ACCESS(vga_s3_control) { // 9ae8, CMD
	struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;
  bool draws_up;

  if (writeflag != MEM_WRITE) {
    memory_writemax64(cpu, data, len, 0);
  } else {
		written = get_le_16(memory_readmax64(cpu, data, len));
    L(fprintf(stderr, "[ s3: command = %d (raw %04x) ]\n", (int)written, (int)written));
    d->s3_cmd_mx = !!(written & 2);
    d->s3_cmd_pxtrans = !!(written & 0x100);
    draws_up = written & (1 << 7) && d->s3_cmd_pxtrans;
    d->s3_pix_x = d->s3_cur_x;
    d->s3_pix_y = d->s3_cur_y;
    d->s3_v_dir = draws_up ? 1 : -1;
    d->s3_cmd_bus_size = (written >> 9) & 3;
    d->s3_cmd_swap = (written >> 12) & 1;

    switch (written >> 13) {
    case 0: // Nop
      break;

    case 2: // Rectangle Fill
      if (!(written & 0x100)) {
        L(fprintf(stderr, "[ s3 fillrect ]\n"));
        fillrect(cpu, d, written & 0x1fff);
      } // Otherwise accept pixel fill below.
      break;

    case 6: // BitBlt
      bitblt(cpu, d);
      break;

    default:
      L(fprintf(stderr, "[ s3: unimplemented cmd %d (%04x) ]\n", written >> 13, written));
    }
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_curx) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  if (writeflag == MEM_WRITE) {
    written = get_le_16(memory_readmax64(cpu, data, len));
    d->s3_cur_x = written;
    L(fprintf(stderr, "[ s3: set pix x = %d (raw %04x) ]\n", (int)written, (int)written));
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_cury) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  if (writeflag == MEM_WRITE) {
    written = get_le_16(memory_readmax64(cpu, data, len));
    d->s3_cur_y = written;
    L(fprintf(stderr, "[ s3: set pix y = %d (raw %04x) ]\n", (int)written, (int)written));
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_bg_color) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  if (writeflag == MEM_WRITE) {
    d->s3_bg_color = get_le_16(memory_readmax64(cpu, data, len));
    L(fprintf(stderr, "[ s3: set bg color = %d (raw %04x) ]\n", (int)d->s3_bg_color, (int)written));
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_fg_color) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  if (writeflag == MEM_WRITE) {
    d->s3_fg_color = get_le_16(memory_readmax64(cpu, data, len));
    L(fprintf(stderr, "[ s3: set fg color = %d (raw %04x) ]\n", (int)d->s3_fg_color, (int)written));
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_fg_color_mix) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  if (writeflag == MEM_WRITE) {
    d->s3_fg_color_mix = get_le_16(memory_readmax64(cpu, data, len));
    L(fprintf(stderr, "[ s3: set fg color mix = %d (raw %04x) ]\n", (int)d->s3_fg_color_mix, (int)written));
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_destx) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  if (writeflag == MEM_WRITE) {
    d->s3_destx = get_le_16(memory_readmax64(cpu, data, len));
    L(fprintf(stderr, "[ s3: set destx = %d (raw %04x) ]\n", (int)d->s3_fg_color_mix, (int)written));
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_desty) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  if (writeflag == MEM_WRITE) {
    d->s3_desty = get_le_16(memory_readmax64(cpu, data, len));
    L(fprintf(stderr, "[ s3: set desty = %d (raw %04x) ]\n", (int)d->s3_fg_color_mix, (int)written));
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_major_axis_len) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  if (writeflag == MEM_WRITE) {
    written = get_le_16(memory_readmax64(cpu, data, len));
    d->s3_draw_width = written + 1;
    L(fprintf(stderr, "[ s3: set draw width %d (raw %04x) ]\n", d->s3_draw_width, (int)written));
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_color_compare) {
  struct vga_data *d = (struct vga_data *) extra;
  int write_index;

  if (writeflag == MEM_WRITE) {
    d->s3_color_compare = get_le_32(memory_readmax64(cpu, data, len));
    L(fprintf(stderr, "[ s3: set color compare %08x ]\n", (int)d->s3_color_compare));
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_pio_cmd) {
  struct vga_data *d = (struct vga_data *) extra;
  uint64_t idata = 0;
  uint16_t written;
  int write_index;

  if (writeflag == MEM_WRITE) {
    idata = memory_readmax64(cpu, data, len);
    written = ((idata >> 8) & 0xff) | ((idata << 8) & 0xff00);
    write_index = written >> 12;
    d->bee8_regs[write_index] = written & 0xfff;

    L(fprintf(stderr, "[ s3: bee8 write reg %x = %03x ]\n", write_index, (int)(written & 0xfff)));

    switch (write_index) {
    case 0:
      d->s3_rem_height = d->bee8_regs[0] + 1;
      break;

    case 13:
      if ((written >> 8) & 15) { // apply command?
        uint8_t command = written & 0xff;
        if (command == 0x31) {
        }
      }
      break;
    }

  }

  return 1;
}


DEVICE_ACCESS(vga_s3_pix_transfer) {
  struct vga_data *d = (struct vga_data *) extra;
  uint64_t idata;
  uint8_t to_write[4];

  if (writeflag == MEM_WRITE) {
    idata = memory_readmax64(cpu, data, len);
    if (d->s3_cmd_swap) {
      to_write[1] = idata;
      to_write[0] = idata >> 8;
    } else {
      to_write[0] = idata;
      to_write[1] = idata >> 8;
    }

    int dt_ext_src = (d->bee8_regs[0xa] >> 6) & 3;

    if (d->s3_cmd_mx && dt_ext_src == 2) {
      // Transfer across the plane, 1bpp
      uint8_t pixels[32];
      int pxcount = 8 + (8 * d->s3_cmd_bus_size);
      for (int i = 0; i < pxcount; i++) {
        // XXX Implement dst and src mix etc.
        bool pix = 1 & (to_write[i / 8] >> ((7 - i) % 8));
        pixels[i] = pix ? d->s3_fg_color : d->s3_bg_color;
      }
      int old_fg_color_mix = d->s3_fg_color_mix;
      pixel_transfer(cpu, d, 0x67, pixels, pxcount);
    } else {
      pixel_transfer(cpu, d, d->s3_fg_color_mix, to_write, 1 + d->s3_cmd_bus_size);
    }
  }

  return 1;
}


/*
 *  vga_crtc_reg_write():
 *
 *  Writes to VGA CRTC registers.
 */
static void vga_crtc_reg_write(struct machine *machine, struct vga_data *d,
	int regnr, int idata)
{
	int i, grayscale;

	switch (regnr) {
	case VGA_CRTC_CURSOR_SCANLINE_START:		/*  0x0a  */
	case VGA_CRTC_CURSOR_SCANLINE_END:		/*  0x0b  */
		break;
	case VGA_CRTC_START_ADDR_HIGH:			/*  0x0c  */
	case VGA_CRTC_START_ADDR_LOW:			/*  0x0d  */
		d->update_x1 = 0;
		d->update_x2 = d->max_x - 1;
		d->update_y1 = 0;
		d->update_y2 = d->max_y - 1;
		d->modified = 1;
		recalc_cursor_position(d);
		break;
	case VGA_CRTC_CURSOR_LOCATION_HIGH:		/*  0x0e  */
	case VGA_CRTC_CURSOR_LOCATION_LOW:		/*  0x0f  */
		recalc_cursor_position(d);
		break;
	case 0xff:
		grayscale = 0;
		switch (d->crtc_reg[0xff]) {
		case 0x00:
			grayscale = 1;
		case 0x01:
			d->cur_mode = MODE_CHARCELL;
			d->max_x = 40; d->max_y = 25;
			d->pixel_repx = machine->x11_md.scaleup * 2;
			d->pixel_repy = machine->x11_md.scaleup;
			d->font_width = 8;
			d->font_height = 16;
			d->font = font8x16;
			break;
		case 0x02:
			grayscale = 1;
		case 0x03:
			d->cur_mode = MODE_CHARCELL;
			d->max_x = 80; d->max_y = 25;
			d->pixel_repx = d->pixel_repy = machine->x11_md.scaleup;
			d->font_width = 8;
			d->font_height = 16;
			d->font = font8x16;
			break;
		case 0x08:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 160;	d->max_y = 200;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = 4 * machine->x11_md.scaleup;
			d->pixel_repy = 2 * machine->x11_md.scaleup;
			break;
		case 0x09:
		case 0x0d:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 320;	d->max_y = 200;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = d->pixel_repy =
			    2 * machine->x11_md.scaleup;
			break;
		case 0x0e:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 640;	d->max_y = 200;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = machine->x11_md.scaleup;
			d->pixel_repy = machine->x11_md.scaleup * 2;
			break;
		case 0x10:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 640; d->max_y = 350;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = d->pixel_repy = machine->x11_md.scaleup;
			break;
		case 0x12:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 640; d->max_y = 480;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = d->pixel_repy = machine->x11_md.scaleup;
			break;
		case 0x13:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 320;	d->max_y = 200;
			d->graphics_mode = GRAPHICS_MODE_8BIT;
			d->bits_per_pixel = 8;
			d->pixel_repx = d->pixel_repy =
			    2 * machine->x11_md.scaleup;
			break;
		default:
			fatal("TODO! video mode change hack (mode 0x%02x)\n",
			    d->crtc_reg[0xff]);
			exit(1);
		}

		if (d->cur_mode == MODE_CHARCELL) {
			dev_fb_resize(d->fb, d->max_x * d->font_width *
			    d->pixel_repx, d->max_y * d->font_height *
			    d->pixel_repy);
			d->fb_size = d->max_x * d->pixel_repx * d->font_width *
			     d->max_y * d->pixel_repy * d->font_height * 3;
		} else {
			dev_fb_resize(d->fb, d->max_x * d->pixel_repx,
			    d->max_y * d->pixel_repy);
			d->fb_size = d->max_x * d->pixel_repx *
			     d->max_y * d->pixel_repy * 3;
		}

		for (i=0; i<machine->ncpus; i++)
			machine->cpus[i]->invalidate_translation_caches(
			    machine->cpus[i], 0, INVALIDATE_ALL);

		if (d->gfx_mem != NULL)
			free(d->gfx_mem);
		d->gfx_mem_size = 1;
		if (d->cur_mode == MODE_GRAPHICS)
			d->gfx_mem_size = d->max_x * d->max_y /
			    (d->graphics_mode == GRAPHICS_MODE_8BIT? 1 : 2);

		CHECK_ALLOCATION(d->gfx_mem = (unsigned char *) malloc(d->gfx_mem_size));

		/*  Clear screen and reset the palette:  */
		memset(d->charcells_outputed, 0, d->charcells_size);
		memset(d->charcells_drawn, 0, d->charcells_size);
		memset(d->gfx_mem, 0, d->gfx_mem_size);
		d->update_x1 = 0;
		d->update_x2 = d->max_x - 1;
		d->update_y1 = 0;
		d->update_y2 = d->max_y - 1;
		d->modified = 1;
		reset_palette(d, grayscale);
		register_reset(d);
		break;
	default:fatal("[ vga_crtc_reg_write: regnr=0x%02x idata=0x%02x ]\n",
		    regnr, idata);
	}
}


/*
 *  vga_sequencer_reg_write():
 *
 *  Writes to VGA Sequencer registers.
 */
static void vga_sequencer_reg_write(struct machine *machine, struct vga_data *d,
	int regnr, int idata)
{
	switch (regnr) {
	case VGA_SEQ_RESET:
	case VGA_SEQ_MAP_MASK:
	case VGA_SEQ_SEQUENCER_MEMORY_MODE:
		debug("[ vga_sequencer_reg_write: select %i: TODO ]\n", regnr);
		break;
	default:fatal("[ vga_sequencer_reg_write: select %i ]\n", regnr);
		/*  cpu->running = 0;  */
	}
}


/*
 *  vga_graphcontr_reg_write():
 *
 *  Writes to VGA Graphics Controller registers.
 */
static void vga_graphcontr_reg_write(struct machine *machine,
	struct vga_data *d, int regnr, int idata)
{
	switch (regnr) {
	case VGA_GRAPHCONTR_READMAPSELECT:
	case VGA_GRAPHCONTR_GRAPHICSMODE:
	case VGA_GRAPHCONTR_MISC:
	case VGA_GRAPHCONTR_MASK:
		debug("[ vga_graphcontr_reg_write: select %i: TODO ]\n", regnr);
		break;
	default:fatal("[ vga_graphcontr_reg_write: select %i ]\n", regnr);
		/*  cpu->running = 0;  */
	}
}


/*
 *  vga_attribute_reg_write():
 *
 *  Writes to VGA Attribute registers.
 */
static void vga_attribute_reg_write(struct machine *machine, struct vga_data *d,
	int regnr, int idata)
{
	/*  0-15 are palette registers: TODO  */
	if (regnr >= 0 && regnr <= 0xf)
		return;

	switch (regnr) {
	default:fatal("[ vga_attribute_reg_write: select %i ]\n", regnr);
		/*  cpu->running = 0;  */
	}
}


/*
 *  dev_vga_ctrl_access():
 *
 *  Reads and writes of the VGA control registers.
 */
DEVICE_ACCESS(vga_ctrl)
{
	struct vga_data *d = (struct vga_data *) extra;
	size_t i;
	uint64_t idata = 0, odata = 0;

  fprintf(stderr, "vga ctr at %08x\n", (unsigned int)cpu->pc);

	for (i=0; i<len; i++) {
		idata = data[i];

		/*  0x3C0 + relative_addr...  */

		switch (relative_addr) {

		case VGA_ATTRIBUTE_ADDR:		/*  0x00  */
			switch (d->attribute_state) {
			case 0:	if (writeflag == MEM_READ)
					odata = d->attribute_reg_select;
				else {
					d->attribute_reg_select = 1;
					d->attribute_state = 1;
				}
				break;
			case 1:	d->attribute_state = 0;
				d->attribute_reg[d->attribute_reg_select] =
				    idata;
				vga_attribute_reg_write(cpu->machine, d,
				    d->attribute_reg_select, idata);
				break;
			}
			break;
		case VGA_ATTRIBUTE_DATA_READ:		/*  0x01  */
			if (writeflag == MEM_WRITE)
				fatal("[ dev_vga: WARNING: Write to "
				    "VGA_ATTRIBUTE_DATA_READ? ]\n");
			else {
				if (d->attribute_state == 0)
					fatal("[ dev_vga: WARNING: Read from "
					    "VGA_ATTRIBUTE_DATA_READ, but no"
					    " register selected? ]\n");
				else
					odata = d->attribute_reg[
					    d->attribute_reg_select];
			}
			break;

		case VGA_MISC_OUTPUT_W:			/*  0x02  */
			if (writeflag == MEM_WRITE)
				d->misc_output_reg = idata;
			else {
				/*  Reads: Input Status 0  */
				odata = 0x00;
			}
			break;

		case VGA_SEQUENCER_ADDR:		/*  0x04  */
			if (writeflag == MEM_READ)
				odata = d->sequencer_reg_select;
			else
				d->sequencer_reg_select = idata;
			break;
		case VGA_SEQUENCER_DATA:		/*  0x05  */
			if (writeflag == MEM_READ)
				odata = d->sequencer_reg[
				    d->sequencer_reg_select];
			else {
				d->sequencer_reg[d->
				    sequencer_reg_select] = idata;
				vga_sequencer_reg_write(cpu->machine, d,
				    d->sequencer_reg_select, idata);
			}
			break;

		case VGA_DAC_ADDR_READ:			/*  0x07  */
			if (writeflag == MEM_WRITE) {
				d->palette_read_index = idata;
				d->palette_read_subindex = 0;
			} else {
				debug("[ dev_vga: WARNING: Read from "
				    "VGA_DAC_ADDR_READ? TODO ]\n");
				/*  TODO  */
			}
			break;
		case VGA_DAC_ADDR_WRITE:		/*  0x08  */
			if (writeflag == MEM_WRITE) {
				d->palette_write_index = idata;
				d->palette_write_subindex = 0;

				d->palette_read_index = idata;
				d->palette_read_subindex = 0;
			} else {
				fatal("[ dev_vga: WARNING: Read from "
				    "VGA_DAC_ADDR_WRITE? ]\n");
				odata = d->palette_write_index;
			}
			break;
		case VGA_DAC_DATA:			/*  0x09  */
			if (writeflag == MEM_WRITE) {
        L(fprintf(stderr, "[ vga: writing dac data (index %d sub %d) ]\n", d->palette_write_index, d->palette_write_subindex));
				int new_ = (idata & 63) << 2;
				int old = d->fb->rgb_palette[d->
				    palette_write_index*3+d->
				    palette_write_subindex];
				d->fb->rgb_palette[d->palette_write_index * 3 +
				    d->palette_write_subindex] = new_;
				/*  Redraw whole screen, if the
				    palette changed:  */
				if (new_ != old) {
					d->modified = 1;
					d->palette_modified = 1;
					d->update_x1 = d->update_y1 = 0;
					d->update_x2 = d->max_x - 1;
					d->update_y2 = d->max_y - 1;
				}
				d->palette_write_subindex ++;
				if (d->palette_write_subindex == 3) {
					d->palette_write_index ++;
					d->palette_write_subindex = 0;
				}
        if (d->palette_write_index > 255) {
          d->palette_write_index = 0;
        }
			} else {
				odata = (d->fb->rgb_palette[d->
				    palette_read_index * 3 +
				    d->palette_read_subindex] >> 2) & 63;
				d->palette_read_subindex ++;
				if (d->palette_read_subindex == 3) {
					d->palette_read_index ++;
					d->palette_read_subindex = 0;
				}
			}
			break;

		case VGA_MISC_OUTPUT_R:
			odata = d->misc_output_reg;
			break;

		case VGA_GRAPHCONTR_ADDR:		/*  0x0e  */
			if (writeflag == MEM_READ)
				odata = d->graphcontr_reg_select;
			else
				d->graphcontr_reg_select = idata;
			break;
		case VGA_GRAPHCONTR_DATA:		/*  0x0f  */
			if (writeflag == MEM_READ)
				odata = d->graphcontr_reg[
				    d->graphcontr_reg_select];
			else {
				d->graphcontr_reg[d->
				    graphcontr_reg_select] = idata;
				vga_graphcontr_reg_write(cpu->machine, d,
				    d->graphcontr_reg_select, idata);
			}
			break;

		case VGA_CRTC_ADDR:			/*  0x14  */
			if (writeflag == MEM_READ)
				odata = d->crtc_reg_select;
			else
				d->crtc_reg_select = idata;
			break;
		case VGA_CRTC_DATA:			/*  0x15  */
			if (writeflag == MEM_READ)
				odata = d->crtc_reg[d->crtc_reg_select];
			else {
				d->crtc_reg[d->crtc_reg_select] = idata;
				vga_crtc_reg_write(cpu->machine, d,
				    d->crtc_reg_select, idata);
			}
			break;

		case VGA_INPUT_STATUS_1:	/*  0x1A  */
			odata = 0;
			d->n_is1_reads ++;
			d->current_retrace_line ++;
			d->current_retrace_line %= (MAX_RETRACE_SCANLINES * 8);
			/*  Whenever we are "inside" a scan line, copy the
			    current palette into retrace_palette[][]:  */
			if ((d->current_retrace_line & 7) == 7) {
				if (d->retrace_palette == NULL &&
				    d->n_is1_reads > N_IS1_READ_THRESHOLD) {
					CHECK_ALLOCATION(d->retrace_palette =
					    (unsigned char *) malloc(
					    MAX_RETRACE_SCANLINES * 256*3));
				}
				if (d->retrace_palette != NULL)
					memcpy(d->retrace_palette + (d->
					    current_retrace_line >> 3) * 256*3,
					    d->fb->rgb_palette, d->cur_mode ==
					    MODE_CHARCELL? (16*3) : (256*3));
			}
			/*  These need to go on and off, to fake the
			    real vertical and horizontal retrace info.  */
			if (d->current_retrace_line < 20*8)
				odata |= VGA_IS1_DISPLAY_VRETRACE;
			else {
				if ((d->current_retrace_line & 7) == 0)
					odata = VGA_IS1_DISPLAY_VRETRACE | VGA_IS1_DISPLAY_DISPLAY_DISABLE;
			}
			L(fprintf(stderr, "input status 1: %08" PRIx64"\n", odata));
			break;

		default:
			if (writeflag==MEM_READ) {
				debug("[ vga_ctrl: read from 0x%08lx ]\n",
				    (long)relative_addr);
			} else {
				debug("[ vga_ctrl: write to  0x%08lx: 0x%08x"
				    " ]\n", (long)relative_addr, (int)idata);
			}
		}

		if (writeflag == MEM_READ)
			data[i] = odata;

		/*  For multi-byte accesses:  */
		relative_addr ++;
	}

	return 1;
}


DEVICE_ACCESS(vga_option_rom)
{
  uint8_t rom_data[0x2000] = {
    0x55, 0xaa, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
    //          Target Loc
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 10
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 20
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 30
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 40
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 50
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 60
    0x01, 0xff, 0x6e, 0x64, 0x20, 0x4d, 0x75, 0x6c,
    0x74, 0x69, 0x6d, 0x65, 0x64, 0x69, 0x61, 0x20, // 70
    0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x73, 0x2c,
    0x20, 0x49, 0x6e, 0x63, 0x2e, 0x20, 0x20, 0x4d, // 80
    0x42, 0x20, 0x64, 0x69, 0x73, 0x70, 0x6c, 0x61,
    0x79, 0x20, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, // 90
    0x20, 0x69, 0x6e, 0x73, 0x74, 0x61, 0x6c, 0x6c,
    0x65, 0x64, 0x30, 0x35, 0x2f, 0x30, 0x31, 0x2f, // a0
    0x39, 0x35, 0x30, 0x35, 0x2f, 0x30, 0x31, 0x2f,
    0x39, 0x35, 0x09, 0x02, 0x03, 0xd0, 0x30, 0x00, // b0
    0x7f, 0x11, 0x00, 0x00, 0x00, 0x00, 0x94, 0x38,
    0xa4, 0x38, 0xce, 0x38, 0x00, 0x00, 0x00, 0x00, // c0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x4a,
    0xff, 0x7f, 0xd6, 0x00, 0x21, 0x01, 0x25, 0x38, // d0
    0x48, 0x39, 0xa5, 0x31, 0x05, 0x50, 0x00, 0x51,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // e0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // f0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    //                      Size______
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03,
    0x04, 0x05, 0x06, 0x07, 0x00, 0x09, 0x0a, 0x0b,
    0x01, 0x0d, 0x0f, 0x0f
  };
  int rom_size = sizeof(rom_data);
  if (writeflag != MEM_WRITE) {
    uint32_t result;

    if (len == 1) {
      result = rom_data[(relative_addr) % rom_size];
    } else if (len == 2) {
      result = (rom_data[relative_addr % rom_size] << 8) | rom_data[(relative_addr + 1) % rom_size];
    } else {
      result = rom_data[relative_addr % rom_size] | (rom_data[(relative_addr + 1) % rom_size] << 8) | (rom_data[(relative_addr + 2) % rom_size] << 16) | (rom_data[(relative_addr + 3) % rom_size] << 24);
    }

    fprintf(stderr, "vga: access option rom: %08x @ %08x = %08x\n", relative_addr, cpu->pc, result);
    memory_writemax64(cpu, data, len, result);
  }
  return 1;
}

/*
 *  dev_vga_init():
 *
 *  Register a VGA text console device. max_x and max_y could be something
 *  like 80 and 25, respectively.
 */
void dev_vga_init(struct machine *machine, struct memory *mem,
	uint64_t videomem_base, uint64_t control_base, const char *name)
{
	struct vga_data *d;
	size_t allocsize, i;

	CHECK_ALLOCATION(d = (struct vga_data *) malloc(sizeof(struct vga_data)));
	memset(d, 0, sizeof(struct vga_data));

	d->console_handle = console_start_slave(machine, "vga",
	    CONSOLE_OUTPUT_ONLY);

	d->videomem_base  = videomem_base;
	d->control_base   = control_base | VIRTUAL_ISA_PORTBASE;
	d->max_x          = 100;
	d->max_y          = 38;
	d->cur_mode       = MODE_CHARCELL;
	d->crtc_reg[0xff] = 0x03;
	d->charcells_size = 0x8000;
	d->gfx_mem_size   = 64;	/*  Nothing, as we start in text mode,
			but size large enough to make gfx_mem aligned.  */
	d->pixel_repx = d->pixel_repy = machine->x11_md.scaleup;

	/*  Allocate in full pages, to make it possible to use dyntrans:  */
	allocsize = ((d->charcells_size-1) | (machine->arch_pagesize-1)) + 1;
	CHECK_ALLOCATION(d->charcells = (unsigned char *) malloc(d->charcells_size));
	CHECK_ALLOCATION(d->charcells_outputed = (unsigned char *) malloc(d->charcells_size));
	CHECK_ALLOCATION(d->charcells_drawn = (unsigned char *) malloc(d->charcells_size));
	CHECK_ALLOCATION(d->gfx_mem = (unsigned char *) malloc(d->gfx_mem_size));

	memset(d->charcells_drawn, 0, d->charcells_size);

	for (i=0; i<d->charcells_size; i+=2) {
		d->charcells[i] = ' ';
		d->charcells[i+1] = 0x07;  /*  Default color  */
		d->charcells_drawn[i] = ' ';
		d->charcells_drawn[i+1] = 0x07;
	}

	memset(d->charcells_outputed, 0, d->charcells_size);
	memset(d->gfx_mem, 0, d->gfx_mem_size);

	d->font = font8x16;
	d->font_width  = 8;
	d->font_height = 16;

	d->fb_max_x = d->pixel_repx * d->max_x;
	d->fb_max_y = d->pixel_repy * d->max_y;
	if (d->cur_mode == MODE_CHARCELL) {
		d->fb_max_x *= d->font_width;
		d->fb_max_y *= d->font_height;
	}

	/*
	memory_device_register(mem, "vga_charcells", videomem_base + 0x18000,
	    allocsize, dev_vga_access, d, DM_DYNTRANS_OK |
	    DM_DYNTRANS_WRITE_OK | DM_READS_HAVE_NO_SIDE_EFFECTS,
	    d->charcells);
	*/
	memory_device_register(mem, "vga_gfx", videomem_base, GFX_ADDR_WINDOW,
                         dev_vga_graphics_access, d, DM_DEFAULT |
                         DM_READS_HAVE_NO_SIDE_EFFECTS, d->gfx_mem);

	// memory_device_register(mem, "vga_gfx", 0xc0000000, 0xc0000,
  //                        dev_vga_graphics_access, d, DM_DEFAULT |
  //                        DM_READS_HAVE_NO_SIDE_EFFECTS, d->gfx_mem);

  // memory_device_register(mem, "vga_rom", 0xc00c0000,
  //     65536, dev_vga_option_rom_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_ctrl", control_base,
	    32, dev_vga_ctrl_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_control", VIRTUAL_ISA_PORTBASE | 0x80009ae8, 4,
      dev_vga_s3_control_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_curx", VIRTUAL_ISA_PORTBASE | 0x800086e8, 4,
	    dev_vga_s3_curx_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_cury", VIRTUAL_ISA_PORTBASE | 0x800082e8, 4,
	    dev_vga_s3_cury_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_major_axis_len", VIRTUAL_ISA_PORTBASE | 0x800096e8, 4,
	    dev_vga_s3_major_axis_len_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_pix_transfer", VIRTUAL_ISA_PORTBASE | 0x8000e2e8, 4,
	    dev_vga_s3_pix_transfer_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_bg_color", VIRTUAL_ISA_PORTBASE | 0x8000a2e8, 4,
      dev_vga_s3_bg_color_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_fg_color", VIRTUAL_ISA_PORTBASE | 0x8000a6e8, 4,
      dev_vga_s3_fg_color_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_color_compare", VIRTUAL_ISA_PORTBASE | 0x8000b2e8, 4,
      dev_vga_s3_color_compare_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_fg_color_mix", VIRTUAL_ISA_PORTBASE | 0x8000bae8, 4,
                         dev_vga_s3_fg_color_mix_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_destx", VIRTUAL_ISA_PORTBASE | 0x80008ee8, 4,
                         dev_vga_s3_destx_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_desty", VIRTUAL_ISA_PORTBASE | 0x80008ae8, 4,
                         dev_vga_s3_desty_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_pio_cmd", VIRTUAL_ISA_PORTBASE | 0x8000bee8, 4,
      dev_vga_s3_pio_cmd_access, d, DM_DEFAULT, NULL);

	d->fb = dev_fb_init(machine, mem, VGA_FB_ADDR, VFB_GENERIC,
	    d->fb_max_x, d->fb_max_y, d->fb_max_x, d->fb_max_y, 24, "VGA");
	d->fb_size = d->fb_max_x * d->fb_max_y * 3;

	reset_palette(d, 0);

	/*  This will force an initial redraw/resynch:  */
	d->update_x1 = 0;
	d->update_x2 = d->max_x - 1;
	d->update_y1 = 0;
	d->update_y2 = d->max_y - 1;
	d->modified = 1;

	machine_add_tickfunction(machine, dev_vga_tick, d, VGA_TICK_SHIFT);

	register_reset(d);

	vga_update_cursor(machine, d);
  vga_hack_start(d);
}

