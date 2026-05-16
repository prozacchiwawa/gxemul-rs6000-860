/*
 *  Copyright (C) 2005-2009  Anders Gavare.  All rights reserved.
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
 *  Common routines for CPU emulation. (Not specific to any CPU type.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <string.h>

#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "settings.h"
#include "timer.h"


extern size_t dyntrans_cache_size;
extern uint32_t required_sr1;
extern uint64_t trace_low, trace_high;

static struct cpu_family *first_cpu_family = NULL;
int stored_syscall;
constexpr int STORED_REGS = 5;
constexpr int STORED_SYSCALL_IOCTL = 1;
constexpr int STORED_SYSCALL_READ = 2;
constexpr int STORED_SYSCALL_ODM_GET_OBJ = 3;
constexpr int STORED_SYSCALL_WRITE = 4;
constexpr int STORED_SYSCALL_EXECV = 5;
constexpr int STORED_CALL_CHKTYPE = 6;
constexpr int STORED_SYSCALL_SCDISK_CONFIG = 7;
constexpr int STORED_CALL_CMPKMCH = 8;
constexpr int STORED_CALL_CFGSCCD_ODM_GET_OBJ_PROXY = 9;
constexpr int STORED_CALL_QUERY_VPD = 10;
constexpr int STORED_CALL_ODM_GET_LIST = 11;
constexpr int STORED_CALL_ODM_ADD_OBJ_PROXY = 12;
constexpr int STORED_CALL_DEVWRITE = 13;
// constexpr int STORED_SYSCALL_LOG_ERROR = 5;
// constexpr int STORED_SYSCALL_LOG_MESSAGE = 6;
// constexpr int STORED_CALL_DEVSWQRY = 7;
// constexpr int STORED_CALL_DEVSWADD = 8;
// constexpr int STORED_CALL_DEVSWDEL = 9;
// constexpr int STORED_CALL_DEVSWCHG = 10;

struct match_functions_t {
  const char *name;
  uint32_t dump_regs;
  uint64_t return_addr1;
  uint64_t return_addr2;
  uint64_t stored[STORED_REGS];
};

struct match_functions_t trace_functions[] = {
  { "not used" },
  { "__ioctl", 1 << 5, 0xd0007300 },
  { "read_sc", 0, 0xd0008b80, 0xd0008cc0 },
  { "odm_get_obj", 1 << 5, 0xd00f78c8, 0xd00f6acc },
  { "write", 0, 0xd0016360 },
  { "execv" },
  { "chktype", 0, 0x10006fa4 },
  { "sccd_proxy_scdisk_config", 0, 0x3868, 0x385c },
  { "cmpkmch", 0, 0xd00fa07c },
  { "cfgsccd_odm_get_obj_proxy", 1 << 5, 0xd000d610 },
  { "query_vpd", (1 << 3) | (1 << 4) | (1 << 6) },
  { "odm_get_list_proxy", (1 << 4), 0xd00f8110, 0xd00f8114 },
  { "odm_add_obj_proxy", (1 << 5) },
  { "devwrite", (1 << 3) | (1 << 4) },
//  { "devswqry", 1 << 4, 0x57a34, 0x57864 },
//  { "devswadd", 1 << 4, 0xfbfbc },
//   { "devswdel", 1 << 4, 0xfbdd8 },
//  { "devswchg", 0, 0xfbbe0 },
//   { "odm_get_first", 1 << 5, 0xd00f8188 },
  { }
};

struct match_functions_t *trace_match;

/*
 *  cpu_new():
 *
 *  Create a new cpu object.  Each family is tried in sequence until a
 *  CPU family recognizes the cpu_type_name.
 *
 *  If there was no match, NULL is returned. Otherwise, a pointer to an
 *  initialized cpu struct is returned.
 */
struct cpu *cpu_new(struct memory *mem, struct machine *machine,
        int cpu_id, char *name)
{
	struct cpu *cpu;
	struct cpu_family *fp;
	char *cpu_type_name;
	char tmpstr[30];

	if (name == NULL) {
		fprintf(stderr, "cpu_new(): cpu name = NULL?\n");
		exit(1);
	}

	CHECK_ALLOCATION(cpu_type_name = strdup(name));

	cpu = (struct cpu *) zeroed_alloc(sizeof(struct cpu));

	CHECK_ALLOCATION(cpu->path = (char *) malloc(strlen(machine->path) + 15));
	snprintf(cpu->path, strlen(machine->path) + 15,
	    "%s.cpu[%i]", machine->path, cpu_id);

	cpu->memory_rw  = NULL;
	cpu->name       = cpu_type_name;
	cpu->mem        = mem;
	cpu->machine    = machine;
	cpu->cpu_id     = cpu_id;
	cpu->byte_order = EMUL_UNDEFINED_ENDIAN;
	cpu->running    = 0;

	/*  Create settings, and attach to the machine:  */
	cpu->settings = settings_new();
	snprintf(tmpstr, sizeof(tmpstr), "cpu[%i]", cpu_id);
	settings_add(machine->settings, tmpstr, 1,
	    SETTINGS_TYPE_SUBSETTINGS, 0, cpu->settings);

	settings_add(cpu->settings, "name", 0, SETTINGS_TYPE_STRING,
	    SETTINGS_FORMAT_STRING, (void *) &cpu->name);
	settings_add(cpu->settings, "running", 0, SETTINGS_TYPE_UINT8,
	    SETTINGS_FORMAT_YESNO, (void *) &cpu->running);

	fp = first_cpu_family;

	while (fp != NULL) {
		if (fp->cpu_new != NULL) {
			if (fp->cpu_new(cpu, mem, machine, cpu_id,
			    cpu_type_name)) {
				/*  Sanity check:  */
				if (cpu->memory_rw == NULL) {
					fatal("\ncpu_new(): memory_rw == "
					    "NULL\n");
					exit(1);
				}
				break;
			}
		}

		fp = fp->next;
	}

	if (fp == NULL) {
		fatal("\ncpu_new(): unknown cpu type '%s'\n", cpu_type_name);
		return NULL;
	}

	fp->init_tables(cpu);

	if (cpu->byte_order == EMUL_UNDEFINED_ENDIAN) {
		fatal("\ncpu_new(): Internal bug: Endianness not set.\n");
		exit(1);
	}

	return cpu;
}


/*
 *  cpu_destroy():
 *
 *  Destroy a cpu object.
 */
void cpu_destroy(struct cpu *cpu)
{
	settings_remove(cpu->settings, "name");
	settings_remove(cpu->settings, "running");

	/*  Remove any remaining level-1 settings:  */
	settings_remove_all(cpu->settings);

	settings_destroy(cpu->settings);

	if (cpu->path != NULL)
		free(cpu->path);

	/*  TODO: This assumes that zeroed_alloc() actually succeeded
	    with using mmap(), and not malloc()!  */
	munmap((void *)cpu, sizeof(struct cpu));
}


/*
 *  cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *                                              
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void cpu_tlbdump(struct machine *m, int x, int rawflag)
{
	if (m->cpu_family == NULL || m->cpu_family->tlbdump == NULL)
		fatal("cpu_tlbdump(): NULL\n");
	else
		m->cpu_family->tlbdump(m, x, rawflag);
}


/*
 *  cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 */
int cpu_disassemble_instr(struct machine *m, struct cpu *cpu,
	unsigned char *instr, int running, uint64_t addr)
{
	if (m->cpu_family == NULL || m->cpu_family->disassemble_instr == NULL) {
		fatal("cpu_disassemble_instr(): NULL\n");
		return 0;
	} else
		return m->cpu_family->disassemble_instr(cpu, instr,
		    running, addr);
}


/*                       
 *  cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs. (CPU dependent.)
 *  coprocs: set bit 0..x to dump registers in coproc 0..x. (CPU dependent.)
 */
void cpu_register_dump(struct machine *m, struct cpu *cpu,
	int gprs, int coprocs)
{
	if (m->cpu_family == NULL || m->cpu_family->register_dump == NULL)
		fatal("cpu_register_dump(): NULL\n");
	else
		m->cpu_family->register_dump(cpu, gprs, coprocs);
}

struct match_functions_t *try_match_function(struct cpu *cpu, uint64_t f, const char *symbol) {
  if (!symbol) {
    return nullptr;
  }

  for (auto i = 0; trace_functions[i].name; i++) {
    if (!strcmp(symbol, trace_functions[i].name)) {
      for (int g = 0; g < STORED_REGS; g++) {
        trace_functions[i].stored[g] = cpu->cd.ppc.gpr[g+3];
      }
      return &trace_functions[i];
    }
  }

  return nullptr;
}

static bool load_uint32(struct cpu *cpu, uint32_t addr, uint32_t &result) {
  uint8_t buf[sizeof(uint32_t)] = { };
  auto r = cpu->memory_rw(cpu, cpu->mem, addr, &buf[0], sizeof(buf), MEM_READ, CACHE_NONE | NO_EXCEPTIONS | HOST_ACCESS);
  if (r == MEMORY_ACCESS_FAILED) {
    return false;
  }
  result = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3];
  return true;
}

/*
 *  cpu_functioncall_trace():
 *
 *  This function should be called if machine->show_trace_tree is enabled, and
 *  a function call is being made. f contains the address of the function.
 */
void cpu_functioncall_trace(struct cpu *cpu, uint64_t f)
{
	int show_symbolic_function_name = 1;
	int i, n_args = -1;
	const char *symbol;
	uint64_t offset;

  auto pc = cpu->pc;
  if (pc == 0x97f04 || pc == 0x9b00 || (pc >= 0x150000 && pc < 0x160000) || (trace_low != trace_high && pc < trace_low || pc >= trace_high)) {
    return;
  }

  if (required_sr1 && (cpu->cd.ppc.sr[1] != required_sr1)) {
    return;
  }

	/*  Special hack for M88K userspace:  */
	if (cpu->machine->arch == ARCH_M88K &&
	    !(cpu->cd.m88k.cr[M88K_CR_PSR] & M88K_PSR_MODE))
		show_symbolic_function_name = 0;

  if (cpu->machine->ncpus > 1)
		fatal("cpu%i:\t", cpu->cpu_id);

	if (cpu->trace_tree_depth > 100)
		cpu->trace_tree_depth = 100;
	for (i=0; i<cpu->trace_tree_depth; i++)
		fatal("  ");

	cpu->trace_tree_depth ++;

	symbol = get_symbol_name_and_n_args(cpu, &cpu->machine->symbol_context,
	    f, &offset, &n_args);

        if (pc == 0x97f04 || pc == 0x9b00 || (pc >= 0x150000 && pc < 0x160000) || ((symbol && strchr(symbol, '+')) && (trace_low != trace_high && pc < trace_low || pc >= trace_high))) {
          return;
        }

  auto matched = try_match_function(cpu, f, symbol);
  if (matched) {
    trace_match = matched;
  }

  if (matched - trace_functions == STORED_CALL_CMPKMCH) {
    return;
  }

	fatal("<");

	if (symbol && show_symbolic_function_name) {
		fatal("%s", symbol);
  } else {
		if (cpu->is_32bit)
			fatal("0x%" PRIx32, (uint32_t) f);
		else
			fatal("0x%" PRIx64, (uint64_t) f);
	}
	fatal("(");

	if (cpu->machine->cpu_family->functioncall_trace != NULL)
		cpu->machine->cpu_family->functioncall_trace(cpu, n_args);

	fatal(") %" PRIx64" %08x >\n", cpu->ninstrs, (unsigned int)cpu->cd.ppc.sr[1]);

  if (matched) {
    cpu_register_dump(cpu->machine, cpu, 1, 0);
  }

  switch (matched - trace_functions) {
  case STORED_SYSCALL_WRITE:
    debug_mem_hexdump(cpu, cpu->mem, matched->stored[4 - 3], matched->stored[4 - 3] + matched->stored[5 - 3]);
    break;

  case STORED_SYSCALL_EXECV: {
    debug_mem_hexdump(cpu, cpu->mem, matched->stored[3 - 3], matched->stored[3 - 3] + 0x100);
    uint32_t addr;
    uint32_t r4 = matched->stored[4 - 3];
    while (load_uint32(cpu, r4, addr) && addr) {
      debug_mem_hexdump(cpu, cpu->mem, addr, addr + 0x20);
      r4 += 4;
    }
    break;
  }

  case STORED_CALL_CHKTYPE:
    debug_mem_hexdump(cpu, cpu->mem, matched->stored[3 - 3], matched->stored[3 - 3] + 0x100);
    break;

  case STORED_SYSCALL_SCDISK_CONFIG: {
    debug_mem_hexdump(cpu, cpu->mem, matched->stored[4 - 3], matched->stored[4 - 3] + 0x100);
    uint32_t uio_addr;
    if (load_uint32(cpu, matched->stored[4 - 3] + 12, uio_addr)) {
      debug_mem_hexdump(cpu, cpu->mem, uio_addr, uio_addr + 0x100);
    }
    break;

  case STORED_CALL_QUERY_VPD:
    fprintf(stderr, "r3\n");
    debug_mem_hexdump(cpu, cpu->mem, matched->stored[3 - 3], matched->stored[3 - 3] + 0x200);
    fprintf(stderr, "r4\n");
    debug_mem_hexdump(cpu, cpu->mem, matched->stored[4 - 3], matched->stored[4 - 3] + 0x200);
    fprintf(stderr, "r6\n");
    debug_mem_hexdump(cpu, cpu->mem, matched->stored[6 - 3], matched->stored[6 - 3] + 0x200);
    break;

  case STORED_CALL_ODM_ADD_OBJ_PROXY:
    fprintf(stderr, "r3\n");
    debug_mem_hexdump(cpu, cpu->mem, matched->stored[3 - 3], matched->stored[3 - 3] + 0x100);
    fprintf(stderr, "r4\n");
    debug_mem_hexdump(cpu, cpu->mem, matched->stored[4 - 3], matched->stored[4 - 3] + 0x200);
    break;

  case STORED_CALL_DEVWRITE:
    fprintf(stderr, "r3\n");
    
  }

    /*
      case STORED_SYSCALL_LOG_ERROR:
      case STORED_SYSCALL_LOG_MESSAGE:
      for (int i = 4; i < 8; i++) {
      fprintf(stderr, "r%d\n", i);
      debug_mem_hexdump(cpu, cpu->mem, matched->stored[i - 3], matched->stored[i - 3] + 0x100);
      }
      break;
    */
  }

#ifdef PRINT_MEMORY_CHECKSUM
	/*  Temporary hack for finding bugs:  */
	fatal("call chksum=%016" PRIx64"\n", memory_checksum(cpu->mem));
#endif
}


/*
 *  cpu_functioncall_trace_return():
 *
 *  This function should be called if machine->show_trace_tree is enabled, and
 *  a function is being returned from.
 *
 *  TODO: Print return value? This could be implemented similar to the
 *  cpu->functioncall_trace function call above.
 */
void cpu_functioncall_trace_return(struct cpu *cpu, uint64_t pc, uint64_t *return_reg)
{
  if (pc == 0x10934 || pc == 0xf3bc || pc == 0xf3b4 || pc == 0xf3cc || pc == 0x9b14 || (pc >= 0x150000 && pc < 0x160000) || (trace_low != trace_high && cpu->pc < trace_low || cpu->pc >= trace_high)) {
    return;
  }

  if (trace_match - trace_functions == STORED_CALL_CMPKMCH) {
    trace_match = nullptr;
    return;
  }

  if (required_sr1 && (cpu->cd.ppc.sr[1] != required_sr1)) {
    return;
  }

  if (trace_match && ((trace_match->return_addr1 == cpu->pc) || (trace_match->return_addr2 == cpu->pc))) {
    auto ioctl = trace_match->stored[4 - 3];

    fprintf(stderr, "return from %s\n", trace_match->name);
    cpu_register_dump(cpu->machine, cpu, 1, 0);
    switch (trace_match - trace_functions) {
    case STORED_SYSCALL_READ: {
      uint32_t read_addr = 0, len = 0;
      uint32_t *ptrs[] = { &read_addr, &len };
      for (auto i = 0; i < 2; i++) {
        if (!load_uint32(cpu, trace_match->stored[4 - 3] + 4 * i, *ptrs[i])) {
          break;
        }
      }
      if (read_addr && len) {
        debug_mem_hexdump(cpu, cpu->mem, read_addr, read_addr + len);
      }
      break;
    }

    case STORED_SYSCALL_IOCTL:
      fprintf(stderr, "ioctl %d %08x\n", (int)trace_match->stored[4 - 3], (unsigned int)trace_match->stored[4 - 3]);
      if (ioctl == 5 || ioctl == 14 || ioctl == 12 || ioctl == 16 || ioctl == 20 || ioctl == 21 || ioctl == 25 || ioctl == 26 || ioctl == 27 || ioctl == 32 || ioctl == 39 || ioctl == 42 || ioctl == 44 || ioctl == 46 || ioctl == 49) { // IOCTL 14 MIPLCB
        uint32_t md_read[6] = { };

        debug_mem_hexdump(cpu, cpu->mem, trace_match->stored[5 - 3], trace_match->stored[5 - 3] + 24);

        for (int i = 0; i < sizeof(md_read) / sizeof(md_read[0]); i++) {
          if (!load_uint32(cpu, trace_match->stored[5 - 3] + 4 * i, md_read[i])) {
            fprintf(stderr, "read failed for entry %d of md_ioctl\n", i);
            break;
          }
        }

        if (md_read[3]) {
          fprintf(stderr, "md_data\n");
          debug_mem_hexdump(cpu, cpu->mem, md_read[3], md_read[3] + 0x100);
        }
        if (md_read[5]) {
          fprintf(stderr, "md_length\n");
          debug_mem_hexdump(cpu, cpu->mem, md_read[5], md_read[5] + 0x100);
        }
      } else if (ioctl == 996) {
        uint32_t md_read = 0;

        debug_mem_hexdump(cpu, cpu->mem, trace_match->stored[5 - 3], trace_match->stored[5 - 3] + 24);

        if (!load_uint32(cpu, trace_match->stored[5 - 3] + 4, md_read)) {
          fprintf(stderr, "read failed for scsi inq ioctl\n");
          break;
        }

        fprintf(stderr, "inq result\n");
        debug_mem_hexdump(cpu, cpu->mem, md_read, md_read + 256);
      } else if (ioctl == 0xff01) {
	fprintf(stderr, "device info\n");
	debug_mem_hexdump(cpu, cpu->mem, trace_match->stored[5 - 3], trace_match->stored[5 - 3] + 0x200);
      } else {
        fprintf(stderr, "don't know ioctl type\n");
      }
      break;

      /*
    case STORED_CALL_VERIFY_TAG: {
      uint32_t pointer = 0;
      if (!load_uint32(trace_match->stored[4 - 3] + 16, pointer)) {
        fprintf(stderr, "read failed for verify tag p1\n");
        break;
      }
      if (!load_uint32(pointer + 108, pointer)) {
        fprintf(stderr, "read failed for verify tag p2\n");
        break;
      }
      if (!load_uint32(pointer + 8, pointer)) {
        fprintf(stderr, "read failed for verify tag p3\n");
        break;
      }
      fprintf(stderr, "verify tag: want DSP between %08x and %08x\n", pointer + 256, pointer + 3436);
      break;
    }
      */

    case STORED_CALL_ODM_GET_LIST:
      {
        auto r5 = trace_match->stored[5 - 3];
        auto r3 = cpu->cd.ppc.gpr[3];
        fprintf(stderr, "listinfo:\n");
        debug_mem_hexdump(cpu, cpu->mem, r5, r5 + 0x400);
        uint32_t valid, num_returned;
        if (!load_uint32(cpu, r5 + 256 + 256 + 4, valid) || !load_uint32(cpu, r5 + 256 + 256, num_returned)) {
          fprintf(stderr, "read failed for odm get list\n");
          break;
        }
        if (valid) {
          fprintf(stderr, "valid result, %d objects returned\n", (int)num_returned);
          debug_mem_hexdump(cpu, cpu->mem, r3, r3 + num_returned * 256);
        } else {
          fprintf(stderr, "no valid result returned\n");
        }
      }
      break;

    default:
      for (auto i = 3; i < 3 + STORED_REGS; i++) {
        if (trace_match->dump_regs & (1 << i)) {
          fprintf(stderr, "dump r%d\n", i);
          debug_mem_hexdump(cpu, cpu->mem, trace_match->stored[i - 3], trace_match->stored[i - 3] + 0x200);
        }
      }
      break;
    }
    fprintf(stderr, "%s return end\n", trace_match->name);
    trace_match = nullptr;
  }

	if (return_reg) {
		for (int i=0; i<cpu->trace_tree_depth; i++)
			fatal("  ");
		fatal("<%08x return %08x from %08x %08x >\n", (uint32_t)pc, (uint32_t)*return_reg, (uint32_t)cpu->pc, (uint32_t)cpu->cd.ppc.sr[1]);
	}

	cpu->trace_tree_depth --;
	if (cpu->trace_tree_depth < 0)
		cpu->trace_tree_depth = 0;
}


/*
 *  cpu_create_or_reset_tc():
 *
 *  Create the translation cache in memory (ie allocate memory for it), if
 *  necessary, and then reset it to an initial state.
 */
void cpu_create_or_reset_tc(struct cpu *cpu)
{
	size_t s = dyntrans_cache_size + DYNTRANS_CACHE_MARGIN;

  cpu->invalidate_code_translation(cpu, 0, INVALIDATE_ALL);
}


/*
 *  cpu_dumpinfo():
 *
 *  Dumps info about a CPU using debug(). "cpu0: CPUNAME, running" (or similar)
 *  is outputed, and it is up to CPU dependent code to complete the line.
 */
void cpu_dumpinfo(struct machine *m, struct cpu *cpu)
{
	debug("cpu%i: %s, %s", cpu->cpu_id, cpu->name,
	    cpu->running? "running" : "stopped");

	if (m->cpu_family == NULL || m->cpu_family->dumpinfo == NULL)
		fatal("cpu_dumpinfo(): NULL\n");
	else
		m->cpu_family->dumpinfo(cpu);
}


/*
 *  cpu_list_available_types():
 *
 *  Print a list of available CPU types for each cpu family.
 */
void cpu_list_available_types(void)
{
	struct cpu_family *fp;
	int iadd = DEBUG_INDENTATION;

	fp = first_cpu_family;

	if (fp == NULL) {
		debug("No CPUs defined!\n");
		return;
	}

	while (fp != NULL) {
		debug("%s:\n", fp->name);
		debug_indentation(iadd);
		if (fp->list_available_types != NULL)
			fp->list_available_types();
		else
			debug("(internal error: list_available_types"
			    " = NULL)\n");
		debug_indentation(-iadd);

		fp = fp->next;
	}
}


/*
 *  cpu_run_deinit():
 *
 *  Shuts down all CPUs in a machine when ending a simulation. (This function
 *  should only need to be called once for each machine.)
 */
void cpu_run_deinit(struct machine *machine)
{
	int te;

	/*
	 *  Two last ticks of every hardware device.  This will allow e.g.
	 *  framebuffers to draw the last updates to the screen before halting.
	 *
	 *  TODO: This should be refactored when redesigning the mainbus
	 *        concepts!
	 */
        for (te=0; te<machine->tick_functions.n_entries; te++) {
		machine->tick_functions.f[te](machine->cpus[0],
		    machine->tick_functions.extra[te]);
		machine->tick_functions.f[te](machine->cpus[0],
		    machine->tick_functions.extra[te]);
	}

	if (machine->show_nr_of_instructions)
		cpu_show_cycles(machine, 1);

	fflush(stdout);
}


/*
 *  cpu_show_cycles():
 *
 *  If show_nr_of_instructions is on, then print a line to stdout about how
 *  many instructions/cycles have been executed so far.
 */
void cpu_show_cycles(struct machine *machine, int forced)
{
	uint64_t offset, pc;
	const char *symbol;
	int64_t mseconds, ninstrs, is, avg;
	struct timeval tv;
	struct cpu *cpu = machine->cpus[machine->bootstrap_cpu];

	static int64_t mseconds_last = 0;
	static int64_t ninstrs_last = -1;

	pc = cpu->pc;

	gettimeofday(&tv, NULL);
	mseconds = (tv.tv_sec - cpu->starttime.tv_sec) * 1000
	         + (tv.tv_usec - cpu->starttime.tv_usec) / 1000;

	if (mseconds == 0)
		mseconds = 1;

	if (mseconds - mseconds_last == 0)
		mseconds ++;

	ninstrs = cpu->ninstrs_since_gettimeofday;

	/*  RETURN here, unless show_nr_of_instructions (-N) is turned on:  */
	if (!machine->show_nr_of_instructions && !forced)
		goto do_return;

	printf("[ %" PRIi64" instrs", (int64_t) cpu->ninstrs);

	/*  Instructions per second, and average so far:  */
	is = 1000 * (ninstrs-ninstrs_last) / (mseconds-mseconds_last);
	avg = (long long)1000 * ninstrs / mseconds;
	if (is < 0)
		is = 0;
	if (avg < 0)
		avg = 0;

	if (cpu->has_been_idling) {
		printf("; idling");
		cpu->has_been_idling = 0;
	} else
		printf("; i/s=%" PRIi64" avg=%" PRIi64, is, avg);

	symbol = get_symbol_name(cpu, &machine->symbol_context, pc, &offset);

	if (machine->ncpus == 1) {
		if (cpu->is_32bit)
			printf("; pc=0x%08" PRIx32, (uint32_t) pc);
		else
			printf("; pc=0x%016" PRIx64, (uint64_t) pc);
	}

	/*  Special hack for M88K userland:  (Don't show symbols.)  */
	if (cpu->machine->arch == ARCH_M88K &&
	    !(cpu->cd.m88k.cr[M88K_CR_PSR] & M88K_PSR_MODE))
		symbol = NULL;

	if (symbol != NULL)
		printf(" <%s>", symbol);
	printf(" ]\n");

do_return:
	ninstrs_last = ninstrs;
	mseconds_last = mseconds;
}


/*
 *  cpu_run_init():
 *
 *  Prepare to run instructions on all CPUs in this machine. (This function
 *  should only need to be called once for each machine.)
 */
void cpu_run_init(struct machine *machine)
{
	int i;

	if (machine->ncpus == 0) {
		printf("Machine with no CPUs? TODO.\n");
		exit(1);
	}

	for (i=0; i<machine->ncpus; i++) {
		struct cpu *cpu = machine->cpus[i];

		cpu->ninstrs_flush = 0;
		cpu->ninstrs = 0;
		cpu->ninstrs_show = 0;

		/*  For performance measurement:  */
		gettimeofday(&cpu->starttime, NULL);
		cpu->ninstrs_since_gettimeofday = 0;
	}
}


/*
 *  add_cpu_family():
 *
 *  Allocates a cpu_family struct and calls an init function for the
 *  family to fill in reasonable data and pointers.
 */
static void add_cpu_family(int (*family_init)(struct cpu_family *), int arch)
{
	struct cpu_family *fp, *tmp;
	int res;

	CHECK_ALLOCATION(fp = (struct cpu_family *) malloc(sizeof(struct cpu_family)));
	memset(fp, 0, sizeof(struct cpu_family));

	/*
	 *  family_init() returns 1 if the struct has been filled with
	 *  valid data, 0 if suppor for the cpu family isn't compiled
	 *  into the emulator.
	 */
	res = family_init(fp);
	if (!res) {
		free(fp);
		return;
	}
	fp->arch = arch;
	fp->next = NULL;

	/*  Add last in family chain:  */
	tmp = first_cpu_family;
	if (tmp == NULL) {
		first_cpu_family = fp;
	} else {
		while (tmp->next != NULL)
			tmp = tmp->next;
		tmp->next = fp;
	}
}


/*
 *  cpu_family_ptr_by_number():
 *
 *  Returns a pointer to a CPU family based on the ARCH_* integers.
 */
struct cpu_family *cpu_family_ptr_by_number(int arch)
{
	struct cpu_family *fp;
	fp = first_cpu_family;

	/*  YUCK! This is too hardcoded! TODO  */

	while (fp != NULL) {
		if (arch == fp->arch)
			return fp;
		fp = fp->next;
	}

	return NULL;
}


/*
 *  cpu_init():
 *
 *  Should be called before any other cpu_*() function.
 *
 *  This function calls add_cpu_family() for each processor architecture.
 *  ADD_ALL_CPU_FAMILIES is defined in the config.h file generated by the
 *  configure script.
 */
void cpu_init(void)
{
	ADD_ALL_CPU_FAMILIES;
}

void debug_mem_hexdump(struct cpu *c, struct memory *mem, uint64_t addr_start, uint64_t addr_end) {
  int r, x;
  auto addr = addr_start & ~0xf;
  extern volatile int ctrl_c;

  ctrl_c = 0;

	while (addr < addr_end) {
		unsigned char buf[16];
		memset(buf, 0, sizeof(buf));
		r = c->memory_rw(c, mem, addr, &buf[0], sizeof(buf),
                     MEM_READ, CACHE_NONE | NO_EXCEPTIONS | HOST_ACCESS);

		if (c->is_32bit)
			printf("0x%08" PRIx32"  ", (uint32_t) addr);
		else
			printf("0x%016" PRIx64"  ", (uint64_t) addr);

		if (r == MEMORY_ACCESS_FAILED)
			printf("(memory access failed)\n");
		else {
			for (x=0; x<16; x++) {
				if (addr + x >= addr_start &&
				    addr + x < addr_end)
					printf("%02x%s", buf[x],
                 (x&3)==3? " " : "");
				else
					printf("  %s", (x&3)==3? " " : "");
			}
			printf(" ");
			for (x=0; x<16; x++) {
				if (addr + x >= addr_start &&
				    addr + x < addr_end)
					printf("%c", (buf[x]>=' ' &&
                        buf[x]<127)? buf[x] : '.');
				else
					printf(" ");
			}
			printf("\n");
		}

		if (ctrl_c)
			return;

		addr += sizeof(buf);
	}
}

struct ba_target_name ba_names[] = {
  { 0x000a4f78, "p_getpte_ppc" },
  { 0x000a556c, "p_delpte_ppc" },
  { 0x000a56d0, "p_delallpte_ppc" },
  { 0x000a5820, "p_inspte_ppc" },
  { 0x000a5a7c, "p_lookup_ppc" },
  { 0x000a5bb8, "p_page_protect_ppc" },
  { 0x000a5ce4, "p_protect_ppc" },
  { 0x000a5e30, "p_clear_modify_ppc" },
  { 0x000a5f80, "p_is_referenced_ppc" },
  { 0x000a60d4, "p_is_modified_ppc" },
  { 0x000a61cc, "p_remove_all_ppc" },
  { 0x000a6268, "p_remove_ppc" },
  { 0x000a63f0, "p_rename_ppc" },
  { 0x000a6488, "p_enter_ppc" },
  {}
};
