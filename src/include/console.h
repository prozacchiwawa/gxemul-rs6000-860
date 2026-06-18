#ifndef	CONSOLE_H
#define	CONSOLE_H

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
 *  Console functions.  (See console.c for more info.)
 */

#include "misc.h"

/*  Fixed default console handle for the main console:  */
#define	MAIN_CONSOLE		0

#define	CONSOLE_OUTPUT_ONLY	-1

namespace KeyNames {
enum KeyNames {
  UNUSED,
  ESC,
  F1,
  F2,
  F3,
  F4,
  F5,
  F6,
  F7,
  F8,
  F9,
  F10,
  F11,
  F12,
  PrtSc,
  Insert,
  Delete,
  N1,
  N2,
  N3,
  N4,
  N5,
  N6,
  N7,
  N8,
  N9,
  N0,
  Minus,
  Equals,
  Backspace,
  Tab,
  Q,
  W,
  E,
  R,
  T,
  Y,
  U,
  I,
  O,
  P,
  LBrace,
  RBrace,
  Backslash,
  CapsLock,
  A,
  S,
  D,
  F,
  G,
  H,
  J,
  K,
  L,
  Semicolon,
  Quote,
  Return,
  LShift,
  Z,
  X,
  C,
  V,
  B,
  N,
  M,
  Comma,
  Dot,
  Slash,
  RShift,
  Ctrl,
  Alt,
  Space,
  Up,
  Down,
  Left,
  Right,
  PgUp,
  PgDn,
  Home,
  End,
  Backquote,
};
}

extern const int KEY_RELEASE;
extern const char *keynames[];

#define	CONSOLE_FIFO_LEN	4096

struct console_handle {
	int		in_use;
	int		in_use_for_input;
	int		using_xterm;
	int		inputonly;
	int		outputonly;
	int		warning_printed;

	char		*machine_name;
	char		*name;

	unsigned int	fifo[CONSOLE_FIFO_LEN];
	int		fifo_head;
	int		fifo_tail;

  void *platform;
};

/*  A simple array of console_handles  */
extern struct console_handle *console_handles;
extern int n_console_handles;
extern int allow_slaves;

extern int console_mouse_x;		/*  absolute x, 0-based  */
extern int console_mouse_y;		/*  absolute y, 0-based  */
extern int console_mouse_fb_nr;		/*  framebuffer number of
                                      host movement, 0-based  */
extern int console_mouse_buttons;	/*  left=4, middle=2, right=1  */

extern char *stderr_redirect_log;

extern const char *keynames[];

#define ESCAPE 1
#define SHIFTED 2
#define CTRL 4
#define ASCII_CASE 8
#define NUL 16

#define	NOT_USING_XTERM				0
#define	USING_XTERM_BUT_NOT_YET_OPEN		1
#define	USING_XTERM				2

void console_deinit_main(void);
void console_sigcont(int x);
void console_makeavail(int handle, int ch);
int console_charavail(int handle);
int console_readchar(int handle);
void console_putchar(int handle, int ch);
void console_flush(void);
void console_mouse_coordinates(int x, int y, int fb_nr);
void console_mouse_button(int, int);
void console_getmouse(int *x, int *y, int *buttons, int *fb_nr);
void console_slave(const char *arg);
int console_are_slaves_allowed(void);
int console_warn_if_slaves_are_needed(int init);
int console_start_slave(struct machine *, const char *consolename, int use_for_input);
int console_start_slave_inputonly(struct machine *, const char *consolename,
	int use_for_input);
int console_change_inputability(int handle, int inputability);
void console_init_main(struct emul *);
void console_debug_dump(struct machine *);
void console_allow_slaves(int);

void console_init(void);
void console_deinit(void);

int console_new_handle_platform(int handle);
int console_init_main_platform(struct emul *emul);
int console_stdin_avail_platform(int handle);
int console_putchar_platform(int handle, int ch);
int console_read_platform(int handle, uint8_t *buf, size_t len);
void start_xterm_platform(int handle);
int redirect_stderr_platform(const char *new_file);
int console_makeavail_platform(int handle, int ch);

#endif	/*  CONSOLE_H  */
