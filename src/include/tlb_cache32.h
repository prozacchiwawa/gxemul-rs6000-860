#ifndef TLB_CACHE32_H
#define TLB_CACHE32_H

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <map>
#include "tlb_cache.h"

extern size_t dyntrans_cache_size;
extern unsigned char *memory_paddr_to_hostaddr
(struct memory *mem, uint64_t paddr, int writeflag);

#define MAX_CACHE_SIZE (5 * 1024 * 1024)
#define SMALL_ENTRIES 128

template <typename TcPhyspage, typename VaddrToTlb, typename VpgTlbEntry, typename Cpu> struct vph32 {
private:
  host_load_store_t *pages;
  uint64_t cur_ic_virt;
  TcPhyspage *cur_physpage;
  TcPhyspage *physpage_template;
  decltype(&((TcPhyspage *)0)->ics[0]) next_ic;
  std::map<uint64_t, VaddrToTlb> *vaddr_to_tlbindex;
  std::map<uint64_t, TcPhyspage> *physpage_map;

  // Pointers to other cpu members (consider moving them here)
	VpgTlbEntry vph_tlb_entry[max_vph_tlb_entries<TcPhyspage>()];

  int max_tlb_entries;
  uint32_t *is_userpage;

protected:
  int clear_cache(int index) {
    pages[index] = host_load_store_t { };
    auto found = vaddr_to_tlbindex->find(index);
    int result = 0;
    if (found != vaddr_to_tlbindex->end()) {
      auto tlbi = found->second;
      vaddr_to_tlbindex->erase(index);
      result = tlbi;
    }
    return result;
  }

  void decomission_vph(Cpu *cpu, int r) {
    if (vph_tlb_entry[r].valid) {
      invalidate_tlb_entry(cpu, vph_tlb_entry[r].vaddr_page, 0);
      vph_tlb_entry[r].valid=0;
    }
  }

  void update_cache_page
  (Cpu *cpu, uint64_t vaddr_page, uint64_t paddr_page, uint8_t *host_page, int writeflag, int r, bool instr) {
    auto index = get_page_index(cpu, vaddr_page, instr);
    auto old_tlbindex = vaddr_to_tlbindex->find(index);
    if (old_tlbindex != vaddr_to_tlbindex->end()) {
      fprintf(stderr, "we shouldn't orphan a cache page\n");
      abort();
    }
    pages[index] = host_load_store_t {
    physaddr: paddr_page,
    ppp: nullptr,
    host_load: host_page,
    host_store: writeflag? host_page : nullptr
    };
		vaddr_to_tlbindex->insert(std::make_pair(index, r + 1));
    show_tlb(cpu);
    invariant(cpu);
  }

  void invalidate_tlb_entry(Cpu *cpu, uint64_t vaddr_page, int flags)
  {
    auto index = get_page_index(cpu, vaddr_page, false);

    if (is_arm<TcPhyspage>()) {
      is_userpage[index >> 5] &= ~(1 << (index & 31));
    }

    if (flags & JUST_MARK_AS_NON_WRITABLE) {
      /*  printf("JUST MARKING NON-W: vaddr 0x%08x\n",
          (int)vaddr_page);  */
      clear_writable(cpu, vaddr_page);
    } else {
      index %= N_VPH32_ENTRIES;
      for (auto i = 0; i < 3; i++) {
        auto real_index = i * N_VPH32_ENTRIES + index;
        int tlbi = clear_cache(real_index);
        if (tlbi > 0) {
          decomission_vph(cpu, tlbi - 1);
        }
      }
    }
    invariant(cpu);
  }

  // public:
  int max_entries() const { return max_tlb_entries; }

  void show_tlb(Cpu *cpu) {
#if 0
    fprintf(stderr, "CACHE STATE\n");
    for (auto i = 0; i < max_entries(); i++) {
      if (this->vph_tlb_entry[i].valid) {
        auto code_host_page = get_cached_tlb_pages(cpu, this->vph_tlb_entry[i].vaddr_page, true);
        auto data_host_page = get_cached_tlb_pages(cpu, this->vph_tlb_entry[i].vaddr_page, false);
        fprintf
          (stderr, "%d %s V %08x P %08x C %08x D %08x %s%s\n",
           i,
           this->vph_tlb_entry[i].writeflag ? "W" : "R",
           (unsigned int)this->vph_tlb_entry[i].vaddr_page,
           (unsigned int)this->vph_tlb_entry[i].paddr_page,
           (unsigned int)code_host_page.physaddr,
           (unsigned int)data_host_page.physaddr,
           data_host_page.host_load ? "L" : "l",
           data_host_page.host_store ? "S" : "s"
           );
      }
    }
    const char *cache_names[] = {
      "PHYS", "DATA", "CODE"
    };
    for (auto j = 0; j < 3; j++) {
      for (auto i = 0; i < N_VPH32_ENTRIES; i++) {
        auto page_index = j * N_VPH32_ENTRIES + i;
        auto page = &pages[page_index];
        if (page->physaddr) {
          auto found = vaddr_to_tlbindex->find(page_index);
          auto idx = -1;
          if (found != vaddr_to_tlbindex->end()) {
            idx = found->second - 1;
          }
          fprintf(stderr, "%s %d %08x -> %08x\n", cache_names[j], idx, i * pagesize<TcPhyspage>(), page->physaddr);
        }
      }
    }
#endif
  }

  void invariant(Cpu *cpu) const {
#if 0
    const char *cache_names[] = {
      "PHYS", "DATA", "CODE"
    };
    assert(vaddr_to_tlbindex->size() <= max_entries());
    for (auto j = 0; j < 3; j++) {
      for (auto i = 0; i < N_VPH32_ENTRIES; i++) {
        auto page_index = j * N_VPH32_ENTRIES + i;
        if (pages[page_index].physaddr) {
          auto found = vaddr_to_tlbindex->find(page_index);
          if (found == vaddr_to_tlbindex->end()) {
            fprintf(stderr, "%s page %08x does not have a tlb index\n", cache_names[j], (unsigned int)(i * pagesize<TcPhyspage>()));
            abort();
          }
        }
      }
    }
#endif
  }

  int get_page_index(Cpu *cpu, uint64_t vaddr, bool instr) const {
    auto index = addr_to_pagenr<TcPhyspage>(vaddr & 0xffffffff);
    assert(index < N_VPH32_ENTRIES);
    index += N_VPH32_ENTRIES * cpu_get_addr_space<TcPhyspage>(cpu, instr);
    return index;
  }

  void set_physpage(uint64_t virt, TcPhyspage *page) {
    this->cur_ic_virt = virt;
    this->cur_physpage = page;
  }

  void clear_writable(Cpu *cpu, uint64_t addr) {
    auto index = get_page_index(cpu, addr, false);
    pages[index].host_store = nullptr;
  }

  void clear_phys(Cpu *cpu, uint64_t vaddr_page, bool instr) {
    auto index = get_page_index(cpu, vaddr_page, instr);
    pages[index].ppp = nullptr;
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

public:
  void set_physpage_template(TcPhyspage *templ) {
    max_tlb_entries = std::min(SMALL_ENTRIES, max_vph_tlb_entries<TcPhyspage>());
    physpage_template = templ;
    physpage_map = new std::map<uint64_t, TcPhyspage>();
    vaddr_to_tlbindex = new std::map<uint64_t, VaddrToTlb>();
    pages = (host_load_store_t *)calloc(3 * N_VPH32_ENTRIES, sizeof(host_load_store_t));
  }

  TcPhyspage *get_physpage() const {
    return this->cur_physpage;
  }

  uint64_t get_low_pc() const {
    return this->next_ic - &this->cur_physpage->ics[0];
  }

  decltype(&((TcPhyspage *)0)->ics[0]) get_next_ic() const {
    return this->next_ic;
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
      show_tlb(cpu);
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
    invariant(cpu);
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
    invariant(cpu);
  }

  void set_next_ic(uint64_t pc) {
    next_ic = get_ic_page() + pc_to_ic_entry<TcPhyspage>(pc);
  }

  decltype(&((TcPhyspage *)0)->ics[0]) next_insn() {
    return next_ic++;
  }

  void set_tlb_physpage(Cpu *cpu, uint64_t addr, TcPhyspage *ppp) {
    auto index = get_page_index(cpu, addr, true);
    pages[index].ppp = ppp;
  }

  host_load_store_t get_cached_tlb_pages(Cpu *cpu, uint64_t addr, bool instr) {
    auto index = get_page_index(cpu, addr, instr);
    return pages[index];
  }

  decltype(&((TcPhyspage *)0)->ics[0]) get_ic_page() const {
    return &this->cur_physpage->ics[0];
  }

  uint64_t sync_low_pc(Cpu *cpu, decltype(&((TcPhyspage*)0)->ics[0]) ic) {
    return ((size_t)ic - (size_t)get_ic_page()) / sizeof(*ic);
  }

  uint64_t get_ic_phys() const { return this->cur_physpage->physaddr; }

  void add_tlb_data(uint32_t *new_is_userpage) {
    this->is_userpage = new_is_userpage;
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

  void update_make_valid_translation
  (Cpu *cpu, uint64_t vaddr_page, uint64_t paddr_page, uint8_t *host_page, int writeflag, bool instr) {
    auto index = get_page_index(cpu, vaddr_page, instr);

    auto useraccess = 0;
    auto found = -1;

    auto vf = vaddr_to_tlbindex->find(index);
    if (vf != vaddr_to_tlbindex->end()) {
      found = (int)vf->second - 1;
    }

    if (found < 0) {
      /*  Create the new TLB entry, overwriting a "random" entry:  */
      static unsigned int x = 0;
      auto r = (x++) % max_entries();

      if (vph_tlb_entry[r].valid) {
        /*  This one has to be invalidated first:  */
        decomission_vph(cpu, r);
      }

      vph_tlb_entry[r].valid = 1;
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

      pages[index].ppp = nullptr;
      if (is_arm<TcPhyspage>()) {
        is_userpage[index>>5] &= ~(1<<(index&31));
        if (useraccess)
          is_userpage[index >> 5]
            |= 1 << (index & 31);
      }

      if (writeflag & MEM_DOWNGRADE) {
        pages[index].host_store = nullptr;
      } else {
        /*  Change the entire physical/host mapping:  */
        auto p = &pages[index];
        p->host_load = host_page;
        p->host_store = writeflag ? host_page : nullptr;
        p->physaddr = paddr_page;
      }

      show_tlb(cpu);
      invariant(cpu);
    }
  }

  void invalidate_tc(Cpu *cpu, uint64_t addr, int flags) {
    int r;
    uint64_t addr_page = addr & ~(pagesize<TcPhyspage>() - 1);

    /*  fatal("invalidate(): ");  */

    /*  Invalidate everything:  */
    if (flags & INVALIDATE_ALL) {
      /*  fatal("all\n");  */
      for (r=0; r<max_entries(); r++) {
        decomission_vph(cpu, r);
      }
      return;
    }

    /*  Quick case for _one_ virtual addresses: see note above.  */
    if (flags & INVALIDATE_VADDR) {
      /*  fatal("vaddr 0x%08x\n", (int)addr_page);  */
      this->invalidate_tlb_entry(cpu, addr_page, flags);
      return;
    }

    /*  Invalidate a physical page:  */

    if (!(flags & INVALIDATE_PADDR)) {
      fprintf(stderr, "HUH? Invalidate: Not vaddr, all, or paddr?\n");
    }

    /*  fatal("addr 0x%08x\n", (int)addr_page);  */

    for (r=0; r<max_entries(); r++) {
      if (vph_tlb_entry[r].valid && addr_page == vph_tlb_entry[r].paddr_page) {
        this->invalidate_tlb_entry(cpu, vph_tlb_entry[r].vaddr_page, flags);
        if (flags & JUST_MARK_AS_NON_WRITABLE) {
          vph_tlb_entry[r].writeflag = 0;
        } else {
          decomission_vph(cpu, r);
        }
      }
    }
    show_tlb(cpu);
    invariant(cpu);
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
    for (r = 0; r < max_entries(); r ++) {
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

    show_tlb(cpu);
    invariant(cpu);
  }
};

#endif//TLB_CACHE32_H
