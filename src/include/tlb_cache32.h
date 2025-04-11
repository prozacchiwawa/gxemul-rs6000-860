#ifndef TLB_CACHE32_H
#define TLB_CACHE32_H

#include <stdio.h>
#include <string.h>
#include <map>
#include "tlb_cache.h"

extern size_t dyntrans_cache_size;
extern unsigned char *memory_paddr_to_hostaddr
(struct memory *mem, uint64_t paddr, int writeflag);

#define MAX_CACHE_SIZE (5 * 1024 * 1024)

template <typename TcPhyspage, typename VaddrToTlb, typename VpgTlbEntry, typename Cpu> struct vph32 {
private:
	unsigned char *host_load[3 * N_VPH32_ENTRIES];
	unsigned char *host_store[3 * N_VPH32_ENTRIES];
	uint32_t phys_addr[3 * N_VPH32_ENTRIES];
	TcPhyspage *phys_page[3 * N_VPH32_ENTRIES];
	VaddrToTlb vaddr_to_tlbindex[3 * N_VPH32_ENTRIES];
  uint64_t cur_ic_virt;
  TcPhyspage *cur_physpage;
  TcPhyspage *physpage_template;
  decltype(&((TcPhyspage *)0)->ics[0]) next_ic;
  std::map<uint64_t, TcPhyspage> *physpage_map;

  // Pointers to other cpu members (consider moving them here)
	VpgTlbEntry vph_tlb_entry[max_vph_tlb_entries<TcPhyspage>()];

  uint32_t *is_userpage;

public:
  TcPhyspage *get_physpage() const {
    return this->cur_physpage;
  }

  decltype(&((TcPhyspage *)0)->ics[0]) get_ic_page() const {
    return &this->cur_physpage->ics[0];
  }

  decltype(&((TcPhyspage *)0)->ics[0]) get_next_ic() const {
    return this->next_ic;
  }

  decltype(&((TcPhyspage *)0)->ics[0]) next_insn() {
    return next_ic++;
  }

  uint64_t get_ic_phys() const { return this->cur_physpage->physaddr; }

  uint64_t get_low_pc() const {
    return this->next_ic - &this->cur_physpage->ics[0];
  }

  void set_physpage(uint64_t virt, TcPhyspage *page) {
    this->cur_ic_virt = virt;
    this->cur_physpage = page;
  }

  void add_tlb_data(uint32_t *new_is_userpage) {
    this->is_userpage = new_is_userpage;
  }

  void set_physpage_template(TcPhyspage *templ) {
    physpage_template = templ;
    physpage_map = new std::map<uint64_t, TcPhyspage>();
  }

  host_load_store_t get_cached_tlb_pages(Cpu *cpu, uint64_t addr, bool instr) {
    auto index = addr_to_pagenr<TcPhyspage>(addr & 0xffffffff);
    index += N_VPH32_ENTRIES * cpu_get_addr_space<TcPhyspage>(cpu, instr);
    return host_load_store_t {
      phys_addr[index],
      phys_page[index],
      host_load[index],
      host_store[index],
    };
  }

  void set_tlb_physpage(Cpu *cpu, uint64_t addr, TcPhyspage *ppp) {
    auto index = addr_to_pagenr<TcPhyspage>(addr & 0xffffffff);
    index += N_VPH32_ENTRIES * cpu_get_addr_space<TcPhyspage>(cpu, true);
    phys_page[index] = ppp;
  }

  void clear_writable(Cpu *cpu, uint64_t addr) {
    auto index = addr_to_pagenr<TcPhyspage>(addr & 0xffffffff);
    host_store[index] = nullptr;
  }

  void clear_phys(Cpu *cpu, uint64_t vaddr_page, bool instr) {
    uint32_t index = addr_to_pagenr<TcPhyspage>(vaddr_page & 0xffffffff);
    index += N_VPH32_ENTRIES * cpu_get_addr_space<TcPhyspage>(cpu, instr);
    phys_page[index] = nullptr;
  }

  int clear_cache(Cpu *cpu, uint64_t addr, bool instr) {
    auto index = addr_to_pagenr<TcPhyspage>(addr & 0xffffffff);
    index += N_VPH32_ENTRIES * cpu_get_addr_space<TcPhyspage>(cpu, instr);
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
      this->invalidate_tlb_entry(cpu, addr_page, flags, false);
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
          invalidate_tlb_entry(cpu, vph_tlb_entry[r].vaddr_page, 0, false);
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
        this->invalidate_tlb_entry(cpu, vph_tlb_entry[r].vaddr_page, flags, false);
        if (flags & JUST_MARK_AS_NON_WRITABLE) {
          vph_tlb_entry[r].writeflag = 0;
        } else
          vph_tlb_entry[r].valid = 0;
      }
    }
  }

  void update_cache_page
  (Cpu *cpu, uint64_t vaddr_page, uint64_t paddr_page, uint8_t *host_page, int writeflag, int r, bool instr) {
		auto index = addr_to_pagenr<TcPhyspage>(vaddr_page);
    index += N_VPH32_ENTRIES * cpu_get_addr_space<TcPhyspage>(cpu, instr);
		host_load[index] = host_page;
		host_store[index] = writeflag? host_page : nullptr;
		phys_addr[index] = paddr_page;
		phys_page[index] = nullptr;
		vaddr_to_tlbindex[index] = r + 1;
  }

  void invalidate_tlb_entry(Cpu *cpu, uint64_t vaddr_page, int flags, bool instr)
  {
    uint32_t index = addr_to_pagenr<TcPhyspage>(vaddr_page);
    index += N_VPH32_ENTRIES * cpu_get_addr_space<TcPhyspage>(cpu, instr);

    if (is_arm<TcPhyspage>()) {
      is_userpage[index >> 5] &= ~(1 << (index & 31));
    }

    if (flags & JUST_MARK_AS_NON_WRITABLE) {
      /*  printf("JUST MARKING NON-W: vaddr 0x%08x\n",
          (int)vaddr_page);  */
      clear_writable(cpu, vaddr_page);
    } else {
      int tlbi = clear_cache(cpu, vaddr_page, instr);
      if (tlbi > 0) {
        vph_tlb_entry[tlbi-1].valid = 0;
      }
    }
  }

  void update_make_valid_translation
  (Cpu *cpu, uint64_t vaddr_page, uint64_t paddr_page, uint8_t *host_page, int writeflag, bool instr) {
    uint32_t index = addr_to_pagenr<TcPhyspage>(vaddr_page);
    index += N_VPH32_ENTRIES * cpu_get_addr_space<TcPhyspage>(cpu, instr);

    auto useraccess = 0;
    auto found = (int)vaddr_to_tlbindex[index] - 1;

    if (found < 0) {
      /*  Create the new TLB entry, overwriting a "random" entry:  */
      static unsigned int x = 0;
      auto r = (x++) % max_vph_tlb_entries<TcPhyspage>();

      if (vph_tlb_entry[r].valid) {
        /*  This one has to be invalidated first:  */
        invalidate_tlb_entry(cpu, vph_tlb_entry[r].vaddr_page, 0, instr);
      }

      vph_tlb_entry[r].valid = 1;
      vph_tlb_entry[r].host_page = host_page;
      vph_tlb_entry[r].paddr_page = paddr_page;
      vph_tlb_entry[r].vaddr_page = vaddr_page;
      vph_tlb_entry[r].writeflag = writeflag & MEM_WRITE;

      /*  Add the new translation to the table:  */
      update_cache_page
        (cpu, vaddr_page, paddr_page, host_page, writeflag, r, instr);

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

      index = addr_to_pagenr<TcPhyspage>(vaddr_page);
      index += N_VPH32_ENTRIES * cpu_get_addr_space<TcPhyspage>(cpu, instr);

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

  void invalidate_tc_code(Cpu *cpu, uint64_t addr, int flags) {
    int r;
    uint32_t vaddr_page, paddr_page;

    addr &= ~(pagesize<TcPhyspage>()-1);

    /*  printf("DYNTRANS_INVALIDATE_TC_CODE addr=0x%08x flags=%i\n",
        (int)addr, flags);  */

    if (flags & INVALIDATE_PADDR) {
      TcPhyspage *ppp;

      auto found = physpage_map->find(addr);
      if (found == physpage_map->end()) {
        return;
      }

      ppp = &found->second;

      if (ppp != nullptr && !ppp->translations_bitmap.empty()) {
        for (auto i = 0; i < ic_entries_per_page<TcPhyspage>(); i++) {
          ppp->ics[i].f = physpage_template->ics[0].f;
        }

        memset(&ppp->translations_bitmap, 0, sizeof(ppp->translations_bitmap));
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
          clear_phys(cpu, vaddr_page, true);
        }
      }
    }
  }

  /*
   *  XXX_tc_allocate_default_page():
   *
   *  Create a default page (with just pointers to instr(to_be_translated)
   *  at cpu->translation_cache_cur_ofs.
   */
  TcPhyspage *allocate_physpage(Cpu *cpu, uint64_t physaddr)
  {
    auto found = physpage_map->insert(std::make_pair(physaddr, *physpage_template));
    return &found.first->second;
  }

  void pc_to_pointers_generic(Cpu *cpu) {
    uint32_t cached_pc = cpu->pc;
    int ok;
    TcPhyspage *ppp;

    auto host_pages = get_cached_tlb_pages(cpu, cached_pc, true);

    /*  Virtual to physical address translation:  */
    ok = 0;
    if (host_pages.host_load != nullptr) {
      ok = 1;
    }

    if (!ok) {
      uint64_t paddr;
      if (cpu->translate_v2p != NULL) {
        uint64_t vaddr = is_mips<TcPhyspage>() ? (int32_t)cached_pc : cached_pc;
        ok = cpu->translate_v2p(cpu, vaddr, &paddr, FLAG_INSTR);
      } else {
        paddr = cached_pc;
        ok = 1;
      }
      if (!ok) {
        /*
         *  The PC is now set to the exception handler.
         *  Try to find the paddr in the translation arrays,
         *  or if that fails, call translate_v2p for the
         *  exception handler.
         */
        /*  fatal("TODO: instruction vaddr=>paddr translation "
            "failed. vaddr=0x%" PRIx64"\n", (uint64_t)cached_pc);
            fatal("!! cpu->pc=0x%" PRIx64"\n", (uint64_t)cpu->pc); */

        /*  If there was an exception, the PC has changed.
            Update cached_pc:  */
        cached_pc = cpu->pc;

        host_pages = get_cached_tlb_pages(cpu, cached_pc, true);

        if (host_pages.host_load) {
          paddr = host_pages.physaddr;
          ok = 1;
        }

        if (!ok) {
          ok = cpu->translate_v2p(cpu, cpu->pc, &paddr, FLAG_INSTR);
        }

        /*  printf("EXCEPTION HANDLER: vaddr = 0x%x ==> "
            "paddr = 0x%x\n", (int)cpu->pc, (int)paddr);
            fatal("!? cpu->pc=0x%" PRIx64"\n", (uint64_t)cpu->pc); */

        if (!ok) {
          fprintf(stderr, "FATAL: could not find physical"
                " address of the exception handler?");
          exit(1);
        }
      }

      host_pages.physaddr = paddr;
    }

    host_pages.physaddr &= ~(pagesize<TcPhyspage>() - 1);

    if (host_pages.host_load == nullptr) {
      int q = pagesize<TcPhyspage>() - 1;
      unsigned char *host_page =
        memory_paddr_to_hostaddr(cpu->mem, host_pages.physaddr, MEM_READ);
      if (host_page != NULL) {
        cpu->update_translation_table
          (cpu, cached_pc & ~q, host_page, 0, host_pages.physaddr, true);
      }
    }

    auto found = physpage_map->find(host_pages.physaddr);
    if (found != physpage_map->end()) {
      ppp = &found->second;
    } else {
      ppp = allocate_physpage(cpu, host_pages.physaddr);
    }

    /*  Here, ppp points to a valid physical page struct.  */
    if (host_pages.host_load != nullptr) {
      set_tlb_physpage(cpu, cached_pc, ppp);
    }

    /*
     *  If there are no translations yet on this page, then mark it
     *  as non-writable. If there are already translations, then it
     *  should already have been marked as non-writable.
     */
    if (ppp->translations_bitmap.empty()) {
      cpu->invalidate_translation_caches
        (cpu, host_pages.physaddr, JUST_MARK_AS_NON_WRITABLE | INVALIDATE_PADDR);
    }

    set_physpage(cached_pc & ~0xfff, ppp);

    set_next_ic(cached_pc);
  }

  void move_to_physpage(Cpu *cpu) {
    uint32_t cached_pc = cpu->pc;

    auto host_pages = get_cached_tlb_pages(cpu, cached_pc, true);

    if (host_pages.ppp == nullptr) {
      pc_to_pointers_generic(cpu);
      return;
    }

    auto ppp = static_cast<TcPhyspage*>(host_pages.ppp);
    set_physpage(cpu->pc & ~(pagesize<TcPhyspage>() - 1), ppp);
    next_ic = get_ic_page() + pc_to_ic_entry<TcPhyspage>(cached_pc);
  }

  void nothing() {
    next_ic --;
  }

  void bump_ic() {
    next_ic ++;
  }

  void do_nothing(decltype(&((TcPhyspage*)0)->ics[0]) nothing_call) {
    next_ic = nothing_call;
  }

  decltype(&((TcPhyspage*)0)->ics[0]) bad_translation(decltype(&((TcPhyspage*)0)->ics[0]) nothing_call) {
    do_nothing(nothing_call);
    return next_ic ++;
  }

  void set_next_ic(uint64_t pc) {
    next_ic = get_ic_page() + pc_to_ic_entry<TcPhyspage>(pc);
  }

  uint64_t sync_low_pc(Cpu *cpu, decltype(&((TcPhyspage*)0)->ics[0]) ic) {
    return ((size_t)ic - (size_t)get_ic_page()) / sizeof(*ic);
  }
};

#endif//TLB_CACHE32_H
