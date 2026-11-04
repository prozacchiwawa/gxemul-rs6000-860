#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <deque>

#include "console.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "debugger.h"

#include "thirdparty/kbdreg.h"

std::deque<keyboard_event_t> keyboard_debug_events;

#define	PCKBC_TICKSHIFT		11

#define STATE_NORMAL 0
#define	STATE_WAITING_FOR_TRANSLTABLE	3
#define	STATE_WAITING_FOR_RATE		4
#define	STATE_WAITING_FOR_ONEKEY_MB	5
#define STATE_WAITING_FOR_LEDS 10

struct pckeyboard_data {
  int   state;
  int   console_handle;
	int		keyscanning_enabled;
	int		translation_table;
	int		last_scancode;
};

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
  { KeyNames::F1, 0, NONE, F0, 0x3b },
  { KeyNames::F2, 0, NONE, F0, 0x3c },
  { KeyNames::F3, 0, NONE, F0, 0x3d },
  { KeyNames::F4, 0, NONE, F0, 0x3e },
  { KeyNames::F5, 0, NONE, F0, 0x3f },
  { KeyNames::F6, 0, NONE, F0, 0x40 },
  { KeyNames::F7, 0, NONE, F0, 0x41 },
  { KeyNames::F8, 0, NONE, F0, 0x42 },
  { KeyNames::F9, 0, NONE, F0, 0x43 },
  { KeyNames::F10, 0, NONE, F0, 0x44 },
  { KeyNames::F11, 0, NONE, F0, 0x57 },
  { KeyNames::F12, 0, NONE, F0, 0x58 },
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

// https://www.scs.stanford.edu/10wi-cs140/pintos/specs/kbd/scancodes-9.html
// 9.6 Table, Set 2
KeynameToPCKeyboard pc_type_2[] = {
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
  { KeyNames::ESC, 0, NONE, F0, 0x76 },
  { KeyNames::N1, 0, NONE, F0, 0x16 },
  { KeyNames::N2, 0, NONE, F0, 0x1e },
  { KeyNames::N3, 0, NONE, F0, 0x26 },
  { KeyNames::N4, 0, NONE, F0, 0x25 },
  { KeyNames::N5, 0, NONE, F0, 0x2e },
  { KeyNames::N6, 0, NONE, F0, 0x36 },
  { KeyNames::N7, 0, NONE, F0, 0x3d },
  { KeyNames::N8, 0, NONE, F0, 0x4e },
  { KeyNames::N9, 0, NONE, F0, 0x46 },
  { KeyNames::N0, 0, NONE, F0, 0x45 },
  { KeyNames::Minus, 0, NONE, F0, 0x4e },
  { KeyNames::Equals, 0, NONE, F0, 0x55 },
  { KeyNames::Backspace, 0, NONE, F0, 0x66 },
  { KeyNames::Tab, 0, NONE, F0, 0x0d },
  { KeyNames::Q, 0, NONE, F0, 0x15 },
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
  { KeyNames::A, 0, NONE, F0, 0x1c },
  { KeyNames::S, 0, NONE, F0, 0x1b },
  { KeyNames::D, 0, NONE, F0, 0x23 },
  { KeyNames::F, 0, NONE, F0, 0x2b },
  { KeyNames::G, 0, NONE, F0, 0x34 },
  { KeyNames::H, 0, NONE, F0, 0x33 },
  { KeyNames::J, 0, NONE, F0, 0x3b },
  { KeyNames::K, 0, NONE, F0, 0x42 },
  { KeyNames::L, 0, NONE, F0, 0x4b },
  { KeyNames::Semicolon, 0, NONE, F0, 0x4c },
  { KeyNames::Quote, 0, NONE, F0, 0x52 },
  { KeyNames::Backquote, 0, NONE, F0, 0x0e },
  { KeyNames::LShift, 0, NONE, F0, 0x12 },
  { KeyNames::Backslash, 0, NONE, F0, 0x5d },
  { KeyNames::Z, 0, NONE, F0, 0x1a },
  { KeyNames::X, 0, NONE, F0, 0x22 },
  { KeyNames::C, 0, NONE, F0, 0x21 },
  { KeyNames::V, 0, NONE, F0, 0x2a },
  { KeyNames::B, 0, NONE, F0, 0x32 },
  { KeyNames::N, 0, NONE, F0, 0x31 },
  { KeyNames::M, 0, NONE, F0, 0x3a },
  { KeyNames::Comma, 0, NONE, F0, 0x41 },
  { KeyNames::Dot, 0, NONE, F0, 0x49 },
  { KeyNames::Slash, 0, NONE, F0, 0x4a },
  { KeyNames::RShift, 0, NONE, F0, 0x59 },
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
  { KeyNames::F1, 0, NONE, F0, 0x07 },
  { KeyNames::F2, 0, NONE, F0, 0x0f },
  { KeyNames::F3, 0, NONE, F0, 0x17 },
  { KeyNames::F4, 0, NONE, F0, 0x1f },
  { KeyNames::F5, 0, NONE, F0, 0x27 },
  { KeyNames::F6, 0, NONE, F0, 0x2f },
  { KeyNames::F7, 0, NONE, F0, 0x37 },
  { KeyNames::F8, 0, NONE, F0, 0x3f },
  { KeyNames::F9, 0, NONE, F0, 0x47 },
  { KeyNames::F10, 0, NONE, F0, 0x4f },
  { KeyNames::F11, 0, NONE, F0, 0x56 },
  { KeyNames::F12, 0, NONE, F0, 0x5e },
  { KeyNames::ESC, 0, NONE, F0, 0x08 },
  { KeyNames::N1, 0, NONE, F0, 0x16 },
  { KeyNames::N2, 0, NONE, F0, 0x1e },
  { KeyNames::N3, 0, NONE, F0, 0x26 },
  { KeyNames::N4, 0, NONE, F0, 0x25 },
  { KeyNames::N5, 0, NONE, F0, 0x2e },
  { KeyNames::N6, 0, NONE, F0, 0x36 },
  { KeyNames::N7, 0, NONE, F0, 0x3d },
  { KeyNames::N8, 0, NONE, F0, 0x3e },
  { KeyNames::N9, 0, NONE, F0, 0x46 },
  { KeyNames::N0, 0, NONE, F0, 0x45 },
  { KeyNames::Minus, 0, NONE, F0, 0x4e },
  { KeyNames::Equals, 0, NONE, F0, 0x55 },
  { KeyNames::Backspace, 0, NONE, F0, 0x66 },
  { KeyNames::Tab, 0, NONE, F0, 0x0d },
  { KeyNames::Q, 0, NONE, F0, 0x15 },
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
  { KeyNames::Backslash, 0, NONE, F0, 0x5c },
  { KeyNames::Return, 0, NONE, F0, 0x5a },
  { KeyNames::Ctrl, 0, NONE, F0, 0x11 },
  { KeyNames::A, 0, NONE, F0, 0x1c },
  { KeyNames::S, 0, NONE, F0, 0x1b },
  { KeyNames::D, 0, NONE, F0, 0x23 },
  { KeyNames::F, 0, NONE, F0, 0x2b },
  { KeyNames::G, 0, NONE, F0, 0x34 },
  { KeyNames::H, 0, NONE, F0, 0x33 },
  { KeyNames::J, 0, NONE, F0, 0x3b },
  { KeyNames::K, 0, NONE, F0, 0x42 },
  { KeyNames::L, 0, NONE, F0, 0x4b },
  { KeyNames::Semicolon, 0, NONE, F0, 0x4c },
  { KeyNames::Quote, 0, NONE, F0, 0x52 },
  { KeyNames::Backquote, 0, NONE, F0, 0x0e },
  { KeyNames::LShift, 0, NONE, F0, 0x12 },
  { KeyNames::Z, 0, NONE, F0, 0x1a },
  { KeyNames::X, 0, NONE, F0, 0x22 },
  { KeyNames::C, 0, NONE, F0, 0x21 },
  { KeyNames::V, 0, NONE, F0, 0x2a },
  { KeyNames::B, 0, NONE, F0, 0x32 },
  { KeyNames::N, 0, NONE, F0, 0x31 },
  { KeyNames::M, 0, NONE, F0, 0x3a },
  { KeyNames::Comma, 0, NONE, F0, 0x41 },
  { KeyNames::Dot, 0, NONE, F0, 0x49 },
  { KeyNames::Slash, 0, NONE, F0, 0x4a },
  { KeyNames::RShift, 0, NONE, F0, 0x59 },
  { KeyNames::Space, 0, NONE, F0, 0x29 },
  { KeyNames::CapsLock, 0, NONE, F0, 0x14 },
  { KeyNames::Home, 0, NONE, F0, 0x6e },
  { KeyNames::Up, 0, NONE, F0, 0x63 },
  { KeyNames::Down, 0, NONE, F0, 0x60 },
  { KeyNames::PgUp, 0, NONE, F0, 0x6f },
  { KeyNames::PgDn, 0, NONE, F0, 0x6d },
  { KeyNames::Left, 0, NONE, F0, 0x61 },
  { KeyNames::Right, 0, NONE, F0, 0x6a },
  { KeyNames::End, 0, NONE, F0, 0x65 },
  { }
};

typedef struct {
  int type;
  KeynameToPCKeyboard *table;
} KeynameToPCKeyboardTable;

KeynameToPCKeyboardTable translation_tables[] = {
  { 1, pc_type_1 },
  { 2, pc_type_2 },
  { 3, pc_type_3 },
  { }
};

/*
 *  pckbc_add_code():
 *
 *  Adds a byte to the data queue.
 */
static void pckbc_add_code(struct cpu *cpu, struct pckeyboard_data *d, uint8_t code)
{
  cpu->memory_rw
    (cpu,
     cpu->mem,
     I8042_PRIVATE,
     &code,
     sizeof(code),
     MEM_WRITE,
     PHYSICAL | NO_EXCEPTIONS);
}

static void pckbc_reset_interface(struct cpu *cpu)
{
  uint8_t cmd = I8042_ACTION_RESET_KBD;
  cpu->memory_rw
    (cpu,
     cpu->mem,
     I8042_PRIVATE + I8042_ACTION,
     &cmd,
     sizeof(cmd),
     MEM_WRITE,
     PHYSICAL | NO_EXCEPTIONS);
}

/*
 *  dev_pckbc_command():
 *
 *  Handle commands to the 8048 in the emulated keyboard.
 */
static void dev_pckbc_command(struct cpu *cpu, struct pckeyboard_data *d, uint8_t cmd) {
  if (d->state == STATE_WAITING_FOR_LEDS) {
    debug("[ pckbc: leds: %x ]\n", cmd);
    d->state = STATE_NORMAL;
		pckbc_add_code(cpu, d, KBR_ACK);
    return;
  }

	if (d->state == STATE_WAITING_FOR_TRANSLTABLE) {
		debug("[ pckbc: switching to translation table "
		    "0x%02x ]\n", cmd);
		switch (cmd) {
    case 1:
		case 2:
		case 3:	d->translation_table = cmd;
			break;
		default:fatal("[ pckbc: translation table "
			    "0x%02x is NOT YET IMPLEMENTED ]\n",
			    cmd);
		}
		pckbc_add_code(cpu, d, KBR_ACK);
		d->state = STATE_NORMAL;
		return;
	}

	if (d->state == STATE_WAITING_FOR_RATE) {
		debug("[ pckbc: received Typematic Rate data: "
		    "0x%02x ]\n", cmd);
		pckbc_add_code(cpu, d, KBR_ACK);
		d->state = STATE_NORMAL;
		return;
	}

	if (d->state == STATE_WAITING_FOR_ONEKEY_MB) {
		debug("[ pckbc: received One-key make/break data: "
		    "0x%02x ]\n", cmd);
		pckbc_add_code(cpu, d, KBR_ACK);
		d->state = STATE_NORMAL;
		return;
	}

	switch (cmd) {
	case 0x00:
		/*
		 *  TODO: What does this do? This is possibly due to an
		 *  error in the handling of some other command code.
		 */
		pckbc_add_code(cpu, d, KBR_ACK);
		break;

  case 0x11:
    pckbc_add_code(cpu, d, 0xfa);
    break;

  case 0x12:
    pckbc_add_code(cpu, d, 0xfa);
    break;

  case 0x14:
    pckbc_add_code(cpu, d, 0xfa);
    break;

  case 0x19:
    pckbc_add_code(cpu, d, 0xfa);
    break;

  case 0x21:
    pckbc_add_code(cpu, d, 0xfa);
    break;

  case 0x58:
    pckbc_add_code(cpu, d, 0xfa);
    break;

  case 0x59:
    pckbc_add_code(cpu, d, 0xfa);
    break;

  case 0x76:
    pckbc_add_code(cpu, d, 0xfa);
    break;

	case KBC_MODEIND:	/*  Set LEDs  */
		/*  Just ACK, no LEDs are actually set.  */
    d->state = STATE_WAITING_FOR_LEDS;
		pckbc_add_code(cpu, d, KBR_ACK);
		break;

	case KBC_SETTABLE:
		pckbc_add_code(cpu, d, KBR_ACK);
		d->state = STATE_WAITING_FOR_TRANSLTABLE;
		break;

	case KBC_ENABLE:
		d->keyscanning_enabled = 1;
		pckbc_add_code(cpu, d, KBR_ACK);
		break;

	case KBC_DISABLE:
		d->keyscanning_enabled = 0;
		pckbc_add_code(cpu, d, KBR_ACK);
		break;

	case KBC_SETDEFAULT:
		pckbc_add_code(cpu, d, KBR_ACK);
		break;

	case KBC_GETID:
		/*  Get keyboard ID.  NOTE/TODO: Ugly hardcoded answer.  */
		// pckbc_add_code(cpu, d, KBR_ACK);
		pckbc_add_code(cpu, d, 0xab);
		pckbc_add_code(cpu, d, 0x83);
		break;

	case KBC_TYPEMATIC:
		/*  Set typematic (auto-repeat) delay/speed:  */
		pckbc_add_code(cpu, d, KBR_ACK);
		d->state = STATE_WAITING_FOR_RATE;
		break;

	case KBC_ALLKEYS_TMB:
		/*  "Make all keys typematic/make/break"  */
		pckbc_add_code(cpu, d, KBR_ACK);
		break;

	case KBC_ONEKEY_MB:
		/*  "Make one key typematic/make/break"  */
		pckbc_add_code(cpu, d, KBR_ACK);
		d->state = STATE_WAITING_FOR_ONEKEY_MB;
		break;

  case KBC_GETTABLE:
		pckbc_add_code(cpu, d, 0xfa);
		pckbc_add_code(cpu, d, 0xb1);
    pckbc_add_code(cpu, d, 0xbf);
    break;

	case KBC_RESET:
    pckbc_reset_interface(cpu);
    pckbc_add_code(cpu, d, KBR_ACK);
    pckbc_add_code(cpu, d, KBR_RSTDONE);
    pckbc_add_code(cpu, d, 0xb2);

		/*
		 *  Disable interrupts during reset, or Linux 2.6
		 *  prints warnings about spurious interrupts.
		 */
		break;

	default:
		fatal("[ pckbc: UNIMPLEMENTED 8048 command"
		    " 0x%02x ]\n", cmd);
		// exit(1);
    break;
	}
}

DEVICE_ACCESS(pckeyboard) {
	struct pckeyboard_data *d = (struct pckeyboard_data *) extra;
  uint64_t idata = 0;

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
  }

  dev_pckbc_command(cpu, d, idata);

  if (writeflag == MEM_READ) {
    memory_writemax64(cpu, data, len, idata);
  }

  return 1;
}

DEVICE_TICK(pckeyboard)
{
	struct pckeyboard_data *d = (struct pckeyboard_data *) extra;
	int key, release;
  KeynameToPCKeyboard *table = nullptr;
  KeynameToPCKeyboard *found = nullptr;

  if (d->console_handle == -1) {
    d->console_handle = console_find("pckbc");
    if (d->console_handle == -1) {
      return;
    }
  }

  while (keyboard_debug_events.size()) {
    auto event = keyboard_debug_events.front();
    pckbc_add_code(cpu, d, event.code);
    keyboard_debug_events.pop_front();
  }

  if (console_charavail(d->console_handle)) {
    auto ch = console_readchar(d->console_handle);
    if (ch >= 0x100) {
      key = (ch >> 8) & 0xff;
      release = ch & KEY_RELEASE;

      for (int t = 0; translation_tables[t].table; t++) {
        if (translation_tables[t].type == d->translation_table) {
          fprintf(stderr, "[ pckbc: using table %d (%d) ]\n", t, d->translation_table);
          table = translation_tables[t].table;
          for (int i = 0; table[i].make_prefix; i++) {
            if (table[i].keyname_idx == key) {
              fprintf(stderr, "[ pckbc: using table entry %d ]\n", i);
              found = &table[i];
              break;
            }
          }
          break;
        }
      }
    }
  }

  if (found) {
    fprintf(stderr, "sending key %c%d -> %s\n", release ? '^' : '.', key, keynames[key]);
    int or_break_code = 0;
    if (release) {
      for (int j = 0; found->break_prefix[j]; j++) {
        pckbc_add_code(cpu, d, found->break_prefix[j]);
      }
      or_break_code = found->high_bit_break ? 0x80 : 0;
    } else {
      for (int j = 0; found->make_prefix[j]; j++) {
        pckbc_add_code(cpu, d, found->make_prefix[j]);
      }
    }
    pckbc_add_code(cpu, d, found->native_code | or_break_code);
  }
}

DEVINIT(pckeyboard) {
  struct pckeyboard_data *d;

  CHECK_ALLOCATION(d = (struct pckeyboard_data *)malloc(sizeof(struct pckeyboard_data)));
  memset(d, 0, sizeof(struct pckeyboard_data));

	d->translation_table = 2;
  d->console_handle = -1;

  memory_device_register
    (devinit->machine->memory,
     devinit->name,
     KEYBOARD_PRIVATE,
     16,
     dev_pckeyboard_access,
     d,
     DM_DEFAULT,
     NULL);

	machine_add_tickfunction(devinit->machine, dev_pckeyboard_tick, d,
                           PCKBC_TICKSHIFT);

  return 1;
}
