/*
 *  Copyright (C) 2005-2011  Art Yerkes.  All rights reserved.
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
 *  COMMENT: IBM Carerra (PCI 0x0f on an RS/6000 model 860).
 *   PCI CONFIG: 1014:001C:00FF
 *   BAR1: 00004101
 *
 */

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

DEVICE_ACCESS(ibm_isa) {
    unsigned char b = 0;
    uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE) {
		b = idata = memory_readmax64(cpu, data, len);
    }

    if (writeflag != MEM_WRITE) {
        odata = 0;
    }
    debug("[ carerra %s %x -> %x ]\n", writeflag == MEM_WRITE ? "write" : "read", relative_addr, odata);

	if (writeflag == MEM_READ) {
		memory_writemax64(cpu, data, len, odata);
    }

    return 1;
}

DEVINIT(ibm_isa) {
    memory_device_register
        (devinit->machine->memory, devinit->name,
         devinit->addr, 
         2,
         dev_ibm_isa_access, 
         NULL, // Device own data ptr
         DM_DEFAULT,
         NULL);
    return 1;
}
