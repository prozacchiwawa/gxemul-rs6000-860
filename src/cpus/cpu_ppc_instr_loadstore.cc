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
 *  POWER/PowerPC load/store instructions.
 *
 *
 *  Load/store instructions have the following arguments:
 *
 *  arg[0] = pointer to the register to load to or store from
 *  arg[1] = pointer to the base register
 *
 *  arg[2] = offset (as an int32_t)
 *	     (or, for Indexed load/stores: pointer to index register)
 */

extern void access_log(struct cpu *cpu, int write, uint64_t addr, void *data, int size, int swizzle);

#ifndef LS_IGNOREOFS
void LS_GENERIC_N(struct cpu *cpu, struct ppc_instr_call *ic)
{
#ifndef LS_IGNOREOFS
	int32_t ofs = ic->arg[2];
#endif

#ifdef MODE32
	uint32_t addr =
#else
	uint64_t addr =
#endif
	    reg(ic->arg[1]) +
#ifdef LS_INDEXED
	    reg(ic->arg[2]);
#else
	    ofs;
#endif
	unsigned char data[LS_SIZE];

  int swizzle, offset;
  cpu_ppc_swizzle_offset(cpu, LS_SIZE, 0, &swizzle, &offset);

#ifdef LS_BYTEREVERSE
#ifdef LS_H
  swizzle ^= 1;
#endif
#ifdef LS_W
  swizzle ^= 3;
#endif
#ifdef LS_D
  swizzle ^= 7;
#endif
#endif
	/*  Synchronize the PC:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.ppc.cur_ic_page)
	    / sizeof(struct ppc_instr_call);
	cpu->pc &= ~((PPC_IC_ENTRIES_PER_PAGE-1) << PPC_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << PPC_INSTR_ALIGNMENT_SHIFT);

#ifndef LS_B
	if ((addr & 0xfff) + LS_SIZE-1 > 0xfff) {
		fatal("PPC LOAD/STORE misalignment across page boundary: TODO"
		    " (addr=0x%08x, LS_SIZE=%i)\n", (int)addr, LS_SIZE);
		//exit(1);
	}
#endif

#ifdef LS_LOAD
	if (!cpu->memory_rw(cpu, cpu->mem, addr ^ offset, data, sizeof(data),
	    MEM_READ, CACHE_DATA)) {
		/*  Exception.  */
		return;
	}
#ifdef LS_B
	reg(ic->arg[0]) =
#ifndef LS_ZERO
	    (int8_t)
#endif
	    data[0];
#endif
#ifdef LS_H
	reg(ic->arg[0]) =
#ifndef LS_ZERO
	    (int16_t)
#endif
	    ((data[0^swizzle] << 8) + data[1^swizzle]);
#endif
#ifdef LS_W
	reg(ic->arg[0]) =
#ifndef LS_ZERO
	    (int32_t)
#else
	    (uint32_t)
#endif
	    ((data[0^swizzle] << 24) + (data[1^swizzle] << 16) +
	    (data[2^swizzle] << 8) + data[3^swizzle]);
#endif
#ifdef LS_D
	(*(uint64_t *)(ic->arg[0])) =
	    ((uint64_t)data[0^swizzle] << 56) +
      ((uint64_t)data[1^swizzle] << 48) +
	    ((uint64_t)data[2^swizzle] << 40) +
      ((uint64_t)data[3^swizzle] << 32) +
      ((uint64_t)data[4^swizzle] << 24) +
      ((uint64_t)data[5^swizzle] << 16) +
      ((uint64_t)data[6^swizzle] << 8) +
      (uint64_t)data[7^swizzle];
#endif

  access_log(cpu, 0, addr^offset, data, sizeof(data), swizzle);
#else	/*  store:  */

#ifdef LS_B
	data[0] = reg(ic->arg[0]);
#endif
#ifdef LS_H
	data[0^swizzle] = reg(ic->arg[0]) >> 8;
	data[1^swizzle] = reg(ic->arg[0]);
#endif
#ifdef LS_W
	data[0^swizzle] = reg(ic->arg[0]) >> 24;
	data[1^swizzle] = reg(ic->arg[0]) >> 16;
	data[2^swizzle] = reg(ic->arg[0]) >> 8;
	data[3^swizzle] = reg(ic->arg[0]);
#endif
#ifdef LS_D
	{ uint64_t x = *(uint64_t *)(ic->arg[0]);
    data[0^swizzle] = x >> 56;
    data[1^swizzle] = x >> 48;
    data[2^swizzle] = x >> 40;
    data[3^swizzle] = x >> 32;
    data[4^swizzle] = x >> 24;
    data[5^swizzle] = x >> 16;
    data[6^swizzle] = x >> 8;
    data[7^swizzle] = x; }
#endif
  access_log(cpu, 1, addr^offset, data, sizeof(data), swizzle);

	if (!cpu->memory_rw(cpu, cpu->mem, addr^offset, data, sizeof(data),
	    MEM_WRITE, CACHE_DATA)) {
		/*  Exception.  */
		return;
	}
#endif

#ifdef LS_UPDATE
	reg(ic->arg[1]) = addr;
#endif
}
#endif


void LS_N(struct cpu *cpu, struct ppc_instr_call *ic)
{
#ifndef MODE32
	/************************************************************/
	/* For now, 64-bit access is done using the slow fallback.  */
	if (!cpu->is_32bit) {
		LS_GENERIC_N(cpu, ic);
		return;
	}
#endif


#ifndef LS_IGNOREOFS
	int32_t ofs = ic->arg[2];
#endif

#ifdef MODE32
	uint32_t addr =
#else
	uint64_t addr =
#endif
	    reg(ic->arg[1])
#ifdef LS_INDEXED
	    + reg(ic->arg[2])
#else
#ifndef LS_IGNOREOFS
	    + ofs
#endif
#endif
	    ;

  uint64_t full_addr = addr;

  int swizzle, offset;
  cpu_ppc_swizzle_offset(cpu, LS_SIZE, 0, &swizzle, &offset);

#ifdef LS_BYTEREVERSE
#ifdef LS_H
  swizzle ^= 1;
#endif
#ifdef LS_W
  swizzle ^= 3;
#endif
#ifdef LS_D
  swizzle ^= 7;
#endif
#endif

	unsigned char *page = cpu->cd.ppc.
#ifdef LS_LOAD
    host_load
#else
    host_store
#endif
    [addr >> 12];
#ifdef LS_UPDATE
	uint32_t new_addr = addr;
#endif

#ifndef LS_B
	if (addr & (LS_SIZE-1)) {
		LS_GENERIC_N(cpu, ic);
		return;
	}
#endif

	if (page == NULL) {
		LS_GENERIC_N(cpu, ic);
		return;
	} else {
		addr &= 4095;
#ifdef LS_LOAD
		/*  Load:  */
#ifdef LS_B
		reg(ic->arg[0]) =
#ifndef LS_ZERO
		    (int8_t)
#endif
		    page[addr ^ offset];
#endif	/*  LS_B  */
#ifdef LS_H
		reg(ic->arg[0]) =
#ifndef LS_ZERO
		    (int16_t)
#endif
          ((page[addr^offset^swizzle] << 8) + page[(addr+1)^offset^swizzle]);
#endif	/*  LS_H  */
#ifdef LS_W
    // In the case where an stwbrx was previously written we want to
    // ensure that we load only in BE as would have been intended.
		reg(ic->arg[0]) =
#ifndef LS_ZERO
		    (int32_t)
#else
		    (uint32_t)
#endif
          ((page[addr^offset^swizzle] << 24) + (page[(addr+1)^offset^swizzle] << 16) +
           (page[(addr+2)^offset^swizzle] << 8) + page[(addr+3)^offset^swizzle]);
#endif	/*  LS_W  */
#ifdef LS_D
		(*(uint64_t *)(ic->arg[0])) =
      ((uint64_t)page[(addr+0)^offset^swizzle] << 56) +
      ((uint64_t)page[(addr+1)^offset^swizzle] << 48) +
      ((uint64_t)page[(addr+2)^offset^swizzle] << 40) +
      ((uint64_t)page[(addr+3)^offset^swizzle] << 32) +
      ((uint64_t)page[(addr+4)^offset^swizzle] << 24) +
      ((uint64_t)page[(addr+5)^offset^swizzle] << 16) +
      ((uint64_t)page[(addr+6)^offset^swizzle] << 8) +
      (uint64_t)page[(addr+7)^offset^swizzle];
#endif	/*  LS_D  */

    access_log(cpu, 0, full_addr, (void *)ic->arg[0], LS_SIZE, swizzle);
#else	/*  !LS_LOAD  */

		/*  Store:  */
#ifdef LS_B
		page[addr^offset] = reg(ic->arg[0]);
#endif
#ifdef LS_H
    if (reg(ic->arg[0]) == 0x2ad4) {
      fprintf(stderr, "2ad4 halfword write to %08x: %08x\n", (unsigned int)addr, (unsigned int)cpu->pc);
    }
    page[addr^offset^swizzle]   = reg(ic->arg[0]) >> 8;
    page[(addr+1)^offset^swizzle] = reg(ic->arg[0]);
#endif
#ifdef LS_W
    if (reg(ic->arg[0]) == 0x2ad40000) {
      fprintf(stderr, "2ad40000 word write to %08x: %08x\n", (unsigned int)addr, (unsigned int)cpu->pc);
    }
		page[addr^offset^swizzle]   = reg(ic->arg[0]) >> 24;
		page[(addr+1)^offset^swizzle] = reg(ic->arg[0]) >> 16;
		page[(addr+2)^offset^swizzle] = reg(ic->arg[0]) >> 8;
		page[(addr+3)^offset^swizzle] = reg(ic->arg[0]);
#endif
#ifdef LS_D
		{ uint64_t x = *(uint64_t *)(ic->arg[0]);
      page[addr^offset^swizzle]   = x >> 56;
      page[(addr+1)^offset^swizzle] = x >> 48;
      page[(addr+2)^offset^swizzle] = x >> 40;
      page[(addr+3)^offset^swizzle] = x >> 32;
      page[(addr+4)^offset^swizzle] = x >> 24;
      page[(addr+5)^offset^swizzle] = x >> 16;
      page[(addr+6)^offset^swizzle] = x >> 8;
      page[(addr+7)^offset^swizzle] = x; }
#endif

    access_log(cpu, 1, full_addr, (void *)ic->arg[0], LS_SIZE, swizzle);
#endif	/*  !LS_LOAD  */
	}

#ifdef LS_UPDATE
	reg(ic->arg[1]) = new_addr;
#endif
}

