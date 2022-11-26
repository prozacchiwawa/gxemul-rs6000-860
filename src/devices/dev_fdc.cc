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
 *  COMMENT: PC-style floppy controller
 *
 *  TODO!  (This is just a dummy skeleton right now.)
 *
 *  TODO 2: Make it work nicely with both ARC and PC emulation.
 *
 *  See http://members.tripod.com/~oldboard/assembly/765.html for a
 *  quick overview.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "diskimage.h"
#include "interrupt.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#define MIN(x,y) (((x) < (y)) ? (x) : (y))


#define	DEV_FDC_LENGTH		6	/*  TODO 8, but collision with wdc  */

#define STATE_CMD_BYTES 0x8000
#define STATE_CMD_QUEUE 0x4000
#define STATE_CMD_DMA 0x2000
#define STATE_CMD_BUSY 0x1000
#define STATE_EMPTY 0
#define STATE_VERSION 1
#define STATE_SPECIFY 0x03
#define STATE_RECAL 0x07
#define SENSE_INTERRUPT 0x08
#define STATE_SEEK 0x0f
#define STATE_CONFIGURE 0x13
#define STATE_READ_ID 0x0a
#define STATE_READ_NORMAL_DATA 0x06

struct fdc_data {
	uint8_t			reg[DEV_FDC_LENGTH];
	int                     state;
	int                     command_size;
	int			command_result;
	int			command_bytes[16];
	int			seek_head, seek_track;
	int			read_sector, eot_sector;
	int			status_read;
	struct interrupt	irq;
};


DEVICE_ACCESS(fdc)
{
	struct fdc_data *d = (struct fdc_data *) extra;
	uint64_t idata = 0, read_addr = 0, read_len = 0, offset = 0;
	int oldstate = d->state;
	size_t i;
  unsigned char eagle_dma_2[8], sector[512];

	d->state = 0;

	idata = memory_readmax64(cpu, data, len);

	if (writeflag == MEM_WRITE) {
		d->reg[relative_addr] = (uint8_t)idata;
	}

	INTERRUPT_DEASSERT(d->irq);

	switch (relative_addr) {
	case 0x04:
		if (writeflag == MEM_WRITE) {
			fprintf(stderr, "[ fdc: Status write %02x ]\n", (int)idata);
		} else {
			if (oldstate & STATE_CMD_QUEUE) {
				idata = 0xd0; /* STATUS_DIR | STATUS_READY | STATUS_BUSY */
			} else {
				idata = 0x80 | ((oldstate & STATE_CMD_BUSY) ? 0x10 : ((oldstate & STATE_CMD_BYTES) ? 0x40 : 0)) | ((oldstate & STATE_CMD_DMA) ? 0x20 : 0);
			}
			d->state = oldstate & ~(STATE_CMD_BUSY | STATE_CMD_DMA);
			if (d->status_read != (int)idata) {
				fprintf(stderr, "[ fdc: Status read %02x ]\n", (int)idata);
        d->status_read = idata;
			}
			memory_writemax64(cpu, data, len, idata);
		}
		break;
	
	case 0x05:
		if (writeflag == MEM_WRITE) {
			fprintf(stderr, "[ fdc: FIFO write %02x cq %d state %04x ]\n", (int)idata, d->command_size, oldstate);
			if (oldstate & STATE_CMD_BYTES) {
				d->command_size--;
				d->command_bytes[d->command_size] = idata;
				if (!d->command_size) {
					int command = oldstate & 0xff;
          int low16_dma_addr;

					oldstate = (oldstate & ~STATE_CMD_BYTES) | STATE_CMD_BUSY;
					fprintf(stderr, "execute command %02x\n", oldstate & 0xff);
					switch (command) {
					case STATE_SEEK:
						d->seek_head = d->command_bytes[1] >> 1;
						d->seek_track = d->command_bytes[0];
            d->command_bytes[1] = 0x20;
            d->command_bytes[0] = d->seek_track;
            d->command_result = 2;
            d->state = STATE_SEEK | STATE_CMD_QUEUE;
            if (d->reg[2] & 8) {
              INTERRUPT_ASSERT(d->irq);
            }
						break;

					case STATE_RECAL:
            d->command_result = 0;
						d->state = STATE_RECAL;
            if (d->reg[2] & 8) {
              INTERRUPT_ASSERT(d->irq);
            }
						break;

					case STATE_SPECIFY:
            d->command_result = 0;
						d->state = STATE_EMPTY;
            if (d->reg[2] & 8) {
              INTERRUPT_ASSERT(d->irq);
            }
						break;

					case STATE_CONFIGURE:
            d->command_result = 0;
						d->state = STATE_EMPTY;
            if (d->reg[2] & 8) {
              INTERRUPT_ASSERT(d->irq);
            }
						break;

					case STATE_READ_ID:
						d->state = STATE_READ_ID | STATE_CMD_QUEUE;
						d->command_result = 7;
						d->command_bytes[6] = 0x20;
						d->command_bytes[5] = 0;
						d->command_bytes[4] = 0;
						d->command_bytes[3] = d->seek_track;
						d->command_bytes[2] = d->seek_head;
						d->command_bytes[1] = 0;
						d->command_bytes[0] = 2;
            if (d->reg[2] & 8) {
              INTERRUPT_ASSERT(d->irq);
            }
						break;

					case STATE_READ_NORMAL_DATA:
						d->state = STATE_READ_NORMAL_DATA | STATE_CMD_QUEUE | STATE_CMD_DMA;
						d->seek_track = d->command_bytes[6];
						d->seek_head = d->command_bytes[5];
						d->read_sector = d->command_bytes[4];
						d->eot_sector = d->command_bytes[2];
						fprintf(stderr, "[ fdc: read C%d H%d S%d ]\n", d->seek_track, d->seek_head, d->read_sector);
						d->command_result = 7;
						d->command_bytes[6] = 0;
						d->command_bytes[5] = 0;
						d->command_bytes[4] = 0;
						d->command_bytes[3] = d->seek_track;
						d->command_bytes[2] = d->seek_head;
						d->command_bytes[1] = d->read_sector;
						d->command_bytes[0] = 2;

            // Ask the DMA system where to send the data in memory.
            memcpy(eagle_dma_2, eagle_comm_area, 8);
            eagle_comm_area[7] = 4;
            eagle_comm_area[8] = 0xff;

            // Read addr programmed into DMA 2
            // Note: lower 16 bits are shifted left 1 because dma is in
            // 16 bit chunks.
            low16_dma_addr = (eagle_dma_2[0] | (eagle_dma_2[1] << 8)) << 1;
            read_addr = (low16_dma_addr | (eagle_dma_2[2] << 16) | (eagle_dma_2[3] << 24)) & 0x7fffffff;
            // Read len programmed into DMA 2
            read_len = ((eagle_dma_2[4] & 0xff) | ((eagle_dma_2[5] & 0xff) << 8)) + 1;

            fprintf(stderr, "[ fdc: read to %08" PRIx64" len %08" PRIx64" ]\n", read_addr, read_len);
            if (diskimage_exist(cpu->machine, 0, DISKIMAGE_FLOPPY)) {
              // LBA = (cylinder * number_of_heads + head) * sectors_per_track + sector - 1
              offset = 512 * ((d->seek_track * 2 + d->seek_head) * 18 + d->read_sector - 1);

              while (read_len > 0) {
                fprintf(stderr, "[ fdc: read diskette from %08x to addr %08" PRIx64" len %08" PRIx64" ]\n", offset, read_addr, read_len);
                diskimage_access(cpu->machine, 0, DISKIMAGE_FLOPPY, 0, offset, sector, 512);
                fprintf(stderr, "[ sector: %02x %02x %02x %02x ... %02x %02x %02x %02x ]\n", sector[0], sector[1], sector[2], sector[3], sector[508], sector[509], sector[510], sector[511]);

                /*
                for (i = 0; i < 128; i++) {
                  unsigned char buf[4];
                  buf[0] = sector[i * 4 + 3];
                  buf[1] = sector[i * 4 + 2];
                  buf[2] = sector[i * 4 + 1];
                  buf[3] = sector[i * 4 + 0];
                  memcpy(sector + i * 4, buf, 4);
                }
                */

                cpu->memory_rw(cpu, cpu->mem, read_addr, sector, 512, MEM_WRITE, PHYSICAL | NO_EXCEPTIONS);

                // XXX Check that we read it
                cpu->memory_rw(cpu, cpu->mem, read_addr, eagle_dma_2, 8, MEM_READ, PHYSICAL | NO_EXCEPTIONS);
                fprintf(stderr, "[ read back %02x %02x %02x %02x ... ]\n", eagle_dma_2[0], eagle_dma_2[1], eagle_dma_2[2], eagle_dma_2[3]);

                read_addr += 512;
                offset += 512;
                read_len -= 512;
              }
            }
            if (d->reg[2] & 8) {
              INTERRUPT_ASSERT(d->irq);
            }
            break;
					}
				} else {
					d->state = oldstate | STATE_CMD_BUSY;
				}
			} else {
				// New command
				switch (idata & 0x1f) {
				case STATE_SPECIFY:
					fprintf(stderr, "[ fdc: specify, NDMA=%d ]\n", d->command_bytes[1] & 1);
					d->command_size = 2;
					d->state = STATE_CMD_BYTES | STATE_CMD_BUSY | STATE_SPECIFY;
					break;

				case STATE_RECAL:
					fprintf(stderr, "[ fdc: recalibrate ]\n");
					d->state = STATE_CMD_BYTES | STATE_CMD_BUSY | STATE_RECAL;
					d->command_size = 1;
					break;

				case SENSE_INTERRUPT:
					fprintf(stderr, "[ fdc: sense interrupt ]\n");
					d->command_size = 0;
					d->command_result = 2;
					d->command_bytes[1] = 0x20;
					d->command_bytes[0] = 0;
					d->state = STATE_VERSION | STATE_CMD_QUEUE;
					break;

				case STATE_SEEK:
					fprintf(stderr, "[ fdc: seek ]\n");
					d->state = STATE_CMD_BYTES | STATE_CMD_BUSY | STATE_SEEK;
					d->command_size = 2;
					break;

				case STATE_CONFIGURE:
					fprintf(stderr, "[ fdc: configure ]\n");
					d->command_size = 3;
					d->state = STATE_CMD_BYTES | STATE_CMD_BUSY | STATE_CONFIGURE;
					break;

				case STATE_READ_ID:
					fprintf(stderr, "[ fdc: read id ]\n");
					d->command_size = 1;
					d->state = STATE_CMD_BYTES | STATE_CMD_BUSY | STATE_READ_ID;
					break;

				case STATE_READ_NORMAL_DATA:
					fprintf(stderr, "[ fdc: read normal data ]\n");
					d->command_size = 8;
					d->state = STATE_CMD_BYTES | STATE_CMD_BUSY | STATE_READ_NORMAL_DATA;
					break;
				}
			}
		} else {
			if (oldstate & STATE_CMD_QUEUE) {
				d->command_result--;
				idata = d->command_bytes[d->command_result];
				memory_writemax64(cpu, data, len, idata);
				fprintf(stderr, "[ fdc: command result %02x (rem %02x) from %d ]\n", (int)idata, d->command_result, d->state & 0xff);
				if (!d->command_result) {
          if ((oldstate & 0xff) == STATE_READ_NORMAL_DATA) {
            d->state = STATE_READ_NORMAL_DATA | STATE_CMD_DMA | STATE_CMD_BUSY;
          } else {
            d->state = STATE_EMPTY;
          }
				} else {
					d->state = oldstate;
				}
			} else {
				fprintf(stderr, "[ fdc: FIFO READ ]\n");
			}
		}
		break;

	case 0x02:
		if (writeflag == MEM_WRITE) {
      fprintf(stderr, "[ fdc: DOR write %02x ]\n", (int)idata);
      d->command_size = 0;
      if (idata & 4) {
        d->state = STATE_EMPTY;
      } else {
        d->state = oldstate;
      }
		} else {
			d->state = oldstate;
		}
		break;

	default:if (writeflag==MEM_READ) {
			fatal("[ fdc: read from reg %i ]\n",
			    (int)relative_addr);
			memory_writemax64(cpu, data, len, d->reg[relative_addr]);
		} else {
			fatal("[ fdc: write to reg %i:", (int)relative_addr);
			for (i=0; i<len; i++)
				fatal(" %02x", data[i]);
			fatal(" ]\n");
			d->reg[relative_addr] = idata;
		}
		break;
	}

	return 1;
}


DEVICE_ACCESS(fdc_3f7)
{
	struct fdc_data *d = (struct fdc_data *) extra;
	uint64_t idata = 0;

	idata = memory_readmax64(cpu, data, len);

	if (writeflag == MEM_WRITE) {
    fprintf(stderr, "[ fdc: write 3f7 %02x ]\n", (int)idata);
    if (d->reg[2] & 8) {
      INTERRUPT_ASSERT(d->irq);
    }
	} else {
	    idata = 3;
	    fprintf(stderr, "[ fdc: read 3f7 -> %02x ]\n", (int)idata);
	    memory_writemax64(cpu, data, len, idata);
	}

	return 1;
}


DEVINIT(fdc)
{
	struct fdc_data *d;

	CHECK_ALLOCATION(d = (struct fdc_data *) malloc(sizeof(struct fdc_data)));
	memset(d, 0, sizeof(struct fdc_data));

	INTERRUPT_CONNECT(devinit->interrupt_path, d->irq);

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_FDC_LENGTH, dev_fdc_access, d,
	    DM_DEFAULT, NULL);
	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr + 7, 1, dev_fdc_3f7_access, d,
	    DM_DEFAULT, NULL);

	return 1;
}

