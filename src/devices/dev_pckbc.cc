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
 *  COMMENT: 8042 PC keyboard controller (+ 8242WB Keyboard/Mouse controller)
 *
 *  This module includes emulation of the 8048 keyboard chip too.
 *
 *  Quick source of good info: http://my.execpc.com/~geezer/osd/kbd/kbd.txt
 *
 *
 *  TODOs:
 *	Finish the rewrite for 8242.
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

#include "thirdparty/kbdreg.h"


/*  #define PCKBC_DEBUG  */
/*  #define debug fatal  */


#define	MAX_8042_QUEUELEN	256

#define	PC_DATA			0
#define	PC_CMD			0
#define	PC_STATUS		1

#define	PS2_TXBUF		0
#define	PS2_RXBUF		1
#define	PS2_CONTROL		2
#define	PS2_STATUS		3

#define	PS2	100

#define	PCKBC_TICKSHIFT		15

struct pckbc_data {
	int		console_handle;
	int		in_use;

	int		reg[DEV_PCKBC_LENGTH];

	struct interrupt irq_keyboard;
	struct interrupt irq_mouse;
	int		currently_asserted[2];
	int		type;
	int		pc_style_flag;

	/*  TODO: one of these for each port?  */
	int		clocksignal;
	int		rx_int_enable;
	int		tx_int_enable;

	int		keyscanning_enabled;
	int		translation_table;
	int		state;
	int		cmdbyte;
	int		output_byte;
	int		last_scancode;

	unsigned	key_queue[2][MAX_8042_QUEUELEN];
	int		head[2], tail[2];

  int mouse_cmd;

  int flex_id;
  int flex_code;

  int mouse_remote, mouse_reporting;
  int mouse_scaling, mouse_resolution, mouse_sample_rate;

  int mouse_ena;
  int mouse_last_x, mouse_last_y, mouse_last_but;
};

#define	STATE_NORMAL			0
#define	STATE_LDCMDBYTE			1
#define	STATE_RDCMDBYTE			2
#define	STATE_WAITING_FOR_TRANSLTABLE	3
#define	STATE_WAITING_FOR_RATE		4
#define	STATE_WAITING_FOR_ONEKEY_MB	5
#define	STATE_WAITING_FOR_AUX		6
#define	STATE_WAITING_FOR_AUX_OUT	7
#define	STATE_LDOUTPUT			8
#define	STATE_RDOUTPUT			9
#define STATE_WAITING_FOR_LEDS 10

const char *NONE = "";
const char *E0 = "\xe0";
const char *F0 = "\xf0";
const char *E0F0 = "\xe0\xf0";

struct KeynameToPCKeyboard {
  int keyname_idx;
  int high_bit_break;
  const char *make_prefix;
  const char *break_prefix;
  int native_code;
};

KeynameToPCKeyboard pc_type_1[] = {
  { KeyNames::ESC, 1, NONE, NONE, 0x01 },
  { KeyNames::N1, 1, NONE, NONE, 0x02 },
  { KeyNames::N2, 1, NONE, NONE, 0x03 },
  { KeyNames::N3, 1, NONE, NONE, 0x04 },
  { KeyNames::N4, 1, NONE, NONE, 0x05 },
  { KeyNames::N5, 1, NONE, NONE, 0x06 },
  { KeyNames::N6, 1, NONE, NONE, 0x07 },
  { KeyNames::N7, 1, NONE, NONE, 0x08 },
  { KeyNames::N8, 1, NONE, NONE, 0x09 },
  { KeyNames::N9, 1, NONE, NONE, 0x0a },
  { KeyNames::N0, 1, NONE, NONE, 0x0b },
  { KeyNames::Minus, 1, NONE, NONE, 0x0c },
  { KeyNames::Equals, 1, NONE, NONE, 0x0d },
  { KeyNames::Backspace, 1, NONE, NONE, 0x0e },
  { KeyNames::Tab, 1, NONE, NONE, 0x0f },
  { KeyNames::Q, 1, NONE, NONE, 0x10 },
  { KeyNames::W, 1, NONE, NONE, 0x11 },
  { KeyNames::E, 1, NONE, NONE, 0x12 },
  { KeyNames::R, 1, NONE, NONE, 0x13 },
  { KeyNames::T, 1, NONE, NONE, 0x14 },
  { KeyNames::Y, 1, NONE, NONE, 0x15 },
  { KeyNames::U, 1, NONE, NONE, 0x16 },
  { KeyNames::I, 1, NONE, NONE, 0x17 },
  { KeyNames::O, 1, NONE, NONE, 0x18 },
  { KeyNames::P, 1, NONE, NONE, 0x19 },
  { KeyNames::LBrace, 1, NONE, NONE, 0x1a },
  { KeyNames::RBrace, 1, NONE, NONE, 0x1b },
  { KeyNames::Return, 1, NONE, NONE, 0x1c },
  { KeyNames::Ctrl, 1, NONE, NONE, 0x1d },
  { KeyNames::A, 1, NONE, NONE, 0x1e },
  { KeyNames::S, 1, NONE, NONE, 0x1f },
  { KeyNames::D, 1, NONE, NONE, 0x20 },
  { KeyNames::F, 1, NONE, NONE, 0x21 },
  { KeyNames::G, 1, NONE, NONE, 0x22 },
  { KeyNames::H, 1, NONE, NONE, 0x23 },
  { KeyNames::J, 1, NONE, NONE, 0x24 },
  { KeyNames::K, 1, NONE, NONE, 0x25 },
  { KeyNames::L, 1, NONE, NONE, 0x26 },
  { KeyNames::Semicolon, 1, NONE, NONE, 0x27 },
  { KeyNames::Quote, 1, NONE, NONE, 0x28 },
  { KeyNames::Backquote, 1, NONE, NONE, 0x29 },
  { KeyNames::LShift, 1, NONE, NONE, 0x2a },
  { KeyNames::Backslash, 1, NONE, NONE, 0x2b },
  { KeyNames::Z, 1, NONE, NONE, 0x2c },
  { KeyNames::X, 1, NONE, NONE, 0x2d },
  { KeyNames::C, 1, NONE, NONE, 0x2e },
  { KeyNames::V, 1, NONE, NONE, 0x2f },
  { KeyNames::B, 1, NONE, NONE, 0x30 },
  { KeyNames::N, 1, NONE, NONE, 0x31 },
  { KeyNames::M, 1, NONE, NONE, 0x32 },
  { KeyNames::Comma, 1, NONE, NONE, 0x33 },
  { KeyNames::Dot, 1, NONE, NONE, 0x34 },
  { KeyNames::Slash, 1, NONE, NONE, 0x35 },
  { KeyNames::RShift, 1, NONE, NONE, 0x36 },
  { KeyNames::Space, 1, NONE, NONE, 0x39 },
  { KeyNames::CapsLock, 1, NONE, NONE, 0x3a },
  { KeyNames::F1, 1, NONE, NONE, 0x3b },
  { KeyNames::F2, 1, NONE, NONE, 0x3c },
  { KeyNames::F3, 1, NONE, NONE, 0x3d },
  { KeyNames::F4, 1, NONE, NONE, 0x3e },
  { KeyNames::F5, 1, NONE, NONE, 0x3f },
  { KeyNames::F6, 1, NONE, NONE, 0x40 },
  { KeyNames::F7, 1, NONE, NONE, 0x41 },
  { KeyNames::F8, 1, NONE, NONE, 0x42 },
  { KeyNames::F9, 1, NONE, NONE, 0x43 },
  { KeyNames::F10, 1, NONE, NONE, 0x44 },
  { KeyNames::Home, 1, NONE, NONE, 0x47 },
  { KeyNames::Up, 1, NONE, NONE, 0x48 },
  { KeyNames::PgUp, 1, NONE, NONE, 0x49 },
  { KeyNames::Left, 1, NONE, NONE, 0x4b },
  { KeyNames::Right, 1, NONE, NONE, 0x4d },
  { KeyNames::End, 1, NONE, NONE, 0x4f },
  { KeyNames::Down, 1, NONE, NONE, 0x50 },
  { KeyNames::PgDn, 1, NONE, NONE, 0x51 },
  { }
};

KeynameToPCKeyboard pc_type_2[] = {
  { KeyNames::ESC, 0, NONE, F0, 0x76 },
  // { KeyNames::N1, 0, NONE, F0, 0x02 },
  // { KeyNames::N2, 0, NONE, F0, 0x03 },
  // { KeyNames::N3, 0, NONE, F0, 0x04 },
  // { KeyNames::N4, 0, NONE, F0, 0x05 },
  // { KeyNames::N5, 0, NONE, F0, 0x06 },
  // { KeyNames::N6, 0, NONE, F0, 0x07 },
  // { KeyNames::N7, 0, NONE, F0, 0x08 },
  // { KeyNames::N8, 0, NONE, F0, 0x09 },
  // { KeyNames::N9, 0, NONE, F0, 0x0a },
  // { KeyNames::N0, 0, NONE, F0, 0x0b },
  // { KeyNames::Minus, 0, NONE, F0, 0x0c },
  // { KeyNames::Equals, 0, NONE, F0, 0x0d },
  { KeyNames::Backspace, 0, NONE, F0, 0x66 },
  { KeyNames::Tab, 0, NONE, F0, 0x0d },
  { KeyNames::Q, 0, NONE, F0, 0x16 },
  { KeyNames::W, 0, NONE, F0, 0x1d },
  { KeyNames::E, 0, NONE, F0, 0x24 },
  { KeyNames::R, 0, NONE, F0, 0x2d },
  { KeyNames::T, 0, NONE, F0, 0x2c },
  { KeyNames::Y, 0, NONE, F0, 0x35 },
  { KeyNames::U, 0, NONE, F0, 0x3c },
  { KeyNames::I, 0, NONE, F0, 0x43 },
  { KeyNames::O, 0, NONE, F0, 0x44 },
  { KeyNames::P, 0, NONE, F0, 0x4d },
  { KeyNames::LBrace, 0, NONE, F0, 0x54 },
  { KeyNames::RBrace, 0, NONE, F0, 0x5b },
  { KeyNames::Return, 0, NONE, F0, 0x5a },
  { KeyNames::Ctrl, 0, NONE, F0, 0x14 },
  // { KeyNames::A, 0, NONE, F0, 0x1e },
  // { KeyNames::S, 0, NONE, F0, 0x1f },
  // { KeyNames::D, 0, NONE, F0, 0x20 },
  // { KeyNames::F, 0, NONE, F0, 0x21 },
  // { KeyNames::G, 0, NONE, F0, 0x22 },
  // { KeyNames::H, 0, NONE, F0, 0x23 },
  // { KeyNames::J, 0, NONE, F0, 0x24 },
  // { KeyNames::K, 0, NONE, F0, 0x25 },
  // { KeyNames::L, 0, NONE, F0, 0x26 },
  // { KeyNames::Semicolon, 0, NONE, F0, 0x27 },
  // { KeyNames::Quote, 0, NONE, F0, 0x28 },
  // { KeyNames::Backquote, 0, NONE, F0, 0x29 },
  // { KeyNames::LShift, 0, NONE, F0, 0x2a },
  // { KeyNames::Backslash, 0, NONE, F0, 0x2b },
  // { KeyNames::Z, 0, NONE, F0, 0x2c },
  // { KeyNames::X, 0, NONE, F0, 0x2d },
  // { KeyNames::C, 0, NONE, F0, 0x2e },
  // { KeyNames::V, 0, NONE, F0, 0x2f },
  // { KeyNames::B, 0, NONE, F0, 0x30 },
  // { KeyNames::N, 0, NONE, F0, 0x31 },
  // { KeyNames::M, 0, NONE, F0, 0x32 },
  // { KeyNames::Comma, 0, NONE, F0, 0x33 },
  // { KeyNames::Dot, 0, NONE, F0, 0x34 },
  // { KeyNames::Slash, 0, NONE, F0, 0x35 },
  // { KeyNames::RShift, 0, NONE, F0, 0x36 },
  { KeyNames::Space, 0, NONE, F0, 0x29 },
  { KeyNames::CapsLock, 0, NONE, F0, 0x58 },
  { KeyNames::F1, 0, NONE, F0, 0x05 },
  { KeyNames::F2, 0, NONE, F0, 0x06 },
  { KeyNames::F3, 0, NONE, F0, 0x04 },
  { KeyNames::F4, 0, NONE, F0, 0x0c },
  { KeyNames::F5, 0, NONE, F0, 0x03 },
  { KeyNames::F6, 0, NONE, F0, 0x0b },
  { KeyNames::F7, 0, NONE, F0, 0x83 },
  { KeyNames::F8, 0, NONE, F0, 0x0a },
  { KeyNames::F9, 0, NONE, F0, 0x01 },
  { KeyNames::F10, 0, NONE, F0, 0x09 },
  { KeyNames::F11, 0, NONE, F0, 0x78 },
  { KeyNames::F12, 0, NONE, F0, 0x07 },
  { KeyNames::Home, 0, E0, E0F0, 0x6c },
  { KeyNames::Up, 0, E0, E0F0, 0x75 },
  { KeyNames::PgUp, 0, E0, E0F0, 0x7d },
  { KeyNames::Left, 0, E0, E0F0, 0x6b },
  { KeyNames::Right, 0, E0, E0F0, 0x74 },
  { KeyNames::End, 0, E0, E0F0, 0x69 },
  { KeyNames::Down, 0, E0, E0F0, 0x72 },
  { KeyNames::PgDn, 0, E0, E0F0, 0x7a },
  { }
};

KeynameToPCKeyboard pc_type_3[] = {
  { KeyNames::ESC, 0, NONE, F0, 0x08 },
  // { KeyNames::N1, 0, NONE, F0, 0x02 },
  // { KeyNames::N2, 0, NONE, F0, 0x03 },
  // { KeyNames::N3, 0, NONE, F0, 0x04 },
  // { KeyNames::N4, 0, NONE, F0, 0x05 },
  // { KeyNames::N5, 0, NONE, F0, 0x06 },
  // { KeyNames::N6, 0, NONE, F0, 0x07 },
  // { KeyNames::N7, 0, NONE, F0, 0x08 },
  // { KeyNames::N8, 0, NONE, F0, 0x09 },
  // { KeyNames::N9, 0, NONE, F0, 0x0a },
  // { KeyNames::N0, 0, NONE, F0, 0x0b },
  // { KeyNames::Minus, 0, NONE, F0, 0x0c },
  // { KeyNames::Equals, 0, NONE, F0, 0x0d },
  // { KeyNames::Backspace, 0, NONE, F0, 0x0e },
  // { KeyNames::Tab, 0, NONE, F0, 0x0f },
  // { KeyNames::Q, 0, NONE, F0, 0x10 },
  // { KeyNames::W, 0, NONE, F0, 0x11 },
  // { KeyNames::E, 0, NONE, F0, 0x12 },
  // { KeyNames::R, 0, NONE, F0, 0x13 },
  // { KeyNames::T, 0, NONE, F0, 0x14 },
  // { KeyNames::Y, 0, NONE, F0, 0x15 },
  // { KeyNames::U, 0, NONE, F0, 0x16 },
  // { KeyNames::I, 0, NONE, F0, 0x17 },
  // { KeyNames::O, 0, NONE, F0, 0x18 },
  // { KeyNames::P, 0, NONE, F0, 0x19 },
  // { KeyNames::LBrace, 0, NONE, F0, 0x1a },
  // { KeyNames::RBrace, 0, NONE, F0, 0x1b },
  // { KeyNames::Return, 0, NONE, F0, 0x1c },
  // { KeyNames::Ctrl, 0, NONE, F0, 0x1d },
  // { KeyNames::A, 0, NONE, F0, 0x1e },
  // { KeyNames::S, 0, NONE, F0, 0x1f },
  // { KeyNames::D, 0, NONE, F0, 0x20 },
  // { KeyNames::F, 0, NONE, F0, 0x21 },
  // { KeyNames::G, 0, NONE, F0, 0x22 },
  // { KeyNames::H, 0, NONE, F0, 0x23 },
  // { KeyNames::J, 0, NONE, F0, 0x24 },
  // { KeyNames::K, 0, NONE, F0, 0x25 },
  // { KeyNames::L, 0, NONE, F0, 0x26 },
  // { KeyNames::Semicolon, 0, NONE, F0, 0x27 },
  // { KeyNames::Quote, 0, NONE, F0, 0x28 },
  // { KeyNames::Backquote, 0, NONE, F0, 0x29 },
  // { KeyNames::LShift, 0, NONE, F0, 0x2a },
  // { KeyNames::Backslash, 0, NONE, F0, 0x2b },
  // { KeyNames::Z, 0, NONE, F0, 0x2c },
  // { KeyNames::X, 0, NONE, F0, 0x2d },
  // { KeyNames::C, 0, NONE, F0, 0x2e },
  // { KeyNames::V, 0, NONE, F0, 0x2f },
  // { KeyNames::B, 0, NONE, F0, 0x30 },
  // { KeyNames::N, 0, NONE, F0, 0x31 },
  // { KeyNames::M, 0, NONE, F0, 0x32 },
  // { KeyNames::Comma, 0, NONE, F0, 0x33 },
  // { KeyNames::Dot, 0, NONE, F0, 0x34 },
  // { KeyNames::Slash, 0, NONE, F0, 0x35 },
  // { KeyNames::RShift, 0, NONE, F0, 0x36 },
  // { KeyNames::Space, 0, NONE, F0, 0x39 },
  // { KeyNames::CapsLock, 0, NONE, F0, 0x3a },
  { KeyNames::F1, 0, NONE, F0, 0x05 },
  { KeyNames::F2, 0, NONE, F0, 0x06 },
  { KeyNames::F3, 0, NONE, F0, 0x04 },
  { KeyNames::F4, 0, NONE, F0, 0x0c },
  { KeyNames::F5, 0, NONE, F0, 0x03 },
  { KeyNames::F6, 0, NONE, F0, 0x0b },
  { KeyNames::F7, 0, NONE, F0, 0x83 },
  { KeyNames::F8, 0, NONE, F0, 0x0a },
  { KeyNames::F9, 0, NONE, F0, 0x01 },
  { KeyNames::F10, 0, NONE, F0, 0x09 },
  { KeyNames::F11, 0, NONE, F0, 0x78 },
  { KeyNames::F12, 0, NONE, F0, 0x07 },
  { KeyNames::Home, 0, NONE, F0, 0x6c },
  { KeyNames::Up, 0, NONE, F0, 0x75 },
  { KeyNames::PgUp, 0, NONE, F0, 0x7d },
  { KeyNames::Left, 0, NONE, F0, 0x6b },
  { KeyNames::Right, 0, NONE, F0, 0x74 },
  { KeyNames::End, 0, NONE, F0, 0x69 },
  { KeyNames::Down, 0, NONE, F0, 0x72 },
  { KeyNames::PgDn, 0, NONE, F0, 0x7a },
  { }
};

/*
 *  pckbc_add_code():
 *
 *  Adds a byte to the data queue.
 */
void pckbc_add_code(struct pckbc_data *d, int code, int port)
{
  fprintf(stderr, "[ pckbc: %s enqueue %02x ]\n", port ? "mouse" : "kbd", code);
	/*  Add at the head, read at the tail:  */
	d->head[port] = (d->head[port]+1) % MAX_8042_QUEUELEN;
	if (d->head[port] == d->tail[port])
		fatal("[ pckbc: queue overrun, port %i! ]\n", port);

	d->key_queue[port][d->head[port]] = code;
}

void send_pc_key(struct KeynameToPCKeyboard *table, int key, struct pckbc_data *d) {
  for (int i = 0; table[i].native_code; i++) {
    if (table[i].keyname_idx != key) {
      continue;
    }

    fprintf(stderr, "sending key %d -> %s\n", key, keynames[key]);
    for (int j = 0; table[i].make_prefix[j]; j++) {
      pckbc_add_code(d, table[i].make_prefix[j], 0);
    }
    pckbc_add_code(d, table[i].native_code, 0);
    for (int j = 0; table[i].break_prefix[j]; j++) {
      pckbc_add_code(d, table[i].break_prefix[j], 0);
    }
    int or_break_code = (table[i].high_bit_break) ? 0x80 : 0;
    pckbc_add_code(d, table[i].native_code | or_break_code, 0);
    break;
  }
}

/*
 *  pckbc_get_code():
 *
 *  Reads a byte from a data queue.
 */
int pckbc_get_code(struct pckbc_data *d, int port)
{
	if (d->head[port] == d->tail[port])
		fatal("[ pckbc: queue empty, port %i! ]\n", port);
	else
		d->tail[port] = (d->tail[port]+1) % MAX_8042_QUEUELEN;
	return d->key_queue[port][d->tail[port]];
}

/*
 *  ascii_to_scancodes_type3():
 *
 *  Conversion from ASCII codes to default (US) keyboard scancodes.
 *  (See http://www.computer-engineering.org/ps2keyboard/scancodes3.html)
 */
static void ascii_to_pc_scancodes_type3(int a, struct pckbc_data *d)
{
	int old_head;
	int p = 0;	/*  port  */
	int shift = 0, ctrl = 0;

  if (a >= 0x100) {
    int unshifted = a >> 8;
    send_pc_key(pc_type_3, unshifted, d);
    return;
  }

	if (a >= 'A' && a <= 'Z') { a += 32; shift = 1; }
	if ((a >= 1 && a <= 26) && (a!='\n' && a!='\t' && a!='\b' && a!='\r'))
		{ a += 96; ctrl = 1; }
	if (a=='!')  {	a = '1'; shift = 1; }
	if (a=='@')  {	a = '2'; shift = 1; }
	if (a=='#')  {	a = '3'; shift = 1; }
	if (a=='$')  {	a = '4'; shift = 1; }
	if (a=='%')  {	a = '5'; shift = 1; }
	if (a=='^')  {	a = '6'; shift = 1; }
	if (a=='&')  {	a = '7'; shift = 1; }
	if (a=='*')  {	a = '8'; shift = 1; }
	if (a=='(')  {	a = '9'; shift = 1; }
	if (a==')')  {	a = '0'; shift = 1; }
	if (a=='_')  {	a = '-'; shift = 1; }
	if (a=='+')  {	a = '='; shift = 1; }
	if (a=='{')  {	a = '['; shift = 1; }
	if (a=='}')  {	a = ']'; shift = 1; }
	if (a==':')  {	a = ';'; shift = 1; }
	if (a=='"')  {	a = '\''; shift = 1; }
	if (a=='|')  {	a = '\\'; shift = 1; }
	if (a=='<')  {	a = ','; shift = 1; }
	if (a=='>')  {	a = '.'; shift = 1; }
	if (a=='?')  {	a = '/'; shift = 1; }

	if (shift)
		pckbc_add_code(d, 0x12, p);
	if (ctrl)
		pckbc_add_code(d, 0x11, p);

	/*
	 *  Note: The ugly hack used to add release codes for all of these
	 *  keys is as follows:  we remember how much of the kbd buf that
	 *  is in use here, before we add any scancode. After we've added
	 *  one or more scancodes (ie an optional shift + another key)
	 *  then we add 0xf0 + the last scancode _if_ the kbd buf was altered.
	 */

	old_head = d->head[p];

  if (a=='\x1b') {
    pckbc_add_code(d, 0xf0, p);
    pckbc_add_code(d, 0x76, p);
    pckbc_add_code(d, 0xf0, p);
    pckbc_add_code(d, 0xe0, p);
    pckbc_add_code(d, 0x76, p);
  }
	if (a=='1')	pckbc_add_code(d, 0x16, p);
	if (a=='2')	pckbc_add_code(d, 0x1e, p);
	if (a=='3')	pckbc_add_code(d, 0x26, p);
	if (a=='4')	pckbc_add_code(d, 0x25, p);
	if (a=='5')	pckbc_add_code(d, 0x2e, p);
	if (a=='6')	pckbc_add_code(d, 0x36, p);
	if (a=='7')	pckbc_add_code(d, 0x3d, p);
	if (a=='8')	pckbc_add_code(d, 0x3e, p);
	if (a=='9')	pckbc_add_code(d, 0x46, p);
	if (a=='0')	pckbc_add_code(d, 0x45, p);
	if (a=='-')	pckbc_add_code(d, 0x4e, p);
	if (a=='=')	pckbc_add_code(d, 0x55, p);

	if (a=='\b')	pckbc_add_code(d, 0x29, p);

	if (a=='\t')	pckbc_add_code(d, 0x0d, p);
	if (a=='q')	pckbc_add_code(d, 0x15, p);
	if (a=='w')	pckbc_add_code(d, 0x1d, p);
	if (a=='e')	pckbc_add_code(d, 0x24, p);
	if (a=='r')	pckbc_add_code(d, 0x2d, p);
	if (a=='t')	pckbc_add_code(d, 0x2c, p);
	if (a=='y')	pckbc_add_code(d, 0x35, p);
	if (a=='u')	pckbc_add_code(d, 0x3c, p);
	if (a=='i')	pckbc_add_code(d, 0x43, p);
	if (a=='o')	pckbc_add_code(d, 0x44, p);
	if (a=='p')	pckbc_add_code(d, 0x4d, p);

	if (a=='[')	pckbc_add_code(d, 0x54, p);
	if (a==']')	pckbc_add_code(d, 0x5b, p);

	if (a=='\n' || a=='\r')	pckbc_add_code(d, 0x5a, p);

	if (a=='a')	pckbc_add_code(d, 0x1c, p);
	if (a=='s')	pckbc_add_code(d, 0x1b, p);
	if (a=='d')	pckbc_add_code(d, 0x23, p);
	if (a=='f')	pckbc_add_code(d, 0x2b, p);
	if (a=='g')	pckbc_add_code(d, 0x34, p);
	if (a=='h')	pckbc_add_code(d, 0x33, p);
	if (a=='j')	pckbc_add_code(d, 0x3b, p);
	if (a=='k')	pckbc_add_code(d, 0x42, p);
	if (a=='l')	pckbc_add_code(d, 0x4b, p);

	if (a==';')	pckbc_add_code(d, 0x4c, p);
	if (a=='\'')	pckbc_add_code(d, 0x52, p);
	if (a=='~')	pckbc_add_code(d, 0x29, p);
	if (a=='\\')	pckbc_add_code(d, 0x5d, p);

	if (a=='z')	pckbc_add_code(d, 0x1a, p);
	if (a=='x')	pckbc_add_code(d, 0x22, p);
	if (a=='c')	pckbc_add_code(d, 0x21, p);
	if (a=='v')	pckbc_add_code(d, 0x2a, p);
	if (a=='b')	pckbc_add_code(d, 0x32, p);
	if (a=='n')	pckbc_add_code(d, 0x31, p);
	if (a=='m')	pckbc_add_code(d, 0x3a, p);

	if (a==',')	pckbc_add_code(d, 0x41, p);
	if (a=='.')	pckbc_add_code(d, 0x49, p);
	if (a=='/')	pckbc_add_code(d, 0x4a, p);

	if (a==' ')	pckbc_add_code(d, 0x29, p);

	/*  Add release code, if a key was pressed:  */
	if (d->head[p] != old_head) {
		int code = d->key_queue[p][d->head[p]];
		pckbc_add_code(d, 0xf0, p);
		pckbc_add_code(d, code, p);
	}

	/*  Release shift and ctrl:  */
	if (shift) {
		pckbc_add_code(d, 0xf0, p);
		pckbc_add_code(d, 0x12, p);
	}
	if (ctrl) {
		pckbc_add_code(d, 0xf0, p);
		pckbc_add_code(d, 0x11, p);
	}
}

/*
 *  ascii_to_scancodes_type1():
 *
 *  Conversion from ASCII codes to default (US) XT keyboard scancodes.
 *  (See http://www.techhelpmanual.com/57-keyboard_scan_codes.html)
 *
 * Hex Dec Key │Hex Dec Key  │Hex Dec Key    │Hex Dec Key    │Hex Dec Key
 * ────────────┼─────────────┼───────────────┼───────────────┼─────────────────
 * 01   1  Esc │12  18  E    │23  35  H      │34  52  . >    │45  69  NumLock
 * 02   2  1 ! │13  19  R    │24  36  J      │35  53  / ?    │46  70  ScrollLck
 * 03   3  2 @ │14  20  T    │25  37  K      │36  54  Shft(R)│47  71  Home [7]
 * 04   4  3 # │15  21  Y    │26  38  L      │37  55  * PrtSc│48  72  ↑    [8]
 * 05   5  4 $ │16  22  U    │27  39  ; :    │38  56  Alt    │49  73  PgUp [9]
 * 06   6  5 % │17  23  I    │28  40  " '    │39  57  space  │4a  74  K -
 * 07   7  6 ^ │18  24  O    │29  41  ` ~    │3a  58  CapsLck│4b  75  ←    [4]
 * 08   8  7 & │19  25  P    │2a  42  Shft(L)│3b  59  F1     │4c  76       [5]
 * 09   9  8 * │1a  26  [ {  │2b  43  \ |    │3c  60  F2     │4d  77  →    [6]
 * 0a  10  9 ( │1b  27  ] }  │2c  44  Z      │3d  61  F3     │4e  78  K +
 * 0b  11  0 ) │1c  28  Enter│2d  45  X      │3e  62  F4     │4f  79  End  [1]
 * 0c  12  - _ │1d  29  Ctrl │2e  46  C      │3f  63  F5     │50  80  ↓    [2]
 * 0d  13  + = │1e  30  A    │2f  47  V      │40  64  F6     │51  81  PgDn [3]
 * 0e  14  bksp│1f  31  S    │30  48  B      │41  65  F7     │52  82  Ins  [0]
 * 0f  15  Tab │20  32  D    │31  49  N      │42  66  F8     │53  83  Del  [.]
 * 10  16  Q   │21  33  F    │32  50  M      │43  67  F9     │
 * 11  17  W   │22  34  G    │33  51  , <    │44  68  F10    │
 */
static void ascii_to_pc_scancodes_type1(int a, struct pckbc_data *d)
{
	int old_head;
	int p = 0;	/*  port  */
	int shift = 0, ctrl = 0;

  if (a >= 0x100) {
    int unshifted = a >> 8;
    send_pc_key(pc_type_1, unshifted, d);
    return;
  }

	if (a >= 'A' && a <= 'Z') { a += 32; shift = 1; }
	if ((a >= 1 && a <= 26) && (a!='\n' && a!='\t' && a!='\b' && a!='\r'))
		{ a += 96; ctrl = 1; }
	if (a=='!')  {	a = '1'; shift = 1; }
	if (a=='@')  {	a = '2'; shift = 1; }
	if (a=='#')  {	a = '3'; shift = 1; }
	if (a=='$')  {	a = '4'; shift = 1; }
	if (a=='%')  {	a = '5'; shift = 1; }
	if (a=='^')  {	a = '6'; shift = 1; }
	if (a=='&')  {	a = '7'; shift = 1; }
	if (a=='*')  {	a = '8'; shift = 1; }
	if (a=='(')  {	a = '9'; shift = 1; }
	if (a==')')  {	a = '0'; shift = 1; }
	if (a=='_')  {	a = '-'; shift = 1; }
	if (a=='+')  {	a = '='; shift = 1; }
	if (a=='{')  {	a = '['; shift = 1; }
	if (a=='}')  {	a = ']'; shift = 1; }
	if (a==':')  {	a = ';'; shift = 1; }
	if (a=='"')  {	a = '\''; shift = 1; }
	if (a=='|')  {	a = '\\'; shift = 1; }
	if (a=='<')  {	a = ','; shift = 1; }
	if (a=='>')  {	a = '.'; shift = 1; }
	if (a=='?')  {	a = '/'; shift = 1; }

	if (shift)
		pckbc_add_code(d, 0x2a, p);
	if (ctrl)
		pckbc_add_code(d, 0x1d, p);

	/*
	 *  Note: The ugly hack used to add release codes for all of these
	 *  keys is as follows:  we remember how much of the kbd buf that
	 *  is in use here, before we add any scancode. After we've added
	 *  one or more scancodes (ie an optional shift + another key)
	 *  then we add 0xf0 + the last scancode _if_ the kbd buf was altered.
	 */

	old_head = d->head[p];

  if (a=='\x1b') pckbc_add_code(d, 0x01, p);
  if (a=='1')	pckbc_add_code(d, 0x02, p);
	if (a=='2')	pckbc_add_code(d, 0x03, p);
	if (a=='3')	pckbc_add_code(d, 0x04, p);
	if (a=='4')	pckbc_add_code(d, 0x05, p);
	if (a=='5')	pckbc_add_code(d, 0x06, p);
	if (a=='6')	pckbc_add_code(d, 0x07, p);
	if (a=='7')	pckbc_add_code(d, 0x08, p);
	if (a=='8')	pckbc_add_code(d, 0x09, p);
	if (a=='9')	pckbc_add_code(d, 0x0a, p);
	if (a=='0')	pckbc_add_code(d, 0x0b, p);
	if (a=='-')	pckbc_add_code(d, 0x0c, p);
	if (a=='=')	pckbc_add_code(d, 0x0d, p);

	if (a=='\b')	pckbc_add_code(d, 0x0e, p);

	if (a=='\t')	pckbc_add_code(d, 0x0f, p);
	if (a=='q')	pckbc_add_code(d, 0x10, p);
	if (a=='w')	pckbc_add_code(d, 0x11, p);
	if (a=='e')	pckbc_add_code(d, 0x12, p);
	if (a=='r')	pckbc_add_code(d, 0x13, p);
	if (a=='t')	pckbc_add_code(d, 0x14, p);
	if (a=='y')	pckbc_add_code(d, 0x15, p);
	if (a=='u')	pckbc_add_code(d, 0x16, p);
	if (a=='i')	pckbc_add_code(d, 0x17, p);
	if (a=='o')	pckbc_add_code(d, 0x18, p);
	if (a=='p')	pckbc_add_code(d, 0x19, p);

	if (a=='[')	pckbc_add_code(d, 0x1a, p);
	if (a==']')	pckbc_add_code(d, 0x1b, p);

	if (a=='\n' || a=='\r')	pckbc_add_code(d, 0x1c, p);

	if (a=='a')	pckbc_add_code(d, 0x1e, p);
	if (a=='s')	pckbc_add_code(d, 0x1f, p);
	if (a=='d')	pckbc_add_code(d, 0x20, p);
	if (a=='f')	pckbc_add_code(d, 0x21, p);
	if (a=='g')	pckbc_add_code(d, 0x22, p);
	if (a=='h')	pckbc_add_code(d, 0x23, p);
	if (a=='j')	pckbc_add_code(d, 0x24, p);
	if (a=='k')	pckbc_add_code(d, 0x25, p);
	if (a=='l')	pckbc_add_code(d, 0x26, p);

	if (a==';')	pckbc_add_code(d, 0x27, p);
	if (a=='\'')	pckbc_add_code(d, 0x28, p);
	if (a=='~')	pckbc_add_code(d, 0x29, p);
	if (a=='\\')	pckbc_add_code(d, 0x2b, p);

	if (a=='z')	pckbc_add_code(d, 0x2c, p);
	if (a=='x')	pckbc_add_code(d, 0x2d, p);
	if (a=='c')	pckbc_add_code(d, 0x2e, p);
	if (a=='v')	pckbc_add_code(d, 0x2f, p);
	if (a=='b')	pckbc_add_code(d, 0x30, p);
	if (a=='n')	pckbc_add_code(d, 0x31, p);
	if (a=='m')	pckbc_add_code(d, 0x32, p);

	if (a==',')	pckbc_add_code(d, 0x33, p);
	if (a=='.')	pckbc_add_code(d, 0x34, p);
	if (a=='/')	pckbc_add_code(d, 0x35, p);

	if (a==' ')	pckbc_add_code(d, 0x39, p);

	/*  Add release code, if a key was pressed:  */
	if (d->head[p] != old_head) {
		int code = d->key_queue[p][d->head[p]];
		pckbc_add_code(d, code | 0x80, p);
	}

	/*  Release shift and ctrl:  */
	if (shift) {
		pckbc_add_code(d, 0xaa, p);
	}
	if (ctrl) {
		pckbc_add_code(d, 0x9d, p);
	}
}


/*
 *  ascii_to_scancodes_type2():
 *
 *  Conversion from ASCII codes to default (US) keyboard scancodes.
 *  (See http://www.win.tue.nl/~aeb/linux/kbd/scancodes-1.html)
 *
 *  NOTE/TODO: This seems to be type 2, not type 1.
 */
static void ascii_to_pc_scancodes_type2(int a, struct pckbc_data *d)
{
	int old_head;
	int p = 0;	/*  port  */
	int shift = 0, ctrl = 0;

  static int kpmode = 0;

  if (a >= 0x100) {
    int unshifted = a >> 8;
    send_pc_key(pc_type_2, unshifted, d);
    return;
  }

	if (a >= 'A' && a <= 'Z') { a += 32; shift = 1; }
	if ((a >= 1 && a <= 26) && (a!='\n' && a!='\t' && a!='\b' && a!='\r'))
		{ a += 96; ctrl = 1; }

	if (a=='!')  {	a = '1'; shift = 1; }
	if (a=='@')  {	a = '2'; shift = 1; }
	if (a=='#')  {	a = '3'; shift = 1; }
	if (a=='$')  {	a = '4'; shift = 1; }
	if (a=='%')  {	a = '5'; shift = 1; }
	if (a=='^')  {	a = '6'; shift = 1; }
	if (a=='&')  {	a = '7'; shift = 1; }
	if (a=='*')  {	a = '8'; shift = 1; }
	if (a=='(')  {	a = '9'; shift = 1; }
	if (a==')')  {	a = '0'; shift = 1; }
	if (a=='_')  {	a = '-'; shift = 1; }
	if (a=='+')  {	a = '='; shift = 1; }
	if (a=='{')  {	a = '['; shift = 1; }
	if (a=='}')  {	a = ']'; shift = 1; }
	if (a==':')  {	a = ';'; shift = 1; }
	if (a=='"')  {	a = '\''; shift = 1; }
	if (a=='|')  {	a = '\\'; shift = 1; }
	if (a=='<')  {	a = ','; shift = 1; }
	if (a=='>')  {	a = '.'; shift = 1; }
	if (a=='?')  {	a = '/'; shift = 1; }
	if (a=='~')  {	a = '`'; shift = 1; }

	if (shift)
    pckbc_add_code(d, 0x2a, p);
	else
    pckbc_add_code(d, 0x2a + 0x80, p);

	if (ctrl)
		pckbc_add_code(d, 0x1d, p);

	/*
	 *  Note: The ugly hack used to add release codes for all of these
	 *  keys is as follows:  we remember how much of the kbd buf that
	 *  is in use here, before we add any scancode. After we've added
	 *  one or more scancodes (ie an optional shift + another key)
	 *  then we duplicate the last scancode | 0x80 _if_ the kbd buf
	 *  was altered.
	 */

	old_head = d->head[p];

	if (a==27)	pckbc_add_code(d, 0x76, p);

	if (a=='1')	pckbc_add_code(d, 0x16, p);
	if (a=='2')	pckbc_add_code(d, 0x1e, p);
	if (a=='3')	pckbc_add_code(d, 0x26, p);
	if (a=='4')	pckbc_add_code(d, 0x25, p);
	if (a=='5')	pckbc_add_code(d, 0x2e, p);
	if (a=='6')	pckbc_add_code(d, 0x36, p);
	if (a=='7')	pckbc_add_code(d, 0x3d, p);
	if (a=='8')	pckbc_add_code(d, 0x3e, p);
	if (a=='9')	pckbc_add_code(d, 0x46, p);
	if (a=='0')	pckbc_add_code(d, 0x45, p);
	if (a=='-')	pckbc_add_code(d, 0x4e, p);
	if (a=='=')	pckbc_add_code(d, 0x55, p);

	if (a=='!')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x02, p); }
	if (a=='@')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x03, p); }
	if (a=='#')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x04, p); }
	if (a=='$')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x05, p); }
	if (a=='%')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x06, p); }
	if (a=='^')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x07, p); }
	if (a=='&')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x08, p); }
	if (a=='*')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x09, p); }
	if (a=='(')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x0a, p); }

	if (a=='\b')	pckbc_add_code(d, 0x0e, p);

	if (a=='\t')	pckbc_add_code(d, 0x0f, p);
	if (a=='q')	pckbc_add_code(d, 0x10, p);
	if (a=='w')	pckbc_add_code(d, 0x11, p);
	if (a=='e')	pckbc_add_code(d, 0x12, p);
	if (a=='r')	pckbc_add_code(d, 0x13, p);
	if (a=='t')	pckbc_add_code(d, 0x14, p);
	if (a=='y')	pckbc_add_code(d, 0x15, p);
	if (a=='u')	pckbc_add_code(d, 0x16, p);
	if (a=='i')	pckbc_add_code(d, 0x17, p);
	if (a=='o')	pckbc_add_code(d, 0x18, p);
	if (a=='p')	pckbc_add_code(d, 0x19, p);

	if (a=='[')	pckbc_add_code(d, 0x1a, p);
	if (a==']')	pckbc_add_code(d, 0x1b, p);

	if (a=='\n' || a=='\r')	pckbc_add_code(d, 0x1c, p);

	if (a=='a')	pckbc_add_code(d, 0x1e, p);
	if (a=='s')	pckbc_add_code(d, 0x1f, p);
	if (a=='d')	pckbc_add_code(d, 0x20, p);
	if (a=='f')	pckbc_add_code(d, 0x21, p);
	if (a=='g')	pckbc_add_code(d, 0x22, p);
	if (a=='h')	pckbc_add_code(d, 0x23, p);
	if (a=='j')	pckbc_add_code(d, 0x24, p);
	if (a=='k')	pckbc_add_code(d, 0x25, p);
	if (a=='l')	pckbc_add_code(d, 0x26, p);

	if (a==';')	pckbc_add_code(d, 0x27, p);
	if (a=='\'')	pckbc_add_code(d, 0x28, p);
	if (a=='`')	pckbc_add_code(d, 0x29, p);
	if (a=='\\')	pckbc_add_code(d, 0x2b, p);

	if (a=='z')	pckbc_add_code(d, 0x2c, p);
	if (a=='x')	pckbc_add_code(d, 0x2d, p);
	if (a=='c')	pckbc_add_code(d, 0x2e, p);
	if (a=='v')	pckbc_add_code(d, 0x2f, p);
	if (a=='b')	pckbc_add_code(d, 0x30, p);
	if (a=='n')	pckbc_add_code(d, 0x31, p);
	if (a=='m')	pckbc_add_code(d, 0x32, p);

	if (a==',')	pckbc_add_code(d, 0x33, p);
	if (a=='.')	pckbc_add_code(d, 0x34, p);
	if (a=='/')	pckbc_add_code(d, 0x35, p);

	if (a==' ')	pckbc_add_code(d, 0x39, p);

	/*  Add release code, if a key was pressed:  */
	if (d->head[p] != old_head) {
		int code = d->key_queue[p][d->head[p]] | 0x80;
		pckbc_add_code(d, code, p);
	}

	/*  Release ctrl:  */
	if (ctrl)
		pckbc_add_code(d, 0x1d + 0x80, p);
}


DEVICE_TICK(pckbc)
{
	struct pckbc_data *d = (struct pckbc_data *) extra;
	int port_nr, ch, ints_enabled;
  int mouse_x, mouse_y, mouse_but, fb_nr;

	ints_enabled = d->rx_int_enable;

  if (d->in_use && console_charavail(d->console_handle)) {
    ch = console_readchar(d->console_handle);
    if (d->translation_table == 1) {
      ascii_to_pc_scancodes_type1(ch, d);
    } else if (d->translation_table == 2) {
      ascii_to_pc_scancodes_type2(ch, d);
    } else {
      ascii_to_pc_scancodes_type3(ch, d);
    }
  }

  console_getmouse(&mouse_x, &mouse_y, &mouse_but, &fb_nr);
  if (d->mouse_ena &&
      ((d->mouse_last_x != mouse_x) ||
       (d->mouse_last_y != mouse_y) ||
       (d->mouse_last_but != mouse_but))
      ) {
    int diff_x = mouse_x - d->mouse_last_x;
    int diff_y = -(mouse_y - d->mouse_last_y);
    int flags =
      ((diff_x < 0) ? 0x10 : 0) |
      ((diff_y < 0) ? 0x20 : 0) |
      8 |
      (mouse_but & 7);
    if (d->mouse_last_x != -1) {
      pckbc_add_code(d, flags, 1);
      pckbc_add_code(d, diff_x, 1);
      pckbc_add_code(d, diff_y, 1);
      // pckbc_add_code(d, 0, 1);
      // pckbc_add_code(d, 0, 1);
    }

    d->mouse_last_x = mouse_x;
    d->mouse_last_y = mouse_y;
    d->mouse_last_but = mouse_but;
  }

	if (d->cmdbyte & KC8_KDISABLE)
		ints_enabled = 0;

	for (port_nr=0; port_nr<2; port_nr++) {
		/*  Cause receive interrupt, if there's something in the
		    receive buffer: (Otherwise deassert the interrupt.)  */

		if (d->head[port_nr] != d->tail[port_nr] && ints_enabled) {
			debug("[ pckbc: interrupt port %i ]\n", port_nr);
			if (port_nr == 0) {
				INTERRUPT_ASSERT(d->irq_keyboard);
      } else {
				INTERRUPT_ASSERT(d->irq_mouse);
      }
			d->currently_asserted[port_nr] = 1;
		} else {
			if (d->currently_asserted[port_nr]) {
				if (port_nr == 0)
					INTERRUPT_DEASSERT(d->irq_keyboard);
				else
					INTERRUPT_DEASSERT(d->irq_mouse);
			}
			d->currently_asserted[port_nr] = 0;
		}
	}
}


/*
 *  dev_pckbc_command():
 *
 *  Handle commands to the 8048 in the emulated keyboard.
 */
static void dev_pckbc_command(struct pckbc_data *d, int port_nr)
{
	int cmd = d->reg[PC_CMD];

	if (d->type == PCKBC_8242)
		cmd = d->reg[PS2_TXBUF];

  if (d->state == STATE_WAITING_FOR_LEDS) {
    debug("[ pckbc: leds: %x ]\n", cmd);
    d->state = STATE_NORMAL;
    return;
  }

	if (d->state == STATE_WAITING_FOR_TRANSLTABLE) {
		debug("[ pckbc: (port %i) switching to translation table "
		    "0x%02x ]\n", port_nr, cmd);
		switch (cmd) {
    case 1:
		case 2:
		case 3:	d->translation_table = cmd;
			break;
		default:fatal("[ pckbc: (port %i) translation table "
			    "0x%02x is NOT YET IMPLEMENTED ]\n",
			    port_nr, cmd);
		}
		pckbc_add_code(d, KBR_ACK, port_nr);
		d->state = STATE_NORMAL;
		return;
	}

	if (d->state == STATE_WAITING_FOR_RATE) {
		debug("[ pckbc: (port %i) received Typematic Rate data: "
		    "0x%02x ]\n", port_nr, cmd);
		pckbc_add_code(d, KBR_ACK, port_nr);
		d->state = STATE_NORMAL;
		return;
	}

	if (d->state == STATE_WAITING_FOR_ONEKEY_MB) {
		debug("[ pckbc: (port %i) received One-key make/break data: "
		    "0x%02x ]\n", port_nr, cmd);
		pckbc_add_code(d, KBR_ACK, port_nr);
		d->state = STATE_NORMAL;
		return;
	}

	if (d->state == STATE_WAITING_FOR_AUX_OUT) {
    port_nr = 1;
    debug("[ pckbc: (port %i) sending to mouse: "
          "0x%02x cmd %02x ]\n", port_nr, cmd, d->mouse_cmd);
    switch (d->mouse_cmd) {
    case 0:
      switch (cmd) {
      case 0xff:
        d->mouse_cmd = 0;
        d->mouse_sample_rate = 100;
        d->mouse_resolution = 2;
        d->mouse_scaling = 0;
        d->mouse_remote = 0;
        d->mouse_ena = 1;

        pckbc_add_code(d, 0xfa, port_nr);
        pckbc_add_code(d, 0xaa, port_nr);
        pckbc_add_code(d, 0, port_nr);
        break;

      case 0xf5:
        d->mouse_reporting = 0;
        pckbc_add_code(d, 0xfa, port_nr);
        break;

      case 0xf4:
        d->mouse_reporting = 1;
        pckbc_add_code(d, 0xfa, port_nr);
        break;

      case 0xf3:
        d->mouse_cmd = 0xf3;
        pckbc_add_code(d, 0xfa, port_nr);
        break;

      case 0xf2:
        pckbc_add_code(d, 0xfa, port_nr);
        pckbc_add_code(d, 0, port_nr);
        break;

      case 0xe2:
        pckbc_add_code(d, 0xfa, port_nr);
        d->mouse_cmd = 0xe2;
        break;

      case 0xe6:
        d->mouse_scaling = 0;
        pckbc_add_code(d, 0xfa, port_nr);
        break;

      case 0xe7:
        d->mouse_scaling = 1;
        pckbc_add_code(d, 0xfa, port_nr);
        break;

      case 0xe8:
        pckbc_add_code(d, 0xfa, port_nr);
        d->mouse_cmd = 0xe8;
        break;

      case 0xe9:
        pckbc_add_code(d, 0xfa, port_nr);
        pckbc_add_code(d, d->mouse_reporting << 6 | d->mouse_reporting << 5 | d->mouse_scaling << 4, port_nr);
        pckbc_add_code(d, d->mouse_resolution, port_nr);
        pckbc_add_code(d, d->mouse_sample_rate, port_nr);
        break;

      case 0xeb:
        pckbc_add_code(d, 0xfa, port_nr);
        pckbc_add_code(d, 0, port_nr);
        pckbc_add_code(d, 0, port_nr);
        pckbc_add_code(d, 0, port_nr);
        break;
      }
      break;

    case 0xf3:
      d->mouse_cmd = 0;
      d->mouse_sample_rate = cmd;
      pckbc_add_code(d, 0xfa, port_nr);
      break;

    case 0xe8:
      d->mouse_cmd = 0;
      d->mouse_resolution = cmd;
      pckbc_add_code(d, 0xfa, port_nr);
      break;

    case 0xe2:
      switch (cmd) {
      case 0x40:
        d->mouse_cmd = 0;
        d->mouse_remote = 0;
        pckbc_add_code(d, 0xfa, port_nr);
        break;

      case 0x41:
        d->mouse_cmd = 0;
        d->mouse_remote = 1;
        pckbc_add_code(d, 0xfa, port_nr);
        break;

      default:
        d->mouse_cmd = 0;
        pckbc_add_code(d, 0xfa, port_nr);
        break;
      }
      break;

    default:
      pckbc_add_code(d, 0xfa, port_nr);
      d->mouse_cmd = 0;
      break;
    }

		d->state = STATE_NORMAL;
		return;
	}

	switch (cmd) {
	case 0x00:
		/*
		 *  TODO: What does this do? This is possibly due to an
		 *  error in the handling of some other command code.
		 */
		pckbc_add_code(d, KBR_ACK, port_nr);
		break;

	case KBC_MODEIND:	/*  Set LEDs  */
		/*  Just ACK, no LEDs are actually set.  */
    d->state = STATE_WAITING_FOR_LEDS;
		pckbc_add_code(d, KBR_ACK, port_nr);
		break;

	case KBC_SETTABLE:
		pckbc_add_code(d, KBR_ACK, port_nr);
		d->state = STATE_WAITING_FOR_TRANSLTABLE;
		break;

	case KBC_ENABLE:
		d->keyscanning_enabled = 1;
		pckbc_add_code(d, KBR_ACK, port_nr);
		break;

	case KBC_DISABLE:
		d->keyscanning_enabled = 0;
		pckbc_add_code(d, KBR_ACK, port_nr);
		break;

	case KBC_SETDEFAULT:
		pckbc_add_code(d, KBR_ACK, port_nr);
		break;

	case KBC_GETID:
		/*  Get keyboard ID.  NOTE/TODO: Ugly hardcoded answer.  */
		pckbc_add_code(d, KBR_ACK, port_nr);
		pckbc_add_code(d, 0xab, port_nr);
		pckbc_add_code(d, 0x83, port_nr);
		break;

	case KBC_TYPEMATIC:
		/*  Set typematic (auto-repeat) delay/speed:  */
		pckbc_add_code(d, KBR_ACK, port_nr);
		d->state = STATE_WAITING_FOR_RATE;
		break;

	case KBC_ALLKEYS_TMB:
		/*  "Make all keys typematic/make/break"  */
		pckbc_add_code(d, KBR_ACK, port_nr);
		break;

	case KBC_ONEKEY_MB:
		/*  "Make one key typematic/make/break"  */
		pckbc_add_code(d, KBR_ACK, port_nr);
		d->state = STATE_WAITING_FOR_ONEKEY_MB;
		break;

	case KBC_RESET:
		pckbc_add_code(d, KBR_ACK, port_nr);
		pckbc_add_code(d, KBR_RSTDONE, port_nr);
		/*
		 *  Disable interrupts during reset, or Linux 2.6
		 *  prints warnings about spurious interrupts.
		 */
		d->rx_int_enable = 0;
		break;

	default:
		fatal("[ pckbc: (port %i) UNIMPLEMENTED 8048 command"
		    " 0x%02x ]\n", port_nr, cmd);
		// exit(1);
    break;
	}
}


DEVICE_ACCESS(pckbc)
{
	struct pckbc_data *d = (struct pckbc_data *) extra;
	uint64_t idata = 0, odata = 0;
	int port_nr = 0;
	size_t i;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

#ifdef PCKBC_DEBUG
	if (writeflag == MEM_WRITE)
		fatal("[ pckbc: write to addr 0x%x: 0x%x ]\n",
		    (int)relative_addr, (int)idata);
	else
		fatal("[ pckbc: read from addr 0x%x ]\n",
		    (int)relative_addr);
#endif

	/*  For JAZZ-based machines:  */
	if (relative_addr >= 0x60) {
		relative_addr -= 0x60;
		if (relative_addr != 0)
			relative_addr = 1;
	} else if (d->type == PCKBC_8242) {
		/*  8242 PS2-style:  */
		/*  when using 8-byte alignment...  */
		relative_addr /= sizeof(uint64_t);
		/*  port_nr = 0 for keyboard, 1 for mouse  */
		port_nr = (relative_addr >> 2);
		relative_addr &= 3;
		relative_addr += PS2;
	} else if (d->pc_style_flag) {
		/*  PC-style:  */
		if (relative_addr != 0 && relative_addr != 4) {
			/*  TODO (port 0x61)  */
			odata = 0x21;
      {
static int x = 0;
x++;
if (x&1)
			odata ^= 0x10;
}
			if (writeflag == MEM_READ)
				memory_writemax64(cpu, data, len, odata);
			return 1;
		}
		if (relative_addr != 0)
			relative_addr = 1;
	} else {
		/*  Others... Non-Jazz ARC-based machines etc.  */
		if (relative_addr != 0)
			relative_addr = 1;
	}

	switch (relative_addr) {

	/*
	 *  8042 (PC):
	 */

	case 0:		/*  data  */
		if (writeflag==MEM_READ) {
			switch (d->state) {
			case STATE_RDCMDBYTE:
				odata = d->cmdbyte;
				d->state = STATE_NORMAL;
				break;
			case STATE_RDOUTPUT:
				odata = d->output_byte;
				d->state = STATE_NORMAL;
				break;

			default: if (d->head[1] != d->tail[1]) {
          odata = pckbc_get_code(d, 1);
        } else if (d->head[0] != d->tail[0]) {
					odata = pckbc_get_code(d, 0);
					d->last_scancode = odata;
				} else {
					odata = d->last_scancode;
					d->last_scancode |= 0x80;
				}
			}
			/*  debug("[ pckbc: read from DATA: 0x%02x ]\n",
			    (int)odata);  */
		} else {
			debug("[ pckbc: write to DATA:");
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");

			switch (d->state) {
			case STATE_LDCMDBYTE:
				d->cmdbyte = idata;
				d->rx_int_enable = d->cmdbyte &
				    (KC8_KENABLE | KC8_MENABLE) ? 1 : 0;
				d->state = STATE_NORMAL;
				break;
			case STATE_LDOUTPUT:
				d->output_byte = idata;
				d->state = STATE_NORMAL;
				break;
			default:d->reg[relative_addr] = idata;
				dev_pckbc_command(d, port_nr);
        break;
			}
		}
		break;
	case 1:		/*  control  */
		if (writeflag==MEM_READ) {
			dev_pckbc_tick(cpu, d);

			odata = 0;

			/*  "Data in buffer" bit  */
      if (d->head[1] != d->tail[1]) {
        fprintf(stderr, "[ pckbc: output ring %d-%d ]\n", d->head[1], d->tail[1]);
        odata |= KBS_DIB | 8;
      } else if (d->head[0] != d->tail[0] ||
                 d->state == STATE_RDCMDBYTE ||
                 d->state == STATE_RDOUTPUT) {
				odata |= KBS_DIB;
      }

			if (d->state == STATE_RDCMDBYTE)
				odata |= KBS_OCMD;

			odata |= KBS_NOSEC;
      if (odata != 0x10) {
        debug("[ pckbc: read from CTL status port: "
              "0x%02x ]\n", (int)odata);
      }
		} else {
			debug("[ pckbc: write to CTL:");
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[relative_addr] = idata;

			switch (idata) {
			case 0x10:
			case 0x11:
				/*  TODO: For now, don't print warnings about
				    these. NetBSD sends these.  */
				break;
			case K_RDCMDBYTE:
				d->state = STATE_RDCMDBYTE;
				break;
			case K_LDCMDBYTE:
				d->state = STATE_LDCMDBYTE;
				break;
			case 0xa7:
				d->cmdbyte |= KC8_MDISABLE;
				break;
			case 0xa8:
				d->cmdbyte &= ~KC8_MDISABLE;
        pckbc_add_code(d, 0xfa, 0);
				break;
			case 0xa9:	/*  test auxiliary port  */
        pckbc_add_code(d, 0x00, 0);
				break;
			case 0xaa:	/*  keyboard self-test  */
				pckbc_add_code(d, 0x55, port_nr);
				break;
			case 0xab:	/*  keyboard interface self-test  */
				pckbc_add_code(d, 0x00, port_nr);
				break;
			case 0xad:
				d->cmdbyte |= KC8_KDISABLE;
				break;
			case 0xae:
				d->cmdbyte &= ~KC8_KDISABLE;
				break;
			case 0xd0:
				d->state = STATE_RDOUTPUT;
				break;
			case 0xd1:
				d->state = STATE_LDOUTPUT;
				break;
			case 0xd3:	/*  write to auxiliary device
					    output buffer  */
				debug("[ pckbc: send data to kbd ]\n");
				d->state = STATE_WAITING_FOR_AUX;
				break;
			case 0xd4:	/*  write to auxiliary port  */
				debug("[ pckbc: send data to mouse ]\n");
				d->state = STATE_WAITING_FOR_AUX_OUT;
				break;
			default:
				fatal("[ pckbc: unknown CONTROL 0x%x ]\n",
				    (int)idata);
				d->state = STATE_NORMAL;
			}
		}
		break;

	/*
	 *  8242 (PS2):
	 */

	case PS2 + PS2_TXBUF:
		if (writeflag==MEM_READ) {
			odata = random() & 0xff;
			debug("[ pckbc: read from port %i, PS2_TXBUF: "
			    "0x%x ]\n", port_nr, (int)odata);
		} else {
			debug("[ pckbc: write to port %i, PS2_TXBUF: "
			    "0x%llx ]\n", port_nr, (long long)idata);

			/*  Handle keyboard commands:  */
			d->reg[PS2_TXBUF] = idata;
			dev_pckbc_command(d, port_nr);
		}
		break;

	case PS2 + PS2_RXBUF:
		if (writeflag==MEM_READ) {
			/*  TODO: What should be returned if no data 
			    is available?  */
			odata = random() & 0xff;
			if (d->head[port_nr] != d->tail[port_nr])
				odata = pckbc_get_code(d, port_nr);
			debug("[ pckbc: read from port %i, PS2_RXBUF: "
			    "0x%02x ]\n", port_nr, (int)odata);
		} else {
			debug("[ pckbc: write to port %i, PS2_RXBUF: "
			    "0x%llx ]\n", port_nr, (long long)idata);
		}
		break;

	case PS2 + PS2_CONTROL:
		if (writeflag==MEM_READ) {
			debug("[ pckbc: read from port %i, PS2_CONTROL"
			    " ]\n", port_nr);
		} else {
			debug("[ pckbc: write to port %i, PS2_CONTROL:"
			    " 0x%llx ]\n", port_nr, (long long)idata);
			d->clocksignal = (idata & 0x10) ? 1 : 0;
			d->rx_int_enable = (idata & 0x08) ? 1 : 0;
			d->tx_int_enable = (idata & 0x04) ? 1 : 0;
		}
		break;

	case PS2 + PS2_STATUS:
		if (writeflag==MEM_READ) {
			/* 0x08 = transmit buffer empty  */
			odata = d->clocksignal + 0x08;

			if (d->head[port_nr] != d->tail[port_nr]) {
				/*  0x10 = receicer data available (?)  */
				odata |= 0x10;
			}

			debug("[ pckbc: read from port %i, PS2_STATUS: "
			    "0x%llx ]\n", port_nr, (long long)odata);
		} else {
			debug("[ pckbc: write to port %i, PS2_STATUS: "
			    "0x%llx ]\n", port_nr, (long long)idata);
		}
		break;

	default:
		if (writeflag==MEM_READ) {
			debug("[ pckbc: read from unimplemented reg %i ]\n",
			    (int)relative_addr);
			odata = d->reg[relative_addr % DEV_PCKBC_LENGTH];
		} else {
			debug("[ pckbc: write to unimplemented reg %i:",
			    (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[relative_addr % DEV_PCKBC_LENGTH] = idata;
		}
	}

	/*  SGI? TODO: fix  */
	if (len == 8)
		odata |= (odata << 8) | (odata << 16) | (odata << 24) |
		    (odata << 32) | (odata << 40) | (odata << 48) |
		    (odata << 56);

	if (writeflag == MEM_READ) {
		memory_writemax64(cpu, data, len, odata);
  }

	dev_pckbc_tick(cpu, d);

	return 1;
}


/*
 *  dev_pckbc_init():
 *
 *  Type should be PCKBC_8042 or PCKBC_8242.
 */
int dev_pckbc_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr, int type, char *keyboard_irqpath,
	char *mouse_irqpath, int in_use, int pc_style_flag)
{
	struct pckbc_data *d;
	int len = DEV_PCKBC_LENGTH;

	CHECK_ALLOCATION(d = (struct pckbc_data *) malloc(sizeof(struct pckbc_data)));
	memset(d, 0, sizeof(struct pckbc_data));

	if (type == PCKBC_8242)
		len = 0x18;

	if (type == PCKBC_JAZZ) {
		type = PCKBC_8042;
		len = DEV_PCKBC_LENGTH + 0x60;
	}

	INTERRUPT_CONNECT(keyboard_irqpath, d->irq_keyboard);
	INTERRUPT_CONNECT(mouse_irqpath, d->irq_mouse);

	d->type              = type;
	d->in_use            = in_use;
	d->pc_style_flag     = pc_style_flag;
	d->translation_table = 3;
	d->rx_int_enable     = 1;
	d->output_byte       = 0x02;	/*  A20 enable on PCs  */

  d->flex_id           = -1;
  d->mouse_last_x      = -1;
  d->mouse_last_y      = -1;
  d->mouse_last_but    = -1;

	d->console_handle = console_start_slave_inputonly(
	    machine, "pckbc", d->in_use);

	memory_device_register(mem, "pckbc", baseaddr,
	    len, dev_pckbc_access, d, DM_DEFAULT, NULL);
	machine_add_tickfunction(machine, dev_pckbc_tick, d,
	    PCKBC_TICKSHIFT);

	return d->console_handle;
}

