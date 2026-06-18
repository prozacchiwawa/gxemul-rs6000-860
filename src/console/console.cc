/*
 *  Copyright (C) 2003-2014  Anders Gavare.  All rights reserved.
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
 *  Generic console support functions.
 *
 *  This module is used by individual device drivers, for example serial
 *  controllers, keyboards, or other devices which need to attach to the
 *  emulator's real stdin/stdout.
 *
 *  The idea is that several input and output streams (console handles) are
 *  allowed. As long as the number of input streams is <= 1, then everything
 *  can be done in the emulator's default terminal window.
 *
 *  If the number of inputs is more than 1, it is necessary to open up slave
 *  xterm windows for each input. (Otherwise the behaviour is undefined; i.e.
 *  which of two emulated serial controllers would get keyboard input?)
 *
 *  (If the -x command line option is NOT used, then slaves are not opened up.
 *  Instead, a warning message is printed, and input is not allowed.)
 *
 *  Note that console handles that _allow_ input but are not yet used for
 *  output are not counted. This allows a machine to have, say, 2 serial ports
 *  which can be used for both input and output, and it will still be possible
 *  to run in the default terminal window as long as only one of those serial
 *  ports is actually used.
 *
 *  xterms are opened up "on demand", when output is sent to them.
 *
 *  The MAIN console handle (fixed as handle nr 0) is the one used by the
 *  default terminal window. A machine which registers a serial controller,
 *  which should be used as the main way of communicating with guest operating
 *  systems running on that machine, should set machine->main_console_handle
 *  to the handle of the correct port on that controller.
 *
 *
 *  NOTE: The code in this module is mostly non-reentrant.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "console.h"
#include "emul.h"
#include "machine.h"
#include "settings.h"

extern int verbose;
extern struct settings *global_settings;

const int KEY_RELEASE = 1 << 17;
const char *keynames[] = {
  "unused",
  "ESC",
  "F1",
  "F2",
  "F3",
  "F4",
  "F5",
  "F6",
  "F7",
  "F8",
  "F9",
  "F10",
  "F11",
  "F12",
  "PrtSc",
  "Insert",
  "Delete",
  "1",
  "2",
  "3",
  "4",
  "5",
  "6",
  "7",
  "8",
  "9",
  "0",
  "-",
  "=",
  "Backspace",
  "Tab",
  "Q",
  "W",
  "E",
  "R",
  "T",
  "Y",
  "U",
  "I",
  "O",
  "P",
  "[",
  "]",
  "\\",
  "CapsLock",
  "A",
  "S",
  "D",
  "F",
  "G",
  "H",
  "J",
  "K",
  "L",
  ";",
  "'",
  "Return",
  "LShift",
  "Z",
  "X",
  "C",
  "V",
  "B",
  "N",
  "M",
  ",",
  ".",
  "/",
  "RShift",
  "Ctrl",
  "Alt",
  "Space",
  "Up",
  "Down",
  "Left",
  "Right",
  "PgUp",
  "PgDn",
  "Home",
  "End",
  "Backquote",
  "F11",
  "F12"
};

#define ESCAPE 1
#define SHIFTED 2
#define CTRL 4
#define ASCII_CASE 8
#define NUL 16

typedef struct {
  int keyname_idx;
  int flags;
  const char *sequence;
} ConsoleToKeyboard;

ConsoleToKeyboard ansi_to_keyboard[] = {
  // Control
  { KeyNames::C, CTRL, "\x02" }, // translate ^B to ^C
  { KeyNames::Backspace, 0, "\x08" },
  { KeyNames::Tab, 0, "\x09" },
  { KeyNames::Return, 0, "\x0d" },
  { KeyNames::Delete, 0, "\x7f" },

  // Escape
  { KeyNames::Up, ESCAPE, "A" },
  { KeyNames::Down, ESCAPE, "B" },
  { KeyNames::Right, ESCAPE, "C" },
  { KeyNames::Left, ESCAPE, "D" },
  { KeyNames::PgUp, ESCAPE, "S" },
  { KeyNames::PgDn, ESCAPE, "T" },
  { KeyNames::F1, ESCAPE, "OP" },
  { KeyNames::F2, ESCAPE, "OQ" },
  { KeyNames::F3, ESCAPE, "OR" },
  { KeyNames::F4, ESCAPE, "OS" },
  { KeyNames::F5, ESCAPE, "Ot" },
  { KeyNames::F6, ESCAPE, "Ou" },
  { KeyNames::F7, ESCAPE, "Ov" },
  { KeyNames::F8, ESCAPE, "Ol" },
  { KeyNames::F9, ESCAPE, "Ow" },
  { KeyNames::F10, ESCAPE, "Ox" },

  // Printable
  { KeyNames::Space, 0, " " },
  { KeyNames::Backquote, 0, "`" },
  { KeyNames::N1, 0, "1" },
  { KeyNames::N2, 0, "2" },
  { KeyNames::N3, 0, "3" },
  { KeyNames::N4, 0, "4" },
  { KeyNames::N5, 0, "5" },
  { KeyNames::N6, 0, "6" },
  { KeyNames::N7, 0, "7" },
  { KeyNames::N8, 0, "8" },
  { KeyNames::N9, 0, "9" },
  { KeyNames::N0, 0, "0" },
  { KeyNames::Minus, 0, "-" },
  { KeyNames::Equals, 0, "=" },
  { KeyNames::Q, ASCII_CASE, "q" },
  { KeyNames::W, ASCII_CASE, "w" },
  { KeyNames::E, ASCII_CASE, "e" },
  { KeyNames::R, ASCII_CASE, "r" },
  { KeyNames::T, ASCII_CASE, "t" },
  { KeyNames::Y, ASCII_CASE, "y" },
  { KeyNames::U, ASCII_CASE, "u" },
  { KeyNames::I, ASCII_CASE, "i" },
  { KeyNames::O, ASCII_CASE, "o" },
  { KeyNames::P, ASCII_CASE, "p" },
  { KeyNames::LBrace, 0, "[" },
  { KeyNames::RBrace, 0, "]" },
  { KeyNames::Backslash, 0, "\\" },
  { KeyNames::A, ASCII_CASE, "a" },
  { KeyNames::S, ASCII_CASE, "s" },
  { KeyNames::D, ASCII_CASE, "d" },
  { KeyNames::F, ASCII_CASE, "f" },
  { KeyNames::G, ASCII_CASE, "g" },
  { KeyNames::H, ASCII_CASE, "h" },
  { KeyNames::J, ASCII_CASE, "j" },
  { KeyNames::K, ASCII_CASE, "k" },
  { KeyNames::L, ASCII_CASE, "l" },
  { KeyNames::Semicolon, 0, ";" },
  { KeyNames::Quote, 0, "'" },
  { KeyNames::Z, ASCII_CASE, "z" },
  { KeyNames::X, ASCII_CASE, "x" },
  { KeyNames::C, ASCII_CASE, "c" },
  { KeyNames::V, ASCII_CASE, "v" },
  { KeyNames::B, ASCII_CASE, "b" },
  { KeyNames::N, ASCII_CASE, "n" },
  { KeyNames::M, ASCII_CASE, "m" },
  { KeyNames::Comma, 0, "," },
  { KeyNames::Dot, 0, "." },
  { KeyNames::Slash, 0, "/" },

  // Shifted
  { KeyNames::Backquote, SHIFTED, "~" },
  { KeyNames::N1, SHIFTED, "!" },
  { KeyNames::N2, SHIFTED, "@" },
  { KeyNames::N3, SHIFTED, "#" },
  { KeyNames::N4, SHIFTED, "$" },
  { KeyNames::N5, SHIFTED, "%" },
  { KeyNames::N6, SHIFTED, "^" },
  { KeyNames::N7, SHIFTED, "&" },
  { KeyNames::N8, SHIFTED, "*" },
  { KeyNames::N9, SHIFTED, "(" },
  { KeyNames::N0, SHIFTED, ")" },
  { KeyNames::Minus, SHIFTED, "_" },
  { KeyNames::Equals, SHIFTED, "=" },
  { KeyNames::LBrace, SHIFTED, "{" },
  { KeyNames::RBrace, SHIFTED, "}" },
  { KeyNames::Backslash, SHIFTED, "|" },
  { KeyNames::Semicolon, SHIFTED, ":" },
  { KeyNames::Quote, SHIFTED, "\"" },
  { KeyNames::Comma, SHIFTED, "<" },
  { KeyNames::Dot, SHIFTED, ">" },
  { KeyNames::Slash, SHIFTED, "?" },

  // Last
  { KeyNames::ESC, 0, "\x1b" },
  { KeyNames::N2, CTRL | SHIFTED | NUL, "\xff" },
  { }
};

int console_initialized = 0;
static struct settings *console_settings = NULL;
static int console_stdout_pending;

int console_mouse_x;		/*  absolute x, 0-based  */
int console_mouse_y;		/*  absolute y, 0-based  */
int console_mouse_fb_nr;		/*  framebuffer number of
					    host movement, 0-based  */
int console_mouse_buttons;	/*  left=4, middle=2, right=1  */

struct console_handle *console_handles = nullptr;
int n_console_handles;
int allow_slaves;

/*
 *  console_makeavail():
 *
 *  Put a character in the queue, so that it will be avaiable,
 *  by inserting it into the char fifo.
 */
void console_makeavail(int handle, int ch)
{
	console_handles[handle].fifo[
                               console_handles[handle].fifo_head] = ch;
	console_handles[handle].fifo_head = (
                                       console_handles[handle].fifo_head + 1) % CONSOLE_FIFO_LEN;

	if (console_handles[handle].fifo_head ==
	    console_handles[handle].fifo_tail)
		fatal("[ WARNING: console fifo overrun, handle %i ]\n", handle);
}


int make_available(int handle, const unsigned char *ch, int len, int *i) {
  const unsigned char *escape = nullptr;
  const unsigned char *escape_start = nullptr;
  const unsigned char *here = ch + *i;
  const unsigned char *end = ch + len;

  if (*i >= len) {
    return 0;
  }

  // Try to match an escape sequence.
  if (*here == '\033') {
    escape_start = here;
    auto check_escape = here + 1;
    while (check_escape < end) {
      if (isalpha(*here)) {
        escape = check_escape;
        break;
      }
      check_escape++;
    }
  }

  for (int c = 0; ansi_to_keyboard[c].sequence; c++) {
    auto atc = ansi_to_keyboard[c];
    auto seqlen = std::min(strlen(atc.sequence), (size_t)(end - here));

    if (atc.flags & ESCAPE) {
      if (!escape) {
        continue;
      }

      if (!memcmp(atc.sequence, escape, seqlen)) {
        console_makeavail(handle, atc.keyname_idx << 8);
        console_makeavail(handle, KEY_RELEASE | (atc.keyname_idx << 8));
        *i = escape + seqlen - here;
        return 0;
      }
    }

    if (atc.flags & ASCII_CASE) {
      int shift_keys[] = { 0, KeyNames::LShift, KeyNames::Ctrl, -1 };
      unsigned char upcase[2] = { atc.sequence[0], 0 };
      for (int x = 0; shift_keys[x] != -1; x++) {
        if (!memcmp(here, upcase, 1)) {
          if (shift_keys[x]) {
            console_makeavail(handle, shift_keys[x] << 8);
          }
          console_makeavail(handle, atc.keyname_idx << 8);
          console_makeavail(handle, KEY_RELEASE | (atc.keyname_idx << 8));
          if (shift_keys[x]) {
            console_makeavail(handle, KEY_RELEASE | (shift_keys[x] << 8));
          }
          (*i) += 1;
          return 0;
        }
        upcase[0] -= 32;
      }
    }

    // Special case of nul byte
    if (((*here == 0) && (atc.flags & NUL)) || !memcmp(here, atc.sequence, seqlen)) {
      int flags[][2] = {
        { CTRL, KeyNames::Ctrl },
        { SHIFTED, KeyNames::LShift },
        { }
      };

      for (int s = 0; flags[s][0]; s++) {
        if (atc.flags & flags[s][0]) {
          console_makeavail(handle, flags[s][1] << 8);
        }
      }
      console_makeavail(handle, atc.keyname_idx << 8);
      console_makeavail(handle, KEY_RELEASE | (atc.keyname_idx << 8));
      for (int s = 0; flags[s][0]; s++) {
        if (atc.flags & flags[s][0]) {
          console_makeavail(handle, KEY_RELEASE | (flags[s][1] << 8));
        }
      }
      (*i) += seqlen;
      return 0;
    }
  }

  // Skip this byte since we don't have a keyboard equivalent.
  (*i) += 1;
  return 0;
}

/*
 *  console_charavail():
 *
 *  Returns 1 if a char is available in the fifo, 0 otherwise.
 */
int console_charavail(int handle)
{
  int avail;
	while (avail = console_stdin_avail_platform(handle)) {
    if (avail == -1) {
      // Fast exit if console hung up.
      exit(0);
    }

		unsigned char ch[100];		/* = getchar(); */
		ssize_t len;
		int i = 0, d;

		// If adding more would lead to a full FIFO, then let's
		// wait.
		int roomLeftInFIFO = console_handles[handle].fifo_tail - console_handles[handle].fifo_head;
		if (roomLeftInFIFO <= 0)
			roomLeftInFIFO += CONSOLE_FIFO_LEN;
		if (roomLeftInFIFO < (int)sizeof(ch) + 1)
			break;

    len = console_read_platform(handle, ch, sizeof(ch));

    while (i < len && (make_available(handle, ch, len, &i) != -1));

    for (auto c = 0; c < len; c++) {
      console_makeavail(handle, ch[c]);
    }
	}

	if (console_handles[handle].fifo_head ==
	    console_handles[handle].fifo_tail)
		return 0;

	return 1;
}


/*
 *  console_readchar():
 *
 *  Returns 0..255 if a char was available, -1 otherwise.
 */
int console_readchar(int handle)
{
	int ch;

	if (console_handles[handle].using_xterm ==
	    USING_XTERM_BUT_NOT_YET_OPEN)
		start_xterm_platform(handle);

	if (!console_charavail(handle))
		return -1;

	ch = console_handles[handle].fifo[console_handles[handle].fifo_tail];
	console_handles[handle].fifo_tail ++;
	console_handles[handle].fifo_tail %= CONSOLE_FIFO_LEN;

	return ch;
}


/*
 *  console_putchar():
 *
 *  Prints a char to stdout, and sets the console_stdout_pending flag.
 */
void console_putchar(int handle, int ch)
{
	if (!console_handles[handle].in_use_for_input &&
	    !console_handles[handle].outputonly)
		console_change_inputability(handle, 1);

	if (!allow_slaves) {
		/*  stdout:  */
		putchar(ch);

		/*  Assume flushes by OS or libc on newlines:  */
		if (ch == '\n')
			console_stdout_pending = 0;
		else
			console_stdout_pending = 1;

		return;
	}

	if (!console_handles[handle].in_use) {
		printf("[ console_putchar(): handle %i not in"
		    " use! ]\n", handle);
		return;
		}

	if (console_handles[handle].using_xterm ==
	    USING_XTERM_BUT_NOT_YET_OPEN)
		start_xterm_platform(handle);

  if (console_putchar_platform(handle, ch) != 1) {
		perror("error writing to console handle");
  }
}


/*
 *  console_flush():
 *
 *  Flushes stdout, if necessary, and resets console_stdout_pending to zero.
 */
void console_flush(void)
{
	if (console_stdout_pending)
		fflush(stdout);

	console_stdout_pending = 0;
}


/*
 *  console_mouse_coordinates():
 *
 *  Sets mouse coordinates. Called by for example an X11 event handler.
 *  x and y are absolute coordinates, fb_nr is where the mouse movement
 *  took place.
 */
void console_mouse_coordinates(int x, int y, int fb_nr)
{
	/*  TODO: fb_nr isn't used yet.  */

	console_mouse_x = x;
	console_mouse_y = y;
	console_mouse_fb_nr = fb_nr;
}


/*
 *  console_mouse_button():
 *
 *  Sets a mouse button to be pressed or released. Called by for example an
 *  X11 event handler.  button is 1 (left), 2 (middle), or 3 (right), and
 *  pressed = 1 for pressed, 0 for not pressed.
 */
void console_mouse_button(int button, int pressed)
{
	int mask = 1 << button;

	if (pressed)
		console_mouse_buttons |= mask;
	else
		console_mouse_buttons &= ~mask;
}


/*
 *  console_getmouse():
 *
 *  Puts current mouse data into the variables pointed to by
 *  the arguments.
 */
void console_getmouse(int *x, int *y, int *buttons, int *fb_nr)
{
	*x = console_mouse_x;
	*y = console_mouse_y;
	*buttons = console_mouse_buttons;
	*fb_nr = console_mouse_fb_nr;
}


/*
 *  console_new_handle():
 *
 *  Allocates a new console_handle struct, and returns a pointer to it.
 *
 *  For internal use.
 */
static struct console_handle *console_new_handle(const char *name, int *handlep)
{
	struct console_handle *chp;
	int i, n, found_free = -1;

	/*  Reuse an old slot, if possible:  */
	n = n_console_handles;
	for (i=0; i<n; i++)
		if (!console_handles[i].in_use) {
			found_free = i;
			break;
		}

	if (found_free == -1) {
		/*  Let's realloc console_handles[], to make room
		    for the new one:  */
		CHECK_ALLOCATION(console_handles = (struct console_handle *)
		    realloc(console_handles, sizeof(
		    struct console_handle) * (n_console_handles + 1)));
		found_free = n_console_handles;
		n_console_handles ++;
	}

	chp = &console_handles[found_free];
	memset(chp, 0, sizeof(struct console_handle));
  console_new_handle_platform(found_free);

	chp->in_use = 1;
	chp->machine_name = strdup("");
	CHECK_ALLOCATION(chp->name = strdup(name));

	*handlep = found_free;
	return chp;
}


/*
 *  console_start_slave():
 *
 *  When using X11:
 *
 *  This routine tries to start up an xterm, with another copy of gxemul
 *  inside. The other gxemul copy is given arguments that will cause it
 *  to run console_slave().
 *
 *  When not using X11:  Things will seem to work the same way without X11,
 *  but no xterm will actually be started.
 *
 *  consolename should be something like "serial 0".
 *
 *  If use_for_input is 1, input is allowed right from the start. (This
 *  can be upgraded later from 0 to 1 using the console_change_inputability()
 *  function.)
 *
 *  If use_for_input is CONSOLE_OUTPUT_ONLY, then this is an output-only stream.
 *
 *  On success, an integer >= 0 is returned. This can then be used as a
 *  'handle' when writing to or reading from an emulated console.
 *
 *  On failure, -1 is returned.
 */
int console_start_slave(struct machine *machine, const char *consolename,
	int use_for_input)
{
	struct console_handle *chp;
	int handle;

	if (machine == NULL || consolename == NULL) {
		printf("console_start_slave(): NULL ptr\n");
		exit(1);
	}

	chp = console_new_handle(consolename, &handle);
	chp->in_use_for_input = use_for_input;
	if (use_for_input == CONSOLE_OUTPUT_ONLY) {
		chp->outputonly = 1;
		chp->in_use_for_input = 0;
	}

	if (machine->machine_name != NULL) {
		CHECK_ALLOCATION(chp->machine_name =
		    strdup(machine->machine_name));
	} else {
		CHECK_ALLOCATION(chp->machine_name = strdup(""));
	}

	CHECK_ALLOCATION(chp->name = strdup(consolename));

	if (allow_slaves)
		chp->using_xterm = USING_XTERM_BUT_NOT_YET_OPEN;

	return handle;
}


/*
 *  console_start_slave_inputonly():
 *
 *  Similar to console_start_slave(), but doesn't open an xterm. This is
 *  useful for devices such as keyboard controllers, that need to have an
 *  input queue, but no xterm window associated with it.
 *
 *  On success, an integer >= 0 is returned. This can then be used as a
 *  'handle' when writing to or reading from an emulated console.
 *
 *  On failure, -1 is returned.
 */
int console_start_slave_inputonly(struct machine *machine, const char *consolename,
	int use_for_input)
{
	struct console_handle *chp;
	int handle;

	if (machine == NULL || consolename == NULL) {
		printf("console_start_slave(): NULL ptr\n");
		exit(1);
	}

	chp = console_new_handle(consolename, &handle);
	chp->inputonly = 1;
	chp->in_use_for_input = use_for_input;

	if (machine->name != NULL) {
		CHECK_ALLOCATION(chp->machine_name = strdup(machine->name));
	} else {
		CHECK_ALLOCATION(chp->machine_name = strdup(""));
	}

	CHECK_ALLOCATION(chp->name = strdup(consolename));

	return handle;
}


/*
 *  console_change_inputability():
 *
 *  Sets whether or not a console handle can be used for input. Return value
 *  is 1 if the change took place, 0 otherwise.
 */
int console_change_inputability(int handle, int inputability)
{
	int old;

	if (handle < 0 || handle >= n_console_handles) {
		fatal("console_change_inputability(): bad handle %i\n",
		    handle);
		exit(1);
	}

	old = console_handles[handle].in_use_for_input;
	console_handles[handle].in_use_for_input = inputability;

	if (inputability != 0) {
		if (console_warn_if_slaves_are_needed(0)) {
			console_handles[handle].in_use_for_input = old;
			if (!console_handles[handle].warning_printed) {
				fatal("%%\n%%  WARNING! Input to console ha"
				    "ndle \"%s\" wasn't enabled,\n%%  because "
				    "it", console_handles[handle].name);
				fatal(" would interfere with other inputs,\n"
				    "%%  and you did not use the -x command "
				    "line option!\n%%\n");
			}
			console_handles[handle].warning_printed = 1;
			return 0;
		}
	}

	return 1;
}


/*
 *  console_init_main():
 *
 *  Puts the host's console into single-character (non-canonical) mode.
 */
void console_init_main(struct emul *emul)
{
	int i, tra;

	if (console_initialized)
		return;

  console_initialized = 1;

	console_handles[MAIN_CONSOLE].fifo_head = 0;
	console_handles[MAIN_CONSOLE].fifo_tail = 0;

	console_mouse_x = 0;
	console_mouse_y = 0;
	console_mouse_buttons = 0;

  if (console_init_main_platform(emul) != 0) {
    perror("problem initializing basic platform console");
  }
}


/*
 *  console_debug_dump():
 *
 *  Dump debug info, if verbose >= 2.
 */
void console_debug_dump(struct machine *machine)
{
	int i, iadd = DEBUG_INDENTATION, listed_main = 0;

	if (verbose < 2)
		return;

	debug("console slaves (xterms): %s\n", allow_slaves?
	    "yes" : "no");

	debug("console handles:\n");
	debug_indentation(iadd);

	for (i=0; i<n_console_handles; i++) {
		if (!console_handles[i].in_use)
			continue;
		debug("%i: \"%s\"", i, console_handles[i].name);
		if (console_handles[i].using_xterm)
			debug(" [xterm]");
		if (console_handles[i].inputonly)
			debug(" [inputonly]");
		if (console_handles[i].outputonly)
			debug(" [outputonly]");
		if (i == machine->main_console_handle) {
			debug(" [MAIN CONSOLE]");
			listed_main = 1;
		}
		debug("\n");
	}

	debug_indentation(-iadd);

	if (!listed_main)
		fatal("WARNING! no main console handle?\n");
}


/*
 *  console_allow_slaves():
 *
 *  This function tells the console subsystem whether or not to open up
 *  slave xterms for each emulated serial controller.
 */
void console_allow_slaves(int allow)
{
	allow_slaves = allow;
}


/*
 *  console_are_slaves_allowed():
 *
 *  Returns the value of allow_slaves.
 */
int console_are_slaves_allowed(void)
{
	return allow_slaves;
}


/*
 *  console_warn_if_slaves_are_needed():
 *
 *  Prints an error (during startup of the emulator) if slave xterms are needed
 *  (i.e. there is more than one console handle in use which is used for
 *  INPUT), but they are not currently allowed.
 *
 *  This function should be called during startup (with init = 1), and every
 *  time a console handle changes/upgrades its in_use_for_input from 0 to 1.
 *
 *  If init is non-zero, this function doesn't return if there was a warning.
 *
 *  If init is zero, no warning is printed. 1 is returned if there were more
 *  than one input, 0 otherwise.
 */
int console_warn_if_slaves_are_needed(int init)
{
	int i, n = 0;

	if (allow_slaves)
		return 0;

	for (i=MAIN_CONSOLE+1; i<n_console_handles; i++)
		if (console_handles[i].in_use &&
		    console_handles[i].in_use_for_input &&
		    !console_handles[i].using_xterm)
			n ++;

	if (n > 1) {
		if (init) {
			fatal("#\n#  ERROR! More than one console input is "
			    "in use,\n#  but xterm slaves are not enabled.\n"
			    "#\n");
			fatal("#  Use -x to enable slave xterms.)\n#\n");
			for (i=MAIN_CONSOLE+1; i<n_console_handles; i++)
				if (console_handles[i].in_use &&
				    console_handles[i].in_use_for_input &&
				    !console_handles[i].using_xterm)
					fatal("#  console handle %i: '%s'\n",
					    i, console_handles[i].name);
			fatal("#\n");
			exit(1);
		}
		return 1;
	}

	return 0;
}


/*
 *  console_init():
 *
 *  This function should be called before any other console_*() function
 *  is used.
 */
void console_init(void)
{
	int handle;
	struct console_handle *chp;

	console_settings = settings_new();

	settings_add(global_settings, "console", 1,
            SETTINGS_TYPE_SUBSETTINGS, 0, console_settings);

	settings_add(console_settings, "allow_slaves", 0,
            SETTINGS_TYPE_INT, SETTINGS_FORMAT_YESNO, (void *)&allow_slaves);

	chp = console_new_handle("MAIN", &handle);
	if (handle != MAIN_CONSOLE) {
		printf("console_init(): fatal error: could not create"
		    " console 0: handle = %i\n", handle);
		exit(1);
	}

	chp->in_use_for_input = 1;
}


/*
 *  console_deinit():
 *
 *  Unregister settings registered by console_init().
 */
void console_deinit(void)
{
	settings_remove(console_settings, "allow_slaves");
	settings_remove(global_settings, "console");
}
