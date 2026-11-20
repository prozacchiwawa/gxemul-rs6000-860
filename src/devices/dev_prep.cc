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
 *  COMMENT: PReP machine mainbus (ISA bus + interrupt controller)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_isa.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


struct prep_data {
};

DEVINIT(prep)
{
	struct prep_data *d;
    char tmps[300];

	CHECK_ALLOCATION(d = (struct prep_data *) malloc(sizeof(struct prep_data)));
	memset(d, 0, sizeof(struct prep_data));

  switch (devinit->machine->machine_subtype) {
    case MACHINE_PREP_IBM860:
    case MACHINE_PREP_IBM850:
        snprintf(tmps, sizeof(tmps), "pcic addr="
                 "0x8086800003e0 irq=%s.pci", devinit->interrupt_path);
        device_add(devinit->machine, tmps);

        snprintf(tmps, sizeof(tmps), "ibm_ethernet_ii addr=0x%" PRIx64 " irq=%s.pci.pcic[0].0", DEV_PCIC_CARD_SPACE(0), devinit->interrupt_path);
        device_add(devinit->machine, tmps);
        break;
    default:
        snprintf(tmps, sizeof(tmps), "%s.pci", devinit->interrupt_path);
        /*  This works for at least the IBM 6050:  */
        bus_isa_init(devinit->machine, devinit->interrupt_path, tmps,
            BUS_ISA_IDE0 | BUS_ISA_IDE1, 0x80000000, 0xc0000000);
        break;
    }
	return 1;
}

