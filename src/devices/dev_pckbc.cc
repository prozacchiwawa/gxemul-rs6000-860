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

#define	PCKBC_TICKSHIFT		11

static inline bool port_enabled(int port, int cmdbyte) {
  if (port) {
    return !(cmdbyte & KC8_MDISABLE);
  } else {
    return !(cmdbyte & KC8_KDISABLE);
  }
}

static inline bool port_int_enabled(int port, int cmdbyte) {
  if (port) {
    return cmdbyte & KC8_MENABLE;
  } else {
    return cmdbyte & KC8_KENABLE;
  }
}

struct pckbc_data {
	int		reg[DEV_PCKBC_LENGTH];

	struct interrupt irq_keyboard;
	struct interrupt irq_mouse;
	int		currently_asserted[2];
	int		type;
	int		pc_style_flag;

	int		state;
	int		cmdbyte;
	int		output_byte;
  int   last_byte;

	unsigned	key_queue[2][MAX_8042_QUEUELEN];
	int		head[2], tail[2];

  int mouse_cmd;

  int rst_order;
};

#define	STATE_NORMAL			0
#define	STATE_LDCMDBYTE			1
#define	STATE_RDCMDBYTE			2
#define	STATE_WAITING_FOR_AUX		6
#define	STATE_WAITING_FOR_AUX_OUT	7
#define	STATE_LDOUTPUT			8
#define	STATE_RDOUTPUT			9

static void interrupt_check(struct pckbc_data *d) {
  for (int port = 0; port < 2; port++) {
    if (port_enabled(port, d->cmdbyte) &&
        port_int_enabled(port, d->cmdbyte) &&
        (d->head[port] != d->tail[port]) &&
        !d->currently_asserted[port]) {
      //fprintf(stderr, "[ pckbc: tick interrupt port %d ]\n", port);
      if (port == 0) {
        INTERRUPT_ASSERT(d->irq_keyboard);
      } else {
        INTERRUPT_ASSERT(d->irq_mouse);
      }
      d->currently_asserted[port] = true;
    }
  }
}

void pckbc_add_code(struct cpu *cpu, struct pckbc_data *d, uint8_t code, int port) {
  /*  Add at the head, read at the tail:  */
  d->head[port] = (d->head[port]+1) % MAX_8042_QUEUELEN;
  if (d->head[port] == d->tail[port]) {
    fatal("[ pckbc: queue overrun, port %i! ]\n", port);
  }

  d->key_queue[port][d->head[port]] = code;
  d->output_byte = (d->output_byte & ~0x30) | (port ? 0x30 : 0x10);
  interrupt_check(d);
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

  if (d->tail[port] == d->head[port]) {
    if (port) {
      INTERRUPT_DEASSERT(d->irq_mouse);
    } else {
      INTERRUPT_DEASSERT(d->irq_keyboard);
    }
    d->currently_asserted[port] = false;
  } else {
    interrupt_check(d);
  }

	return d->key_queue[port][d->tail[port]];
}

static void dev_pckbc_send_command(struct cpu *cpu, struct pckbc_data *d, uint8_t cmd, int port_nr) {
  uint64_t location = port_nr ? MOUSE_PRIVATE : KEYBOARD_PRIVATE;
  cpu->memory_rw
    (cpu,
     cpu->mem,
     location,
     &cmd,
     sizeof(cmd),
     MEM_WRITE,
     PHYSICAL | NO_EXCEPTIONS);
}

DEVICE_TICK(pckbc)
{
	struct pckbc_data *d = (struct pckbc_data *) extra;
  interrupt_check(d);
  d->currently_asserted[0] = d->currently_asserted[1] = false;
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
        // static int x = 0;
        // x++;
        // if (x&1)
        // odata ^= 0x10;
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

			default: {
        if (d->head[0] != d->tail[0]) {
					odata = pckbc_get_code(d, 0);
					d->last_byte = odata;
				} else if (d->head[1] != d->tail[1]) {
          odata = pckbc_get_code(d, 1);
        } else {
					odata = d->last_byte;
					d->last_byte |= 0x80;
				}
				d->state = STATE_NORMAL;

        d->output_byte &= ~0x30;
        if (d->head[0] != d->tail[0]) {
          d->output_byte |= 0x10;
        } else if (d->head[1] != d->tail[1]) {
          d->output_byte |= 0x30;
        }
        break;
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
        d->state = STATE_NORMAL;
				d->cmdbyte = idata;
				break;
			case STATE_LDOUTPUT:
        d->state = STATE_NORMAL;
				d->output_byte = idata;
				break;
      case STATE_WAITING_FOR_AUX_OUT:
        d->state = STATE_NORMAL;
        d->reg[relative_addr] = idata;
				dev_pckbc_send_command(cpu, d, d->reg[relative_addr], 1);
        break;

			default:
        d->state = STATE_NORMAL;
        d->reg[relative_addr] = idata;
				dev_pckbc_send_command(cpu, d, d->reg[relative_addr], 0);
        break;
			}
		}
		break;
	case 1:		/*  control  */
		if (writeflag==MEM_READ) {
			odata = 0;

			/*  "Data in buffer" bit  */
      if ((d->head[0] != d->tail[0]) ||
          d->state == STATE_RDCMDBYTE ||
          d->state == STATE_RDOUTPUT) {
        fprintf(stderr, "[ pckbc: keyboard output ring %d-%d ]\n", d->head[0], d->tail[0]);
        odata |= KBS_DIB;
      } else if (d->head[1] != d->tail[1]) {
        fprintf(stderr, "[ pckbc: mouse output ring %d-%d ]\n", d->head[1], d->tail[1]);
        odata |= KBS_DIB | 0x20;
      }

      if (odata != 0) {
        debug("[ pckbc: read from CTL status port: "
              "0x%02x (pc %08x) ]\n", (int)odata, (unsigned int)cpu->pc);
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
				break;
			case 0xa9:	/*  test auxiliary port  */
        pckbc_add_code(cpu, d, 0x00, 0);
				break;
			case 0xaa:	/*  keyboard self-test  */
        d->head[0] = d->tail[0] = d->head[1] = d->tail[1] = 0;
				pckbc_add_code(cpu, d, 0x55, port_nr);
				break;
			case 0xab:	/*  keyboard interface self-test  */
				pckbc_add_code(cpu, d, 0x00, port_nr);
				break;
			case 0xad:
				d->cmdbyte |= KC8_KDISABLE;
				break;
			case 0xae:
				d->cmdbyte &= ~KC8_KDISABLE;
        d->state = STATE_NORMAL;
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
        break;
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
			dev_pckbc_send_command(cpu, d, d->reg[PS2_TXBUF], port_nr);
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
		}
		break;

	case PS2 + PS2_STATUS:
		if (writeflag==MEM_READ) {
			/* 0x08 = transmit buffer empty  */
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

	return 1;
}


DEVICE_ACCESS(pckbc_private)
{
	struct pckbc_data *d = (struct pckbc_data *) extra;
	uint64_t idata = 0;

  if (writeflag == MEM_WRITE) {
    idata = memory_readmax64(cpu, data, len);

    switch (relative_addr) {
    case I8042_ACTION:
      if (idata & I8042_ACTION_RESET_KBD) {
        d->head[0] = d->tail[0] = 0;
      }
      if (idata & I8042_ACTION_RESET_MOUSE) {
        d->head[1] = d->tail[1] = 0;
      }
      break;
    default:
      pckbc_add_code(cpu, d, idata, relative_addr & 1);
      break;
    }
  } else {
    memory_writemax64(cpu, data, len, idata);
  }

  return 1;
}


/*
 *  dev_pckbc_init():
 *
 *  Type should be PCKBC_8042 or PCKBC_8242.
 */
int dev_pckbc_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr, int type, char *keyboard_irqpath,
	char *mouse_irqpath, int pc_style_flag)
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
	d->pc_style_flag     = pc_style_flag;
	d->output_byte       = 0x02;	/*  A20 enable on PCs  */

	memory_device_register
    (mem, "pckbc", baseaddr,
     len, dev_pckbc_access, d, DM_DEFAULT, NULL);
  memory_device_register
    (mem, "pckbc internal", I8042_PRIVATE,
     16, dev_pckbc_private_access, d, DM_DEFAULT, NULL);

  char tmps[300];
  snprintf(tmps, sizeof(tmps), "pckeyboard addr=0x%" PRIx64, KEYBOARD_PRIVATE);
  device_add(machine, tmps);
  snprintf(tmps, sizeof(tmps), "ps2mouse addr=0x%" PRIx64, MOUSE_PRIVATE);
  device_add(machine, tmps);

	machine_add_tickfunction(machine, dev_pckbc_tick, d,
                           PCKBC_TICKSHIFT);

	return 1;
}
