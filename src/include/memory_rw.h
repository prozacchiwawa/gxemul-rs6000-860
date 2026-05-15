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
	int cache, offset;
  host_load_store_t host_pages = { 0 };
	int dyntrans_device_danger = 0;

	cache = misc_flags & CACHE_FLAGS_MASK;

	if (misc_flags & PHYSICAL || cpu->translate_v2p == NULL) {
		host_pages.physaddr = vaddr;
	} else {
    host_pages = get_tlb_translation<TcPhyspage>(cpu, vaddr, misc_flags & FLAG_INSTR);
    offset = vaddr & offset_mask;
    if (offset + len < offset_mask) {
      if (writeflag) {
        auto host_page = host_pages.host_store;
        if (host_page) {
          memcpy(host_page + offset, data, len);
          return MEMORY_ACCESS_OK;
        }
      } else {
        auto host_page = host_pages.host_load;
        if (host_page) {
          memcpy(data, host_page + offset, len);
          return MEMORY_ACCESS_OK;
        }
      }
    }

		ok = cpu->translate_v2p(cpu, vaddr, &host_pages.physaddr,
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

  struct memory_access_result access_result = memory_device_lookup(mem, host_pages.physaddr);
  
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
        uint64_t p = host_pages.physaddr - *pp;
        host_addr =
          memory_paddr_to_hostaddr
          (mem, p & ~offset_mask,
           MEM_WRITE);
      } else {
        host_addr = access_result.device->
          dyntrans_data +
          (access_result.device_offset & ~offset_mask);
      }
      
      if (!NoExceptions && !(ok & 4)) {
        cpu->update_translation_table
          (cpu,
           vaddr & ~offset_mask, host_addr,
           wf, host_pages.physaddr & ~offset_mask,
           !!(misc_flags & CACHE_INSTRUCTION)
           );
      }
    }

    res = access_result.device->f(cpu, mem, access_result.device_offset,
                                  data, len, writeflag,
                                  access_result.device->extra);
    
    if (res < 1) {
      memset(data, 0, len);
      res = -1;
    }
    
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
        if (memory_cache_R3000(cpu, cache, host_pages.physaddr,
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
	if (host_pages.physaddr >= mem->physical_max) {
    if (is_ppc<TcPhyspage>()) {
      if ((host_pages.physaddr & 0xfffe0000) == 0xfffe0000) {
        host_pages.physaddr &= ~0xffff0000;
      }
    } else if (is_mips<TcPhyspage>()) {
      if ((host_pages.physaddr & 0xffffc00000ULL) == 0x1fc00000) {
        /*  Ok, this is PROM stuff  */
      } else if ((host_pages.physaddr & 0xfffff00000ULL) == 0x1ff00000) {
        /*  Sprite reads from this area of memory...  */
        /*  TODO: is this still correct?  */
        if (writeflag == MEM_READ)
          memset(data, 0, len);
        goto do_return_ok;
      }
    }

    if (host_pages.physaddr >= mem->physical_max && !NoExceptions)
      memory_warn_about_unimplemented_addr
        (cpu, mem, writeflag, host_pages.physaddr, data, len);
    
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
	host_pages.host_load = memory_paddr_to_hostaddr(mem, host_pages.physaddr & ~offset_mask,
	    writeflag);
	if (host_pages.host_load == NULL) {
		if (writeflag == MEM_READ)
			memset(data, 0, len);
		goto do_return_ok;
	}

	offset = host_pages.physaddr & offset_mask;

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
       host_pages.host_load, (misc_flags & MEMORY_USER_ACCESS) |
       (cache == CACHE_INSTRUCTION?
        (writeflag == MEM_WRITE? 1 : 0) : ok - 1),
       host_pages.physaddr & ~offset_mask, cache == CACHE_INSTRUCTION);

	/*
	 *  If writing, or if mapping a page where writing is ok later on,
	 *  then invalidate code translations for the (physical) page address:
	 */

	if ((writeflag == MEM_WRITE
	    || (ok == 2 && cache == CACHE_DATA)
       ) && cpu->invalidate_code_translation != NULL) {
		cpu->invalidate_code_translation(cpu, host_pages.physaddr, INVALIDATE_PADDR);
  }

	if ((host_pages.physaddr&((1<<BITS_PER_MEMBLOCK)-1)) + len > (1<<BITS_PER_MEMBLOCK)) {
		if (!NoExceptions) {
		    printf("Write over host_pages.host_load boundary?\n");
		    exit(1);
		}

		return MEMORY_ACCESS_FAILED;
	}

	/*  And finally, read or write the data:  */
	if (writeflag == MEM_WRITE) {
		memcpy(host_pages.host_load + offset, data, len);
  } else {
		memcpy(data, host_pages.host_load + offset, len);
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
