#ifndef TLB_CACHE_H
#define TLB_CACHE_H

#include <stdio.h>
#include <assert.h>
#include <map>

/*
 *  32-bit dyntrans emulated Virtual -> physical -> host address translation:
 *  -------------------------------------------------------------------------
 *
 *  This stuff assumes that 4 KB pages are used. 20 bits to select a page
 *  means just 1 M entries needed. This is small enough that a couple of
 *  full-size tables can fit in virtual memory on modern hosts (both 32-bit
 *  and 64-bit hosts). :-)
 *
 *  Usage: e.g. VPH32(arm,ARM)
 *           or VPH32(sparc,SPARC)
 *
 *  The vph_tlb_entry entries are cpu dependent tlb entries.
 *
 *  The host_load and host_store entries point to host pages; the phys_addr
 *  entries are uint32_t (emulated physical addresses).
 *
 *  phys_page points to translation cache physpages.
 *
 *  vaddr_to_tlbindex is a virtual address to tlb index hint table.
 *  The values in this array are the tlb index plus 1, so a value of, say,
 *  3 means tlb index 2. A value of 0 would mean a tlb index of -1, which
 *  is not a valid index. (I.e. no hit.)
 *
 *  The VPH32EXTENDED variant adds an additional postfix to the array
 *  names. Used so far only for usermode addresses in M88K emulation.
 */
#define	N_VPH32_ENTRIES		1048576

#define	JUST_MARK_AS_NON_WRITABLE	1
#define	INVALIDATE_ALL			2
#define	INVALIDATE_PADDR		4
#define	INVALIDATE_VADDR		8
#define	INVALIDATE_VADDR_UPPER4		16	/*  useful for PPC emulation  */
#define INVALIDATE_IDENTITY 32
#define INVALIDATE_INSTR    64

#define	N_BASE_TABLE_ENTRIES		65536

#define PHYSPAGE_CACHE_ALIGN 64

struct cpu;
extern void cpu_create_or_reset_tc(struct cpu *cpu);

struct host_load_store_t {
  uint64_t physaddr;
  void *ppp;
  uint8_t *host_load;
  uint8_t *host_store;
};

template <typename TcPhyspage> constexpr bool is_arm() { return false; }
template <> constexpr bool is_arm<struct arm_tc_physpage>() { return true; }

template <typename TcPhyspage> constexpr bool is_m88k() { return false; }
template <> constexpr bool is_m88k<struct m88k_tc_physpage>() { return true; }

template <typename TcPhyspage> constexpr bool is_mips() { return false; }
template <> constexpr bool is_mips<struct mips_tc_physpage>() { return true; }

template <typename TcPhyspage> constexpr int pagesize() { return 1 << 12; }
template <> constexpr int pagesize<struct alpha_tc_physpage>() { return 1 << 13; }

template <typename TcPhyspage> constexpr int max_vph_tlb_entries() { return 128; }
template <> constexpr int max_vph_tlb_entries<struct arm_tc_physpage>() { return 384; }

template <typename TcPhyspage> uint64_t addr_to_pagenr(uint64_t addr) {
  return addr / pagesize<TcPhyspage>();
}

template <typename TcPhyspage> constexpr int ic_entries_per_page() { return 1024; }
template <typename TcPhyspage> constexpr int pc_to_ic_entry(uint64_t pc) {
  auto factor = pagesize<TcPhyspage>() / ic_entries_per_page<TcPhyspage>();
  uint64_t mask = pagesize<TcPhyspage>() - 1;
  uint64_t useful_address = pc & mask;
  return useful_address / factor;
}

template <typename TcPhyspage> int cpu_get_addr_space(struct cpu *cpu, bool instr) {
  return 0;
}
struct ppc_tc_physpage;
template <> int cpu_get_addr_space<ppc_tc_physpage>(struct cpu *cpu, bool instr);

/*
 *  This structure contains a list of ranges within an emulated
 *  physical page that contain translatable code.
 */
#define	PHYSPAGE_RANGES_ENTRIES_PER_LIST		20
struct physpage_ranges {
	uint32_t	next_ofs;	/*  0 for end of chain  */
	uint32_t	n_entries_used;
	uint16_t	base[PHYSPAGE_RANGES_ENTRIES_PER_LIST];
	uint16_t	length[PHYSPAGE_RANGES_ENTRIES_PER_LIST];
	uint16_t	count[PHYSPAGE_RANGES_ENTRIES_PER_LIST];
};

#define MAX_CACHE_SIZE (5 * 1024 * 1024)
#define SMALL_ENTRIES 128

template <typename TcPhyspage, typename VaddrToTlb, typename VpgTlbEntry, typename Cpu> struct tlb_impl {
private:
  host_load_store_t *pages;
  std::map<uint64_t, VaddrToTlb> *vaddr_to_tlbindex;

  // Pointers to other cpu members (consider moving them here)
	VpgTlbEntry *vph_tlb_entry;

  int max_tlb_entries;

protected:
  host_load_store_t &get_host_page_ref(Cpu *cpu, uint64_t addr, bool instr) {
    auto index = get_page_index(cpu, addr, instr);
    return pages[index];
  }

  VpgTlbEntry &get_tlb_entry(int idx) {
    return vph_tlb_entry[idx];
  }

  int clear_cache(int index) {
    assert(index < 2 * N_VPH32_ENTRIES);
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
  }

  void invalidate_tlb_entry(Cpu *cpu, uint64_t vaddr_page, int flags)
  {
    auto index = get_page_index(cpu, vaddr_page, false);

    if (flags & JUST_MARK_AS_NON_WRITABLE) {
      /*  printf("JUST MARKING NON-W: vaddr 0x%08x\n",
          (int)vaddr_page);  */
      clear_writable(cpu, vaddr_page);
    } else {
      index %= N_VPH32_ENTRIES;
      for (auto i = 0; i < 2; i++) {
        auto real_index = i * N_VPH32_ENTRIES + index;
        int tlbi = clear_cache(real_index);
        if (tlbi > 0) {
          decomission_vph(cpu, tlbi - 1);
        }
      }
    }
  }

  // public:
  int max_entries() const { return max_tlb_entries; }

  int get_page_index(Cpu *cpu, uint64_t vaddr, bool instr) const {
    auto index = addr_to_pagenr<TcPhyspage>(vaddr & 0xffffffff);
    assert(index < N_VPH32_ENTRIES);
    index += N_VPH32_ENTRIES * !!cpu_get_addr_space<TcPhyspage>(cpu, instr);
    return index;
  }

  void clear_writable(Cpu *cpu, uint64_t addr) {
    auto index = get_page_index(cpu, addr, false);
    pages[index].host_store = nullptr;
  }

public:
  typedef Cpu cpu_t;
  typedef TcPhyspage physpage_t;

  void initialize() {
    max_tlb_entries = std::min(SMALL_ENTRIES, max_vph_tlb_entries<TcPhyspage>());
    vph_tlb_entry = (VpgTlbEntry*)calloc(max_vph_tlb_entries<TcPhyspage>(), sizeof(VpgTlbEntry));
    vaddr_to_tlbindex = new std::map<uint64_t, VaddrToTlb>();
    pages = (host_load_store_t *)calloc(2 * N_VPH32_ENTRIES, sizeof(host_load_store_t));
  }

  host_load_store_t get_cached_tlb_pages(Cpu *cpu, uint64_t addr, bool instr) {
    return get_host_page_ref(cpu, addr, instr);
  }

  void update_make_valid_translation
  (Cpu *cpu, uint64_t vaddr_page, uint64_t paddr_page, uint8_t *host_page, int writeflag, bool instr, uint32_t *is_userpage) {
    assert(vaddr_to_tlbindex);
    assert(pages);

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
  }
};

template <typename T> class itlb_impl : public T {
private:
  uint64_t cur_ic_virt;
  typename T::physpage_t *cur_physpage;
  typename T::physpage_t *physpage_template;
  decltype(&((typename T::physpage_t *)0)->ics[0]) next_ic;
  std::map<uint64_t, typename T::physpage_t> *physpage_map;

protected:
  /*
   *  XXX_tc_allocate_default_page():
   *
   *  Create a default page (with just pointers to instr(to_be_translated)
   *  at cpu->translation_cache_cur_ofs.
   */
  typename T::physpage_t *allocate_physpage(typename T::cpu_t *cpu, uint64_t physaddr)
  {
    auto found = physpage_map->insert(std::make_pair(physaddr, *physpage_template));
    return &found.first->second;
  }

  void clear_phys(typename T::cpu_t *cpu, uint64_t addr) {
    set_tlb_physpage(cpu, addr, nullptr);
  }

  void clear_physpage(typename T::physpage_t *ppp) {
    for (auto i = 0; i < ic_entries_per_page<typename T::physpage_t>(); i++) {
      ppp->ics[i].f = physpage_template->ics[0].f;
    }

    memset(&ppp->translations_bitmap, 0, sizeof(ppp->translations_bitmap));
    ppp->virtaddr = ~0ull;
  }

  void set_physpage(uint64_t virt, typename T::physpage_t *page) {
    if (page->virtaddr != virt) {
      clear_physpage(page);
    }
    page->virtaddr = virt;
    this->cur_ic_virt = virt;
    this->cur_physpage = page;
  }

public:
  void instr_initialize() {
    physpage_map = new std::map<uint64_t, typename T::physpage_t>();
  }

  void set_tlb_physpage(typename T::cpu_t *cpu, uint64_t addr, typename T::physpage_t *ppp) {
    this->get_host_page_ref(cpu, addr, true).ppp = ppp;
  }

  decltype(&((typename T::physpage_t *)0)->ics[0]) get_ic_page() const {
    return &this->cur_physpage->ics[0];
  }

  uint64_t sync_low_pc(typename T::cpu_t *cpu, decltype(&((typename T::physpage_t*)0)->ics[0]) ic) {
    return ((size_t)ic - (size_t)get_ic_page()) / sizeof(*ic);
  }

  uint64_t get_ic_phys() const { return this->cur_physpage->physaddr; }

  void nothing() {
    next_ic --;
  }

  void bump_ic() {
    next_ic ++;
  }

  void do_nothing(decltype(&((typename T::physpage_t*)0)->ics[0]) nothing_call) {
    next_ic = nothing_call;
  }

  decltype(&((typename T::physpage_t*)0)->ics[0]) bad_translation(decltype(&((typename T::physpage_t*)0)->ics[0]) nothing_call) {
    do_nothing(nothing_call);
    return next_ic ++;
  }

  void set_physpage_template(typename T::physpage_t *templ) {
    physpage_template = templ;
  }

  typename T::physpage_t *get_physpage() const {
    return this->cur_physpage;
  }

  uint64_t get_low_pc() const {
    return this->next_ic - &this->cur_physpage->ics[0];
  }

  decltype(&((typename T::physpage_t *)0)->ics[0]) get_next_ic() const {
    return this->next_ic;
  }

  void pc_to_pointers_generic(typename T::cpu_t *cpu) {
    uint32_t cached_pc = cpu->pc;
    int ok;
    typename T::physpage_t *ppp;

    auto host_pages = this->get_cached_tlb_pages(cpu, cached_pc, true);

    /*  Virtual to physical address translation:  */
    ok = 0;
    if (host_pages.host_load != nullptr) {
      ok = 1;
    }

    if (!ok) {
      uint64_t paddr;
      if (cpu->translate_v2p != NULL) {
        uint64_t vaddr = is_mips<typename T::physpage_t>() ? (int32_t)cached_pc : cached_pc;
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

        host_pages = this->get_cached_tlb_pages(cpu, cached_pc, true);

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

    host_pages.physaddr &= ~(pagesize<typename T::physpage_t>() - 1);

    if (host_pages.host_load == nullptr) {
      int q = pagesize<typename T::physpage_t>() - 1;
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

    this->set_physpage(cached_pc & ~0xfff, ppp);

    set_next_ic(cached_pc);
  }

  void move_to_physpage(typename T::cpu_t *cpu) {
    uint32_t cached_pc = cpu->pc;

    auto host_pages = this->get_cached_tlb_pages(cpu, cached_pc, true);

    if (host_pages.ppp == nullptr) {
      pc_to_pointers_generic(cpu);
      return;
    }

    auto ppp = static_cast<typename T::physpage_t*>(host_pages.ppp);
    this->set_physpage(cpu->pc & ~(pagesize<typename T::physpage_t>() - 1), ppp);
    next_ic = get_ic_page() + pc_to_ic_entry<typename T::physpage_t>(cached_pc);
  }

  void set_next_ic(uint64_t pc) {
    next_ic = get_ic_page() + pc_to_ic_entry<typename T::physpage_t>(pc);
  }

  decltype(&((typename T::physpage_t *)0)->ics[0]) next_insn() {
    return next_ic++;
  }

  void invalidate_tc_code(typename T::cpu_t *cpu, uint64_t addr, int flags) {
    int r;
    uint32_t vaddr_page, paddr_page;

    addr &= ~(pagesize<typename T::physpage_t>()-1);

    /*  printf("DYNTRANS_INVALIDATE_TC_CODE addr=0x%08x flags=%i\n",
        (int)addr, flags);  */

    if (flags & INVALIDATE_PADDR) {
      typename T::physpage_t *ppp;

      auto found = physpage_map->find(addr);
      if (found == physpage_map->end()) {
        return;
      }

      ppp = &found->second;

      if (ppp != nullptr && !ppp->translations_bitmap.empty()) {
        clear_physpage(ppp);
      }
    }

    /*  Invalidate entries in the VPH table:  */
    for (r = 0; r < this->max_entries(); r ++) {
      auto tlb_entry = this->get_tlb_entry(r);
      if (tlb_entry.valid) {
        vaddr_page = tlb_entry.vaddr_page & ~(pagesize<typename T::physpage_t>()-1);
        paddr_page = tlb_entry.paddr_page & ~(pagesize<typename T::physpage_t>()-1);

        if (flags & INVALIDATE_ALL ||
            (flags & INVALIDATE_PADDR && paddr_page == addr) ||
            (flags & INVALIDATE_VADDR && vaddr_page == addr)) {
          this->clear_phys(cpu, vaddr_page);
        }
      }
    }
  }
};

#endif//TLB_CACHE_H
