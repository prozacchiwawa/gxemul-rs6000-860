/*
 *  Copyright (C) 2005-2014  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *
 *
 *  Common dyntrans routines. Included from cpu_*.c.
 *
 *  Note: This might be a bit hard to follow, if you are reading this source
 *  code for the first time. It is basically a hack to implement "templates"
 *  with normal C code, by using suitable defines/macros, and then including
 *  this file.
 */

void ppc_instr_dump_registers(struct cpu *cpu, struct ppc_instr_call *ic);

#ifndef STATIC_STUFF
#define	STATIC_STUFF
/*
 *  gather_statistics():
 */
static void gather_statistics(struct cpu *cpu)
{
// 	char ch, buf[60];
// 	struct DYNTRANS_IC *ic = cpu->cd.DYNTRANS_ARCH.next_ic;
// 	int i = 0;
// 	uint64_t a;
// 	int low_pc = get_low_pc(cpu->cd.DYNTRANS_ARCH);

// 	if (cpu->machine->statistics.file == NULL) {
// 		fatal("statistics gathering with no filename set is"
// 		    " meaningless\n");
// 		return;
// 	}

// 	/*  low_pc must be within the page!  */
// 	if (low_pc < 0 || low_pc > DYNTRANS_IC_ENTRIES_PER_PAGE)
// 		return;

// 	buf[0] = '\0';

// 	while ((ch = cpu->machine->statistics.fields[i]) != '\0') {
// 		if (i != 0)
// 			strlcat(buf, " ", sizeof(buf));

// 		switch (ch) {
// 		case 'i':
// 			snprintf(buf + strlen(buf), sizeof(buf),
// 			    "%p", (void *)ic->f);
// 			break;
// 		case 'p':
// 			/*  Physical program counter address:  */
// #ifdef MODE32
// 			a = cpu->cd.DYNTRANS_ARCH.vph32.get_physpage()->physaddr;
// #else
// 			a = cpu->cd.DYNTRANS_ARCH.vph64.get_physpage()->physaddr;
// #endif
// 			a &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) <<
// 			    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
// 			a += low_pc << DYNTRANS_INSTR_ALIGNMENT_SHIFT;
// 			if (cpu->is_32bit)
// 				snprintf(buf + strlen(buf), sizeof(buf),
// 				    "0x%08" PRIx32, (uint32_t)a);
// 			else
// 				snprintf(buf + strlen(buf), sizeof(buf),
// 				    "0x%016" PRIx64, (uint64_t)a);
// 			break;
// 		case 'v':
// 			/*  Virtual program counter address:  */
// 			a = cpu->pc;
// 			a &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) <<
// 			    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
// 			a += low_pc << DYNTRANS_INSTR_ALIGNMENT_SHIFT;
// 			if (cpu->is_32bit)
// 				snprintf(buf + strlen(buf), sizeof(buf),
// 				    "0x%08" PRIx32, (uint32_t)a);
// 			else
// 				snprintf(buf + strlen(buf), sizeof(buf),
// 				    "0x%016" PRIx64, (uint64_t)a);
// 			break;
// 		}
// 		i++;
// 	}

// 	fprintf(cpu->machine->statistics.file, "%s\n", buf);
}


#define S		gather_statistics(cpu)


#if 1

#ifdef DYNTRANS_PPC
/*  The normal instruction execution core:  */
#define I	{                                                             \
    cpu->cd.ppc.icount++;                                               \
    ic = cpu->cd.ppc.VPH.next_insn();                                   \
    if (ppc_recording) {                                                \
      auto ptr = &ppc_recording[ppc_recording_offset];                  \
      memcpy(&ptr->iword, ic->instr, sizeof(uint32_t));                 \
      memcpy(ptr->gpr, cpu->cd.ppc.gpr, sizeof(cpu->cd.ppc.gpr));       \
      memcpy(ptr->sr, cpu->cd.ppc.sr, sizeof(cpu->cd.ppc.sr));          \
      ptr->pc = ic->pc;                                                 \
      ptr->index = cpu->ninstrs;                                        \
      ptr->msr = cpu->cd.ppc.msr;                                       \
      ptr->cr = cpu->cd.ppc.cr;                                         \
      ptr->lr = cpu->cd.ppc.spr[SPR_LR];                                \
      ptr->ctr = cpu->cd.ppc.spr[SPR_CTR];                              \
      ptr->dec = cpu->cd.ppc.spr[SPR_DEC];                              \
      ptr->dar = cpu->cd.ppc.spr[SPR_DAR];                              \
      ptr->sdr1 = cpu->cd.ppc.spr[SPR_SDR1];                            \
      ptr->dsisr = cpu->cd.ppc.spr[SPR_DSISR];                          \
      ptr->srr[0] = cpu->cd.ppc.spr[SPR_SRR0];                          \
      ptr->srr[1] = cpu->cd.ppc.spr[SPR_SRR1];                          \
      memcpy(ptr->sprg, &cpu->cd.ppc.spr[SPR_SPRG0], sizeof(uint64_t) * 4); \
      ppc_recording_offset = (ppc_recording_offset + 1) & (PPC_RECORDING_LENGTH - 1); \
    }                                                                   \
    ic->f(cpu, ic);                                                     \
  }
#else

/*  The normal instruction execution core:  */
#ifdef MODE32
#define I	do {                                  \
  ic = cpu->cd.DYNTRANS_ARCH.vph32.next_insn(); \
  ic->f(cpu, ic);                               \
  } while (0)
#else
#define I	do {                                  \
  ic = cpu->cd.DYNTRANS_ARCH.vph64.next_insn(); \
  ic->f(cpu, ic);                               \
  } while (0)
#endif
#endif

#else

/*  For heavy debugging:  */
#define I	next_insn(ic, cpu->cd.DYNTRANS_ARCH);           \
  {                                                       \
    int low_pc = get_low_pc(cpu->cd.DYNTRANS_ARCH);       \
    printf("cur_ic_page=%p ic=%p (low_pc=0x%x)\n",        \
           cpu->cd.DYNTRANS_ARCH.get_ic_page(),           \
           ic, low_pc << DYNTRANS_INSTR_ALIGNMENT_SHIFT); \
  }                                                       \
  ic->f(cpu, ic);

#endif

/*  static long long nr_of_I_calls = 0;  */

/*  Temporary hack for finding NULL bugs:  */
/*  #define I	ic = cpu->cd.DYNTRANS_ARCH.next_ic ++; 			\
		nr_of_I_calls ++;					\
		if (ic->f == NULL) {					\
			int low_pc = ((size_t)cpu->cd.DYNTRANS_ARCH.next_ic - \
			    (size_t)cpu->cd.DYNTRANS_ARCH.get_ic_page()) / \
			    sizeof(struct DYNTRANS_IC);			\
			cpu->pc &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) << \
			    DYNTRANS_INSTR_ALIGNMENT_SHIFT);		\
			cpu->pc += (low_pc << DYNTRANS_INSTR_ALIGNMENT_SHIFT);\
			printf("Crash at %016" PRIx64"\n", cpu->pc);	\
			printf("nr of I calls: %lli\n", nr_of_I_calls);	\
			printf("Next ic = %p\n", cpu->cd.		\
				DYNTRANS_ARCH.next_ic);			\
			printf("cur ic page = %p\n", cpu->cd.		\
				DYNTRANS_ARCH.get_ic_page());		\
			cpu->running = 0;				\
			return 0;					\
		}							\
		ic->f(cpu, ic);  */

/*  Temporary hack for MIPS, to hunt for 32-bit/64-bit sign-extension bugs:  */
/*  #define I		{ int k; for (k=1; k<=31; k++)	\
	cpu->cd.mips.gpr[k] = (int32_t)cpu->cd.mips.gpr[k];\
	if (cpu->cd.mips.gpr[0] != 0) {			\
		fatal("NOOOOOO\n"); exit(1);		\
	}						\
	ic = cpu->cd.DYNTRANS_ARCH.next_ic ++; ic->f(cpu, ic); }
*/

#ifdef CPU_BITS_32
struct host_load_store_t CPU32(get_cached_tlb_pages)(struct cpu *cpu, uint64_t addr, bool instr) {
  return cpu->cd.DYNTRANS_ARCH.vph32.get_cached_tlb_pages(cpu, addr, instr);
}

void CPU32(set_tlb_physpage)(struct cpu *cpu, uint64_t addr, struct DYNTRANS_TC_PHYSPAGE *ppp) {
  cpu->cd.DYNTRANS_ARCH.vph32.set_tlb_physpage(cpu, addr, ppp);
}
#endif

#ifdef CPU_BITS_64
struct host_load_store_t CPU64(get_cached_tlb_pages)(struct cpu *cpu, uint64_t addr, bool instr) {
  cpu->cd.DYNTRANS_ARCH.vph64.get_cached_tlb_pages(cpu, addr);
}

void CPU64(set_tlb_physpage)(struct cpu *cpu, uint64_t addr, struct DYNTRANS_TC_PHYSPAGE *ppp) {
  cpu->cd.DYNTRANS_ARCH.vph64.set_tlb_physpage(addr, ppp);
}
#endif

static inline int sync_pc(struct cpu *cpu, struct DYNTRANS_IC *ic) {
  auto low_pc = cpu->cd.DYNTRANS_ARCH.VPH.sync_low_pc(cpu, ic);
  uint64_t pc_low_mask =
    (cpu_traits<decltype(cpu->cd.DYNTRANS_ARCH)>::ic_entries_per_page() <<
     cpu_traits<decltype(cpu->cd.DYNTRANS_ARCH)>::instr_alignment_shift()) - 1;
  cpu->pc =
    (cpu->pc & ~pc_low_mask) +
    (low_pc << cpu_traits<decltype(cpu->cd.DYNTRANS_ARCH)>::instr_alignment_shift());
  return low_pc;
}
#endif	/*  STATIC STUFF  */


#ifdef	DYNTRANS_RUN_INSTR_DEF
/*
 *  XXX_run_instr():
 *
 *  Execute one or more instructions on a specific CPU, using dyntrans.
 *  (For dualmode archs, this function is included twice.)
 *
 *  Return value is the number of instructions executed during this call,
 *  0 if no instructions were executed.
 */
int DYNTRANS_RUN_INSTR_DEF(struct cpu *cpu)
{
	MODE_uint_t cached_pc;
	int low_pc, n_instrs;

	/*  Ugly... fix this some day.  */
#ifdef DYNTRANS_DUALMODE_32
#ifdef MODE32
	DYNTRANS_PC_TO_POINTERS32(cpu);
#else
	DYNTRANS_PC_TO_POINTERS(cpu);
#endif
#else
	DYNTRANS_PC_TO_POINTERS(cpu);
#endif

	/*
	 *  Interrupt assertion?  (This is _below_ the initial PC to pointer
	 *  conversion; if the conversion caused an exception of some kind
	 *  then interrupts are probably disabled, and the exception will get
	 *  priority over device interrupts.)
	 *
	 *  TODO: Turn this into a family-specific function somewhere...
 	 */

	/*  Note: Do not cause interrupts while single-stepping. It is
	    so horribly annoying.  */
	if (!single_step) {
#ifdef DYNTRANS_ARM
		if (cpu->cd.arm.irq_asserted && !(cpu->cd.arm.cpsr & ARM_FLAG_I))
			arm_exception(cpu, ARM_EXCEPTION_IRQ);
#endif
#ifdef DYNTRANS_M88K
		if (cpu->cd.m88k.irq_asserted &&
		    !(cpu->cd.m88k.cr[M88K_CR_PSR] & M88K_PSR_IND))
			m88k_exception(cpu, M88K_EXCEPTION_INTERRUPT, 0);
#endif
#ifdef DYNTRANS_MIPS
		int enabled, mask;
		int status = cpu->cd.mips.coproc[0]->reg[COP0_STATUS];
		if (cpu->cd.mips.cpu_type.exc_model == EXC3K) {
			/*  R3000:  */
			enabled = status & MIPS_SR_INT_IE;
		} else {
			/*  R4000 and others:  */
			enabled = (status & STATUS_IE)
			    && !(status & STATUS_EXL) && !(status & STATUS_ERL);
			/*  Special case for R5900/C790/TX79:  */
			if (cpu->cd.mips.cpu_type.rev == MIPS_R5900 &&
			    !(status & R5900_STATUS_EIE))
				enabled = 0;
		}
		mask = status & cpu->cd.mips.coproc[0]->reg[COP0_CAUSE]
		    & STATUS_IM_MASK;

		if (enabled && mask)
			mips_cpu_exception(cpu, EXCEPTION_INT, 0, 0, 0, 0, 0,0);
#endif
#ifdef DYNTRANS_PPC
		if (cpu->cd.ppc.dec_intr_pending && (cpu->cd.ppc.msr & PPC_MSR_EE)) {
			cpu->cd.ppc.dec_intr_pending = 0;
			if (!(cpu->cd.ppc.cpu_type.flags & PPC_NO_DEC)) {
				ppc_exception(cpu, PPC_EXCEPTION_DEC, 0);
      }
		}
		if (cpu->cd.ppc.irq_asserted && (cpu->cd.ppc.msr & PPC_MSR_EE))
			ppc_exception(cpu, PPC_EXCEPTION_EI, 0);
#endif
#ifdef DYNTRANS_SH
		if (cpu->cd.sh.int_to_assert > 0 && !(cpu->cd.sh.sr & SH_SR_BL)
		    && ((cpu->cd.sh.sr & SH_SR_IMASK) >> SH_SR_IMASK_SHIFT)
		    < cpu->cd.sh.int_level)
			sh_exception(cpu, 0, cpu->cd.sh.int_to_assert, 0);
#endif
	}

#ifdef DYNTRANS_ARM
	if (cpu->cd.arm.cpsr & ARM_FLAG_T) {
		fatal("THUMB execution not implemented.\n");
		cpu->running = false;
		return 0;
	}
#endif

	cached_pc = cpu->pc;

	cpu->n_translated_instrs = 0;

  if (GdblibCheckWaiting(cpu)) {
    if (GdblibSerialInterrupt(cpu)) {
      single_step = 1;
    }
  }

  uint64_t prev_instrs = cpu->ninstrs;
  uint64_t next_limit =
    MIN(prev_instrs + N_SAFE_DYNTRANS_LIMIT, cpu->ninstrs_async + INSTR_BETWEEN_INTERRUPTS) -
    cpu->ninstrs;

#ifdef DYNTRANS_PPC
  if (cpu->machine->show_trace_tree) {
    cpu->functioncall_trace = ppc_trace;
    cpu->functioncall_end_trace = ppc_end_trace;
  } else {
    cpu->functioncall_trace = ppc_no_trace;
    cpu->functioncall_end_trace = ppc_no_end_trace;
  }
#endif

  auto instr_trace = [&cpu](struct DYNTRANS_IC *ic) {
#ifdef DYNTRANS_PPC
    ppc_instr_dump_registers(cpu, ic);
#endif
    I;
  };

#ifdef MODE32
  struct DYNTRANS_IC *ic = cpu->cd.DYNTRANS_ARCH.vph32.get_next_ic();
#else
  struct DYNTRANS_IC *ic = cpu->cd.DYNTRANS_ARCH.vph64.get_next_ic();
#endif
	if (single_step & 0xff) {
		/*
		 *  Single-step:
		 */
    instr_trace(ic);

		if (cpu->machine->statistics.enabled) {
			S;
		}

		n_instrs = 1;
	} else if (cpu->machine->statistics.enabled) {
		/*  Gather statistics while executing multiple instructions:  */
		n_instrs = 0;
		while (n_instrs + 24 < next_limit) {
			S; I; S; I; S; I; S; I; S; I; S; I;
			S; I; S; I; S; I; S; I; S; I; S; I;
			S; I; S; I; S; I; S; I; S; I; S; I;
			S; I; S; I; S; I; S; I; S; I; S; I;

			n_instrs += 24;
		}
		while (n_instrs < next_limit) {
			S; I;
			n_instrs ++;
		}
	} else if (cpu->machine->instruction_trace) {
    instr_trace(ic);
    n_instrs = 1;
	} else {
		/*
		 *  Execute multiple instructions:
		 *
		 *  (This is the core dyntrans loop.)
		 */
		n_instrs = 0;
		while (n_instrs + 24 < next_limit) {
			I; I; I; I; I; I; I; I;
			I; I; I; I; I; I; I; I;
			I; I; I; I; I; I; I; I;

      n_instrs += 24;
		}
    while (n_instrs < next_limit) {
      I;

      n_instrs ++;
		}
	}

  cpu->n_translated_instrs += n_instrs;

	/*  Synchronize the program counter:  */
#ifdef MODE32
  low_pc = cpu->cd.DYNTRANS_ARCH.vph32.get_low_pc();
#else
  low_pc = cpu->cd.DYNTRANS_ARCH.vph64.get_low_pc();
#endif
	if (low_pc >= 0 && low_pc < DYNTRANS_IC_ENTRIES_PER_PAGE) {
		cpu->pc &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << DYNTRANS_INSTR_ALIGNMENT_SHIFT);
	} else if (low_pc == DYNTRANS_IC_ENTRIES_PER_PAGE) {
		/*  Switch to next page:  */
		cpu->pc &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (DYNTRANS_IC_ENTRIES_PER_PAGE <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
	} else if (low_pc == DYNTRANS_IC_ENTRIES_PER_PAGE + 1) {
		/*  Switch to next page and skip an instruction which was
		    already executed (in a delay slot):  */
		cpu->pc &= ~((DYNTRANS_IC_ENTRIES_PER_PAGE-1) <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += ((DYNTRANS_IC_ENTRIES_PER_PAGE + 1) <<
		    DYNTRANS_INSTR_ALIGNMENT_SHIFT);
	}

#ifdef DYNTRANS_MIPS
	/*  Update the count register (on everything except EXC3K):  */
	if (cpu->cd.mips.cpu_type.exc_model != EXC3K) {
		uint32_t old;
		int32_t diff1, diff2;
		cpu->cd.mips.coproc[0]->reg[COP0_COUNT] -= cpu->cd.mips.count_register_read_count;
		cpu->cd.mips.count_register_read_count = 0;
		old = cpu->cd.mips.coproc[0]->reg[COP0_COUNT];
		diff1 = cpu->cd.mips.coproc[0]->reg[COP0_COMPARE] - old;
		cpu->cd.mips.coproc[0]->reg[COP0_COUNT] =
		    (int32_t) (old + n_instrs);
		diff2 = cpu->cd.mips.coproc[0]->reg[COP0_COMPARE] -
		    cpu->cd.mips.coproc[0]->reg[COP0_COUNT];

		if (cpu->cd.mips.compare_register_set) {
#if 1
/*  Not yet.  TODO  */
			if (cpu->machine->emulated_hz > 0) {
				if (cpu->cd.mips.compare_interrupts_pending > 0)
					INTERRUPT_ASSERT(
					    cpu->cd.mips.irq_compare);
			} else
#endif
			{
				if (diff1 > 0 && diff2 <= 0)
					INTERRUPT_ASSERT(
					    cpu->cd.mips.irq_compare);
			}
		}
	}
#endif
#ifdef DYNTRANS_PPC
	/*  Update the Decrementer and Time base registers:  */
  uint64_t new_instrs = prev_instrs + n_instrs;
  uint64_t compare_steps = (single_step | 0xffull) - 255ull;
  bool should_stop = new_instrs >= compare_steps;
  if ((single_step >= 0x100ull) && should_stop) {
    fprintf(stderr, "until limit reached\n");
    single_step = 1;
  }
  if (cpu->ninstrs_async + INSTR_BETWEEN_INTERRUPTS <= prev_instrs + n_instrs)
	{
    // fprintf(stderr, "recognizing interrupts, timers at %08x\n", (unsigned int)(prev_instrs + n_instrs));
    cpu->ninstrs_async += INSTR_BETWEEN_INTERRUPTS;
    ppc_update_for_icount(cpu);
	}
#endif

  cpu->ninstrs = prev_instrs + n_instrs;

	/*  Return the nr of instructions executed:  */
	return n_instrs;
}
#endif	/*  DYNTRANS_RUN_INSTR  */



#ifdef DYNTRANS_FUNCTION_TRACE_DEF
/*
 *  XXX_cpu_functioncall_trace():
 *
 *  Without this function, the main trace tree function prints something
 *  like    <f()>  or  <0x1234()>   on a function call. It is up to this
 *  function to print the arguments passed.
 */
void DYNTRANS_FUNCTION_TRACE_DEF(struct cpu *cpu, int n_args)
{
	int show_symbolic_function_name = 1;
        char strbuf[100];
	const char *symbol;
	uint64_t ot;
	int x, print_dots = 1, n_args_to_print =
#if defined(DYNTRANS_ALPHA)
	    6
#else
#if defined(DYNTRANS_SH) || defined(DYNTRANS_M88K)
	    8	/*  Both for 32-bit and 64-bit SuperH, and M88K  */
#else
	    4	/*  Default value for most archs  */
#endif
#endif
	    ;

	if (n_args >= 0 && n_args <= n_args_to_print) {
		print_dots = 0;
		n_args_to_print = n_args;
	}

#ifdef DYNTRANS_M88K
	/*  Special hack for M88K userspace:  */
	if (!(cpu->cd.m88k.cr[M88K_CR_PSR] & M88K_PSR_MODE))
		show_symbolic_function_name = 0;
#endif


	/*
	 *  TODO: The type of each argument should be taken from the symbol
	 *  table, in some way.
	 *
	 *  The code here does a kind of "heuristic guess" regarding what the
	 *  argument values might mean. Sometimes the output looks weird, but
	 *  usually it looks good enough.
	 *
	 *  Print ".." afterwards to show that there might be more arguments
	 *  than were passed in register.
	 */
	for (x=0; x<n_args_to_print; x++) {
		int64_t d = cpu->cd.DYNTRANS_ARCH.
#ifdef DYNTRANS_ALPHA
		    r[ALPHA_A0
#endif
#ifdef DYNTRANS_ARM
		    r[0
#endif
#ifdef DYNTRANS_MIPS
		    gpr[MIPS_GPR_A0
#endif
#ifdef DYNTRANS_M88K
		    r[2		/*  r2..r9  */
#endif
#ifdef DYNTRANS_PPC
		    gpr[3
#endif
#ifdef DYNTRANS_SH
		    r[4		/*  NetBSD seems to use 4? But 2 seems
					to be used by other code? TODO  */
#endif
		    + x];

    symbol = get_symbol_name(cpu, &cpu->machine->symbol_context, d, &ot);

		if (d > -256 && d < 256)
			fatal("%i", (int)d);
		else if (memory_points_to_string(cpu, cpu->mem, d, 2))
			fatal("\"%s\"", memory_conv_to_string(cpu,
			    cpu->mem, d, strbuf, sizeof(strbuf)));
		else if (symbol != NULL && ot == 0 &&
		    show_symbolic_function_name)
			fatal("&%s", symbol);
		else {
			if (cpu->is_32bit)
				fatal("0x%" PRIx32, (uint32_t)d);
			else
				fatal("0x%" PRIx64, (uint64_t)d);
		}

		if (x < n_args_to_print - 1)
			fatal(",");
	}

	if (print_dots)
		fatal(",..");
}
#endif	/*  DYNTRANS_FUNCTION_TRACE_DEF  */

#ifdef DYNTRANS_PC_TO_POINTERS_FUNC
/*
 *  XXX_pc_to_pointers_generic():
 *
 *  Generic case. See DYNTRANS_PC_TO_POINTERS_FUNC below.
 */
void DYNTRANS_PC_TO_POINTERS_GENERIC(struct cpu *cpu)
{
#ifdef MODE32
  cpu->cd.DYNTRANS_ARCH.vph32.pc_to_pointers_generic(cpu);
#else
  cpu->cd.DYNTRANS_ARCH.vph64.pc_to_pointers_generic(cpu);
#endif
}


/*
 *  XXX_pc_to_pointers():
 *
 *  This function uses the current program counter (a virtual address) to
 *  find out which physical translation page to use, and then sets the current
 *  translation page pointers to that page.
 *
 *  If there was no translation page for that physical page, then an empty
 *  one is created.
 *
 *  NOTE: This is the quick lookup version. See
 *  DYNTRANS_PC_TO_POINTERS_GENERIC above for the generic case.
 */
void DYNTRANS_PC_TO_POINTERS_FUNC(struct cpu *cpu)
{
#ifdef MODE32
  cpu->cd.DYNTRANS_ARCH.vph32.move_to_physpage(cpu);
#else
  cpu->cd.DYNTRANS_ARCH.vph64.move_to_physpage(cpu);
#endif

	/*  printf("cached_pc=0x%016" PRIx64"  pagenr=%lli  table_index=%lli, "
	    "physpage_ofs=0x%016" PRIx64"\n", (uint64_t)cached_pc, (long long)
	    pagenr, (long long)table_index, (uint64_t)physpage_ofs);  */
}
#endif	/*  DYNTRANS_PC_TO_POINTERS_FUNC  */



#ifdef DYNTRANS_INIT_TABLES

/*  forward declaration of to_be_translated and end_of_page:  */
static void instr(to_be_translated)(struct cpu *, struct DYNTRANS_IC *);
static void instr(end_of_page)(struct cpu *,struct DYNTRANS_IC *);
#ifdef DYNTRANS_DUALMODE_32
static void instr32(to_be_translated)(struct cpu *, struct DYNTRANS_IC *);
static void instr32(end_of_page)(struct cpu *,struct DYNTRANS_IC *);
#endif

#ifdef DYNTRANS_DUALMODE_32
#define TO_BE_TRANSLATED    ( cpu->is_32bit? instr32(to_be_translated) : \
			      instr(to_be_translated) )
#else
#define TO_BE_TRANSLATED    ( instr(to_be_translated) )
#endif

#ifdef DYNTRANS_DELAYSLOT
static void instr(end_of_page2)(struct cpu *,struct DYNTRANS_IC *);
#ifdef DYNTRANS_DUALMODE_32
static void instr32(end_of_page2)(struct cpu *,struct DYNTRANS_IC *);
#endif
#endif

/*
 *  XXX_init_tables():
 *
 *  Initializes the default translation page (for newly allocated pages), and
 *  for 64-bit emulation it also initializes 64-bit dummy tables and pointers.
 */
void DYNTRANS_INIT_TABLES(struct cpu *cpu)
{
#ifndef MODE32
	struct DYNTRANS_L2_64_TABLE *dummy_l2;
	struct DYNTRANS_L3_64_TABLE *dummy_l3;
	int x1, x2;
#endif
	int i;
	struct DYNTRANS_TC_PHYSPAGE *ppp;

	CHECK_ALLOCATION(ppp =
	    (struct DYNTRANS_TC_PHYSPAGE *) malloc(sizeof(struct DYNTRANS_TC_PHYSPAGE)));

	ppp->next_ofs = 0;
	memset(&ppp->translations_bitmap, 0, sizeof(ppp->translations_bitmap));
	ppp->translation_ranges_ofs = 0;
	/*  ppp->physaddr is filled in by the page allocator  */

  auto fill = [&cpu, &ppp]() {
    for (auto i=0; i<DYNTRANS_IC_ENTRIES_PER_PAGE; i++) {
      ppp->ics[i].f = TO_BE_TRANSLATED;
    }
  };

  fill();

	/*  End-of-page:  */
	ppp->ics[DYNTRANS_IC_ENTRIES_PER_PAGE + 0].f =
#ifdef DYNTRANS_DUALMODE_32
	    cpu->is_32bit? instr32(end_of_page) :
#endif
	    instr(end_of_page);

	/*  End-of-page-2, for delay-slot architectures:  */
#ifdef DYNTRANS_DELAYSLOT
	ppp->ics[DYNTRANS_IC_ENTRIES_PER_PAGE + 1].f =
#ifdef DYNTRANS_DUALMODE_32
	    cpu->is_32bit? instr32(end_of_page2) :
#endif
	    instr(end_of_page2);
#endif

#ifdef MODE32
  cpu->cd.DYNTRANS_ARCH.vph32.set_physpage_template(ppp);
#elif defined(DYNTRANS_DUALMODE_32)
  if (cpu->is_32bit) {
    cpu->cd.DYNTRANS_ARCH.vph32.set_physpage_template(ppp);
  } else {
    cpu->cd.DYNTRANS_ARCH.vph64.set_physpage_template(ppp);
  }
#else
  cpu->cd.DYNTRANS_ARCH.vph64.set_physpage_template(ppp);
#endif


	/*  Prepare 64-bit virtual address translation tables:  */
#ifndef MODE32
	if (cpu->is_32bit) {
		return;
  }

  cpu->cd.DYNTRANS_ARCH.vph64.init_tables();
#endif
}
#endif	/*  DYNTRANS_INIT_TABLES  */

/*****************************************************************************/


#ifdef DYNTRANS_TO_BE_TRANSLATED_HEAD
	/*
	 *  Check for breakpoints.
	 */
	if (!single_step_breakpoint && !cpu->translation_readahead) {
		MODE_uint_t curpc = cpu->pc;
		int i;
		for (i=0; i<cpu->machine->breakpoints.n; i++)
			if (curpc == (MODE_uint_t)
			    cpu->machine->breakpoints.addr[i]) {
				if (!cpu->machine->instruction_trace) {
					int tmp_old_quiet_mode = quiet_mode;
					quiet_mode = 0;
					DISASSEMBLE(cpu, ib, 1, 0);
					quiet_mode = tmp_old_quiet_mode;
				}
#ifdef MODE32
				fatal("BREAKPOINT: pc = 0x%" PRIx32"\n(The "
				    "instruction has not yet executed.)\n",
				    (uint32_t)cpu->pc);
#else
				fatal("BREAKPOINT: pc = 0x%" PRIx64"\n(The "
				    "instruction has not yet executed.)\n",
				    (uint64_t)cpu->pc);
#endif
#ifdef DYNTRANS_DELAYSLOT
				if (cpu->delay_slot != NOT_DELAYED)
					fatal("ERROR! Breakpoint in a delay"
					    " slot! Not yet supported.\n");
#endif
        single_step_breakpoint = 1;
        single_step = ENTER_SINGLE_STEPPING;
				goto stop_running_translated;
			}
	}

  if (GdblibCheckWaiting(cpu)) {
      fprintf(stderr, "gdb pollin\n");
      GdblibSerialInterrupt(cpu);
  }
#endif	/*  DYNTRANS_TO_BE_TRANSLATED_HEAD  */


/*****************************************************************************/


#ifdef DYNTRANS_TO_BE_TRANSLATED_TAIL
	/*
	 *  If we end up here, then an instruction was translated. Let's mark
	 *  the page as containing a translation at this part of the page.
	 */

	/*  Make sure cur_physpage is in synch:  */
	{
		int x = (addr & (DYNTRANS_PAGESIZE - 1)) >> DYNTRANS_INSTR_ALIGNMENT_SHIFT;

#ifdef MODE32
		cpu->cd.DYNTRANS_ARCH.vph32.get_physpage()->
      translations_bitmap.set(x);
#else
		cpu->cd.DYNTRANS_ARCH.vph64.get_physpage()->
      translations_bitmap.set(x);
#endif
	}


	/*
	 *  Now it is time to check for combinations of instructions that can
	 *  be converted into a single function call.
	 *
	 *  Note: Single-stepping or instruction tracing doesn't work with
	 *  instruction combinations. For architectures with delay slots,
	 *  we also ignore combinations if the delay slot is across a page
	 *  boundary.
	 */
	if (!single_step && !cpu->machine->instruction_trace
#ifdef DYNTRANS_DELAYSLOT
	    && !in_crosspage_delayslot
#endif
	    && cpu->cd.DYNTRANS_ARCH.combination_check != NULL
	    && cpu->machine->allow_instruction_combinations) {
		cpu->cd.DYNTRANS_ARCH.combination_check(cpu, ic,
		    addr & (DYNTRANS_PAGESIZE - 1));
	}

	cpu->cd.DYNTRANS_ARCH.combination_check = NULL;

	/*  An additional check, to catch some bugs:  */
	if (ic->f == TO_BE_TRANSLATED) {
		fatal("INTERNAL ERROR: ic->f not set!\n");
		goto bad;
	}
	if (ic->f == NULL) {
		fatal("INTERNAL ERROR: ic->f == NULL!\n");
		goto bad;
	}


	/*
	 *  ... and finally execute the translated instruction:
	 */

	/*  (Except when doing read-ahead!)  */
	if (cpu->translation_readahead)
		return;

	/*
	 *  Special case when single-stepping: Execute the translated
	 *  instruction, but then replace it with a "to be translated"
	 *  directly afterwards.
	 */
	if ((single_step_breakpoint && cpu->delay_slot == NOT_DELAYED)
#ifdef DYNTRANS_DELAYSLOT
	    || in_crosspage_delayslot
#endif
	    ) {
		single_step_breakpoint = 0;
		ic->f(cpu, ic);
		ic->f = TO_BE_TRANSLATED;
		return;
	}


	/*  Translation read-ahead:  */
	if (!single_step && !cpu->machine->instruction_trace &&
	    cpu->machine->breakpoints.n == 0) {
		uint64_t baseaddr = cpu->pc;
		uint64_t pagenr = addr_to_pagenr<struct DYNTRANS_TC_PHYSPAGE *>(baseaddr);
		int i = 1;

		cpu->translation_readahead = MAX_DYNTRANS_READAHEAD;

		while (
           addr_to_pagenr<struct DYNTRANS_TC_PHYSPAGE *>
           (baseaddr +
            (i << DYNTRANS_INSTR_ALIGNMENT_SHIFT))
           == pagenr
           && cpu->translation_readahead > 0
           )
      {
			void (*old_f)(struct cpu *,
			    struct DYNTRANS_IC *) = ic[i].f;

			/*  Already translated? Then abort:  */
			if (old_f != TO_BE_TRANSLATED)
				break;

			/*  Translate the instruction:  */
			ic[i].f(cpu, ic+i);

			/*  Translation failed? Then abort.  */
			if (ic[i].f == old_f)
				break;

			cpu->translation_readahead --;
			++i;
		}

		cpu->translation_readahead = 0;
	}


	/*
	 *  Finally finally :-), execute the instruction.
	 *
	 *  Note: The instruction might have changed during read-ahead, if
	 *  instruction combinations are used.
	 */

	ic->f(cpu, ic);

	return;


bad:	/*
	 *  Nothing was translated. (Unimplemented or illegal instruction.)
	 */

	/*  Clear the translation, in case it was "half-way" done:  */
	ic->f = TO_BE_TRANSLATED;

	if (cpu->translation_readahead) {
		fprintf(stderr, "bad: readahead failed at %08x\n", (unsigned int)addr);
		return;
	}

  fprintf(stderr, "to be translated failed iword = %08x @ %08x\n", iword, (unsigned int)addr);
	quiet_mode = 0;
	cpu->running = 0;

	/*  Note: Single-stepping can jump here.  */
stop_running_translated:

  /* Simple evaluator here? to make tracepoints? */
	debugger_n_steps_left_before_interaction = 0;

#ifdef MODE32
  ic = cpu->cd.DYNTRANS_ARCH.vph32.bad_translation(&nothing_call);
#else
  ic = cpu->cd.DYNTRANS_ARCH.vph64.bad_translation(&nothing_call);
#endif

#ifdef DYNTRANS_DELAYSLOT
	/*  Special hack: If the bad instruction was in a delay slot,
	    make sure that execution does not continue anyway:  */
	if (cpu->delay_slot)
		cpu->delay_slot |= EXCEPTION_IN_DELAY_SLOT;
#endif

	/*  Execute the "nothing" instruction:  */
	ic->f(cpu, ic);

#endif	/*  DYNTRANS_TO_BE_TRANSLATED_TAIL  */

