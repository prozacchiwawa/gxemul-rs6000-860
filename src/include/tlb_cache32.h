#ifndef TLB_CACHE32_H
#define TLB_CACHE32_H

#include <stdio.h>
#include "tlb_cache.h"

template <typename TcPhyspage, typename VaddrToTlb, typename VpgTlbEntry, typename Cpu> struct vph32 {
private:
	unsigned char *host_load[2 * N_VPH32_ENTRIES];
	unsigned char *host_store[2 * N_VPH32_ENTRIES];
	uint32_t phys_addr[2 * N_VPH32_ENTRIES];
	TcPhyspage *phys_page[2 * N_VPH32_ENTRIES];
	VaddrToTlb vaddr_to_tlbindex[2 * N_VPH32_ENTRIES];
	VpgTlbEntry *vph_tlb_entry;
  uint32_t *is_userpage;

public:
  void add_tlb_data(uint32_t *is_userpage, VpgTlbEntry *vph_tlb_entry) {
    this->is_userpage = is_userpage;
    this->vph_tlb_entry = vph_tlb_entry;
  }

  static uint64_t addr_to_pagenr(uint64_t addr) {
    return addr >> 12;
  }

  host_load_store_t get_cached_tlb_pages(Cpu *cpu, uint64_t addr) {
    auto index = addr_to_pagenr(addr & 0xffffffff);
    return host_load_store_t {
      phys_addr[index],
      phys_page[index],
      host_load[index],
      host_store[index],
    };
  }

  void set_tlb_physpage(uint64_t addr, TcPhyspage *ppp) {
    auto index = addr_to_pagenr(addr & 0xffffffff);
    phys_page[index] = ppp;
  }

  void clear_writable(uint64_t addr) {
    auto index = addr_to_pagenr(addr & 0xffffffff);
    host_store[index] = nullptr;
  }

  void clear_phys(uint64_t vaddr_page) {
    uint32_t index = addr_to_pagenr(vaddr_page & 0xffffffff);
    phys_page[index] = nullptr;
  }

  int clear_cache(uint64_t addr) {
    auto index = addr_to_pagenr(addr & 0xffffffff);
		host_load[index] = nullptr;
		host_store[index] = nullptr;
		phys_addr[index] = 0;
		phys_page[index] = nullptr;
		int tlbi = vaddr_to_tlbindex[index];
		vaddr_to_tlbindex[index] = 0;
    return tlbi;
  }

  void invalidate_tc(Cpu *cpu, uint64_t addr, int flags) {
    int r;
    uint64_t addr_page = addr & ~(pagesize<TcPhyspage>() - 1);

    /*  fatal("invalidate(): ");  */

    /*  Quick case for _one_ virtual addresses: see note above.  */
    if (flags & INVALIDATE_VADDR) {
      /*  fatal("vaddr 0x%08x\n", (int)addr_page);  */
      this->invalidate_tlb_entry(addr_page, flags);
      return;
    }

    /*  Invalidate everything:  */
#ifdef DYNTRANS_PPC
    if (flags & INVALIDATE_ALL && flags & INVALIDATE_VADDR_UPPER4) {
      /*  fatal("all, upper4 (PowerPC segment)\n");  */
      for (r=0; r<max_vph_tlb_entries<TcPhyspage>(); r++) {
        if (vph_tlb_entry[r].valid &&
            (vph_tlb_entry[r].vaddr_page
             & 0xf0000000) == addr_page) {
          this->invalidate_tlb_entry(vph_tlb_entry[r].vaddr_page, 0);
          cpu->cd.DYNTRANS_ARCH.vph_tlb_entry[r].valid=0;
        }
      }
      return;
    }

    if ((flags & INVALIDATE_ALL) && (flags & INVALIDATE_IDENTITY)) {
      // Invalidate mappings that overlap ram.
      for (r=0; r<max_vph_tlb_entries<TcPhyspage>(); r++) {
        if (vph_tlb_entry[r].valid &&
            (vph_tlb_entry[r].vaddr_page < cpu->mem->physical_max)) {
          this->invalidate_tc(cpu, vph_tlb_entry[r].vaddr_page, INVALIDATE_VADDR);
          vph_tlb_entry[r].valid=0;
        }
      }
      return;
    }
#endif
    if (flags & INVALIDATE_ALL) {
      /*  fatal("all\n");  */
      for (r=0; r<max_vph_tlb_entries<TcPhyspage>(); r++) {
        if (vph_tlb_entry[r].valid) {
          invalidate_tlb_entry(vph_tlb_entry[r].vaddr_page, 0);
          vph_tlb_entry[r].valid=0;
        }
      }
      return;
    }

    /*  Invalidate a physical page:  */

    if (!(flags & INVALIDATE_PADDR)) {
      fprintf(stderr, "HUH? Invalidate: Not vaddr, all, or paddr?\n");
    }

    /*  fatal("addr 0x%08x\n", (int)addr_page);  */

    for (r=0; r<max_vph_tlb_entries<TcPhyspage>(); r++) {
      if (vph_tlb_entry[r].valid && addr_page == vph_tlb_entry[r].paddr_page) {
        this->invalidate_tlb_entry(vph_tlb_entry[r].vaddr_page, flags);
        if (flags & JUST_MARK_AS_NON_WRITABLE) {
          vph_tlb_entry[r].writeflag = 0;
        } else
          vph_tlb_entry[r].valid = 0;
      }
    }
  }

  void update_cache_page
  (uint64_t vaddr_page, uint64_t paddr_page, uint8_t *host_page, int writeflag, int r) {
		auto index = addr_to_pagenr(vaddr_page);
		host_load[index] = host_page;
		host_store[index] = writeflag? host_page : nullptr;
		phys_addr[index] = paddr_page;
		phys_page[index] = nullptr;
		vaddr_to_tlbindex[index] = r + 1;
  }

  void invalidate_tlb_entry(uint64_t vaddr_page, int flags)
  {
    uint32_t index = addr_to_pagenr(vaddr_page);

    if (is_arm<TcPhyspage>()) {
      is_userpage[index >> 5] &= ~(1 << (index & 31));
    }

    if (flags & JUST_MARK_AS_NON_WRITABLE) {
      /*  printf("JUST MARKING NON-W: vaddr 0x%08x\n",
          (int)vaddr_page);  */
      clear_writable(vaddr_page);
    } else {
      int tlbi = clear_cache(vaddr_page);
      if (tlbi > 0) {
        vph_tlb_entry[tlbi-1].valid = 0;
      }
    }
  }

  void update_make_valid_translation
  (uint64_t vaddr_page, uint64_t paddr_page, uint8_t *host_page, int writeflag) {
    uint32_t index = addr_to_pagenr(vaddr_page);
    auto useraccess = 0;
    auto found = (int)vaddr_to_tlbindex[addr_to_pagenr(vaddr_page)] - 1;

    if (found < 0) {
      /*  Create the new TLB entry, overwriting a "random" entry:  */
      static unsigned int x = 0;
      auto r = (x++) % max_vph_tlb_entries<TcPhyspage>();

      if (vph_tlb_entry[r].valid) {
        /*  This one has to be invalidated first:  */
        invalidate_tlb_entry(vph_tlb_entry[r].vaddr_page, 0);
      }

      vph_tlb_entry[r].valid = 1;
      vph_tlb_entry[r].host_page = host_page;
      vph_tlb_entry[r].paddr_page = paddr_page;
      vph_tlb_entry[r].vaddr_page = vaddr_page;
      vph_tlb_entry[r].writeflag = writeflag & MEM_WRITE;

      /*  Add the new translation to the table:  */
      update_cache_page
        (vaddr_page, paddr_page, host_page, writeflag, r);

      if (is_arm<TcPhyspage>() && useraccess) {
        is_userpage[index >> 5] |= 1 << (index & 31);
      }
    } else {
      /*
       *  The translation was already in the TLB.
       *	Writeflag = 0:  Do nothing.
       *	Writeflag = 1:  Make sure the page is writable.
       *	Writeflag = MEM_DOWNGRADE: Downgrade to readonly.
       */
      auto r = found;
      if (writeflag & MEM_WRITE) {
        vph_tlb_entry[r].writeflag = 1;
      }
      if (writeflag & MEM_DOWNGRADE) {
        vph_tlb_entry[r].writeflag = 0;
      }

      index = addr_to_pagenr(vaddr_page);
      phys_page[index] = nullptr;
      if (is_arm<TcPhyspage>()) {
        is_userpage[index>>5] &= ~(1<<(index&31));
        if (useraccess)
          is_userpage[index >> 5]
            |= 1 << (index & 31);
      }

      if (phys_addr[index] == paddr_page) {
        if (writeflag & MEM_WRITE) {
          host_store[index] = host_page;
        }
        if (writeflag & MEM_DOWNGRADE) {
          host_store[index] = nullptr;
        }
      } else {
        /*  Change the entire physical/host mapping:  */
        host_load[index] = host_page;
        host_store[index] = writeflag ? host_page : nullptr;
        phys_addr[index] = paddr_page;
      }
    }
  }

  void invalidate_tc_code(struct cpu *cpu, uint64_t addr, int flags, decltype(((TcPhyspage*)0)->ics->f) to_be_translated) {
    int r;
    uint32_t vaddr_page, paddr_page;

    addr &= ~(pagesize<TcPhyspage>()-1);

    /*  printf("DYNTRANS_INVALIDATE_TC_CODE addr=0x%08x flags=%i\n",
        (int)addr, flags);  */

    if (flags & INVALIDATE_PADDR) {
      int pagenr, table_index;
      uint32_t physpage_ofs, *physpage_entryp;
      TcPhyspage *ppp, *prev_ppp;

      pagenr = addr_to_pagenr(addr);
      table_index = PAGENR_TO_TABLE_INDEX(pagenr);

      physpage_entryp = &(((uint32_t *)cpu->translation_cache)[table_index]);
      physpage_ofs = *physpage_entryp;

      /*  Return immediately if there is no code translation
          for this page.  */
      if (physpage_ofs == 0)
        return;

      prev_ppp = ppp = NULL;

      /*  Traverse the physical page chain:  */
      while (physpage_ofs != 0) {
        prev_ppp = ppp;
        ppp = (TcPhyspage *)(cpu->translation_cache + physpage_ofs);

        /*  If we found the page in the cache,
            then we're done:  */
        if (ppp->physaddr == addr)
          break;

        /*  Try the next page in the chain:  */
        physpage_ofs = ppp->next_ofs;
      }

      /*  If there is no translation, there is no need to go
          on and try to remove it from the vph_tlb_entry array:  */
      if (physpage_ofs == 0)
        return;

      prev_ppp = prev_ppp;	// shut up compiler warning

      /*
       *  Instead of removing the page from the code cache, each
       *  entry can be set to "to_be_translated". This is slow in
       *  the general case, but in the case of self-modifying code,
       *  it might be faster since we don't risk wasting cache
       *  memory as quickly (which would force unnecessary Restarts).
       */
      if (ppp != NULL && ppp->translations_bitmap != 0) {
        uint32_t x = ppp->translations_bitmap;	/*  TODO: urk Should be same type as the bitmap */
        int i, j, n, m;

        if (is_arm<TcPhyspage>()) {
          /*
           *  Note: On ARM, PC-relative load instructions are
           *  implemented as immediate mov instructions. When
           *  setting parts of the page to "to be translated",
           *  we cannot keep track of which of the immediate
           *  movs that were affected, so we need to clear
           *  the entire page. (ARM only; not for the general
           *  case.)
           */
          x = 0xffffffff;
        }

        n = 8 * sizeof(x);
        m = ic_entries_per_page<TcPhyspage>() / n;

        for (i=0; i<n; i++) {
          if (x & 1) {
            for (j=0; j<m; j++)
              ppp->ics[i*m + j].f = to_be_translated;
          }

          x >>= 1;
        }

        ppp->translations_bitmap = 0;

        /*  Clear the list of translatable ranges:  */
        if (ppp->translation_ranges_ofs != 0) {
          struct physpage_ranges *physpage_ranges =
            (struct physpage_ranges *)
            (cpu->translation_cache +
             ppp->translation_ranges_ofs);
          physpage_ranges->next_ofs = 0;
          physpage_ranges->n_entries_used = 0;
        }
      }
    }

    /*  Invalidate entries in the VPH table:  */
    for (r = 0; r < max_vph_tlb_entries<TcPhyspage>(); r ++) {
      if (vph_tlb_entry[r].valid) {
        vaddr_page = vph_tlb_entry[r].vaddr_page & ~(pagesize<TcPhyspage>()-1);
        paddr_page = vph_tlb_entry[r].paddr_page & ~(pagesize<TcPhyspage>()-1);

        if (flags & INVALIDATE_ALL ||
            (flags & INVALIDATE_PADDR && paddr_page == addr) ||
            (flags & INVALIDATE_VADDR && vaddr_page == addr)) {
          clear_phys(vaddr_page);
        }
      }
    }
  }
};

#endif//TLB_CACHE32_H
