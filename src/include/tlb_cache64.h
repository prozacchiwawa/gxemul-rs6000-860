#ifndef TLB_CACHE64_H
#define TLB_CACHE64_H

#include <stdio.h>
#include <stdlib.h>
#include "tlb_cache.h"

/*
 *  64-bit dyntrans emulated Virtual -> physical -> host address translation:
 *  -------------------------------------------------------------------------
 *
 *  Usage: e.g. VPH64(alpha,ALPHA)
 *           or VPH64(sparc,SPARC)
 *
 *  l1_64 is an array containing poiners to l2 tables.
 *
 *  l2_64_dummy is a pointer to a "dummy l2 table". Instead of having NULL
 *  pointers in l1_64 for unused slots, a pointer to the dummy table can be
 *  used.
 */

template <typename TcPhyspage> constexpr int dyntrans_l1n() { return 17; }
template <typename TcPhyspage> constexpr int dyntrans_l2n() { return 17; }
template <typename TcPhyspage> constexpr int dyntrans_l3n() { return 17; }

template <typename TcPhyspage, typename L3Table, typename L2Table, typename VpgTlbEntry, typename Cpu> struct vph64 {
private:
	L3Table *l3_64_dummy;
	L3Table *next_free_l3;
	L2Table *l2_64_dummy;
	L2Table *next_free_l2;
	L2Table *l1_64[1 << dyntrans_l1n<TcPhyspage>()];
  uint64_t cur_ic_virt;
  TcPhyspage *cur_physpage;
  TcPhyspage *physpage_template;
  decltype(&((TcPhyspage *)0)->ics[0]) next_ic;
  std::map<uint64_t, TcPhyspage> *physpage_map;

  // Pointers to other cpu members (consider moving them here)
	VpgTlbEntry *vph_tlb_entry;
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

  uint64_t get_low_pc() const {
    auto inumber = next_ic - &cur_physpage->ics[0];
    auto factor = pagesize<TcPhyspage>() / ic_entries_per_page<TcPhyspage>();
    return factor * inumber;
  }

  uint64_t get_ic_phys() const { return this->cur_physpage->physaddr; }
  void set_physpage(uint64_t virt, TcPhyspage *page) {
    this->cur_ic_virt = virt;
    this->cur_physpage = page;
  }

  void set_physpage_template(TcPhyspage *templ) {
    physpage_template = templ;
    physpage_map = new std::map<uint64_t, TcPhyspage>();
  }

  struct host_load_store_t get_cached_tlb_pages(Cpu *cpu, uint64_t addr) {
    const uint32_t mask1 = (1 << dyntrans_l1n<TcPhyspage>()) - 1;
    const uint32_t mask2 = (1 << dyntrans_l2n<TcPhyspage>()) - 1;
    const uint32_t mask3 = (1 << dyntrans_l3n<TcPhyspage>()) - 1;
    uint32_t x1, x2, x3;
    L2Table *l2;
    L3Table *l3;

    x1 = (addr >> (64-dyntrans_l1n<TcPhyspage>())) & mask1;
    x2 = (addr >> (64-dyntrans_l1n<TcPhyspage>()-dyntrans_l2n<TcPhyspage>())) & mask2;
    x3 = (addr >> (64-dyntrans_l1n<TcPhyspage>()-dyntrans_l2n<TcPhyspage>()-dyntrans_l3n<TcPhyspage>())) & mask3;
    /*  fatal("X3: addr=%016" PRIx64" x1=%x x2=%x x3=%x\n",
        (uint64_t)addr, (int)x1, (int)x2, (int)x3);  */
    l2 = l1_64[x1];
    /*  fatal("  l2 = %p\n", l2);  */
    l3 = l2->l3[x2];
    /*  fatal("  l3 = %p\n", l3);  */
    return host_load_store_t {
      l3->phys_addr[x3],
      l3->phys_page[x3],
      l3->host_load[x3],
      l3->host_store[x3]
    };
  }
  void invalidate_tlb_entry(uint64_t vaddr_page, int flags) {
    const uint32_t mask1 = (1 << dyntrans_l1n<TcPhyspage>()) - 1;
    const uint32_t mask2 = (1 << dyntrans_l2n<TcPhyspage>()) - 1;
    const uint32_t mask3 = (1 << dyntrans_l3n<TcPhyspage>()) - 1;
    uint32_t x1, x2, x3;
    L2Table *l2;
    L3Table *l3;

    x1 = (vaddr_page >> (64-dyntrans_l1n<TcPhyspage>())) & mask1;
    x2 = (vaddr_page >> (64-dyntrans_l1n<TcPhyspage>()-dyntrans_l2n<TcPhyspage>())) & mask2;
    x3 = (vaddr_page >> (64-dyntrans_l1n<TcPhyspage>()-dyntrans_l2n<TcPhyspage>()-dyntrans_l3n<TcPhyspage>()))& mask3;

    l2 = l1_64[x1];
    if (l2 == l2_64_dummy)
      return;

    l3 = l2->l3[x2];
    if (l3 == l3_64_dummy)
      return;

    if (flags & JUST_MARK_AS_NON_WRITABLE) {
      l3->host_store[x3] = nullptr;
      return;
    }

    l3->host_load[x3] = nullptr;
    l3->host_store[x3] = nullptr;
    l3->phys_addr[x3] = 0;
    l3->phys_page[x3] = nullptr;
    if (l3->vaddr_to_tlbindex[x3] != 0) {
      vph_tlb_entry[l3->vaddr_to_tlbindex[x3] - 1].valid = 0;
      l3->refcount --;
    }
    l3->vaddr_to_tlbindex[x3] = 0;

    if (l3->refcount < 0) {
      fprintf(stderr, "xxx_invalidate_tlb_entry(): huh? Refcount bug.\n");
      exit(1);
    }

    if (l3->refcount == 0) {
      l3->next = next_free_l3;
      next_free_l3 = l3;
      l2->l3[x2] = l3_64_dummy;
      l2->refcount --;
      if (l2->refcount < 0) {
        fprintf(stderr, "xxx_invalidate_tlb_entry(): Refcount bug L2.\n");
        exit(1);
      }
      if (l2->refcount == 0) {
        l2->next = next_free_l2;
        next_free_l2 = l2;
        l1_64[x1] = l2_64_dummy;
      }
    }
  }

  void set_tlb_physpage(uint64_t addr, TcPhyspage *ppp) {
    const uint32_t mask1 = (1 << dyntrans_l1n<TcPhyspage>()) - 1;
    const uint32_t mask2 = (1 << dyntrans_l2n<TcPhyspage>()) - 1;
    const uint32_t mask3 = (1 << dyntrans_l3n<TcPhyspage>()) - 1;
    uint32_t x1, x2, x3;
    L2Table *l2;
    L3Table *l3;

    x1 = (addr >> (64-dyntrans_l1n<TcPhyspage>())) & mask1;
    x2 = (addr >> (64-dyntrans_l1n<TcPhyspage>()-dyntrans_l2n<TcPhyspage>())) & mask2;
    x3 = (addr >> (64-dyntrans_l1n<TcPhyspage>()-dyntrans_l2n<TcPhyspage>()-dyntrans_l3n<TcPhyspage>())) & mask3;
    /*  fatal("X3: addr=%016" PRIx64" x1=%x x2=%x x3=%x\n",
        (uint64_t)addr, (int)x1, (int)x2, (int)x3);  */
    l2 = l1_64[x1];
    /*  fatal("  l2 = %p\n", l2);  */
    l3 = l2->l3[x2];
    l3->phys_page[x3] = ppp;
  }

  void clear_phys(uint64_t vaddr_page) {
    const uint32_t mask1 = (1 << dyntrans_l1n<TcPhyspage>()) - 1;
    const uint32_t mask2 = (1 << dyntrans_l2n<TcPhyspage>()) - 1;
    const uint32_t mask3 = (1 << dyntrans_l3n<TcPhyspage>()) - 1;
    uint32_t x1, x2, x3;
    L2Table *l2;
    L3Table *l3;

    x1 = (vaddr_page >> (64-dyntrans_l1n<TcPhyspage>())) & mask1;
    x2 = (vaddr_page >> (64-dyntrans_l1n<TcPhyspage>() -
                         dyntrans_l2n<TcPhyspage>())) & mask2;
    x3 = (vaddr_page >> (64-dyntrans_l1n<TcPhyspage>() -
                         dyntrans_l2n<TcPhyspage>() - dyntrans_l3n<TcPhyspage>())) & mask3;
    l2 = l1_64[x1];
    l3 = l2->l3[x2];
    l3->phys_page[x3] = NULL;
  }

  void init_tables() {
    auto dummy_l2 = (L2Table *) calloc(1, sizeof(L2Table));
    auto dummy_l3 = (L3Table *) calloc(1, sizeof(L3Table));

    l2_64_dummy = dummy_l2;
    l3_64_dummy = dummy_l3;

    for (auto x1 = 0; x1 < (1 << dyntrans_l1n<TcPhyspage>()); x1 ++)
      l1_64[x1] = dummy_l2;

    for (auto x2 = 0; x2 < (1 << dyntrans_l2n<TcPhyspage>()); x2 ++)
      dummy_l2->l3[x2] = dummy_l3;
  }

  void update_make_valid_translation
  (Cpu *cpu, uint64_t vaddr_page, uint64_t paddr_page, uint8_t *host_page, int writeflag, bool instr)
  {
    int found, r, useraccess = 0;

    const uint32_t mask1 = (1 << dyntrans_l1n<TcPhyspage>()) - 1;
    const uint32_t mask2 = (1 << dyntrans_l2n<TcPhyspage>()) - 1;
    const uint32_t mask3 = (1 << dyntrans_l3n<TcPhyspage>()) - 1;
    uint32_t x1, x2, x3;
    L2Table *l2;
    L3Table *l3;

    /*  fatal("update_translation_table(): v=0x%016" PRIx64", h=%p w=%i"
        " p=0x%016" PRIx64"\n", (uint64_t)vaddr_page, host_page, writeflag,
        (uint64_t)paddr_page);  */

    assert((vaddr_page & (pagesize<TcPhyspage>()-1)) == 0);
    assert((paddr_page & (pagesize<TcPhyspage>()-1)) == 0);

    if (writeflag & MEMORY_USER_ACCESS) {
      writeflag &= ~MEMORY_USER_ACCESS;
      useraccess = 1;
    }

    useraccess = useraccess;  // shut up compiler warning about unused var

    /*  Scan the current TLB entries:  */

    x1 = (vaddr_page >> (64-dyntrans_l1n<TcPhyspage>())) & mask1;
    x2 = (vaddr_page >> (64-dyntrans_l1n<TcPhyspage>()-dyntrans_l2n<TcPhyspage>())) & mask2;
    x3 = (vaddr_page >> (64-dyntrans_l1n<TcPhyspage>()-dyntrans_l2n<TcPhyspage>()-dyntrans_l3n<TcPhyspage>()))
	    & mask3;

    l2 = l1_64[x1];
    if (l2 == l2_64_dummy)
      found = -1;
    else {
      l3 = l2->l3[x2];
      if (l3 == l3_64_dummy)
        found = -1;
      else
        found = (int)l3->vaddr_to_tlbindex[x3] - 1;
    }

    if (found < 0) {
      /*  Create the new TLB entry, overwriting a "random" entry:  */
      static unsigned int x = 0;
      r = (x++) % max_vph_tlb_entries<TcPhyspage>();

      if (vph_tlb_entry[r].valid) {
        /*  This one has to be invalidated first:  */
        invalidate_tlb_entry(vph_tlb_entry[r].vaddr_page, 0);
      }

      vph_tlb_entry[r].valid = 1;
      vph_tlb_entry[r].host_page = host_page;
      vph_tlb_entry[r].paddr_page = paddr_page;
      vph_tlb_entry[r].vaddr_page = vaddr_page;
      vph_tlb_entry[r].writeflag =
		    writeflag & MEM_WRITE;

      /*  Add the new translation to the table:  */
      l2 = l1_64[x1];
      if (l2 == l2_64_dummy) {
        if (next_free_l2 != NULL) {
          l2 = l1_64[x1] =
				    next_free_l2;
          next_free_l2 = l2->next;
        } else {
          int i;
          l2 = l1_64[x1] = (L2Table *) malloc(sizeof(L2Table));
          l2->refcount = 0;
          for (i=0; i<(1 << dyntrans_l2n<TcPhyspage>()); i++)
            l2->l3[i] = l3_64_dummy;
        }
        if (l2->refcount != 0) {
          fprintf(stderr, "Huh? l2 Refcount problem.\n");
          exit(1);
        }
      }
      if (l2 == l2_64_dummy) {
        fprintf(stderr, "INTERNAL ERROR L2 reuse\n");
        exit(1);
      }
      l3 = l2->l3[x2];
      if (l3 == l3_64_dummy) {
        if (next_free_l3 != NULL) {
          l3 = l2->l3[x2] =
				    next_free_l3;
          next_free_l3 = l3->next;
        } else {
          l3 = l2->l3[x2] = (L3Table *)calloc(1, sizeof(L3Table));
        }
        if (l3->refcount != 0) {
          fprintf(stderr, "Huh? l3 Refcount problem.\n");
          exit(1);
        }
        l2->refcount ++;
      }
      if (l3 == l3_64_dummy) {
        fprintf(stderr, "INTERNAL ERROR L3 reuse\n");
        exit(1);
      }

      l3->host_load[x3] = host_page;
      l3->host_store[x3] = writeflag? host_page : NULL;
      l3->phys_addr[x3] = paddr_page;
      l3->phys_page[x3] = NULL;
      l3->vaddr_to_tlbindex[x3] = r + 1;
      l3->refcount ++;
    } else {
      /*
       *  The translation was already in the TLB.
       *	Writeflag = 0:  Do nothing.
       *	Writeflag = 1:  Make sure the page is writable.
       *	Writeflag = MEM_DOWNGRADE: Downgrade to readonly.
       */
      r = found;
      if (writeflag & MEM_WRITE)
        vph_tlb_entry[r].writeflag = 1;
      if (writeflag & MEM_DOWNGRADE)
        vph_tlb_entry[r].writeflag = 0;
      x1 = (vaddr_page >> (64-dyntrans_l1n<TcPhyspage>())) & mask1;
      x2 = (vaddr_page >> (64-dyntrans_l1n<TcPhyspage>()-dyntrans_l2n<TcPhyspage>())) & mask2;
      x3 = (vaddr_page >> (64-dyntrans_l1n<TcPhyspage>()-dyntrans_l2n<TcPhyspage>()-dyntrans_l3n<TcPhyspage>()))
		    & mask3;
      l2 = l1_64[x1];
      l3 = l2->l3[x2];
      if (l3->phys_addr[x3] == paddr_page) {
        if (writeflag & MEM_WRITE)
          l3->host_store[x3] = host_page;
        if (writeflag & MEM_DOWNGRADE)
          l3->host_store[x3] = NULL;
      } else {
        /*  Change the entire physical/host mapping:  */
        l3->host_load[x3] = host_page;
        l3->host_store[x3] = writeflag? host_page : NULL;
        l3->phys_addr[x3] = paddr_page;
      }

      /*  HM!  /2013-11-17  */
      /*  Should this be here?  2014-08-02  */
      //l3->phys_page[x3] = NULL;
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
          clear_phys(vaddr_page);
        }
      }
    }
  }

  void invalidate_tc(Cpu *cpu, uint64_t addr, int flags)
  {
    int r;
    uint64_t addr_page = addr & ~(pagesize<TcPhyspage>() - 1);

    /*  fatal("invalidate(): ");  */

    /*  Quick case for _one_ virtual addresses: see note above.  */
    if (flags & INVALIDATE_VADDR) {
      /*  fatal("vaddr 0x%08x\n", (int)addr_page);  */
      invalidate_tlb_entry(addr_page, flags);
      return;
    }

    /*  Invalidate everything:  */
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

    if (!(flags & INVALIDATE_PADDR))
      fprintf(stderr, "HUH? Invalidate: Not vaddr, all, or paddr?\n");

    /*  fatal("addr 0x%08x\n", (int)addr_page);  */

    for (r=0; r<max_vph_tlb_entries<TcPhyspage>(); r++) {
      if (vph_tlb_entry[r].valid && addr_page
          == vph_tlb_entry[r].paddr_page) {
        invalidate_tlb_entry(vph_tlb_entry[r].vaddr_page, flags);
        if (flags & JUST_MARK_AS_NON_WRITABLE) {
          vph_tlb_entry[r].writeflag = 0;
        } else {
          vph_tlb_entry[r].valid = 0;
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

    auto host_pages = get_cached_tlb_pages(cpu, cached_pc);

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

        host_pages = get_cached_tlb_pages(cpu, cached_pc);

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
      set_tlb_physpage(cached_pc, ppp);
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
    uint64_t cached_pc = cpu->pc;

    auto host_pages = get_cached_tlb_pages(cpu, cached_pc);

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
    next_ic ++;
    return nothing_call;
  }

  void set_next_ic(uint64_t pc) {
    next_ic = get_ic_page() + pc_to_ic_entry<TcPhyspage>(pc);
  }

  uint64_t sync_low_pc(Cpu *cpu, decltype(&((TcPhyspage*)0)->ics[0]) ic) {
    return ((size_t)ic - (size_t)get_ic_page()) / sizeof(*ic);
  }
};

#endif//TLB_CACHE64_H
