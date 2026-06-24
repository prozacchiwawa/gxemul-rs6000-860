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
 *  COMMENT: NS16550 serial controller
 *
 *  TODO: Implement the FIFO.
 */

#include <deque>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "device.h"
#include "interrupt.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "thirdparty/comreg.h"

std::deque<uint8_t> debug_serial0_chars;

/*  #define debug fatal  */

#define	TICK_SHIFT		11
#define	DEV_NS16550_LENGTH	8

#define com_fcr DEV_NS16550_LENGTH
#define com_dlow (DEV_NS16550_LENGTH + 1)
#define com_dhi (DEV_NS16550_LENGTH + 2)
#define com_thr (DEV_NS16550_LENGTH + 3)
#define com_lcr 3

#define INT_QUEUE_RXNOW 1
#define INT_QUEUE_RXWAT 2
#define INT_QUEUE_TXWAT 4
#define INT_QUEUE_TXTIMEOUT 8
#define INT_QUEUE_RXTIMEOUT 16

struct ns_fifo {
  int head, count;
  uint8_t data[14];
};

void fifo_push(struct ns_fifo *f, uint8_t data) {
  if (f->count < 14) {
    f->data[(f->head + f->count) % 14] = data;
    f->count++;
  }
}

int fifo_have(struct ns_fifo *f) {
  return f->count;
}

int fifo_take(struct ns_fifo *f) {
  if (f->count > 0) {
    auto result = f->data[f->head];
    f->head = (f->head + 1) % 14;
    f->count--;
    return result;
  }

  return 0;
}

struct ns_data {
	int		addrmult;
	int		in_use;
	const char	*name;
  bool  interrupt_asserted;
  int   queued_int;
	int		console_handle;
	int		enable_fifo;
  struct ns_fifo recv_f;
  struct ns_fifo send_f;

	struct interrupt irq;

	unsigned char	reg[DEV_NS16550_LENGTH + 4];
  bool  pending_timeout;
};

static void clear_fifo(struct ns_data *d) {
  memset(&d->recv_f, 0, sizeof(d->recv_f));
  memset(&d->send_f, 0, sizeof(d->send_f));
}

#define QUEUED_RECV 1
#define QUEUED_SEND 2
#define QUEUED_THR_EMPTY 4
#define QUEUED_SEND_INT 8

int trigger_levels[4] = { 1, 4, 8, 14 };

 static int trigger_level(struct ns_data *d) {
   return trigger_levels[d->reg[com_fcr] >> 6];
}

static bool fifo_ena(struct ns_data *d) {
  return d->reg[com_fcr] & 1;
}

static int device_tick(struct ns_data *d) {
  // Don't allow overrun even though real life would.
  if (fifo_ena(d) && fifo_have(&d->recv_f) > 13) {
    return 0;
  }

  // Don't allow overrun in non fifo mode either.
  if (!fifo_ena(d) && d->queued_int & QUEUED_RECV) {
    return 0;
  }

  // Handle a pending character if available.
  while (console_charavail(d->console_handle)) {
    auto ch = console_readchar(d->console_handle);
    if (ch >= 0x100) {
      continue;
    }

    if (isprint(ch)) {
      fprintf(stderr, "[ ns16550: making char '%c' available ]\n", ch);
    } else {
      fprintf(stderr, "[ ns16550: making byte '%02x' available ]\n", ch);
    }

    if (fifo_ena(d)) {
      fifo_push(&d->recv_f, ch);
      d->reg[com_lsr] |= LSR_RXRDY;
      d->queued_int |= QUEUED_RECV;
      return 0;
    }

    d->reg[0] = ch;
    d->reg[com_lsr] |= LSR_RXRDY;
    d->queued_int |= QUEUED_RECV;
    return 0;
  }

  return 0;
}

static bool deassert_condition(struct ns_data *d, int iir, int queue_mask) {
  if (d->reg[com_iir] == iir) {
    d->interrupt_asserted = false;
    INTERRUPT_DEASSERT(d->irq);
    d->reg[com_iir] = 1;
    d->queued_int = (d->queued_int | queue_mask) ^ queue_mask;
    return true;
  }

  return false;
}

static void redo_interrupt(struct ns_data *d) {
  // Always drain transmitter.
  if (fifo_have(&d->send_f) && fifo_ena(d)) {
    d->reg[com_thr] = fifo_take(&d->send_f);
    d->reg[com_lsr] |= LSR_TXRDY | LSR_TSRE;
    d->queued_int &= ~QUEUED_THR_EMPTY;
    d->queued_int |= QUEUED_SEND_INT;
  } else if (!fifo_ena(d) && (d->queued_int & QUEUED_SEND)) {
    d->queued_int = ((d->queued_int | QUEUED_SEND) ^ QUEUED_SEND) | QUEUED_THR_EMPTY;
    d->queued_int |= QUEUED_SEND_INT;
    d->reg[com_lsr] |= LSR_TXRDY | LSR_TSRE;
  }

  // Handle things that can cause interrupts one at a time.
  if (fifo_have(&d->recv_f) && fifo_ena(d)) {
    d->queued_int = (d->queued_int | QUEUED_RECV) ^ QUEUED_RECV;
    d->reg[com_lsr] |= LSR_RXRDY;
    d->reg[com_iir] = 12;
  } else if (d->queued_int & QUEUED_RECV && !fifo_ena(d)) {
    d->queued_int = (d->queued_int | QUEUED_RECV) ^ QUEUED_RECV;
    d->reg[com_iir] = 4;
  } else if ((d->queued_int & (QUEUED_THR_EMPTY | QUEUED_SEND_INT)) && (d->reg[com_iir] == 1)) {
    d->queued_int = (d->queued_int | (QUEUED_THR_EMPTY | QUEUED_SEND_INT)) ^ (QUEUED_THR_EMPTY | QUEUED_SEND_INT);
    d->reg[com_lsr] |= LSR_TXRDY | LSR_TSRE;
    d->reg[com_iir] = 2;
  } else {
    d->reg[com_lsr] |= LSR_TXRDY | LSR_TSRE;
    d->reg[com_iir] = 1;
  }

  if ((d->reg[com_iir] != 1) && !d->interrupt_asserted) {
    fprintf(stderr, "[ ns16550: iir %02x assert interrupt ]\n", d->reg[com_iir]);
    INTERRUPT_ASSERT(d->irq);
    d->interrupt_asserted = true;
    return;
  }
  if ((d->reg[com_iir] == 1) && d->interrupt_asserted) {
    fprintf(stderr, "[ ns16550: iir 01 deassert interrupt ]\n", d->reg[com_iir]);
    INTERRUPT_DEASSERT(d->irq);
    d->interrupt_asserted = false;
    return;
  }
}

DEVICE_TICK(ns16550)
{
	struct ns_data *d = (struct ns_data *) extra;

  device_tick(d);
  redo_interrupt(d);
}

static void data_output(struct ns_data *d, uint8_t data) {
  console_putchar(d->console_handle, data);
  if (fifo_ena(d)) {
    d->reg[com_lsr] |= LSR_TSRE;
    fifo_push(&d->send_f, data);
    if (fifo_have(&d->send_f) < 14) {
      d->reg[com_lsr] |= LSR_TXRDY | LSR_TSRE;
    } else {
      d->reg[com_lsr] = (d->reg[com_lsr] | LSR_TXRDY | LSR_TSRE) ^ (LSR_TXRDY | LSR_TSRE);
    }
    return;
  }


  d->reg[com_thr] = data;
  d->reg[com_lsr] |= LSR_TSRE;
  if (!deassert_condition(d, 2, QUEUED_THR_EMPTY)) {
    d->queued_int |= QUEUED_THR_EMPTY;
  }
}

static uint8_t data_input(struct ns_data *d) {
  if (fifo_ena(d)) {
    if (fifo_have(&d->recv_f)) {
      d->reg[0] = fifo_take(&d->recv_f);
      if (!fifo_have(&d->recv_f)) {
        d->reg[com_lsr] = (d->reg[com_lsr] | LSR_RXRDY) ^ LSR_RXRDY;
      }
      deassert_condition(d, 12, QUEUED_RECV);
    }

    return d->reg[0];
  }

  d->reg[com_lsr] = (d->reg[com_lsr] | LSR_RXRDY) ^ LSR_RXRDY;
  if (!fifo_ena(d)) {
    deassert_condition(d, 4, QUEUED_RECV);
  }

  redo_interrupt(d);

  return d->reg[0];
}

DEVICE_ACCESS(ns16550)
{
	uint64_t idata = 0, odata=0;
	size_t i;
	struct ns_data *d = (struct ns_data *) extra;

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
    fprintf(stderr, "[ ns16550 (%s): write %02x <- %02x ]\n", d->name, (unsigned int)relative_addr, (unsigned int)idata);
  }

  int dlab = !!(d->reg[com_lcr] & LCR_DLAB);
  if (relative_addr == com_iir && writeflag == MEM_WRITE) {
    relative_addr = com_fcr;
  } else if (relative_addr == 0 && dlab) {
    relative_addr = com_dlow;
  } else if (relative_addr == 1 && dlab) {
    relative_addr = com_dhi;
  } else if (relative_addr == 0 && writeflag == MEM_WRITE) {
    relative_addr = com_thr;
  }

  if (writeflag == MEM_WRITE) {
    switch (relative_addr) {
    case com_thr:
      data_output(d, idata);
      break;

    default:
      d->reg[relative_addr] = idata;
      break;
    }
  } else {
    odata = d->reg[relative_addr];
    switch (relative_addr) {
    case 0:
      odata = d->reg[relative_addr] = data_input(d);
      break;

    case com_lsr:
      d->reg[com_lsr] = (d->reg[com_lsr] | LSR_RXRDY) ^ LSR_RXRDY;
      if (fifo_ena(d)) {
        deassert_condition(d, 12, QUEUED_RECV);
      } else {
        deassert_condition(d, 4, QUEUED_RECV);
      }
      break;

    case com_iir:
      odata |= d->reg[com_fcr] & 0xc0;
      d->reg[com_iir] = 1;
      d->interrupt_asserted = false;
      INTERRUPT_DEASSERT(d->irq);
      break;
    }
  }

	if (writeflag == MEM_READ) {
    fprintf(stderr, "[ ns16550 (%s): read %08x -> %08x ]\n", d->name, relative_addr, (unsigned int)odata);
		memory_writemax64(cpu, data, len, odata);
  }

	return 1;
}


DEVINIT(ns16550)
{
	struct ns_data *d;
	size_t nlen;
	char *name;

	CHECK_ALLOCATION(d = (struct ns_data *) malloc(sizeof(struct ns_data)));
	memset(d, 0, sizeof(struct ns_data));

	d->addrmult	= devinit->addr_mult;
	d->in_use	= devinit->in_use;
	d->name		= devinit->name2 != NULL? devinit->name2 : "";
	d->console_handle =
	    console_start_slave(devinit->machine, devinit->name2 != NULL?
	    devinit->name2 : devinit->name, d->in_use);

  // THR empty
  d->reg[com_lsr] = LSR_TXRDY | LSR_TSRE;

	INTERRUPT_CONNECT(devinit->interrupt_path, d->irq);

	nlen = strlen(devinit->name) + 10;
	if (devinit->name2 != NULL)
		nlen += strlen(devinit->name2);
	CHECK_ALLOCATION(name = (char *) malloc(nlen));
	if (devinit->name2 != NULL && devinit->name2[0])
		snprintf(name, nlen, "%s [%s]", devinit->name, devinit->name2);
	else
		snprintf(name, nlen, "%s", devinit->name);

	memory_device_register(devinit->machine->memory, name, devinit->addr,
	    DEV_NS16550_LENGTH * d->addrmult, dev_ns16550_access, d,
	    DM_DEFAULT, NULL);
	machine_add_tickfunction(devinit->machine,
	    dev_ns16550_tick, d, TICK_SHIFT);

	/*
	 *  NOTE:  Ugly cast into a pointer, because this is a convenient way
	 *         to return the console handle to code in src/machines/.
	 */
	devinit->return_ptr = (void *)(size_t)d->console_handle;

	return 1;
}
