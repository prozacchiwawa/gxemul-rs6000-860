#ifndef MEMORY_RW_H
#define MEMORY_RW_H

/*
 *  memory_rw():
 *
 *  Read or write data from/to memory.
 *
 *	cpu		the cpu doing the read/write
 *	mem		the memory object to use
 *	vaddr		the virtual address
 *	data		a pointer to the data to be written to memory, or
 *			a placeholder for data when reading from memory
 *	len		the length of the 'data' buffer
 *	writeflag	set to MEM_READ or MEM_WRITE
 *	misc_flags	CACHE_{NONE,DATA,INSTRUCTION} | other flags
 *
 *  If the address indicates access to a memory mapped device, that device'
 *  read/write access function is called.
 *
 *  This function should not be called with cpu == NULL.
 *
 *  Returns one of the following:
 *	MEMORY_ACCESS_FAILED
 *	MEMORY_ACCESS_OK
 *
 *  (MEMORY_ACCESS_FAILED is 0.)
 */

#include "cop0.h"
#include "mips_cpu_types.h"

template <class TcPhyspage, bool NoExceptions>
int gen_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
                  unsigned char *data, size_t len, int writeflag, int misc_flags)
{
	const int offset_mask = is_alpha<TcPhyspage>() ? 0x1fff : 0xfff;

	int ok = 2;
	uint64_t paddr;
	int cache, offset;
	unsigned char *memblock;
	int dyntrans_device_danger = 0;

	cache = misc_flags & CACHE_FLAGS_MASK;

	if (misc_flags & PHYSICAL || cpu->translate_v2p == NULL) {
		paddr = vaddr;
	} else {
		ok = cpu->translate_v2p(cpu, vaddr, &paddr,
		    (writeflag? FLAG_WRITEFLAG : 0) +
		    (NoExceptions? FLAG_NOEXCEPTIONS : 0)
		    + (misc_flags & MEMORY_USER_ACCESS)
		    + (cache==CACHE_INSTRUCTION? FLAG_INSTR : 0));

		/*
		 *  If the translation caused an exception, or was invalid in
		 *  some way, then simply return without doing the memory
		 *  access:
		 */
		if (!ok)
			return MEMORY_ACCESS_FAILED;
	}

  struct memory_access_result access_result = memory_device_lookup(mem, paddr);
  
  int res = access_result.res;
  if (access_result.res > 0) {
    if (access_result.device_offset + len > access_result.device->length)
      len = access_result.device->length - access_result.device_offset;

    if (cpu->update_translation_table != NULL &&
        !(ok & MEMORY_NOT_FULL_PAGE) &&
        access_result.device->flags & DM_DYNTRANS_OK) {
      int wf = writeflag == MEM_WRITE? 1 : 0;
      unsigned char *host_addr;

      if (!(access_result.device->flags &
            DM_DYNTRANS_WRITE_OK))
        wf = 0;

      if (writeflag && wf) {
        if (access_result.device_offset < access_result.device->
            dyntrans_write_low)
          access_result.device->
            dyntrans_write_low =
            access_result.device_offset &~offset_mask;
        if (access_result.device_offset >= access_result.device->
            dyntrans_write_high)
          access_result.device->
            dyntrans_write_high =
            access_result.device_offset | offset_mask;
      }

      if (access_result.device->flags &
          DM_EMULATED_RAM) {
        /*  MEM_WRITE to force the page
            to be allocated, if it
            wasn't already  */
        uint64_t *pp = (uint64_t *)access_result.device->dyntrans_data;
        uint64_t p = paddr - *pp;
        host_addr =
          memory_paddr_to_hostaddr
          (mem, p & ~offset_mask,
           MEM_WRITE);
      } else {
        host_addr = access_result.device->
          dyntrans_data +
          (access_result.device_offset & ~offset_mask);
      }
      
      if (!NoExceptions) {
        cpu->update_translation_table
          (cpu,
           vaddr & ~offset_mask, host_addr,
           wf, paddr & ~offset_mask,
           !!(misc_flags & CACHE_INSTRUCTION)
           );
      }
    }

    res = 0;
    if (!NoExceptions || (access_result.device->flags &
                           DM_READS_HAVE_NO_SIDE_EFFECTS))
      res = access_result.device->f(cpu, mem, access_result.device_offset,
                              data, len, writeflag,
                              access_result.device->extra);
    
    if (res == 0)
      res = -1;
    
    /*
     *  If accessing the memory mapped device
     *  failed, then return with an exception.
     *  (Architecture specific.)
     */
    if (res <= 0 && !NoExceptions) {
      debug("[ %s device '%s' addr %08lx "
            "failed ]\n", writeflag?
            "writing to" : "reading from",
            access_result.device->name, (long)access_result.device_offset);
      if (is_mips<TcPhyspage>()) {
        mips_cpu_exception(cpu,
                           cache == CACHE_INSTRUCTION?
                           EXCEPTION_IBE : EXCEPTION_DBE,
                           0, vaddr, 0, 0, 0, 0);
      }
      
      
      if (is_m88k<TcPhyspage>()) {
        /*  TODO: This is enough for
            OpenBSD/mvme88k's badaddr()
            implementation... but the
            faulting address should probably
            be included somewhere too!  */
        m88k_exception(cpu, cache == CACHE_INSTRUCTION
                       ? M88K_EXCEPTION_INSTRUCTION_ACCESS
                       : M88K_EXCEPTION_DATA_ACCESS, 0);
      }
      return MEMORY_ACCESS_FAILED;
    }
    goto do_return_ok;
  }
  
  if (is_mips<TcPhyspage>()) {
    /*
     *  Data and instruction cache emulation:
     */

    switch (cpu->cd.mips.cpu_type.mmu_model) {
    case MMU3K:
      /*  if not uncached addess  (TODO: generalize this)  */
      if (!(misc_flags & PHYSICAL) && cache != CACHE_NONE &&
          !((vaddr & 0xffffffffULL) >= 0xa0000000ULL &&
            (vaddr & 0xffffffffULL) <= 0xbfffffffULL)) {
        if (memory_cache_R3000(cpu, cache, paddr,
                               writeflag, len, data))
          goto do_return_ok;
      }
      break;
    default:
      /*  R4000 etc  */
      /*  TODO  */
      ;
    }
  }


	/*  Outside of physical RAM?  */
	if (paddr >= mem->physical_max) {
    if (is_ppc<TcPhyspage>()) {
      if ((paddr & 0xfffe0000) == 0xfffe0000) {
        paddr &= ~0xffff0000;
      }
    } else if (is_mips<TcPhyspage>()) {
      if ((paddr & 0xffffc00000ULL) == 0x1fc00000) {
        /*  Ok, this is PROM stuff  */
      } else if ((paddr & 0xfffff00000ULL) == 0x1ff00000) {
        /*  Sprite reads from this area of memory...  */
        /*  TODO: is this still correct?  */
        if (writeflag == MEM_READ)
          memset(data, 0, len);
        goto do_return_ok;
      }
    }

    if (paddr >= mem->physical_max && !NoExceptions)
      memory_warn_about_unimplemented_addr
        (cpu, mem, writeflag, paddr, data, len);
    
    if (writeflag == MEM_READ) {
      /*  Return all zeroes? (Or 0xff? TODO)  */
      memset(data, 0, len);
    }

    /*  Hm? Shouldn't there be a DBE exception for
        invalid writes as well?  TODO  */
    
    goto do_return_ok;
  }


	/*
	 *  Uncached access:
	 *
	 *  1)  Translate the physical address to a host address.
	 *
	 *  2)  Insert this virtual->physical->host translation into the
	 *      fast translation arrays (using update_translation_table()).
	 *
	 *  3)  If this was a Write, then invalidate any code translations
	 *      in that page.
	 */
	memblock = memory_paddr_to_hostaddr(mem, paddr & ~offset_mask,
	    writeflag);
	if (memblock == NULL) {
		if (writeflag == MEM_READ)
			memset(data, 0, len);
		goto do_return_ok;
	}

	offset = paddr & offset_mask;

	if (cpu->update_translation_table != NULL && !dyntrans_device_danger
      && (!is_mips<TcPhyspage>() ||
          (/*  Ugly hack for R2000/R3000 caches:  */
           (cpu->cd.mips.cpu_type.mmu_model != MMU3K ||
            !(cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & MIPS1_ISOL_CACHES))
           )
          )
      && !(ok & MEMORY_NOT_FULL_PAGE)
	    && !NoExceptions)
    cpu->update_translation_table
      (cpu, vaddr & ~offset_mask,
       memblock, (misc_flags & MEMORY_USER_ACCESS) |
       (cache == CACHE_INSTRUCTION?
        (writeflag == MEM_WRITE? 1 : 0) : ok - 1),
       paddr & ~offset_mask, cache == CACHE_INSTRUCTION);

	/*
	 *  If writing, or if mapping a page where writing is ok later on,
	 *  then invalidate code translations for the (physical) page address:
	 */

	if ((writeflag == MEM_WRITE
	    || (ok == 2 && cache == CACHE_DATA)
	    ) && cpu->invalidate_code_translation != NULL)
		cpu->invalidate_code_translation(cpu, paddr, INVALIDATE_PADDR);

	if ((paddr&((1<<BITS_PER_MEMBLOCK)-1)) + len > (1<<BITS_PER_MEMBLOCK)) {
		if (!NoExceptions) {
		    printf("Write over memblock boundary?\n");
		    exit(1);
		}

		return MEMORY_ACCESS_FAILED;
	}

	/*  And finally, read or write the data:  */
	if (writeflag == MEM_WRITE) {
		memcpy(memblock + offset, data, len);
  } else {
		memcpy(data, memblock + offset, len);
  }

do_return_ok:
	return MEMORY_ACCESS_OK;
}

template <class TcPhyspage>
int memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
                  unsigned char *data, size_t len, int writeflag, int misc_flags) {
  auto no_exceptions = misc_flags & NO_EXCEPTIONS;
  if (no_exceptions) {
    return gen_memory_rw<TcPhyspage, true>(cpu, mem, vaddr, data, len, writeflag, misc_flags);
  } else {
    return gen_memory_rw<TcPhyspage, false>(cpu, mem, vaddr, data, len, writeflag, misc_flags);
  }
}

#endif
