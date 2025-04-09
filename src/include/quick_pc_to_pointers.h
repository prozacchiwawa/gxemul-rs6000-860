#ifdef quick_pc_to_pointers
#undef quick_pc_to_pointers
#endif

#ifdef MODE32
#ifdef DYNTRANS_PPC
#define	quick_pc_to_pointers(cpu) {             \
		DYNTRANS_PC_TO_POINTERS(cpu);               \
    }
#else
#define	quick_pc_to_pointers(cpu) {                                     \
    uint32_t pc_tmp32 = cpu->pc;                                        \
    auto host_page = cpu->cd.DYNTRANS_ARCH.vph32.get_cached_tlb_pages(pc_tmp32); \
    struct DYNTRANS_TC_PHYSPAGE *ppp_tmp = (struct DYNTRANS_TC_PHYSPAGE *)host_page.ppp; \
    if (ppp_tmp != NULL) {                                              \
      cpu->cd.DYNTRANS_ARCH.set_physpage(pc_tmp32 & ~0xfff, ppp_tmp);   \
      cpu->cd.DYNTRANS_ARCH.next_ic =                                   \
		    cpu->cd.DYNTRANS_ARCH.get_ic_page() +                           \
		    DYNTRANS_PC_TO_IC_ENTRY(pc_tmp32);                              \
    } else                                                              \
      DYNTRANS_PC_TO_POINTERS(cpu);                                     \
  }
#endif

#ifndef quick_pc_to_pointers_arm
#define	quick_pc_to_pointers_arm(cpu) {					\
	if (cpu->cd.arm.cpsr & ARM_FLAG_T) {				\
		cpu->cd.arm.next_ic = &nothing_call;			\
	} else								\
		quick_pc_to_pointers(cpu);				\
}
#endif

#else
#define quick_pc_to_pointers(cpu)	DYNTRANS_PC_TO_POINTERS(cpu)
#endif


