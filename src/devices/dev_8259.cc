/*
 *  Copyright (C) 2005-2009  Anders Gavare.  All rights reserved.
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
 *  COMMENT: Intel 8259 Programmable Interrupt Controller
 *
 *  See the following URL for more details:
 *	http://www.nondot.org/sabre/os/files/MiscHW/8259pic.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define	DEV_8259_LENGTH		2

#define DEV_8259_DEBUG

#define N -1
static int pri_lut[] = {
  N,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  6,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  7,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  6,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
  4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
};

static inline uint8_t rotate(int rotate, uint16_t imask) {
  uint16_t bits = imask << (rotate & 7);
  return (bits >> 8) | bits;
}

static int get_best_interrupt(int rotation, uint8_t imask) {
  uint8_t pri_bits = rotate(rotation, imask);
  int raw_priority = pri_lut[pri_bits];
  if (raw_priority == -1) {
    return -1;
  }

  return (raw_priority + 8 - (rotation & 7)) & 7;
}

static void do_deassert(struct pic8259_data *d) {
  if (d->chained_to) {
#ifdef DEV_8259_DEBUG
    fprintf(stderr, "8259(%d): chained deassert\n", d->irq_base);
#endif
    dev_8259_deassert(d->chained_to, d->chained_int_line);
  } else {
#ifdef DEV_8259_DEBUG
    fprintf(stderr, "8259(%d): deassert\n", d->irq_base);
#endif
    INTERRUPT_DEASSERT(d->irq);
  }
}

static void do_assert(struct pic8259_data *d) {
  if (d->chained_to) {
#ifdef DEV_8259_DEBUG
    fprintf(stderr, "8259(%d): chained assert\n", d->irq_base);
#endif
    dev_8259_assert(d->chained_to, d->chained_int_line);
  } else {
#ifdef DEV_8259_DEBUG
    fprintf(stderr, "8259(%d): assert\n", d->irq_base);
#endif
    INTERRUPT_ASSERT(d->irq);
  }
}

static void dev_8259_recalc_interrupts(struct pic8259_data *d, uint8_t old_isr) {
  // isr frozen when poll_cmd.
  if (d->poll_cmd) {
    return;
  }

  uint8_t unmasked = d->irr & ~d->ier;
  int pri_enabled = get_best_interrupt(d->rotation_pri, unmasked);
#ifdef DEV_8259_DEBUG
  fprintf(stderr, "8259(%d): new pri %d have irr %02x ier %02x isr %02x\n", d->irq_base, pri_enabled, d->irr, d->ier, d->isr);
#endif

  if (pri_enabled >= 0) {
    uint8_t pri_mask = 1 << pri_enabled;
    if (!(d->isr & pri_mask)) {
#ifdef DEV_8259_DEBUG
      fprintf(stderr, "8259(%d): new interrupt %d\n", d->irq_base, pri_enabled);
#endif
      d->isr |= (1 << pri_enabled);
      do_assert(d);
    }
    return;
  }

  if (old_isr && !d->isr) {
#ifdef DEV_8259_DEBUG
    fprintf(stderr, "8259(%d): dismissed %d\n", d->irq_base, pri_enabled);
#endif
    do_deassert(d);
  }
}

void dev_8259_assert(struct pic8259_data *d, int line) {
  d->irr |= 1 << line;

#ifdef DEV_8259_DEBUG
  fprintf(stderr, "8259(%d): assert(%d)\n", d->irq_base, line);
#endif

  dev_8259_recalc_interrupts(d, d->isr);
}

void dev_8259_deassert(struct pic8259_data *d, int line) {
  d->irr &= ~(1 << line);

#ifdef DEV_8259_DEBUG
  fprintf(stderr, "8259(%d): deassert(%d)\n", d->irq_base, line);
#endif

  dev_8259_recalc_interrupts(d, d->isr);
}

static void write_ocw2(struct cpu *cpu, struct pic8259_data *d, int idata) {
  int rse = (idata >> 5) & 7;
  int level = idata & 7;
  int to_ack, old_isr = d->isr;

#ifdef DEV_8259_DEBUG
  fprintf(stderr, "8259(%d): ocw2 rse %d level %d\n", d->irq_base, rse, level);
#endif

  switch (rse) {
  case 1: // Non specific eoi
    to_ack = get_best_interrupt(d->rotate, d->isr);
    if (to_ack != -1) {
      d->isr &= ~(1 << to_ack);
      dev_8259_recalc_interrupts(d, old_isr);
    }
    break;

  case 3: // Specific eoi
    d->isr &= ~(1 << level);
    dev_8259_recalc_interrupts(d, old_isr);
    break;

  case 5: // Rotate on non specific eoi
    to_ack = get_best_interrupt(d->rotation_pri, d->isr);
    d->rotation_pri = to_ack;
    if (to_ack != -1) {
      d->isr &= ~(1 << to_ack);
      dev_8259_recalc_interrupts(d, old_isr);
    }
    break;

  case 4: // Rotate in automatic eoi mode (set)
    d->rotate = 1;
    break;

  case 0: // Rotate in automatic eoi mode (clear)
    d->rotate = 0;
    d->rotation_pri = 0;
    break;

  case 7: // Rotate on specific eoi command
    d->rotate = 1;
    d->rotation_pri = level;
    d->isr &= ~(1 << level);
    fprintf(stderr, "8259(%d): rotate on specific eoi: %d new isr %02x\n", d->irq_base, d->rotation_pri, d->isr);
    dev_8259_recalc_interrupts(d, old_isr);
    break;

  case 6: // Set priority command
    d->rotation_pri = level;
    break;

  case 2: // Nop
    break;
  }
}

static void write_ocw3(struct cpu *cpu, struct pic8259_data *d, int idata) {
  int rr_ris_read = idata & 3;
  int esmm_smm = (idata >> 5) & 3;
  int to_ack;

  d->poll_cmd = (idata >> 2) & 1;
  if (!d->poll_cmd) {
    to_ack = get_best_interrupt(d->rotation_pri, d->irr);
    if (to_ack != -1) {
      dev_8259_recalc_interrupts(d, d->isr);
    } else {
      do_deassert(d);
    }
  }

  if (rr_ris_read > 2) {
    d->read_ir_is = rr_ris_read & 1;
  }

  if (esmm_smm > 2) {
    d->special_mask_mode = esmm_smm & 1;
  }
}

static void write_init_start(struct cpu *cpu, struct pic8259_data *d, int idata) {
#ifdef DEV_8259_DEBUG
  fprintf(stderr, "8259(%d): write init start (%02x)\n", d->irq_base, idata);
#endif
  d->icw[0] = idata;
  d->init_state = 1;
  d->irr = 0;
  d->ier = 0xff;
  d->special_mask_mode = 0;
  d->read_ir_is = 0;
}

static int doing_init(struct pic8259_data *d) {
  return d->init_state >= 0;
}

static void finish_init(struct pic8259_data *d) {
#ifdef DEV_8259_DEBUG
  fprintf(stderr, "8259(%d) finish init\n", d->irq_base);
#endif
  d->init_state = -1;
}

static void command_write(struct cpu *cpu, struct pic8259_data *d, int idata) {
  if (doing_init(d)) {
#ifdef DEV_8259_DEBUG
    fprintf(stderr, "8259(%d): write init (%02x) with state %d\n", d->irq_base, idata, d->init_state);
#endif
    d->icw[d->init_state] = idata;
    d->init_state += 1;
    if ((d->icw[0] & 2) && d->init_state == 2) {
      d->icw[2] = 0;
      d->init_state = 3;
    }
    if (d->init_state >= 3 + (d->icw[0] & 1)) {
      finish_init(d);
    }
    return;
  }

  int ocw_type = (idata >> 3) & 3;

  switch (ocw_type) {
  case 0: // ocw2
    write_ocw2(cpu, d, idata);
    break;
  case 1: // ocw3
    write_ocw3(cpu, d, idata);
    break;
  default: // init (d4 = 1)
    write_init_start(cpu, d, idata);
    break;
  }
}

DEVICE_ACCESS(8259)
{
	struct pic8259_data *d = (struct pic8259_data *) extra;
	uint64_t idata = 0, odata = 0;
  int to_ack;

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
#ifdef DEV_8259_DEBUG
    fatal("[ 8259(%d): write to 0x%x: 0x%x ]\n",
          d->irq_base, (int)relative_addr, (int)idata);
#endif

    switch (relative_addr) {
    case 0:
      command_write(cpu, d, idata);
      break;

    default:
      if (doing_init(d)) {
        command_write(cpu, d, idata);
        break;
      }

      // OCW1
#ifdef DEV_8259_DEBUG
      fprintf(stderr, "8259(%d): ier = %02x\n", d->irq_base, idata);
#endif
      d->ier = idata;
      dev_8259_recalc_interrupts(d, d->isr);
      break;
    }
  } else {
    switch (relative_addr) {
    case 0:
      // read isr when read_ir_is otherwise read irr
      if (d->poll_cmd) {
        d->isr = d->irr;
        to_ack = get_best_interrupt(d->rotation_pri, d->isr);
        if (to_ack != -1) {
          d->isr &= ~(1 << to_ack);
          odata = 0x80 | to_ack;
        } else {
          odata = 0;
        }
      } else if (d->read_ir_is) {
        odata = d->isr;
      } else {
        odata = d->irr;
      }
      break;

    default:
      // read imr
      odata = d->ier;
      break;
    }

#ifdef DEV_8259_DEBUG
		fatal("[ 8259(%d): read from 0x%x: 0x%x ]\n",
          d->irq_base, (int)relative_addr, (int)odata);
#endif
		memory_writemax64(cpu, data, len, odata);
  }

	return 1;
}


/*
 *  devinit_8259():
 *
 *  Initialize an 8259 PIC. Important notes:
 *
 *	x)  Most systems use _TWO_ 8259 PICs. These should be registered
 *	    as separate devices.
 *
 *	x)  The irq number specified is the number used to re-calculate
 *	    CPU interrupt assertions.  It is _not_ the irq number at
 *	    which the PIC is connected. (That is left to machine specific
 *	    code in src/machine.c.)
 */
DEVINIT(8259)
{
	struct pic8259_data *d;
	char *name2;
	size_t nlen = strlen(devinit->name) + 20;

	CHECK_ALLOCATION(d = (struct pic8259_data *) malloc(sizeof(struct pic8259_data)));
	memset(d, 0, sizeof(struct pic8259_data));
  d->init_state = -1;

	INTERRUPT_CONNECT(devinit->interrupt_path, d->irq);

	CHECK_ALLOCATION(name2 = (char *) malloc(nlen));
	snprintf(name2, nlen, "%s", devinit->name);
	if ((devinit->addr & 0xfff) == 0xa0) {
		strlcat(name2, " [secondary]", nlen);
		d->irq_base = 8;
	}

  assert(devinit->addr > 0x808680000000ull);

	memory_device_register(devinit->machine->memory, name2,
	    devinit->addr, DEV_8259_LENGTH, dev_8259_access, d,
	    DM_DEFAULT, NULL);

	devinit->return_ptr = d;
	return 1;
}

