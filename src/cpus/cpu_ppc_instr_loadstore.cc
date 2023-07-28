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

#ifdef LS_D
#define SWIZZLE_SIZE 0
#elif defined(LS_W)
#define SWIZZLE_SIZE 4
#elif defined(LS_H)
#define SWIZZLE_SIZE 6
#else
#define SWIZZLE_SIZE 7
#endif

#ifndef LS_IGNOREOFS
void LS_GENERIC_N(struct cpu *cpu, struct ppc_instr_call *ic)
{
#ifdef MODE32
	uint32_t addr =
#else
	uint64_t addr =
#endif
	    reg(ic->arg[1]) +
#ifdef LS_INDEXED
	    reg(ic->arg[2]);
#else
	    (int32_t)ic->arg[2];
#endif
  uint32_t offset = ppc_swizzle(cpu, SWIZZLE_SIZE);

	unsigned char data[LS_SIZE];

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
  int swap_swizzle = do_bytelane_swapping() ? 1 : 0;
	reg(ic->arg[0]) =
#ifdef LS_BYTEREVERSE
	    ((data[1^swap_swizzle] << 8) + data[0^swap_swizzle]);
#else
#ifndef LS_ZERO
	    (int16_t)
#endif
	    ((data[0&swap_swizzle] << 8) + data[1^swap_swizzle]);
#endif /*  !BYTEREVERSE  */
#endif
#ifdef LS_W
      int swap_swizzle = do_bytelane_swapping() ? 3 : 0;
	reg(ic->arg[0]) =
#ifdef LS_BYTEREVERSE
	    ((data[3^swap_swizzle] << 24) + (data[2^swap_swizzle] << 16) +
	    (data[1^swap_swizzle] << 8) + data[0^swap_swizzle]);
#else  /* !LS_BYTEREVERSE  */
#ifndef LS_ZERO
	    (int32_t)
#else
	    (uint32_t)
#endif
	    ((data[0^swap_swizzle] << 24) + (data[1^swap_swizzle] << 16) +
	    (data[2^swap_swizzle] << 8) + data[3^swap_swizzle]);
#endif /* !LS_BYTEREVERSE */
#endif
#ifdef LS_D
      int swap_swizzle = do_bytelane_swapping() ? 7 : 0;
	(*(uint64_t *)(ic->arg[0])) =
	    ((uint64_t)data[0^swap_swizzle] << 56) + ((uint64_t)data[1^swap_swizzle] << 48) +
	    ((uint64_t)data[2^swap_swizzle] << 40) + ((uint64_t)data[3^swap_swizzle] << 32) +
	    ((uint64_t)data[4^swap_swizzle] << 24) + (data[5^swap_swizzle] << 16) +
	    (data[6^swap_swizzle] << 8) + data[7^swap_swizzle];
#endif

#else	/*  store:  */

#ifdef LS_B
	data[0] = reg(ic->arg[0]);
#endif
#ifdef LS_H
  int swap_swizzle = do_bytelane_swapping() ? 1 : 0;
#ifdef LS_BYTEREVERSE
	data[0^swap_swizzle] = reg(ic->arg[0]);
	data[1^swap_swizzle] = reg(ic->arg[0]) >> 8;
#else
	data[0^swap_swizzle] = reg(ic->arg[0]) >> 8;
	data[1^swap_swizzle] = reg(ic->arg[0]);
#endif
#endif
#ifdef LS_W
  int swap_swizzle = do_bytelane_swapping() ? 3 : 0;
#ifdef LS_BYTEREVERSE
	data[0^swap_swizzle] = reg(ic->arg[0]);
	data[1^swap_swizzle] = reg(ic->arg[0]) >> 8;
	data[2^swap_swizzle] = reg(ic->arg[0]) >> 16;
	data[3^swap_swizzle] = reg(ic->arg[0]) >> 24;
#else
	data[0^swap_swizzle] = reg(ic->arg[0]) >> 24;
	data[1^swap_swizzle] = reg(ic->arg[0]) >> 16;
	data[2^swap_swizzle] = reg(ic->arg[0]) >> 8;
	data[3^swap_swizzle] = reg(ic->arg[0]);
#endif /* !LS_BYTEREVERSE */
#endif
#ifdef LS_D
  int swap_swizzle = do_bytelane_swapping() ? 7 : 0;
	{ uint64_t x = *(uint64_t *)(ic->arg[0]);
	data[0^swap_swizzle] = x >> 56;
	data[1^swap_swizzle] = x >> 48;
	data[2^swap_swizzle] = x >> 40;
	data[3^swap_swizzle] = x >> 32;
	data[4^swap_swizzle] = x >> 24;
	data[5^swap_swizzle] = x >> 16;
	data[6^swap_swizzle] = x >> 8;
	data[7^swap_swizzle] = x; }
#endif
	if (!cpu->memory_rw(cpu, cpu->mem, addr ^ offset, data, sizeof(data),
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
	    + (int32_t)ic->arg[2]
#endif
#endif
	    ;

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

  uint32_t offset = ppc_swizzle(cpu, SWIZZLE_SIZE);
	if (page == NULL) {
		LS_GENERIC_N(cpu, ic);
		return;
	} else {
    offset ^= bytelane_swizzle(SWIZZLE_SIZE);
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
#ifdef LS_BYTEREVERSE
      ((page[(addr^offset)+1] << 8) + page[(addr^offset)]);
#else
#ifndef LS_ZERO
		    (int16_t)
#endif
          ((page[(addr^offset)] << 8) + page[(addr^offset)+1]);
#endif /* !BYTEREVERSE */
#endif	/*  LS_H  */
#ifdef LS_W
		reg(ic->arg[0]) =
#ifdef LS_BYTEREVERSE
      ((page[(addr^offset)+3] << 24) + (page[(addr^offset)+2] << 16) +
       (page[(addr^offset)+1] << 8) + page[addr ^ offset]);
#else  /*  !LS_BYTEREVERSE  */
#ifndef LS_ZERO
		    (int32_t)
#else
		    (uint32_t)
#endif
          ((page[(addr^offset)] << 24) + (page[(addr^offset)+1] << 16) +
           (page[(addr^offset)+2] << 8) + page[(addr^offset)+3]);
#endif  /*  !LS_BYTEREVERSE  */
#endif	/*  LS_W  */
#ifdef LS_D
		(*(uint64_t *)(ic->arg[0])) =
      ((uint64_t)page[(addr^offset)+0] << 56) +
      ((uint64_t)page[(addr^offset)+1] << 48) +
      ((uint64_t)page[(addr^offset)+2] << 40) +
      ((uint64_t)page[(addr^offset)+3] << 32) +
      ((uint64_t)page[(addr^offset)+4] << 24) + (page[(addr^offset)+5] << 16) +
      (page[(addr^offset)+6] << 8) + page[(addr^offset)+7];
#endif	/*  LS_D  */

#else	/*  !LS_LOAD  */

		/*  Store:  */
#ifdef LS_B
		page[(addr^offset)] = reg(ic->arg[0]);
#endif
#ifdef LS_H
#ifdef LS_BYTEREVERSE
		page[(addr^offset)]   = reg(ic->arg[0]);
		page[(addr^offset)+1] = reg(ic->arg[0]) >> 8;
#else
		page[(addr^offset)]   = reg(ic->arg[0]) >> 8;
		page[(addr^offset)+1] = reg(ic->arg[0]);
#endif /* !BYTEREVERSE */
#endif
#ifdef LS_W
#ifdef LS_BYTEREVERSE
		page[(addr^offset)]   = reg(ic->arg[0]);
		page[(addr^offset)+1] = reg(ic->arg[0]) >> 8;
		page[(addr^offset)+2] = reg(ic->arg[0]) >> 16;
		page[(addr^offset)+3] = reg(ic->arg[0]) >> 24;
#else
		page[(addr^offset)]   = reg(ic->arg[0]) >> 24;
		page[(addr^offset)+1] = reg(ic->arg[0]) >> 16;
		page[(addr^offset)+2] = reg(ic->arg[0]) >> 8;
		page[(addr^offset)+3] = reg(ic->arg[0]);
#endif /* !LS_BYTEREVERSE  */
#endif
#ifdef LS_D
		{ uint64_t x = *(uint64_t *)(ic->arg[0]);
      page[(addr^offset)]   = x >> 56;
      page[(addr^offset)+1] = x >> 48;
      page[(addr^offset)+2] = x >> 40;
      page[(addr^offset)+3] = x >> 32;
      page[(addr^offset)+4] = x >> 24;
      page[(addr^offset)+5] = x >> 16;
      page[(addr^offset)+6] = x >> 8;
      page[(addr^offset)+7] = x; }
#endif
#endif	/*  !LS_LOAD  */
	}

#ifdef LS_UPDATE
	reg(ic->arg[1]) = new_addr;
#endif
}

#undef SWIZZLE_SIZE
