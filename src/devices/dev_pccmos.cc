/*
 *  Copyright (C) 2005-2011  Anders Gavare.  All rights reserved.
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
 *  COMMENT: PC CMOS/RTC device (ISA ports 0x70 and 0x71)
 *
 *  The main point of this device is to be a "PC style wrapper" for accessing
 *  the MC146818 (the RTC). In most other respects, this device is bogus, and
 *  just acts as a 256-byte RAM device.
 *
 *  Added the 0x74-0x76 io space used by IBM NVRAM, as it's conveniently
 *  contiguous.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "diskimage.h"

extern int verbose;

#define	DEV_PCCMOS_LENGTH		2
#define	PCCMOS_MC146818_FAKE_ADDR	0x1d00000000ULL

#define DEV_PCCMOS_IBM_LENGTH   16
#define DEV_PCCMOS_IBM_NVRAM_LENGTH 8192

struct pccmos_data {
    unsigned char	select;
    unsigned char	ram[256];

    unsigned int    extended_select;
    unsigned int    extended_size;
    unsigned char  *extended_nvram;
    struct diskimage *extended_nvram_disk;

    unsigned char upper_half[8];
};

// Find the NVRAM disk if it exists.
static struct diskimage *find_nvram_disk(struct machine *machine) {
    struct diskimage *d = machine->first_diskimage;
    while (d != NULL) {
        if (d->type == DISKIMAGE_NVRAM) {
            return d;
        }
        d = d->next;
    }
    return d;
}

DEVICE_ACCESS(pccmos)
{
    struct pccmos_data *d = (struct pccmos_data *) extra;
    uint64_t idata = 0, odata = 0;
    unsigned char b = 0;
    int r = 1;

    if (writeflag == MEM_WRITE) {
        b = idata = memory_readmax64(cpu, data, len);
        //fprintf(stderr, "pccmos: write at relative addr %08" PRIx64 ": %08" PRIx64"\n", relative_addr, idata);
    } else {
      // fprintf(stderr, "pccmos: read at relative_addr %08x selector %d\n", relative_addr, d->select);
    }

    /*
     *  Accesses to CMOS register 0 .. 0xd are rerouted to the
     *  RTC; all other access are treated as CMOS RAM read/writes.
     */

    if (relative_addr == 1) {
        if (writeflag == MEM_WRITE) {
            d->select = idata;
        } else if (d->select == 0x80) {
            fprintf(stderr, "[ pccmos: faking selector 0x80 ]\n");
            odata = ++d->ram[0x80];
        } else {
            odata = d->select;
            if (d->select == 13) {
                odata = 0x80;
            }
        }
    } else if (relative_addr == 0 && writeflag == MEM_WRITE) {
        d->select = idata;
        // IBM machines have an extra 16k nvram (which appears in 4 copies)
        // in the 64k address space.  These will only be alive if we initialized
        // the size of the CMOS ISA device at 8 ports.
    } else if (relative_addr == 4) {
        if (writeflag == MEM_WRITE) {
            d->extended_select = (d->extended_select & 0xff00) | idata;
        } else {
            odata = d->extended_select & 0xff;
        }
    } else if (relative_addr == 5) {
        if (writeflag == MEM_WRITE) {
            d->extended_select = (d->extended_select & 0xff) | (idata << 8);
        } else {
            odata = (d->extended_select >> 8) & 0xff;
        }
    } else if (relative_addr == 6) {
        if (verbose && writeflag == MEM_WRITE) {
            debug
                ("[ extended nvram write %04x = %02x ]\n",
                 d->extended_select,
                 idata);
        }
        if (d->extended_nvram != NULL) {
            if (writeflag == MEM_WRITE) {
                d->extended_nvram
                    [d->extended_select % d->extended_size] = idata;
            } else {
                odata = d->extended_nvram
                    [d->extended_select % d->extended_size];
            }
        } else {
            uint8_t buf = idata;
            diskimage__internal_access
                (d->extended_nvram_disk,
                 writeflag,
                 d->extended_select,
                 &buf,
                 1);
            odata = buf;
        }
        if (verbose && writeflag != MEM_WRITE) {
            debug
                ("[ extended nvram read %04x -> %02x ]\n",
                 d->extended_select,
                 odata);
        }
    } else if (relative_addr > 7) {
        relative_addr &= 7;
        if (writeflag == MEM_WRITE) {
            idata &= 0xff;
            debug("[ write to cmos upper half: relative addr %d, %02x ]\n", relative_addr+8, (int)idata);
            d->upper_half[relative_addr] = idata;
        } else {
            odata = d->upper_half[relative_addr];
            if (relative_addr == 4) {
                d->upper_half[relative_addr] += 0x35;
            }
            debug("[ read cmos upper half: relative addr %d, %02x ]\n", relative_addr+8, (int)(odata & 0xff));
        }
    } else {
        if (d->select == 10) {
            odata = 0x40;
        } else if (d->select <= 0x0d) {
            if (writeflag == MEM_WRITE) {
                r = cpu->memory_rw(cpu, cpu->mem,
                                   PCCMOS_MC146818_FAKE_ADDR + 1, &b, 1,
                                   MEM_WRITE, PHYSICAL);
            } else {
                debug("[ pccmos delegating read to mc146818 (select %d) ]\n", d->select);
                r = cpu->memory_rw(cpu, cpu->mem,
                                   PCCMOS_MC146818_FAKE_ADDR + 1, &b, 1,
                                   MEM_READ, PHYSICAL);
                odata = b;
            }
        } else if (d->select == 13) {
            odata = 0xff;
        } else {
            odata = d->ram[d->select];
        }
    }

    if (r == 0)
        fatal("[ pccmos: memory_rw() error! ]\n");

    if (writeflag == MEM_READ) {
        memory_writemax64(cpu, data, len, odata);
        //fprintf(stderr, "pccmos: read at relative addr %08" PRIx64" (dselect %08x): %08" PRIx64"\n", relative_addr, d->select, odata);
    }

    return 1;
}

DEVINIT(pccmos)
{
	int type = MC146818_PC_CMOS, len = DEV_PCCMOS_LENGTH;
	struct pccmos_data *d;

	CHECK_ALLOCATION(d = (struct pccmos_data *) malloc(sizeof(struct pccmos_data)));
	memset(d, 0, sizeof(struct pccmos_data));

	switch (devinit->machine->machine_type) {
	case MACHINE_CATS:
	case MACHINE_NETWINDER:
		type = MC146818_CATS;
		d->ram[0x48] = 20;		/*  century  */
		len = DEV_PCCMOS_LENGTH * 2;
		break;
	case MACHINE_ALGOR:
		type = MC146818_ALGOR;
		break;
	case MACHINE_ARC:
		fatal("\nARC pccmos: TODO\n\n");
		type = MC146818_ALGOR;
		break;
	case MACHINE_EVBMIPS:
		/*  Malta etc.  */
		type = MC146818_ALGOR;
		break;
	case MACHINE_QEMU_MIPS:
	case MACHINE_COBALT:
	case MACHINE_PREP:
        if (devinit->machine->machine_subtype == MACHINE_PREP_IBM860) {
            len = DEV_PCCMOS_IBM_LENGTH;
            d->extended_size = DEV_PCCMOS_IBM_NVRAM_LENGTH;
            struct diskimage *nvdisk = find_nvram_disk(devinit->machine);
            if (nvdisk) {
                d->extended_nvram_disk = nvdisk;
                d->extended_nvram = NULL;
            } else {
                d->extended_nvram_disk = NULL;
                d->extended_nvram = (unsigned char *)malloc(d->extended_size);
            }
        }
        break;
	case MACHINE_MVMEPPC:
	case MACHINE_ALPHA:
	case MACHINE_IYONIX:	// TODO: not sure about exact type.
		break;
	default:fatal("devinit_pccmos(): unimplemented machine type"
		    " %i\n", devinit->machine->machine_type);
		exit(1);
	}

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, len, dev_pccmos_access, (void *)d,
	    DM_DEFAULT, NULL);

	dev_mc146818_init(devinit->machine, devinit->machine->memory,
	    PCCMOS_MC146818_FAKE_ADDR, devinit->interrupt_path, type, 1);

	return 1;
}

