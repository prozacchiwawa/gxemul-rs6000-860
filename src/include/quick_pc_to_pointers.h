#ifndef QUICK_PC_TO_POINTERS_H
#define QUICK_PC_TO_POINTERS_H

#define	quick_pc_to_pointers32(cpu) cpu->cd.DYNTRANS_ARCH.vph32.move_to_physpage(cpu)
#define	quick_pc_to_pointers64(cpu) cpu->cd.DYNTRANS_ARCH.vph64.move_to_physpage(cpu)

#define	quick_pc_to_pointers_arm(cpu) {					\
	if (cpu->cd.arm.cpsr & ARM_FLAG_T) {				\
    cpu->cd.arm.vph32.do_nothing(&nothing_call);  \
	} else								\
		quick_pc_to_pointers32(cpu);				\
}

#endif//QUICK_PC_TO_POINTERS_H
