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
 *  COMMENT: Motorola MPC105 "Eagle" host bridge
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_isa.h"
#include "bus_pci.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "interrupt.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

struct eagle_glob eagle_comm;

DEVICE_ACCESS(eagle)
{
	struct eagle_data *d = (struct eagle_data *) extra;
	uint64_t idata = 0, odata = 0;
	int bus, dev, func, reg;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);

	/*
	 *  Pass accesses to ISA ports 0xcf8 and 0xcfc onto bus_pci_*:
	 */

	switch (relative_addr) {

	case 0:	/*  Address:  */
		bus_pci_decompose_1(idata, &bus, &dev, &func, &reg);
		bus_pci_setaddr(cpu, d->pci_data, bus, dev, func, reg);
		break;

	case 4:	/*  Data:  */
		bus_pci_data_access(cpu, d->pci_data, writeflag == MEM_READ?
		    &odata : &idata, len, writeflag);
		break;
	}

	if (writeflag == MEM_READ)
      memory_writemax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN, odata);

	return 1;
}

int io_pass(struct cpu *cpu, struct eagle_data *d, int writeflag, bool io_space, uint32_t real_addr, uint8_t *data, int len) {
	uint64_t idata = 0, odata = 0;
	uint8_t data_buf[4];
  uint64_t target_addr = bus_pci_get_io_target(cpu, d->pci_data, true, real_addr, len);

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);
/*
 *      cpu             the cpu doing the read/write
 *      mem             the memory object to use
 *      vaddr           the virtual address
 *      data            a pointer to the data to be written to memory, or
 *                      a placeholder for data when reading from memory
 *      len             the length of the 'data' buffer
 *      writeflag       set to MEM_READ or MEM_WRITE
 *      misc_flags      CACHE_{NONE,DATA,INSTRUCTION} | other flags
 */
		fprintf(stderr, "[ eagle: PCI io passthrough write %08x = %08x ]\n", real_addr, idata);
    data_buf[0] = idata;
    data_buf[1] = idata >> 8;
    data_buf[2] = idata >> 16;
    data_buf[3] = idata >> 24;
    if (target_addr) {
      cpu->memory_rw(cpu, cpu->mem, target_addr, data_buf, len, MEM_WRITE, PHYSICAL);
    }
	} else {
    cpu->memory_rw(cpu, cpu->mem, target_addr, data_buf, len, MEM_READ, PHYSICAL);
    odata = data_buf[0] | (data_buf[1] << 8) | (data_buf[2] << 16) | (data_buf[3] << 24);
		fprintf(stderr, "[ eagle: PCI io passthrough read %08x -> %08x ]\n", real_addr, odata);
		memory_writemax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN, odata);
	}

  return 1;
}

DEVICE_ACCESS(eagle_io_pass)
{
	struct eagle_data *d = (struct eagle_data *) extra;

	uint32_t real_addr = relative_addr + 0x1000000;
  return io_pass(cpu, d, writeflag, true, real_addr, data, len);
}

DEVICE_ACCESS(eagle_mem_pass)
{
	struct eagle_data *d = (struct eagle_data *) extra;

	uint32_t real_addr = relative_addr;
  return io_pass(cpu, d, writeflag, false, real_addr, data, len);
}

DEVICE_ACCESS(eagle_800)
{
    struct eagle_data *d = (struct eagle_data *) extra;
    uint64_t idata = 0, odata = 0;

    if (writeflag == MEM_WRITE)
        odata = idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);

    switch (relative_addr)
    {
    case 0x00:
        if (writeflag == MEM_READ) odata = 6;
        break;

    case 0x08:
        // Spammy: hd light
        return 1;

        // 9.10.2 Equipment Presence Register
        // MSB | D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 | LSB
        //       |    |    |    |    |    |    |    |
        //       |    |    |    |    |    |    |    +---- L2 cache absent
        //       |    |    |    |    |    |    +--------- No proc upgrade
        //       |    |    |    |    |    +-------------- L2 cache 256k
        //       |    |    |    |    +------------------- L2 cache copy-back
        //       |    |    |    +------------------------ PCI Presence Detect 1 (0 means present)
        //       |    |    +----------------------------- PCI Presence Detect 2 (0 means present)
        //       |    +---------------------------------- SCSI Fuse (0 means blown, 1 means good)
        //       +--------------------------------------- Reserved
    case 0x0c:
        if (writeflag == MEM_READ) odata = 0x7e;
        break;

    case 0x10:
        if (writeflag == MEM_READ) {
            odata = eagle_comm.password_protect_1;
        } else {
            eagle_comm.password_protect_1 |= idata;
        }
        break;

      case 0x12:
        if (writeflag == MEM_READ) {
            odata = eagle_comm.password_protect_2;
        } else {
            eagle_comm.password_protect_2 |= idata;
        }
        break;

    case 0x1c:
      if (writeflag == MEM_READ) {
        odata = d->l2_cache;
      } else {
        d->l2_cache = idata & 0xe1;
      }
      break;
    }

    fprintf(stderr, "[ unknown-800 %s %x -> %x ]\n", writeflag == MEM_WRITE ? "write" : "read", relative_addr, odata);

    if (writeflag == MEM_READ)
        memory_writemax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN, odata);

    return 1;
}


DEVICE_ACCESS(eagle_680)
{
    uint64_t idata = 0;

    if (writeflag == MEM_WRITE)
        idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);

    // fprintf(stderr, "[ unknown-680 %s %x -> %x ]\n", writeflag == MEM_WRITE ? "write" : "read", relative_addr, idata);

    return 1;
}


DEVICE_ACCESS(eagle_dma_1)
{
    struct eagle_data *d = (struct eagle_data *) extra;
    uint64_t idata = 0;

    if (writeflag == MEM_WRITE) {
        idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);
    }

    /*
      [ APIC-4d0 write 14 -> 0 ]
      [ eagle:dma20 write a -> 6 ]
      [ eagle:dma20 write b -> 56 ]
      [ memory_rw(): write data={22} paddr=0x8000040b >= physical_max; pc=0x00006df4 <out8> ]
      [ eagle:dma20 write c -> 0 ]
      [ eagle:dma20 write 4 -> 80 ]
      [ eagle:dma20 write 4 -> 1f ]
      [ eagle:dma80 write 1 -> 20 ]
      [ memory_rw(): write data={80} paddr=0x80000481 >= physical_max; pc=0x00006df4 <out8> ]
      [ eagle:dma20 write c -> 0 ]
      [ eagle:dma20 write 5 -> ff ]
      [ eagle:dma20 write 5 -> 1 ]
      [ eagle:dma20 write a -> 2 ]
    */
    switch (relative_addr) {
    case 12:
      if (writeflag == MEM_WRITE) {
        d->addr_low_high_latch = 0;
        d->len_low_high_latch = 0;
      }
      break;

    case 4:
      if (writeflag == MEM_WRITE) {
        if (!d->addr_low_high_latch) {
            eagle_comm.eagle_comm_area[0] = idata;
        } else {
            eagle_comm.eagle_comm_area[1] = idata;
        }
        d->addr_low_high_latch = !d->addr_low_high_latch;
      }
      break;

    case 5:
      if (writeflag == MEM_WRITE) {
        if (!d->len_low_high_latch) {
            eagle_comm.eagle_comm_area[4] = idata;
        } else {
            eagle_comm.eagle_comm_area[5] = idata;
        }
        d->len_low_high_latch = !d->len_low_high_latch;
      }
      break;

    case 8:
      if (writeflag != MEM_WRITE) {
        INTERRUPT_DEASSERT(d->irq);
        idata = eagle_comm.eagle_comm_area[7] | d->fin_mask;
        eagle_comm.eagle_comm_area[7] = 0;
        d->fin_mask = 0;
      }
      break;

    case 10:
      /*
      if (writeflag == MEM_WRITE) {
        if (idata & 4) {
          eagle_comm_area[7] = 0;
        } else {
          if (eagle_comm_area[7]) {
            INTERRUPT_ASSERT(d->irq);
          }
        }
      }
      */
      break;
    }

    fprintf(stderr, "[ eagle:dma1 %s %x -> %x ]\n", writeflag == MEM_WRITE ? "write" : "read", relative_addr, idata);

    if (writeflag != MEM_WRITE) {
      memory_writemax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN, idata);
    }

    return 1;
}


DEVICE_ACCESS(eagle_dma_2)
{
  struct eagle_data *d = (struct eagle_data *) extra;
  uint64_t idata = 0;

  if (writeflag == MEM_WRITE) {
    idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);
    d->dma_page[relative_addr] = idata;
    if (relative_addr == 1) {
        eagle_comm.eagle_comm_area[2] = idata;
    }
  }

  fprintf(stderr, "[ eagle:dma2 %s %x -> %x ]\n", writeflag == MEM_WRITE ? "write" : "read", relative_addr, idata);

  return 1;
}


DEVICE_ACCESS(eagle_dma_80)
{
  struct eagle_data *d = (struct eagle_data *) extra;
  uint64_t idata = 0;

  if (writeflag == MEM_WRITE) {
    idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);
    d->dma_page[relative_addr] = idata;
    if (relative_addr == 1) {
        eagle_comm.eagle_comm_area[2] = idata;
    }
  }

  fprintf(stderr, "[ eagle:dma80 %s %x -> %x ]\n", writeflag == MEM_WRITE ? "write" : "read", relative_addr, idata);

  return 1;
}


DEVICE_ACCESS(eagle_398)
{
    struct eagle_data *d = (struct eagle_data *) extra;
    uint64_t idata = 0;

    if (writeflag == MEM_WRITE) {
        idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);
    }

    fprintf(stderr, "[ unknown-398: %s %x -> %x ]\n", writeflag == MEM_WRITE ? "write" : "read", relative_addr, idata);

    return 1;
}

DEVICE_ACCESS(eagle_480)
{
    struct eagle_data *d = (struct eagle_data *) extra;
    uint64_t idata = 0;

    if (writeflag == MEM_WRITE) {
        idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);
        d->dma_high[relative_addr] = idata;
        if (relative_addr == 1) {
            eagle_comm.eagle_comm_area[3] = idata;
        }
    } else {
        idata = d->dma_high[relative_addr];
    }

    fprintf(stderr, "[ dma high: %s %x -> %x ]\n", writeflag == MEM_WRITE ? "write" : "read", relative_addr, idata);

    return 1;
}

DEVICE_ACCESS(eagle_830)
{
  struct eagle_data *d = (struct eagle_data *) extra;
  uint64_t idata = 0;

  if (writeflag == MEM_WRITE) {
    idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);
  }

  fprintf(stderr, "[ unknown-830: %s %x -> %x ]\n", writeflag == MEM_WRITE ? "write" : "read", relative_addr, idata);

  return 1;
}

DEVICE_ACCESS(eagle_850)
{
  struct eagle_data *d = (struct eagle_data *) extra;
  uint64_t idata = 0;

  if (writeflag == MEM_WRITE) {
    idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);
  }

  fprintf(stderr, "[ unknown-850: %s %x -> %x ]\n", writeflag == MEM_WRITE ? "write" : "read", relative_addr, idata);

  if (writeflag == MEM_READ) {
    if (relative_addr == 2) {
      idata = 0xda;
    }

    memory_writemax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN, idata);
  }

  return 1;
}

DEVICE_ACCESS(eagle_880)
{
    struct eagle_data *d = (struct eagle_data *) extra;
    uint64_t idata = 0, odata = 0;

    if (writeflag == MEM_WRITE) {
        idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);
    }

    // It's scanning this table:
    // 0x000fd840  c1020101 c2020303 d4010f00 c4020f0f  ................
    // 0x000fd850  b4010f00 b2010300 a2020303 d2010300  ................
    // 0x000fd860  ff000000 00000000 00000000 50414e49  ............PANI
    // 0x000fd870  433a2041 6c6c2062 616e6b73 20646973  C: All banks dis
    //
    // Trying to find a match for r6.
    //
    // Hypothesis is that we should see one of these values from
    // eagle_880
    //
    // Each of these represents memory bank presence.  Populating all 4
    // with the first element in the above table makes memory test pass.
    // c1s: 24Mb
    // c2s: 48Mb
    // c4s: 20000 memory
    // b2s: 40Mb
    // d4s: 20000 memory
    // a2s: panic: all banks disable
    // multiple set (0xc1c1) - same as 0xc1
    // 0 and 4 return 0xc2, 8 and c return 0? 20000 memory
    // 0 and 4 return 0xc2, 8 and c return 0xff: 2 banks present, 2 absent, 32mb.
    // (the default configuration for the RS/6000 model 860).
    switch (relative_addr) {
    case 0:
        odata = 0xc2;
        break;

    case 4:
        odata = 0xc2;
        break;

    case 8:
        odata = 0xff;
        break;

    case 0xc:
        odata = 0xff;
        break;
    }

    fprintf(stderr, "[ unknown-880: %s %x -> %x ]\n", writeflag == MEM_WRITE ? "write" : "read", relative_addr, odata);

    if (writeflag == MEM_READ)
        memory_writemax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN, odata);

    return 1;
}

DEVICE_ACCESS(eagle_8a0)
{
    struct eagle_data *d = (struct eagle_data *) extra;
    uint64_t idata = 0, odata = 0;

    if (writeflag == MEM_WRITE) {
        idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);
    }

    switch (relative_addr) {
    case 0:
        odata = 0xff;
        break;

    case 1:
        odata = 0;
        break;
    }

    fprintf(stderr, "[ unknown-8a0: %s %x -> %x ]\n", writeflag == MEM_WRITE ? "write" : "read", relative_addr, odata);

    if (writeflag == MEM_READ)
      memory_writemax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN, odata);

    return 1;
}

DEVICE_ACCESS(eagle_dma_scatter_gather) {
    struct eagle_data *d = (struct eagle_data *) extra;
    uint64_t idata = 0;

    if (writeflag == MEM_WRITE) {
        // TODO
    } else {
        idata = eagle_comm.eagle_comm_area[8];
        eagle_comm.eagle_comm_area[8] = 0;
    }

    fprintf(stderr, "[ dma scatter gather: %s %x -> %x ]\n", writeflag == MEM_WRITE ? "write" : "read", relative_addr, idata);

    return 1;
}


DEVICE_ACCESS(eagle_4d0)
{
  uint64_t idata = 0;

  if (writeflag == MEM_WRITE)
    idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);

  fprintf(stderr, "[ APIC-4d0 %s %x -> %x ]\n", writeflag == MEM_WRITE ? "write" : "read", relative_addr, idata);

  return 1;
}


DEVICE_TICK(eagle) {
  struct eagle_data *d = (struct eagle_data *) extra;
}


DEVINIT(eagle)
{
	struct eagle_data *d;
	uint64_t pci_io_offset, pci_mem_offset;
	uint64_t isa_portbase = 0, isa_membase = 0;
	uint64_t pci_portbase = 0, pci_membase = 0;
	char pci_irq_base[300];
	char isa_irq_base[300];

	CHECK_ALLOCATION(d = (struct eagle_data *) malloc(sizeof(struct eagle_data)));
	memset(d, 0, sizeof(struct eagle_data));

	/*  The interrupt path to the CPU at which we are connected:  */
	INTERRUPT_CONNECT(devinit->interrupt_path, d->irq);

	/*
	 *  According to http://www.beatjapan.org/mirror/www.be.com/
	 *  aboutbe/benewsletter/Issue27.html#Cookbook :
	 *
	 *  "HARDWARE MEMORY MAP
	 *   The MPC105 defines the physical memory map of the system as
	 *   follows:
	 *
	 *   Start        Size         Description
	 *
	 *   0x00000000   0x40000000   Physical RAM
	 *   0x40000000   0x40000000   Other system memory
	 *                             (motherboard glue regs)
	 *   0x80000000   0x00800000   ISA I/O
	 *   0x81000000   0x3E800000   PCI I/O
	 *   0xBFFFFFF0   0x00000010   PCI/ISA interrupt acknowledge
	 *   0xC0000000   0x3F000000   PCI memory
	 *   0xFF000000   0x01000000   ROM/flash
	 */

	/*  TODO: Make these work like the BE web page stated...  */
	pci_io_offset  = 0x80000000ULL;
	pci_mem_offset = 0xc0000000ULL;
	pci_portbase   = 0x81000000ULL;
	pci_membase    = 0x00000000ULL;
	isa_portbase   = 0x80000000ULL;
	isa_membase    = 0xc0000000ULL;

	switch (devinit->machine->machine_type) {
	/* case MACHINE_BEBOX:
		snprintf(pci_irq_base, sizeof(pci_irq_base), "%s.bebox",
		    devinit->interrupt_path);
		snprintf(isa_irq_base, sizeof(isa_irq_base), "%s.bebox.5",
		    devinit->interrupt_path);
		break; */
	default:
		snprintf(pci_irq_base, sizeof(pci_irq_base), "%s",
		    devinit->interrupt_path);
		snprintf(isa_irq_base, sizeof(isa_irq_base), "%s",
		    devinit->interrupt_path);
	}

	/*  Create a PCI bus:  */
	d->pci_data = bus_pci_init(devinit->machine, devinit->interrupt_path,
	    pci_io_offset, pci_mem_offset,
	    pci_portbase, pci_membase, pci_irq_base,
	    isa_portbase, isa_membase, isa_irq_base);

	/*  Add the PCI glue for the controller itself:  */
	bus_pci_add(devinit->machine, d->pci_data,
	    devinit->machine->memory, 0, 0, 0, "eagle");

	/*  ADDR and DATA configuration ports in ISA space:  */
	memory_device_register(devinit->machine->memory, "eagle",
	    isa_portbase + BUS_PCI_ADDR, 8, dev_eagle_access, d,
	    DM_DEFAULT, NULL);

  memory_device_register(devinit->machine->memory, "eagle feature control",
        isa_portbase + 0x800, 0x20, dev_eagle_800_access, d,
        DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "eagle feature control",
        isa_portbase + 0x680, 0x10, dev_eagle_680_access, d,
        DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "8259 ELCR",
        isa_portbase + 0x4d0, 2, dev_eagle_4d0_access, d,
        DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "DMA 1",
        isa_portbase + 0, 0x20, dev_eagle_dma_1_access, d,
        DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "DMA 2",
        isa_portbase + 0xc0, 0x20, dev_eagle_dma_2_access, d,
        DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "legacy DMA 1 (floppy) 0x80 range",
        isa_portbase + 0x80, 4, dev_eagle_dma_80_access, d,
        DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "DMA Scatter/Gather",
        isa_portbase + 0x40a, 22, dev_eagle_dma_scatter_gather_access, d,
        DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "DMA page address",
        isa_portbase + 0x480, 4, dev_eagle_480_access, d,
        DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "8a0",
        isa_portbase + 0x8a0, 0x20, dev_eagle_8a0_access, d,
        DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "398",
        isa_portbase + 0x398, 8, dev_eagle_398_access, d,
        DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "830",
        isa_portbase + 0x830, 16, dev_eagle_830_access, d,
        DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "850",
                           isa_portbase + 0x850, 4, dev_eagle_850_access, d,
                           DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "880",
        isa_portbase + 0x880, 16, dev_eagle_880_access, d,
        DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "PCI IO Passthrough",
                           isa_portbase + 0x1000000, 0xbf800000 - 0x81000000, dev_eagle_io_pass_access, d,
                           DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "PCI IO Passthrough",
                           0xc0000000, 0x3ff00000, dev_eagle_mem_pass_access, d,
                           DM_DEFAULT, NULL);

    machine_add_tickfunction(devinit->machine, dev_eagle_tick, d, 19);

  switch (devinit->machine->machine_type) {

	/* case MACHINE_BEBOX:
		bus_isa_init(devinit->machine, isa_irq_base,
		    BUS_ISA_IDE0 | BUS_ISA_VGA, isa_portbase, isa_membase);
		bus_pci_add(devinit->machine, d->pci_data,
		    devinit->machine->memory, 0, 11, 0, "i82378zb");
		break; */

	case MACHINE_PREP:
        bus_pci_add(devinit->machine, d->pci_data,
            devinit->machine->memory, 0, 11, 0, "i82378zb");
        break;

	case MACHINE_MVMEPPC:
		bus_isa_init(devinit->machine, isa_irq_base,
		    BUS_ISA_LPTBASE_3BC, isa_portbase, isa_membase);

		switch (devinit->machine->machine_subtype) {
		case MACHINE_MVMEPPC_1600:
			bus_pci_add(devinit->machine, d->pci_data,
			    devinit->machine->memory, 0, 11, 0, "i82378zb");
			break;
		default:fatal("unimplemented machine subtype for "
			    "eagle/mvmeppc\n");
			exit(1);
		}
		break;

	default:fatal("unimplemented machine type for eagle\n");
		exit(1);
	}

	devinit->return_ptr = d->pci_data;

	return 1;
}

