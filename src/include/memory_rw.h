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

class mapping_result_t {
public:
  int ok;
  int cache;
  int offset_mask;
  int offset;
  bool early_success;
  bool outside_ram;
  host_load_store_t host_pages;
};

template <class TcPhyspage, bool NoExceptions>
mapping_result_t determine_paddr
(struct cpu *cpu,
 struct memory *mem,
 uint64_t vaddr,
 size_t len,
 int writeflag,
 int misc_flags)
{
	const int offset_mask = is_alpha<TcPhyspage>() ? 0x1fff : 0xfff;
	mapping_result_t mapping = { 2, misc_flags & CACHE_FLAGS_MASK, offset_mask };

	if (misc_flags & PHYSICAL) {
		mapping.host_pages.physaddr = vaddr;
	} else {
    mapping.host_pages = get_tlb_translation<TcPhyspage>(cpu, vaddr, misc_flags & FLAG_INSTR);
    mapping.offset = vaddr & offset_mask;
    if (mapping.offset + len < mapping.offset_mask) {
      if (writeflag) {
        auto host_page = mapping.host_pages.host_store;
        if (host_page) {
          mapping.early_success = true;
          return mapping;
        }
      } else {
        auto host_page = mapping.host_pages.host_load;
        if (host_page) {
          mapping.early_success = true;
          return mapping;
        }
      }
    }

		mapping.ok = cpu->translate_v2p(cpu, vaddr, &mapping.host_pages.physaddr,
		    (writeflag? FLAG_WRITEFLAG : 0) +
		    (NoExceptions? FLAG_NOEXCEPTIONS : 0)
		    + (misc_flags & MEMORY_USER_ACCESS)
		    + (mapping.cache==CACHE_INSTRUCTION? FLAG_INSTR : 0));

		/*
		 *  If the translation caused an exception, or was invalid in
		 *  some way, then simply return without doing the memory
		 *  access:
		 */
	}

  if (mapping.ok > 0) {
    if (mapping.host_pages.physaddr < mem->physical_max) {
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
      mapping.host_pages.host_load =
        memory_paddr_to_hostaddr
        (mem,
         mapping.host_pages.physaddr & ~mapping.offset_mask,
         writeflag);
      
      mapping.offset = mapping.host_pages.physaddr & mapping.offset_mask;
    } else {
      mapping.outside_ram = true;
    }
  }

  return mapping;
}

template <class TcPhyspage, bool NoExceptions>
int gen_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
                  unsigned char *data, size_t len, int writeflag, int misc_flags)
{
  auto mapping = determine_paddr<TcPhyspage, NoExceptions>
    (cpu,
     mem,
     vaddr,
     len,
     writeflag,
     misc_flags);

  if (mapping.early_success) {
    if (writeflag) {
      memcpy(mapping.host_pages.host_load + mapping.offset, data, len);
    } else {
      memcpy(data, mapping.host_pages.host_load + mapping.offset, len);
    }
    return MEMORY_ACCESS_OK;
  } else if (mapping.ok < 1) {
    return MEMORY_ACCESS_FAILED;
  }

	/*
	 *  If writing, or if mapping a page where writing is ok later on,
	 *  then invalidate code translations for the (physical) page address:
	 */

	if (mapping.ok > 0 && writeflag) {
		cpu->invalidate_code_translation(cpu, mapping.host_pages.physaddr, INVALIDATE_PADDR);
  }
  
  struct memory_access_result access_result = memory_device_lookup(mem, mapping.host_pages.physaddr);
  
  int res = access_result.res;
  if (access_result.res > 0) {
    if (access_result.device_offset + len > access_result.device->length)
      len = access_result.device->length - access_result.device_offset;

    if (cpu->update_translation_table != NULL &&
        !(mapping.ok & MEMORY_NOT_FULL_PAGE) &&
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
            access_result.device_offset &~mapping.offset_mask;
        if (access_result.device_offset >= access_result.device->
            dyntrans_write_high)
          access_result.device->
            dyntrans_write_high =
            access_result.device_offset | mapping.offset_mask;
      }

      if (access_result.device->flags &
          DM_EMULATED_RAM) {
        /*  MEM_WRITE to force the page
            to be allocated, if it
            wasn't already  */
        uint64_t *pp = (uint64_t *)access_result.device->dyntrans_data;
        uint64_t p = mapping.host_pages.physaddr - *pp;
        host_addr =
          memory_paddr_to_hostaddr
          (mem, p & ~mapping.offset_mask,
           MEM_WRITE);
      } else {
        host_addr = access_result.device->
          dyntrans_data +
          (access_result.device_offset & ~mapping.offset_mask);
      }
      
      if (!NoExceptions && !(mapping.ok & 4)) {
        cpu->update_translation_table
          (cpu,
           vaddr & ~mapping.offset_mask, host_addr,
           wf, mapping.host_pages.physaddr & ~mapping.offset_mask,
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
                           mapping.cache == CACHE_INSTRUCTION?
                           EXCEPTION_IBE : EXCEPTION_DBE,
                           0, vaddr, 0, 0, 0, 0);
      }
      
      
      if (is_m88k<TcPhyspage>()) {
        /*  TODO: This is enough for
            OpenBSD/mvme88k's badaddr()
            implementation... but the
            faulting address should probably
            be included somewhere too!  */
        m88k_exception(cpu, mapping.cache == CACHE_INSTRUCTION
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
      if (!(misc_flags & PHYSICAL) && mapping.cache != CACHE_NONE &&
          !((vaddr & 0xffffffffULL) >= 0xa0000000ULL &&
            (vaddr & 0xffffffffULL) <= 0xbfffffffULL)) {
        if (memory_cache_R3000(cpu, mapping.cache, mapping.host_pages.physaddr,
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
	if (mapping.outside_ram) {
    if (is_ppc<TcPhyspage>()) {
      if ((mapping.host_pages.physaddr & 0xfffe0000) == 0xfffe0000) {
        mapping.host_pages.physaddr &= ~0xffff0000;
      }
    } else if (is_mips<TcPhyspage>()) {
      if ((mapping.host_pages.physaddr & 0xffffc00000ULL) == 0x1fc00000) {
        /*  Ok, this is PROM stuff  */
      } else if ((mapping.host_pages.physaddr & 0xfffff00000ULL) == 0x1ff00000) {
        /*  Sprite reads from this area of memory...  */
        /*  TODO: is this still correct?  */
        if (writeflag == MEM_READ)
          memset(data, 0, len);
        goto do_return_ok;
      }
    }

    if (mapping.host_pages.physaddr >= mem->physical_max && !NoExceptions)
      memory_warn_about_unimplemented_addr
        (cpu, mem, writeflag, mapping.host_pages.physaddr, data, len);
    
    if (writeflag == MEM_READ) {
      /*  Return all zeroes? (Or 0xff? TODO)  */
      memset(data, 0, len);
    }

    /*  Hm? Shouldn't there be a DBE exception for
        invalid writes as well?  TODO  */
    
    goto do_return_ok;
  }


	if ((!is_mips<TcPhyspage>() ||
       (/*  Ugly hack for R2000/R3000 caches:  */
        (cpu->cd.mips.cpu_type.mmu_model != MMU3K ||
         !(cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & MIPS1_ISOL_CACHES))
        )
       ) &&
      !NoExceptions)
    cpu->update_translation_table
      (cpu, vaddr & ~mapping.offset_mask,
       mapping.host_pages.host_load, (misc_flags & MEMORY_USER_ACCESS) |
       (mapping.cache == CACHE_INSTRUCTION?
        (writeflag == MEM_WRITE? 1 : 0) : mapping.ok - 1),
       mapping.host_pages.physaddr & ~mapping.offset_mask, mapping.cache == CACHE_INSTRUCTION);

	if ((mapping.host_pages.physaddr&((1<<BITS_PER_MEMBLOCK)-1)) + len > (1<<BITS_PER_MEMBLOCK)) {
		if (!NoExceptions) {
		    printf("Write over host_pages.host_load boundary?\n");
		    exit(1);
		}

		return MEMORY_ACCESS_FAILED;
	}

	/*  And finally, read or write the data:  */
	if (writeflag == MEM_WRITE) {
		memcpy(mapping.host_pages.host_load + mapping.offset, data, len);
  } else {
		memcpy(data, mapping.host_pages.host_load + mapping.offset, len);
  }

do_return_ok:
	return MEMORY_ACCESS_OK;
}

template <class TcPhyspage>
int memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
                  unsigned char *data, size_t len, int writeflag, int misc_flags) {
  return gen_memory_rw<TcPhyspage, true>(cpu, mem, vaddr, data, len, writeflag, misc_flags);
}

#endif
