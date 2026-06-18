/*
 *  Copyright (C) 2003-2009  Anders Gavare.  All rights reserved.
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
 *  X11-related functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "x11.h"
#include "console.h"
#include "emul.h"
#include "machine.h"
#include "misc.h"
#include "cpu.h"

#ifndef	WITH_X11


/*  Dummy functions:  */
void x11_redraw_cursor(struct machine *m, int i) { }
void x11_redraw(struct machine *m, int x) { }
void x11_init(struct machine *machine) { }
struct fb_window *x11_fb_init(int xsize, int ysize, char *name,
	int scaledown, struct machine *machine)
    { return NULL; }
void x11_check_event(struct emul *emul) { }


#else	/*  WITH_X11  */
/* SDL Library */
#include <SDL.h>

struct KeysymAndKeyName {
  SDL_Scancode keysym;
  int keyname_idx;
};

KeysymAndKeyName x11_keys[] = {
  { SDL_SCANCODE_ESCAPE, KeyNames::ESC },
  { SDL_SCANCODE_F1, KeyNames::F1 },
  { SDL_SCANCODE_F2, KeyNames::F2 },
  { SDL_SCANCODE_F3, KeyNames::F3 },
  { SDL_SCANCODE_F4, KeyNames::F4 },
  { SDL_SCANCODE_F5, KeyNames::F5 },
  { SDL_SCANCODE_F6, KeyNames::F6 },
  { SDL_SCANCODE_F7, KeyNames::F7 },
  { SDL_SCANCODE_F8, KeyNames::F8 },
  { SDL_SCANCODE_F9, KeyNames::F9 },
  { SDL_SCANCODE_F10, KeyNames::F10 },
  { SDL_SCANCODE_F11, KeyNames::F11 },
  { SDL_SCANCODE_F12, KeyNames::F12 },
  { SDL_SCANCODE_SYSREQ, KeyNames::PrtSc },
  { SDL_SCANCODE_INSERT, KeyNames::Insert },
  { SDL_SCANCODE_DELETE, KeyNames::Delete },
  { SDL_SCANCODE_1, KeyNames::N1 },
  { SDL_SCANCODE_2, KeyNames::N2 },
  { SDL_SCANCODE_3, KeyNames::N3 },
  { SDL_SCANCODE_4, KeyNames::N4 },
  { SDL_SCANCODE_5, KeyNames::N5 },
  { SDL_SCANCODE_6, KeyNames::N6 },
  { SDL_SCANCODE_7, KeyNames::N7 },
  { SDL_SCANCODE_8, KeyNames::N8 },
  { SDL_SCANCODE_9, KeyNames::N9 },
  { SDL_SCANCODE_0, KeyNames::N0 },
  { SDL_SCANCODE_MINUS, KeyNames::Minus },
  { SDL_SCANCODE_EQUALS, KeyNames::Equals },
  { SDL_SCANCODE_BACKSPACE, KeyNames::Backspace },
  { SDL_SCANCODE_TAB, KeyNames::Tab },
  { SDL_SCANCODE_Q, KeyNames::Q },
  { SDL_SCANCODE_W, KeyNames::W },
  { SDL_SCANCODE_E, KeyNames::E },
  { SDL_SCANCODE_R, KeyNames::R },
  { SDL_SCANCODE_T, KeyNames::T },
  { SDL_SCANCODE_Y, KeyNames::Y },
  { SDL_SCANCODE_U, KeyNames::U },
  { SDL_SCANCODE_I, KeyNames::I },
  { SDL_SCANCODE_O, KeyNames::O },
  { SDL_SCANCODE_P, KeyNames::P },
  { SDL_SCANCODE_LEFTBRACKET, KeyNames::LBrace },
  { SDL_SCANCODE_RIGHTBRACKET, KeyNames::RBrace },
  { SDL_SCANCODE_BACKSLASH, KeyNames::Backslash },
  { SDL_SCANCODE_CAPSLOCK, KeyNames::CapsLock },
  { SDL_SCANCODE_A, KeyNames::A },
  { SDL_SCANCODE_S, KeyNames::S },
  { SDL_SCANCODE_D, KeyNames::D },
  { SDL_SCANCODE_F, KeyNames::F },
  { SDL_SCANCODE_G, KeyNames::G },
  { SDL_SCANCODE_H, KeyNames::H },
  { SDL_SCANCODE_J, KeyNames::J },
  { SDL_SCANCODE_K, KeyNames::K },
  { SDL_SCANCODE_L, KeyNames::L },
  { SDL_SCANCODE_SEMICOLON, KeyNames::Semicolon },
  { SDL_SCANCODE_APOSTROPHE, KeyNames::Quote },
  { SDL_SCANCODE_RETURN, KeyNames::Return },
  { SDL_SCANCODE_LSHIFT, KeyNames::LShift },
  { SDL_SCANCODE_Z, KeyNames::Z },
  { SDL_SCANCODE_X, KeyNames::X },
  { SDL_SCANCODE_C, KeyNames::C },
  { SDL_SCANCODE_V, KeyNames::V },
  { SDL_SCANCODE_B, KeyNames::B },
  { SDL_SCANCODE_N, KeyNames::N },
  { SDL_SCANCODE_M, KeyNames::M },
  { SDL_SCANCODE_COMMA, KeyNames::Comma },
  { SDL_SCANCODE_PERIOD, KeyNames::Dot },
  { SDL_SCANCODE_SLASH, KeyNames::Slash },
  { SDL_SCANCODE_RSHIFT, KeyNames::RShift },
  { SDL_SCANCODE_LCTRL, KeyNames::Ctrl },
  { SDL_SCANCODE_LALT, KeyNames::Alt },
  { SDL_SCANCODE_SPACE, KeyNames::Space },
  { SDL_SCANCODE_UP, KeyNames::Up },
  { SDL_SCANCODE_DOWN, KeyNames::Down },
  { SDL_SCANCODE_LEFT, KeyNames::Left },
  { SDL_SCANCODE_RIGHT, KeyNames::Right },
  { SDL_SCANCODE_PAGEUP, KeyNames::PgUp },
  { SDL_SCANCODE_PAGEDOWN, KeyNames::PgDn },
  { SDL_SCANCODE_HOME, KeyNames::Home },
  { SDL_SCANCODE_END, KeyNames::End },
  { SDL_SCANCODE_GRAVE, KeyNames::Backquote }
};

/*
 *  x11_redraw_cursor():
 *
 *  Redraw a framebuffer's X11 cursor.
 *
 *  NOTE: It is up to the caller to call XFlush.
 */
void x11_redraw_cursor(struct machine *m, int i)
{
    int last_color_used = 0;
    int n_colors_used = 0;
    struct fb_window *fbwin = m->x11_md.fb_windows[i];

    SDL_Rect cursor_back_source;
    cursor_back_source.x = fbwin->OLD_cursor_x/fbwin->scaledown;
    cursor_back_source.y = fbwin->OLD_cursor_y/fbwin->scaledown;
    cursor_back_source.w = fbwin->OLD_cursor_xsize/fbwin->scaledown + 1;
    cursor_back_source.h = fbwin->OLD_cursor_ysize/fbwin->scaledown + 1;
    SDL_Rect cursor_back_dest = cursor_back_source;
    cursor_back_dest.x = 0;
    cursor_back_dest.y = 0;

    /*  Remove old cursor, if any:  */
    if (fbwin->x11_fb_window != nullptr && fbwin->OLD_cursor_on) {
        // Make a cursor sized texture.
        SDL_SetRenderTarget(fbwin->x11_fb_render, fbwin->cursor_reserve);
        SDL_RenderCopy(fbwin->x11_fb_render, fbwin->fb_data, &cursor_back_source, &cursor_back_dest);
        SDL_SetRenderTarget(fbwin->x11_fb_render, nullptr);
    }

    if (fbwin->x11_fb_window != NULL && fbwin->cursor_on) {
        SDL_RenderCopy(fbwin->x11_fb_render, fbwin->host_cursor_pixmap, &cursor_back_dest, &cursor_back_source);

        fbwin->OLD_cursor_on = fbwin->cursor_on;
        fbwin->OLD_cursor_x = fbwin->cursor_x;
        fbwin->OLD_cursor_y = fbwin->cursor_y;
        fbwin->OLD_cursor_xsize =
            fbwin->cursor_xsize;
        fbwin->OLD_cursor_ysize =
            fbwin->cursor_ysize;
    }
}


/*
 *  x11_redraw():
 *
 *  Redraw X11 windows.
 */
void x11_redraw(struct machine *m, int i)
{
	if (i < 0 || i >= m->x11_md.n_fb_windows ||
	    m->x11_md.fb_windows[i]->x11_fb_winxsize <= 0)
		return;

	x11_putimage_fb(m, i);
	// x11_redraw_cursor(m, i);

  if (m->x11_md.fb_windows[i]->x11_fb_winxsize > 0) {
      fprintf(stderr, "[ SDL: render present ]\n");
      SDL_RenderPresent(m->x11_md.fb_windows[i]->x11_fb_render);
  }
}


/*
 *  x11_putimage_fb():
 *
 *  Output an entire XImage to a framebuffer window. i is the
 *  framebuffer number.
 */
void x11_putimage_fb(struct machine *m, int i)
{
	struct fb_window *fbwin;
	if (i < 0 || i >= m->x11_md.n_fb_windows)
		return;

	fbwin = m->x11_md.fb_windows[i];

	if (fbwin->x11_fb_winxsize <= 0)
		return;

  SDL_Rect cursor_rect;
  cursor_rect.x = 0;
  cursor_rect.y = 0;
  cursor_rect.w = fbwin->x11_fb_winxsize;
  cursor_rect.h = fbwin->x11_fb_winysize;
  SDL_RenderCopy(fbwin->x11_fb_render, fbwin->fb_data, &cursor_rect, &cursor_rect);
}


/*
 *  x11_init():
 *
 *  Initialize X11 stuff (but doesn't create any windows).
 *
 *  It is then up to individual drivers, for example framebuffer devices,
 *  to initialize their own windows.
 */
void x11_init(struct machine *m)
{
    if(SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        fprintf(stderr, "[%s: %d]Fatal Error: SDL window init error: %s\n", __FILE__, __LINE__, SDL_GetError());
        exit(1);
    }

    m->x11_md.n_fb_windows = 0;

    if (m->x11_md.n_display_names > 0) {
        int i;
        for (i=0; i<m->x11_md.n_display_names; i++)
            fatal("Using X11 display: %s\n",
                  m->x11_md.display_names[i]);
    }

    m->x11_md.current_display_name_nr = 0;
}


/*
 *  x11_fb_resize():
 *
 *  Set a new size for an X11 framebuffer window.  (NOTE: I didn't think of
 *  this kind of functionality during the initial design, so it is probably
 *  buggy. It also needs some refactoring.)
 */
void x11_fb_resize(struct fb_window *win, int new_xsize, int new_ysize)
{
	int alloc_depth;

	if (win == NULL) {
		fatal("x11_fb_resize(): win == NULL\n");
		return;
	}

	win->x11_fb_winxsize = new_xsize;
	win->x11_fb_winysize = new_ysize;

	alloc_depth = win->x11_screen_depth;
	if (alloc_depth == 24)
		alloc_depth = 32;
	if (alloc_depth == 15)
		alloc_depth = 16;

	if (win->fb_data != nullptr) {
      SDL_DestroyTexture(win->fb_data);
  }
  win->fb_data = SDL_CreateTexture(
      win->x11_fb_render,
      SDL_PIXELFORMAT_RGBA32,
      SDL_TEXTUREACCESS_STREAMING,
      new_xsize,
      new_ysize
  );
  SDL_SetWindowSize(win->x11_fb_window, new_xsize/win->scaledown, new_ysize/win->scaledown);
	/*  TODO: clear for non-truecolor modes  */
  SDL_SetRenderTarget(win->x11_fb_render, win->fb_data);
  SDL_SetRenderDrawColor(win->x11_fb_render, 0, 0, 0, 0xff);
  SDL_RenderClear(win->x11_fb_render);
  SDL_SetRenderTarget(win->x11_fb_render, nullptr);
}


/*
 *  x11_set_standard_properties():
 *
 *  Right now, this only sets the title of a window.
 */
void x11_set_standard_properties(struct fb_window *fb_window, char *name)
{
    SDL_SetWindowTitle(fb_window->x11_fb_window, name);
}


/*
 *  x11_fb_init():
 *
 *  Initialize a framebuffer window.
 */
struct fb_window *x11_fb_init(int xsize, int ysize, char *name,
	int scaledown, struct machine *m)
{
	int x, y, fb_number = 0;
	size_t alloclen, alloc_depth;
	struct fb_window *fbwin;
	int i;
	char fg[80], bg[80];
	char *display_name;

	fb_number = m->x11_md.n_fb_windows;

	CHECK_ALLOCATION(m->x11_md.fb_windows = 
	    (struct fb_window **) realloc(m->x11_md.fb_windows,
	    sizeof(struct fb_window *) * (m->x11_md.n_fb_windows + 1)));
	CHECK_ALLOCATION(fbwin = m->x11_md.fb_windows[fb_number] =
	    (struct fb_window *) malloc(sizeof(struct fb_window)));

	m->x11_md.n_fb_windows ++;

	memset(fbwin, 0, sizeof(struct fb_window));

	fbwin->x11_fb_winxsize = xsize;
	fbwin->x11_fb_winysize = ysize;

	/*  Which display name?  */
	display_name = NULL;
	if (m->x11_md.n_display_names > 0) {
		display_name = m->x11_md.display_names[
		    m->x11_md.current_display_name_nr];
		m->x11_md.current_display_name_nr ++;
		m->x11_md.current_display_name_nr %= m->x11_md.n_display_names;
	}

	if (display_name != NULL)
		debug("[ x11_fb_init(): framebuffer window %i, %ix%i, DISPLAY"
		    "=%s ]\n", fb_number, xsize, ysize, display_name);

	fbwin->bg_color = 0xff << 24;

	alloc_depth = fbwin->x11_screen_depth;

	if (alloc_depth == 24)
		alloc_depth = 32;
	if (alloc_depth == 15)
		alloc_depth = 16;

  fbwin->argb32 = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA32);

  fbwin->x11_fb_window = SDL_CreateWindow(
      name,
      SDL_WINDOWPOS_UNDEFINED,
      SDL_WINDOWPOS_UNDEFINED,
      fbwin->x11_fb_winxsize,
      fbwin->x11_fb_winysize,
      SDL_WINDOW_SHOWN
  );
  fbwin->window_id = SDL_GetWindowID(fbwin->x11_fb_window);
  fbwin->x11_fb_render = SDL_CreateRenderer(fbwin->x11_fb_window, -1, SDL_RENDERER_ACCELERATED);

	/*  Make sure the window is mapped:  */
	/*  Fill the ximage with black pixels:  */
  SDL_SetRenderDrawColor(fbwin->x11_fb_render, 0, 0, 0, 0xff);
  SDL_RenderClear(fbwin->x11_fb_render);

	fbwin->scaledown   = scaledown;

	fbwin->fb_number = fb_number;

  fbwin->fb_data = SDL_CreateTexture(
      fbwin->x11_fb_render,
      SDL_PIXELFORMAT_RGBA32,
      SDL_TEXTUREACCESS_STREAMING,
      fbwin->x11_fb_winxsize/fbwin->scaledown,
      fbwin->x11_fb_winysize/fbwin->scaledown
  );

	x11_putimage_fb(m, fb_number);

	/*  Fill the 64x64 "hardware" cursor with white pixels:  */
	xsize = ysize = 64;

	/*  Fill the cursor ximage with white pixels:  */
	for (y=0; y<ysize; y++)
		for (x=0; x<xsize; x++)
			fbwin->cursor_pixels[y][x] = N_GRAYCOLORS-1;

	return fbwin;
}


/*
 *  x11_check_events_machine():
 *
 *  Check for X11 events on a specific machine.
 *
 *  TODO:  Yuck! This has to be rewritten. Each display should be checked,
 *         and _then_ only those windows that are actually exposed should
 *         be redrawn!
 */
static void x11_check_events_machine(struct emul *emul, struct machine *m)
{
    int fb_nr;
    SDL_Event event;

    SDL_PumpEvents();
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                fprintf(stderr, "[ SDL: redraw ]\n");
                for (int i = 0; i < m->x11_md.n_fb_windows; i++) {
                    if (m->x11_md.fb_windows[i]->window_id != event.window.windowID) {
                        continue;
                    }
                    x11_redraw(m, i);
                }
            } else if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                fprintf(stderr, "[ SDL: close ]\n");
                for (int i = 0; i < m->x11_md.n_fb_windows; i++) {
                    if (m->x11_md.fb_windows[i]->window_id != event.window.windowID) {
                        continue;
                    }
                    for (int j = 0; j < m->ncpus; j++) {
                        m->cpus[j]->running = false;
                    }
                }
#ifdef _WIN32
                exit(0);
#endif
            }
        } else if (event.type == SDL_MOUSEMOTION) {
            for (int i = 0; i < m->x11_md.n_fb_windows; i++) {
                if (m->x11_md.fb_windows[i]->window_id != event.motion.windowID) {
                    continue;
                }

                // Handle multiple windows.
                console_mouse_coordinates(
                    event.motion.x * m->x11_md.fb_windows[i]->scaledown,
                    event.motion.y * m->x11_md.fb_windows[i]->scaledown,
                    i
                );
            }
        } else if (event.type == SDL_MOUSEBUTTONUP) {
            for (int i = 0; i < m->x11_md.n_fb_windows; i++) {
                if (m->x11_md.fb_windows[i]->window_id != event.button.windowID) {
                    continue;
                }
                for (int j = 0; j < 3; j++) {
                    if (event.button.button & (1 << j)) {
                        console_mouse_button(j, 0);
                    }
                }
            }
        } else if (event.type == SDL_MOUSEBUTTONDOWN) {
            for (int i = 0; i < m->x11_md.n_fb_windows; i++) {
                if (m->x11_md.fb_windows[i]->window_id != event.button.windowID) {
                    continue;
                }
                for (int j = 0; j < 3; j++) {
                    if (event.button.button & (1 << j)) {
                        console_mouse_button(j, 1);
                    }
                }
            }
        } else if (event.type == SDL_KEYDOWN) {
            for (int i = 0; i < m->x11_md.n_fb_windows; i++) {
                if (m->x11_md.fb_windows[i]->window_id != event.key.windowID) {
                    continue;
                }
                for (int k = 0; k < sizeof(x11_keys) / sizeof(x11_keys[0]); k++) {
                    if (x11_keys[k].keysym == event.key.keysym.scancode) {
                        fprintf(stderr, "[ SDL: [DN] %08x ]\n", x11_keys[k].keyname_idx);
                        console_makeavail(m->main_console_handle, x11_keys[k].keyname_idx << 8);
                        return;
                    }
                }

                fprintf(stderr, "[ SDL [DOWN]: unknown keysym %x ]\n", event.key.keysym);
                return;
            }
        } else if (event.type == SDL_KEYUP) {
            for (int i = 0; i < m->x11_md.n_fb_windows; i++) {
                if (m->x11_md.fb_windows[i]->window_id != event.key.windowID) {
                    continue;
                }
                for (int k = 0; k < sizeof(x11_keys) / sizeof(x11_keys[0]); k++) {
                    if (x11_keys[k].keysym == event.key.keysym.scancode) {
                        fprintf(stderr, "[ SDL: [UP] %08x ]\n", x11_keys[k].keyname_idx);
                        console_makeavail(m->main_console_handle, KEY_RELEASE | (x11_keys[k].keyname_idx << 8));
                        return;
                    }
                }

                fprintf(stderr, "[ SDL [UP]: unknown keysym %x ]\n", event.key.keysym);
                return;
            }
        }
    }
}


/*
 *  x11_check_event():
 *
 *  Check for X11 events.
 */
void x11_check_event(struct emul *emul)
{
	int i;

	for (i=0; i<emul->n_machines; i++)
		x11_check_events_machine(emul, emul->machines[i]);
}

#endif	/*  WITH_X11  */
