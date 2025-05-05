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
#undef DO_ZERO
#ifdef LS_ZERO
#define DO_ZERO true
#else
#define DO_ZERO false
#endif

#ifndef LS_INDEXED
#ifndef LS_IGNOREOFS
	int32_t ofs = ic->arg[2];
#endif
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
  unsigned char data[LS_SIZE] = { };

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
  sync_pc(cpu, ic);

  assert(cpu->pc == ic->pc);

#ifndef LS_B
	if ((addr & 0xfff) + LS_SIZE-1 > 0xfff) {
		fatal("%08x: PPC LOAD/STORE misalignment across page boundary: TODO"
		    " (addr=0x%08x, LS_SIZE=%i)\n", (unsigned int)cpu->pc, (int)addr, LS_SIZE);
		if (swizzle | offset) {
      fprintf(stderr, "misaligned LE without full translation\n");
      exit(1);
    }

    auto end = addr + LS_SIZE;
    auto second_page = end & ~0xfff;
    auto second_span = end - second_page;
    auto first_span = second_page - addr;

#ifdef LS_LOAD
    if (!cpu->memory_rw(cpu, cpu->mem, second_page, data + first_span, second_span, MEM_READ, CACHE_DATA)) {
      // Throw
      return;
    }

    if (!cpu->memory_rw(cpu, cpu->mem, addr, data, first_span, MEM_READ, CACHE_DATA)) {
      // Throw
      return;
    }

    load_reg<LS_SIZE * 8, DO_ZERO>(ic->arg[0], data, swizzle);
#else
    store_reg<LS_SIZE * 8>(ic->arg[0], data, swizzle);

    if (!cpu->memory_rw(cpu, cpu->mem, second_page, data + first_span, second_span,
                        MEM_WRITE, CACHE_DATA)) {
      return;
    }

    if (!cpu->memory_rw(cpu, cpu->mem, addr, data, first_span,
                        MEM_WRITE, CACHE_DATA)) {
      return;
    }
#endif

#ifdef LS_UPDATE
    reg(ic->arg[1]) = addr;
#endif

    return;
	}
#endif

#ifdef LS_LOAD
	if (!cpu->memory_rw(cpu, cpu->mem, addr ^ offset, data, sizeof(data),
                      MEM_READ, CACHE_DATA)) {
		/*  Exception.  */
		return;
	}

  load_reg<LS_SIZE * 8, DO_ZERO>(ic->arg[0], data, swizzle);

  access_log(cpu, 0, addr^offset, data, sizeof(data), swizzle);
#else	/*  store:  */
  store_reg<LS_SIZE * 8>(ic->arg[0], data, swizzle);

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


#ifndef LS_INDEXED
#ifndef LS_IGNOREOFS
	int32_t ofs = ic->arg[2];
#endif
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

  auto pages =
#ifdef MODE32
    ppc32_get_cached_tlb_pages(cpu, addr, false)
#else
    ppc64_get_cached_tlb_pages(cpu, addr, false)
#endif
    ;

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

#ifdef LS_UPDATE
  uint32_t new_addr = addr;
#endif

  auto page =
#ifdef LS_LOAD
    pages.host_load
#else
    pages.host_store
#endif
    ;

#ifndef LS_B
	if (addr & (LS_SIZE-1)) {
		LS_GENERIC_N(cpu, ic);
		return;
	}
#endif

	if (page == NULL) {
		LS_GENERIC_N(cpu, ic);
		return;
	}

  addr &= 4095;

#ifdef LS_LOAD
  /*  Load:  */
  load_reg<LS_SIZE * 8, DO_ZERO>(ic->arg[0], &page[addr ^ offset], swizzle);

  access_log(cpu, 0, full_addr, (void *)ic->arg[0], LS_SIZE, swizzle);
#else	/*  !LS_LOAD  */
  /*  Store:  */
  store_reg<LS_SIZE * 8>(ic->arg[0], &page[addr ^ offset], swizzle);

  access_log(cpu, 1, full_addr, (void *)ic->arg[0], LS_SIZE, swizzle);
#endif	/*  !LS_LOAD  */

#ifdef LS_UPDATE
	reg(ic->arg[1]) = new_addr;
#endif
}

