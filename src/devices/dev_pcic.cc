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
 *  COMMENT: Intel 82365SL PC Card Interface Controller
 *
 *  (Called "pcic" by NetBSD.)
 *
 *  TODO: Lots of stuff. This is just a quick hack. Don't rely on it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "emul.h"
#include "interrupt.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "thirdparty/i82365reg.h"
#include "thirdparty/pcmciareg.h"


/*  #define debug fatal  */

#define	DEV_PCIC_LENGTH		4

/* One controller has 2 sockets */
struct pcic_controller {
	struct interrupt irq;
	int regnr;
	uint8_t regs[2][0x40];
};

/* The rs/6000 model 860 at least has 2 controllers. */
struct pcic_data {
  struct pcic_controller controller[2];
};

int pcic_controller_access
(struct cpu *cpu, struct pcic_controller *d, int relative_addr, uint8_t *data, int len, int writeflag)
{
	uint64_t idata = 0, odata = 0;
	int socket_nr, regnr;
  bool write_enable = false;

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
  }

	socket_nr = d->regnr & 0x40 ? 1 : 0;
  regnr = d->regnr & 0x3f;

	switch (relative_addr) {
	case 0:	/*  Register select:  */
		if (writeflag == MEM_WRITE) {
			d->regnr = idata;
    } else {
			odata = d->regnr;
    }
		break;

	case 1:	/*  Register access:  */
		switch (regnr) {
    case PCIC_PWRCTL:
      write_enable = true;
      break;
    }
    if (write_enable) {
      d->regs[socket_nr][regnr] = idata;
    }
    break;
  }

  if (writeflag == MEM_READ) {
		memory_writemax64(cpu, data, len, odata);
    fprintf
      (stderr, "[ pcic: socket %c read %d (index %02x) = %02x ]\n",
       socket_nr ? 'B' : 'A',
       (int)relative_addr,
       regnr,
       (unsigned int)odata);
  } else if (writeflag == MEM_WRITE) {
    fprintf
      (stderr, "[ pcic: socket %c write (%s) %d (index %02x) = %02x ]\n",
       socket_nr ? 'B' : 'A',
       write_enable ? "ENA" : "dis",
       (int)relative_addr,
       regnr,
       (unsigned int)idata);
    if (write_enable) {
      d->regs[socket_nr][regnr] = idata;
    }
  }

	return 1;
}

DEVICE_ACCESS(pcic)
{
	struct pcic_data *d = (struct pcic_data *) extra;
  bool controller = !!(relative_addr & PCIC_IOSIZE);
  return pcic_controller_access
    (cpu, &d->controller[controller], relative_addr & (PCIC_IOSIZE - 1), data, len, writeflag);
}

// XXX not machine authentic.  an rs/6000 model 860 or 850 has memory cards in both sockets
// in the first controller.
// This should 
void pcic_socket_setup(struct pcic_controller *d, bool socket) {
  auto regs = &d->regs[socket][0];
  regs[PCIC_IDENT] = 0x82; // Memory and IO, revision 0010
  // Ready, CD0, CD1 (CD0 = CD1 = 0 means card detected), battery good.  Power on.
  regs[PCIC_IF_STATUS] = 0x6f; 
  regs[PCIC_PWRCTL] = 0;
  regs[PCIC_INTR] = 0;
  regs[PCIC_CSC] = 0; // Battery not dead, no warning, no ready changes.
  regs[PCIC_CSC_INTR] = 0;
  regs[PCIC_IOCTL] = 0;
  regs[PCIC_ADDRWIN_ENABLE] = 0;
}

DEVINIT(pcic)
{
	char tmpstr[200];
	struct pcic_data *d;

	CHECK_ALLOCATION(d = (struct pcic_data *) malloc(sizeof(struct pcic_data)));
	memset(d, 0, sizeof(struct pcic_data));

  pcic_socket_setup(&d->controller[0], false);
  pcic_socket_setup(&d->controller[0], true);
  pcic_socket_setup(&d->controller[1], false);
  pcic_socket_setup(&d->controller[1], true);

	INTERRUPT_CONNECT(devinit->interrupt_path, d->controller[0].irq);

	memory_device_register
    (devinit->machine->memory, devinit->name,
     devinit->addr, DEV_PCIC_LENGTH * 2,
     dev_pcic_access, (void *)d, DM_DEFAULT, NULL);

	return 1;
}

