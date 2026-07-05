#ifndef	X11_H
#define	X11_H

/*
 *  Copyright (C) 2003-2010  Anders Gavare.  All rights reserved.
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
 *  Headerfile for src/x11.c.
 */

#include "misc.h"

struct emul;

#ifdef WITH_X11
#include <SDL.h>
#endif


struct x11_cursor {
  bool on;
  uint32_t palette[256];
  int invert_color;
  int width;
  int render_x, render_y;
  int center_x, center_y;
  std::vector<uint8_t> data;
  SDL_Texture *render;

  x11_cursor() :
    on(), invert_color(), width(), render_x(), render_y(), center_x(), center_y(), render()
  {
    memset(palette, 0, sizeof(palette));
  }

  ~x11_cursor();

  void block(int color, int x, int y) {
    width = x;
    data.clear();
    for (int i = 0; i < x * y; i++) {
      data.push_back(color);
    }
  }
};


/*  x11.c:  */
#define N_GRAYCOLORS            16
#define	CURSOR_COLOR_TRANSPARENT	-1
#define	CURSOR_COLOR_INVERT		-2
#define	CURSOR_MAXY		64
#define	CURSOR_MAXX		64
/*  Framebuffer windows:  */
struct fb_window {
	int		fb_number;

#ifdef WITH_X11
	/*  x11_fb_winxsize > 0 for a valid fb_window  */
	int		x11_fb_winxsize, x11_fb_winysize;
	int		scaledown;
  // SDL_Window *x11_display;

	int		x11_screen;
	int		x11_screen_depth;
	unsigned long	fg_color;
	unsigned long	bg_color;
	uint32_t		x11_graycolor[N_GRAYCOLORS];
	SDL_Window		*x11_fb_window;
  SDL_Renderer  *x11_fb_render;

	//XImage		*fb_ximage;
  SDL_Texture *fb_data;
	// unsigned char	*ximage_data;

  std::vector<struct x11_cursor> cursors;

	/*  Host's X11 cursor:  */
  SDL_Texture *pixel;
  uint32_t window_id;
  SDL_PixelFormat *argb32;
#endif
};
void x11_set_num_cursors(struct fb_window *win, size_t n);
void x11_set_cursor_data(struct fb_window *win, size_t n, const struct x11_cursor &cursor);
void x11_update_cursor(struct fb_window *win, size_t n, bool on, int x, int y);

void x11_redraw(struct machine *, int);
#ifdef WITH_X11
void x11_putimage_fb(struct machine *, int);
#endif
void x11_init(struct machine *);
void x11_fb_resize(struct fb_window *win, int new_xsize, int new_ysize);
void x11_set_standard_properties(struct fb_window *fb_window, char *name);
struct fb_window *x11_fb_init(int xsize, int ysize, char *name,
	int scaledown, struct machine *);
void x11_check_event(struct emul *emul);


#endif	/*  X11_H  */
