/*
 *  Copyright (C) 2003-2009  Anders Gavare.  All rights reserved.
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
 *  Address to symbol translation routines.
 *
 *  This module is (probably) independent from the rest of the emulator.
 *  symbol_init() must be called before any other function in this file is used.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#include "symbol.h"
#include "debugger.h"


/*
 *  symbol_nsymbols():
 *
 *  Return n_symbols.
 */
int symbol_nsymbols(struct symbol_context *sc)
{
	return sc->symbols->size();
}


/*
 *  get_symbol_addr():
 *
 *  Find a symbol by name. If addr is non-NULL, *addr is set to the symbol's
 *  address. Return value is 1 if the symbol is found, 0 otherwise.
 *
 *  NOTE:  This is O(n).
 */
int get_symbol_addr(struct symbol_context *sc, const char *symbol, uint64_t *addr)
{
  for (auto i = sc->symbols->begin(); i != sc->symbols->end(); i++) {
    if (strcmp(symbol, i->name.c_str()) == 0) {
      if (addr != NULL) {
					*addr = i->addr;
      }
      return 1;
    }
	}

	return 0;
}


/*
 *  get_symbol_name_and_n_args():
 *
 *  Translate an address into a symbol name.  The return value is a pointer
 *  to a static char array, containing the symbol name.  (In other words,
 *  this function is not reentrant. This removes the need for memory allocation
 *  at the caller's side.)
 *
 *  If offset is not a NULL pointer, *offset is set to the offset within
 *  the symbol. For example, if there is a symbol at address 0x1000 with
 *  length 0x100, and a caller wants to know the symbol name of address
 *  0x1008, the symbol's name will be found in the static char array, and
 *  *offset will be set to 0x8.
 *
 *  If n_argsp is non-NULL, *n_argsp is set to the symbol's n_args value.
 *
 *  If no symbol was found, NULL is returned instead.
 */
static std::string symbol_buf;
const char *get_symbol_name_and_n_args(struct cpu *c, struct symbol_context *sc, uint64_t addr,
	uint64_t *offset, int *n_argsp)
{
  struct ibm_name namebuf;

	if (sc->symbols->size() == 0) {
    if (debugger_get_name(c, addr, addr + 0x4000, &namebuf)) {
      symbol_buf = namebuf.function_name;
      return symbol_buf.c_str();
    }

		return NULL;
  }

  symbol_buf.clear();
	if (offset != NULL) {
		*offset = 0;
  }

  for (auto s = sc->symbols->begin(); s != sc->symbols->end(); s++) {
    if (addr >= s->addr) {
      /*  Found a match?  */
      if (addr == s->addr) {
        symbol_buf = s->name;
        return symbol_buf.c_str();
      } else {
        char buf[1024];
        snprintf(buf, sizeof(buf),
                 "%s+0x%" PRIx64, s->name.c_str(), (uint64_t)
                 (addr - s->addr));
        symbol_buf = buf;
        if (offset != NULL) {
          *offset = addr - s->addr;
        }
        if (n_argsp != NULL) {
          *n_argsp = s->n_args;
        }
      }
    }
  }

  if (symbol_buf.size() && strncmp(symbol_buf.c_str(), "*", 1) != 0) {
    return symbol_buf.c_str();
  } else {
    return nullptr;
  }
}


/*
 *  get_symbol_name():
 *
 *  See get_symbol_name_and_n_args().
 */
const char *get_symbol_name(struct cpu *c, struct symbol_context *sc, uint64_t addr, uint64_t *offs)
{
	return get_symbol_name_and_n_args(c, sc, addr, offs, NULL);
}


/*
 *  add_symbol_name():
 *
 *  Add a symbol to the symbol list.
 */
void add_symbol_name(struct symbol_context *sc,
	uint64_t addr, uint64_t len, const char *name, int type, int n_args)
{
	struct symbol *s;
  int flags = type >> 8;
  type &= 0xff;

	if (name == NULL) {
		fprintf(stderr, "add_symbol_name(): name = NULL\n");
		exit(1);
	}

	if (addr == 0 && strcmp(name, "_DYNAMIC_LINK") == 0)
		return;

	if (name[0] == '\0')
		return;

	/*  TODO: Maybe this should be optional?  */
  if (!flags) {
    if (name[0] == '.' || name[0] == '$')
      return;
  }

	/*  Quick test-hack:  */
	if (n_args < 0) {
		if (strcmp(name, "strlen") == 0)
			n_args = 1;
		if (strcmp(name, "strcmp") == 0)
			n_args = 2;
		if (strcmp(name, "strcpy") == 0)
			n_args = 2;
		if (strcmp(name, "strncpy") == 0)
			n_args = 3;
		if (strcmp(name, "strlcpy") == 0)
			n_args = 3;
		if (strcmp(name, "strlcat") == 0)
			n_args = 3;
		if (strcmp(name, "strncmp") == 0)
			n_args = 3;
		if (strcmp(name, "memset") == 0)
			n_args = 3;
		if (strcmp(name, "memcpy") == 0)
			n_args = 3;
		if (strcmp(name, "bzero") == 0)
			n_args = 2;
		if (strcmp(name, "bcopy") == 0)
			n_args = 3;
	}

  if (!flags) {
    if ((addr >> 32) == 0 && (addr & 0x80000000ULL))
      addr |= 0xffffffff00000000ULL;
  }

  auto sym = symbol();

  if (!flags) {
    sym.name = symbol_demangle_cplusplus(name);
  } else {
    sym.name = name;
  }

	sym.addr   = addr;
	sym.len    = len;
	sym.type   = type;
	sym.n_args = n_args;

  sc->symbols->push_back(sym);
}


/*
 *  symbol_readfile():
 *
 *  Read 'nm -S' style symbols from a file.
 *
 *  TODO: This function is an ugly hack, and should be replaced
 *  with something that reads symbols directly from the executable
 *  images.
 */
void symbol_readfile(struct symbol_context *sc, char *fname)
{
	FILE *f;
	char b1[1024]; uint64_t addr;
	char b2[1024]; uint64_t len;
	char b3[1024]; int type;
	char b4[1024];
	int cur_n_symbols = sc->symbols->size();

	f = fopen(fname, "r");
	if (f == NULL) {
		perror(fname);
		exit(1);
	}

	while (!feof(f)) {
		memset(b1, 0, sizeof(b1));
		memset(b2, 0, sizeof(b2));
		memset(b3, 0, sizeof(b3));
		memset(b4, 0, sizeof(b4));
		if (fscanf(f, "%s %s\n", b1,b2) != 2)
			fprintf(stderr, "warning: symbol file parse error\n");
		if (strlen(b2) < 2 && !(b2[0]>='0' && b2[0]<='9')) {
			strlcpy(b3, b2, sizeof(b3));
			strlcpy(b2, "0", sizeof(b2));
			if (fscanf(f, "%s\n", b4) != 1)
				fprintf(stderr, "warning: symbol file parse error\n");
		} else {
			if (fscanf(f, "%s %s\n", b3,b4) != 2)
				fprintf(stderr, "warning: symbol file parse error\n");
		}

		/*  printf("b1='%s' b2='%s' b3='%s' b4='%s'\n",
		    b1,b2,b3,b4);  */
		addr = strtoull(b1, NULL, 16);
		len  = strtoull(b2, NULL, 16);
		type = b3[0];
		/*  printf("addr=%016" PRIx64" len=%016" PRIx64" type=%i\n",
		    addr, len, type);  */

		if (type == 't' || type == 'r' || type == 'g')
			continue;

		add_symbol_name(sc, addr, len, b4, type, -1);
	}

	fclose(f);

	debug("%i symbols\n", sc->symbols->size() - cur_n_symbols);
}


/*
 *  sym_addr_compare():
 *
 *  Helper function for sorting symbols according to their address.
 */
int sym_addr_compare(const void *a, const void *b)
{
	struct symbol *p1 = (struct symbol *) a;
	struct symbol *p2 = (struct symbol *) b;

	if (p1->addr < p2->addr) {
		return -1;
  }
	if (p1->addr > p2->addr) {
		return 1;
  }

	return 0;
}


/*
 *  symbol_recalc_sizes():
 *
 *  Recalculate sizes of symbols that have size = 0, by creating an array
 *  containing all symbols, qsort()-ing that array according to address, and
 *  recalculating the size fields if necessary.
 */
void symbol_recalc_sizes(struct symbol_context *sc)
{
  std::sort(sc->symbols->begin(), sc->symbols->end(), [](const symbol &a, const symbol &b) {
    return a.addr < b.addr;
  });

  if (sc->symbols->size()) {
    auto last = sc->symbols->begin();
    auto s = last;
    s++;
    for (; s != sc->symbols->end(); s++) {
      if (last->len == 0) {
        last->len = s->addr - last->addr;
      }
      last = s;
    }
  }
}


/*
 *  symbol_init():
 *
 *  Initialize the symbol hashtables.
 */
void symbol_init(struct symbol_context *sc)
{
  sc->symbols = new std::vector<symbol>();
}

