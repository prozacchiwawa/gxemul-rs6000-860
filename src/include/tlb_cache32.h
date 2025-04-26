#ifndef TLB_CACHE32_H
#define TLB_CACHE32_H

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "tlb_cache.h"

extern size_t dyntrans_cache_size;
extern unsigned char *memory_paddr_to_hostaddr
(struct memory *mem, uint64_t paddr, int writeflag);

template <typename TcPhyspage, typename VaddrToTlb, typename VpgTlbEntry, typename Cpu> struct vph32 {
private:
  bool initialized;
  tlb_impl<TcPhyspage, VaddrToTlb, VpgTlbEntry, Cpu> itlb;
  tlb_impl<TcPhyspage, VaddrToTlb, VpgTlbEntry, Cpu> dtlb;

  uint32_t *is_userpage;

public:
  void initialize() {
    itlb.initialize();
    dtlb.initialize();
  }
  void set_physpage_template(TcPhyspage *templ) {
    itlb.set_physpage_template(templ);
  }

  TcPhyspage *get_physpage() const {
    return itlb.get_physpage();
  }

  uint64_t get_low_pc() const {
    return itlb.get_low_pc();
  }

  decltype(&((TcPhyspage *)0)->ics[0]) get_next_ic() const {
    return itlb.get_next_ic();
  }

  void pc_to_pointers_generic(Cpu *cpu) {
    itlb.pc_to_pointers_generic(cpu);
  }

  void move_to_physpage(Cpu *cpu) {
    itlb.move_to_physpage(cpu);
  }

  void set_next_ic(uint64_t pc) {
    itlb.set_next_ic(pc);
  }

  decltype(&((TcPhyspage *)0)->ics[0]) next_insn() {
    return itlb.next_insn();
  }

  void set_tlb_physpage(Cpu *cpu, uint64_t addr, TcPhyspage *ppp) {
    itlb.set_tlb_physpage(cpu, addr, ppp);
  }

  host_load_store_t get_cached_tlb_pages(Cpu *cpu, uint64_t addr, bool instr) {
    if (instr) {
      return itlb.get_cached_tlb_pages(cpu, addr, instr);
    } else {
      return dtlb.get_cached_tlb_pages(cpu, addr, instr);
    }
  }

  decltype(&((TcPhyspage *)0)->ics[0]) get_ic_page() const {
    return itlb.get_ic_page();
  }

  uint64_t sync_low_pc(Cpu *cpu, decltype(&((TcPhyspage*)0)->ics[0]) ic) {
    return itlb.sync_low_pc(cpu, ic);
  }

  uint64_t get_ic_phys() const {
    return itlb.get_ic_phys();
  }

  void add_tlb_data(uint32_t *new_is_userpage) {
    this->is_userpage = new_is_userpage;
  }

  void nothing() {
    itlb.nothing();
  }

  void bump_ic() {
    itlb.bump_ic();
  }

  void do_nothing(decltype(&((TcPhyspage*)0)->ics[0]) nothing_call) {
    itlb.do_nothing(nothing_call);
  }

  decltype(&((TcPhyspage*)0)->ics[0]) bad_translation(decltype(&((TcPhyspage*)0)->ics[0]) nothing_call) {
    return itlb.bad_translation(nothing_call);
  }

  void update_make_valid_translation
  (Cpu *cpu,
   uint64_t vaddr_page,
   uint64_t paddr_page,
   uint8_t *host_page,
   int writeflag,
   bool instr
   ) {
    if (instr) {
      itlb.update_make_valid_translation
        (cpu, vaddr_page, paddr_page, host_page, writeflag, instr, is_userpage);
    } else {
      dtlb.update_make_valid_translation
        (cpu, vaddr_page, paddr_page, host_page, writeflag, instr, is_userpage);
    }
  }

  void invalidate_tc(Cpu *cpu, uint64_t addr, int flags) {
    if (is_arm<TcPhyspage>() && (flags & INVALIDATE_VADDR)) {
      auto index = addr_to_pagenr<TcPhyspage>(addr & 0xffffffff);
      is_userpage[index >> 5] &= ~(1 << (index & 31));
    }

    itlb.invalidate_tc(cpu, addr, flags);
    dtlb.invalidate_tc(cpu, addr, flags);
  }


  void invalidate_tc_code(Cpu *cpu, uint64_t addr, int flags) {
    itlb.invalidate_tc_code(cpu, addr, flags);
  }
};

#endif//TLB_CACHE32_H
