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

#define	PCKBC_TICKSHIFT		11

/*  #define PCKBC_DEBUG  */
/*  #define debug fatal  */

#define STATE_NORMAL 0

struct ps2mouse_data {
  int state;

  int mouse_remote, mouse_reporting;
  int mouse_scaling, mouse_resolution, mouse_sample_rate;

  int mouse_ena, mouse_cmd;
  int mouse_last_x, mouse_last_y, mouse_last_but;
};

/*
 *  pckbc_add_code():
 *
 *  Adds a byte to the data queue.
 */
static void pckbc_add_code(struct cpu *cpu, struct ps2mouse_data *d, uint8_t code)
{
  fprintf(stderr, "[ pckbc: kbd enqueue %02x ]\n", (uint8_t)code);
  cpu->memory_rw
    (cpu,
     cpu->mem,
     I8042_PRIVATE + 1,
     &code,
     sizeof(code),
     MEM_WRITE,
     PHYSICAL | NO_EXCEPTIONS);
}

static void pckbc_reset_interface(struct cpu *cpu)
{
  uint8_t cmd = I8042_ACTION_RESET_MOUSE;
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
static void dev_pckbc_command(struct cpu *cpu, struct ps2mouse_data *d, uint8_t cmd)
{
  debug("[ pckbc: sending to mouse: "
        "0x%02x cmd %02x (pc %08x) ]\n", cmd, d->mouse_cmd, (unsigned int)cpu->pc);
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

      pckbc_reset_interface(cpu);
      pckbc_add_code(cpu, d, 0xfa);
      pckbc_add_code(cpu, d, 0xaa);
      pckbc_add_code(cpu, d, 0);
      break;

      case 0xf6:
        pckbc_add_code(cpu, d, 0xfa);
        break;

      case 0xf5:
        d->mouse_reporting = 0;
        pckbc_add_code(cpu, d, 0xfa);
        break;

      case 0xf4:
        d->mouse_reporting = 1;
        pckbc_add_code(cpu, d, 0xfa);
        break;

      case 0xf3:
        d->mouse_cmd = 0xf3;
        pckbc_add_code(cpu, d, 0xfa);
        break;

      case 0xf2:
        pckbc_add_code(cpu, d, 0xfa);
        pckbc_add_code(cpu, d, 0x00);
        break;

      case 0xe2:
        pckbc_add_code(cpu, d, 0xfa);
        d->mouse_cmd = 0xe2;
        break;

      case 0xe6:
        d->mouse_scaling = 0;
        pckbc_add_code(cpu, d, 0xfa);
        break;

      case 0xe7:
        d->mouse_scaling = 1;
        pckbc_add_code(cpu, d, 0xfa);
        break;

      case 0xe8:
        pckbc_add_code(cpu, d, 0xfa);
        d->mouse_cmd = 0xe8;
        break;

      case 0xe9:
        pckbc_add_code(cpu, d, 0xfa);
        pckbc_add_code(cpu, d, d->mouse_remote << 6 | d->mouse_reporting << 5 | d->mouse_scaling << 4);
        pckbc_add_code(cpu, d, d->mouse_resolution);
        pckbc_add_code(cpu, d, d->mouse_sample_rate);
        break;

      case 0xea:
        pckbc_add_code(cpu, d, 0xfa);
        break;

      case 0xeb:
        pckbc_add_code(cpu, d, 0xfa);
        pckbc_add_code(cpu, d, 8);
        pckbc_add_code(cpu, d, 0);
        pckbc_add_code(cpu, d, 0);
        break;

      default:
        fprintf(stderr, "[ pckbc: unknown command %02x to mouse ]\n", cmd);
      }
      break;

    case 0xf3:
      d->mouse_cmd = 0;
      d->mouse_sample_rate = cmd;
      pckbc_add_code(cpu, d, 0xfa);
      break;

    case 0xe8:
      d->mouse_cmd = 0;
      d->mouse_resolution = cmd;
      pckbc_add_code(cpu, d, 0xfa);
      break;

    case 0xe2:
      switch (cmd) {
      case 0x40:
        d->mouse_cmd = 0;
        d->mouse_remote = 0;
        pckbc_add_code(cpu, d, 0xfa);
        break;

      case 0x41:
        d->mouse_cmd = 0;
        d->mouse_remote = 1;
        pckbc_add_code(cpu, d, 0xfa);
        break;

      default:
        d->mouse_cmd = 0;
        pckbc_add_code(cpu, d, 0xfa);
        break;
      }
      break;

    default:
      pckbc_add_code(cpu, d, 0xfa);
      d->mouse_cmd = 0;
      break;
    }

		d->state = STATE_NORMAL;
		return;
}

DEVICE_ACCESS(ps2mouse) {
	struct ps2mouse_data *d = (struct ps2mouse_data *) extra;
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

DEVICE_TICK(ps2mouse) {
	struct ps2mouse_data *d = (struct ps2mouse_data *) extra;
  int mouse_x, mouse_y, mouse_but, fb_nr;

  console_getmouse(&mouse_x, &mouse_y, &mouse_but, &fb_nr);
  bool mouse_output_generated = false;
  if (d->mouse_ena &&
      ((d->mouse_last_x != mouse_x) ||
       (d->mouse_last_y != mouse_y) ||
       (d->mouse_last_but != mouse_but))
      ) {
    mouse_output_generated = true;
    int diff_x = mouse_x - d->mouse_last_x;
    int diff_y = -(mouse_y - d->mouse_last_y);
    int flags =
      ((diff_x < 0) ? 0x10 : 0) |
      ((diff_y < 0) ? 0x20 : 0) |
      8 |
      (mouse_but & 7);
    if (d->mouse_last_x != -1) {
      pckbc_add_code(cpu, d, flags);
      pckbc_add_code(cpu, d, diff_x);
      pckbc_add_code(cpu, d, diff_y);
      // pckbc_add_code(cpu, d, 0);
      // pckbc_add_code(cpu, d, 0);
    }

    d->mouse_last_x = mouse_x;
    d->mouse_last_y = mouse_y;
    d->mouse_last_but = mouse_but;
  }
}

DEVINIT(ps2mouse) {
  struct ps2mouse_data *d;

  CHECK_ALLOCATION(d = (struct ps2mouse_data *)malloc(sizeof(struct ps2mouse_data)));
  memset(d, 0, sizeof(struct ps2mouse_data));

  memory_device_register
    (devinit->machine->memory,
     devinit->name,
     MOUSE_PRIVATE,
     16,
     dev_ps2mouse_access,
     d,
     DM_DEFAULT,
     NULL);

	machine_add_tickfunction(devinit->machine, dev_ps2mouse_tick, d, PCKBC_TICKSHIFT);

  return 1;
}
