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
#include "devices.h"
#include "emul.h"
#include "interrupt.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "thirdparty/i82365reg.h"
#include "thirdparty/pcmciareg.h"


/*  #define debug fatal  */

#define	DEV_PCIC_LENGTH		4

struct pcic_mem_window {
  uint32_t internal_low;
  uint32_t internal_high;
  uint32_t cardmem_add;
  bool regspace;
  bool readonly;
};

struct pcic_io_window {
  uint16_t low;
  uint16_t high;
};

/* One controller has 2 sockets */
struct pcic_controller {
  struct interrupt inbound_irq[2];

	int regnr;
	uint8_t regs[2][0x40];
  struct pcic_mem_window mem_window[2][5];
  struct pcic_io_window io_window[2][2];

  int card_state[2];
  int asserted;
};

/* The rs/6000 model 860 at least has 2 controllers. */
struct pcic_data {
	struct interrupt irq[16];
  struct pcic_controller controller[2];
  uint32_t range_claim_check;
};

static uint16_t good_interrupt = 0xdeb8;

static void maybe_raise_interrupt(struct pcic_data *c, struct pcic_controller *d, int socket_nr) {
  // Which interrupt to raise?
  int interrupt = d->regs[socket_nr][PCIC_CSC_INTR] >> PCIC_CSC_INTR_IRQ_SHIFT;
  bool enabled = (d->regs[socket_nr][PCIC_CSC_INTR] & PCIC_CSC_INTR_CD_ENABLE);

  fprintf(stderr, "[ pcic: maybe raise interrupt irq %d ena %d good %d ]\n", interrupt, enabled, !!(good_interrupt & (1 << interrupt)));

  if (!enabled || !(good_interrupt & (1 << interrupt))) {
    if (d->asserted != -1) {
      INTERRUPT_DEASSERT(c->irq[d->asserted]);
      d->asserted = -1;
    }
    fprintf(stderr, "[ pcic: not enabled or not good ]\n");
    return;
  }
  if (d->asserted != -1) {
    INTERRUPT_DEASSERT(c->irq[d->asserted]);
    d->asserted = -1;
  }
  if ((d->regs[socket_nr][PCIC_CARD_DETECT] & PCIC_CARD_DETECT_SW_INTR) ||
      d->regs[socket_nr][PCIC_CSC]) {
    d->regs[socket_nr][PCIC_CARD_DETECT] &= ~PCIC_CARD_DETECT_SW_INTR;
    d->asserted = interrupt;
    INTERRUPT_ASSERT(c->irq[interrupt]);
  } else {
    INTERRUPT_DEASSERT(c->irq[interrupt]);
  }
}

int pcic_controller_access
(struct cpu *cpu, int controller, struct pcic_data *c, struct pcic_controller *d, int relative_addr, uint8_t *data, int len, int writeflag)
{
	uint64_t idata = 0, odata = 0;
	int socket_nr, regnr;
  bool write_enable = false;

  /*
  if (d->regnr >= 0x80) {
    controller = 1;
    d = &c->controller[1];
  }
  */
	socket_nr = (controller > 0 || d->regnr >= 0x40) ? 1 : 0;
  regnr = d->regnr & 0x3f;

  if (d->card_state[socket_nr] == 0) {
    // Detect card.
    uint64_t check_address = DEV_PCIC_CARD_MEM_SPACE(controller * 2 + socket_nr);
    uint8_t check_data;
    fprintf(stderr, "[ pcic: detect hardware for socket controller %d socket %d ]\n", controller, socket_nr);
    if (cpu->memory_rw(cpu, cpu->mem, check_address, &check_data, 1, MEM_READ, PHYSICAL | NO_EXCEPTIONS) != MEMORY_ACCESS_OK || check_data == 0) {
      // Detected negative.  No card installed.
      // Slots are already initialized as vacant.
      d->card_state[socket_nr] = -1;
    } else {
      fprintf(stderr, "[ pcic: detected card at controller %d socket %d: %02x ]\n", controller, socket_nr, check_data);
      // Detected positive.  Card installed.
      // CD1 and CD2 on.
      d->regs[socket_nr][1] |= 0xc;
      // Card detect and ready change.
      d->regs[socket_nr][4] |= 0xc;
      d->card_state[socket_nr] = 1;
    }
  }
  
  if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
  }

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
      // Read only.
    case PCIC_IDENT:
    case PCIC_IF_STATUS:
    case PCIC_CIRRUS_PROD_ID:
      odata = d->regs[socket_nr][regnr];
      break;

    case PCIC_CSC:
      odata = d->regs[socket_nr][regnr];
      d->regs[socket_nr][regnr] = 0;
      maybe_raise_interrupt(c, d, socket_nr);
      break;
      
    case PCIC_CARD_DETECT:
      if (writeflag == MEM_WRITE) {
        d->regs[socket_nr][regnr] = idata;
        if (idata & PCIC_CARD_DETECT_SW_INTR) {
          d->regs[socket_nr][PCIC_CSC] |= PCIC_CSC_CD;
          maybe_raise_interrupt(c, d, socket_nr);
        }
      } else {
        odata = d->regs[socket_nr][regnr];
        d->regs[socket_nr][regnr] = 0;
      }
      break;
      
    case PCIC_PWRCTL:
      write_enable = true;
      if (writeflag == MEM_WRITE) {
        d->regs[socket_nr][regnr] = idata;
      } else {
        odata = d->regs[socket_nr][regnr];
      }
      break;

    case PCIC_IOADDR0_START_LSB:
    case PCIC_IOADDR0_STOP_LSB:
    case PCIC_IOADDR1_START_LSB:
    case PCIC_IOADDR1_STOP_LSB:
      if (writeflag == MEM_WRITE) {
        d->regs[socket_nr][regnr] = idata;
        d->io_window[socket_nr][!!(regnr & 2)].low = idata;
      } else {
        odata = d->regs[socket_nr][regnr];
      }
      break;

    case PCIC_IOADDR0_START_MSB:
    case PCIC_IOADDR0_STOP_MSB:
    case PCIC_IOADDR1_START_MSB:
    case PCIC_IOADDR1_STOP_MSB:
      if (writeflag == MEM_WRITE) {
        d->regs[socket_nr][regnr] = idata;
        d->io_window[socket_nr][!!(regnr & 2)].high = idata;
      } else {
        odata = d->regs[socket_nr][regnr];
      }
      break;

    default:
      if (writeflag == MEM_WRITE) {
        d->regs[socket_nr][regnr] = idata;
        if (regnr >= PCIC_SYSMEM_ADDR0_START_LSB && regnr <= PCIC_CARDMEM_ADDR3_MSB) {
          int awindow = (regnr - PCIC_SYSMEM_ADDR0_START_LSB) / 8;
          int subaddr = (regnr - PCIC_SYSMEM_ADDR0_START_LSB) % 8;
          fprintf(stderr, "[ pcic: modify mem %d.%d = %02x ]\n", awindow, subaddr, idata);
          auto window = &d->mem_window[socket_nr][awindow];
          switch (subaddr) {
          case 0: // start lsb
            window->internal_low &= ~(0xff << 12);
            window->internal_low |= (idata & 0xff) << 12;
            break;

          case 1: // start msb
            window->internal_low &= ~(0x0f << 20);
            window->internal_low |= (idata & 0x0f) << 20;
            break;

          case 2: // stop lsb
            window->internal_high &= ~(0xff << 12);
            window->internal_high |= (idata & 0xff) << 12;            
            break;

          case 3: // stop msb
            window->internal_high &= ~(0x0f << 20);
            window->internal_high |= (idata & 0x0f) << 20;
            break;

          case 4: // cardmem a2
            window->cardmem_add &= ~(0xff << 12);
            window->cardmem_add |= (idata & 0xff) << 12;
            break;

          case 5: // cardmem a3
            window->cardmem_add &= ~(0x3f << 20);
            window->cardmem_add |= (idata & 0x3f) << 20;
            window->regspace = !!(idata & 0x40);
            window->readonly = !!(idata & 0x80);
            break;
          }
          fprintf(stderr, "[ pcic: %d.%d check mem %c %08x-%08x+%08x ]\n", socket_nr, awindow, window->regspace ? 'R' : 'm', (unsigned int)window->internal_low, (unsigned int)window->internal_high, window->cardmem_add);
        }
      } else {
        odata = d->regs[socket_nr][regnr];
      }
      break;
    }
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

int pcic_controller_private_access
(struct cpu *cpu, struct pcic_data *d, int relative_addr, uint8_t *data, int len, int writeflag)
{
  uint32_t idata;

	if (writeflag == MEM_WRITE) {
    if (len == sizeof(idata)) {
      memcpy(&idata, data, len);
    } else {
      return 1;
    }
    
    d->range_claim_check = ~0;

    switch (relative_addr) {
    case DEV_PCIC_CLAIM_IO_ADDR:
      // Check our io range registers.
      for (int c = 0; c < 2; c++) {
        for (int s = 0; s < 2; s++) {
          for (int i = 0; i < 2; i++) {
            if (d->controller[c].regs[s][PCIC_ADDRWIN_ENABLE] & (1 << (6 + i))) {
              if (d->controller[c].io_window[s][i].low <= idata &&
                  d->controller[c].io_window[s][i].high >= idata) {
                // Do pass.
                d->range_claim_check = DEV_PCIC_CARD_IO_SPACE(2 * c + s) + idata;
                break;
              }
            }
          }
        }
      }
      break;

    case DEV_PCIC_CLAIM_MEM_ADDR:
      // Check our mem range registers.
      fprintf(stderr, "[ pcic: check for mem map at %08x.%d ]\n", (unsigned int)idata, len);
      for (int c = 0; c < 2; c++) {
        for (int s = 0; s < 2; s++) {
          for (int i = 0; i < 5; i++) {
            bool enabled = d->controller[c].regs[s][PCIC_ADDRWIN_ENABLE] & (1 << i);
            if (enabled) {
              if (d->controller[c].mem_window[s][i].internal_low <= idata &&
                  (d->controller[c].mem_window[s][i].internal_high | 0xfff) >= idata) {
                // Do pass.
                if (d->controller[c].mem_window[s][i].regspace) {
                  d->range_claim_check = DEV_PCIC_CARD_REG_SPACE(2 * c + s) + ((idata + d->controller[c].mem_window[s][i].cardmem_add) & 0x3ffffff);
                } else {
                  d->range_claim_check = DEV_PCIC_CARD_MEM_SPACE(2 * c + s) + ((idata + d->controller[c].mem_window[s][i].cardmem_add) & 0x3ffffff);
                }
                fprintf(stderr, "[ pcic: %d.%d.%d check mem %c %08x-%08x+%08x => %08x ]\n", c, s, i, d->controller[c].mem_window[s][i].regspace ? 'R' : 'm', (unsigned int)d->controller[c].mem_window[s][i].internal_low, (unsigned int)d->controller[c].mem_window[s][i].internal_high, d->controller[c].mem_window[s][i].cardmem_add, d->range_claim_check);
                break;
              }
            }
          }
        }
      }
      break;
    }
  } else {
    switch (relative_addr) {
    case DEV_PCIC_CHECK_CLAIM:
      // Return the address within pcmcia card space that should be retrieved given the
      // previous io or memory check.
      if (writeflag == MEM_READ) {
        idata = d->range_claim_check;
      }
      break;
    }

    if (len == sizeof(idata)) {
      memcpy(data, &idata, len);
    }
  }
  
  return 1;
}

DEVICE_ACCESS(pcic)
{
	struct pcic_data *d = (struct pcic_data *) extra;
  bool controller = !!(relative_addr & PCIC_IOSIZE);
  return pcic_controller_access
    (cpu, controller, d, &d->controller[controller], relative_addr & (PCIC_IOSIZE - 1), data, len, writeflag);
}


DEVICE_ACCESS(pcic_private)
{
	struct pcic_data *d = (struct pcic_data *) extra;
  return pcic_controller_private_access
    (cpu, d, relative_addr & 15, data, len, writeflag);
}


static void pcic_interrupt_assert(struct interrupt *i) {
  struct pcic_data *d = (struct pcic_data *)i->extra;
  int controller = i->line >> 1;
  int socket = i->line & 1;
  auto regs = &d->controller[controller].regs[socket][0];
  if (!(regs[PCIC_INTR] & PCIC_INTR_ENABLE)) {
    auto irq = d->controller[controller].regs[socket][PCIC_INTR] & PCIC_INTR_IRQ_MASK;
    INTERRUPT_ASSERT(d->irq[irq]);
  }
}


static void pcic_interrupt_deassert(struct interrupt *i) {
  struct pcic_data *d = (struct pcic_data *)i->extra;
  int controller = i->line >> 1;
  int socket = i->line & 1;
  auto regs = &d->controller[controller].regs[socket][0];
  if (!(regs[PCIC_INTR] & PCIC_INTR_ENABLE)) {
    auto irq = d->controller[controller].regs[socket][PCIC_INTR] & PCIC_INTR_IRQ_MASK;
    INTERRUPT_DEASSERT(d->irq[irq]);
  }
}


// XXX not machine authentic.  an rs/6000 model 860 or 850 has memory cards in both sockets
// in the first controller.
// This should reflect that.
void pcic_socket_setup(struct pcic_data *d, struct pcic_controller *c, const char *intpath, bool controller, bool socket) {
  auto regs = &c->regs[socket][0];
  char tmpstr[200];
  
  regs[PCIC_IDENT] = 0x82; // Memory and IO, revision 0010
  // Ready, CD0, CD1 (CD0 = CD1 = 0 means card detected), battery good.  Power on.
  regs[PCIC_IF_STATUS] = 0x6f; 
  regs[PCIC_PWRCTL] = 0;
  regs[PCIC_INTR] = 0;
  regs[PCIC_CSC] = 0; // Battery not dead, no warning, no ready changes.
  regs[PCIC_CSC_INTR] = 0;
  regs[PCIC_IOCTL] = 0;
  regs[PCIC_ADDRWIN_ENABLE] = 0;

  c->asserted = -1;

  sprintf(tmpstr, "%s.pcic[%i].%i", intpath, (int)controller, (int)socket);
  fprintf(stderr, "[ pcic: register interrupt %s ]\n", tmpstr);
  c->inbound_irq[socket].name = strdup(tmpstr);
  c->inbound_irq[socket].line = controller << 1 | socket;
  c->inbound_irq[socket].extra = d;
  c->inbound_irq[socket].interrupt_assert = pcic_interrupt_assert;
  c->inbound_irq[socket].interrupt_deassert = pcic_interrupt_deassert;
  interrupt_handler_register(&c->inbound_irq[socket]);
}

DEVINIT(pcic)
{
	char tmpstr[200];
	struct pcic_data *d;

	CHECK_ALLOCATION(d = (struct pcic_data *) malloc(sizeof(struct pcic_data)));
	memset(d, 0, sizeof(struct pcic_data));

  pcic_socket_setup(d, &d->controller[0], devinit->interrupt_path, false, false);
  pcic_socket_setup(d, &d->controller[0], devinit->interrupt_path, false, true);
  pcic_socket_setup(d, &d->controller[1], devinit->interrupt_path, true, false);
  pcic_socket_setup(d, &d->controller[1], devinit->interrupt_path, true, true);

  for (int i = 0; i < 16; i++) {
    sprintf(tmpstr, "%s.%i", devinit->interrupt_path, i);
    INTERRUPT_CONNECT(tmpstr, d->irq[i]);
  }

	memory_device_register
    (devinit->machine->memory, devinit->name,
     devinit->addr, DEV_PCIC_LENGTH * 2,
     dev_pcic_access, (void *)d, DM_DEFAULT, NULL);

  memory_device_register
    (devinit->machine->memory, "pcic private",
     DEV_PCIC_PRIVATE_IO_AREA, DEV_PCIC_PRIVATE_IO_AREA_SIZE,
     dev_pcic_private_access, (void *)d, DM_DEFAULT, NULL);

	return 1;
}

