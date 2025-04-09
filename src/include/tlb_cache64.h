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

template <typename TcPhyspage, typename L3Table, typename L2Table, typename VpgTlbEntry> struct vph64 {
private:
	L3Table *l3_64_dummy;
	L3Table *next_free_l3;
	L2Table *l2_64_dummy;
	L2Table *next_free_l2;
	L2Table *l1_64[1 << dyntrans_l1n<TcPhyspage>()];
	VpgTlbEntry *vph_tlb_entry;
  uint32_t *is_userpage;

public:
  struct host_load_store_t get_cached_tlb_pages(struct cpu *cpu, uint64_t addr) {
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

  void set_tlb_physpage(struct cpu *cpu, uint64_t addr, TcPhyspage *ppp) {
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
  (uint64_t vaddr_page, uint64_t paddr_page, uint8_t *host_page, int writeflag)
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
          l2 = l1_64[x1] = (L2Table *) malloc(sizeof(L2Table *));
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

  void invalidate_tc_code(struct cpu *cpu, uint64_t addr, int flags, decltype(((TcPhyspage*)0)->ics) to_be_translated) {
    int r;
    uint64_t vaddr_page, paddr_page;

    addr &= ~(pagesize<TcPhyspage>()-1);

    /*  printf("DYNTRANS_INVALIDATE_TC_CODE addr=0x%08x flags=%i\n",
        (int)addr, flags);  */

    if (flags & INVALIDATE_PADDR) {
      int pagenr, table_index;
      uint32_t physpage_ofs, *physpage_entryp;
      TcPhyspage *ppp, *prev_ppp;

      pagenr = addr / pagesize<TcPhyspage>();
      table_index = PAGENR_TO_TABLE_INDEX(pagenr);

      physpage_entryp = &(((uint32_t *)cpu->translation_cache)[table_index]);
      physpage_ofs = *physpage_entryp;

      /*  Return immediately if there is no code translation
          for this page.  */
      if (physpage_ofs == 0)
        return;

      prev_ppp = ppp = NULL;

      /*  Traverse the physical page chain:  */
      while (physpage_ofs != 0) {
        prev_ppp = ppp;
        ppp = (TcPhyspage *)(cpu->translation_cache + physpage_ofs);

        /*  If we found the page in the cache,
            then we're done:  */
        if (ppp->physaddr == addr)
          break;

        /*  Try the next page in the chain:  */
        physpage_ofs = ppp->next_ofs;
      }

      /*  If there is no translation, there is no need to go
          on and try to remove it from the vph_tlb_entry array:  */
      if (physpage_ofs == 0)
        return;

      prev_ppp = prev_ppp;	// shut up compiler warning

      /*
       *  Instead of removing the page from the code cache, each
       *  entry can be set to "to_be_translated". This is slow in
       *  the general case, but in the case of self-modifying code,
       *  it might be faster since we don't risk wasting cache
       *  memory as quickly (which would force unnecessary Restarts).
       */
      if (ppp != NULL && ppp->translations_bitmap != 0) {
        uint32_t x = ppp->translations_bitmap;	/*  TODO: urk Should be same type as the bitmap */
        int i, j, n, m;

        n = 8 * sizeof(x);
        m = ic_entries_per_page<TcPhyspage>() / n;

        for (i=0; i<n; i++) {
          if (x & 1) {
            for (j=0; j<m; j++)
              ppp->ics[i*m + j].f = to_be_translated;
          }

          x >>= 1;
        }

        ppp->translations_bitmap = 0;

        /*  Clear the list of translatable ranges:  */
        if (ppp->translation_ranges_ofs != 0) {
          struct physpage_ranges *physpage_ranges =
				    (struct physpage_ranges *)
				    (cpu->translation_cache +
             ppp->translation_ranges_ofs);
          physpage_ranges->next_ofs = 0;
          physpage_ranges->n_entries_used = 0;
        }
      }
    }

    /*  Invalidate entries in the VPH table:  */
    for (r = 0; r < max_vph_tlb_entries<TcPhyspage>(); r ++) {
      if (vph_tlb_entry[r].valid) {
        vaddr_page = vph_tlb_entry[r]
			    .vaddr_page & ~(pagesize<TcPhyspage>()-1);
        paddr_page = vph_tlb_entry[r]
			    .paddr_page & ~(pagesize<TcPhyspage>()-1);

        if (flags & INVALIDATE_ALL ||
            (flags & INVALIDATE_PADDR && paddr_page == addr) ||
            (flags & INVALIDATE_VADDR && vaddr_page == addr)) {
          cpu->cd.DYNTRANS_ARCH.vph64.clear_phys(vaddr_page);
        }
      }
    }
  }
};


#endif//TLB_CACHE64_H
