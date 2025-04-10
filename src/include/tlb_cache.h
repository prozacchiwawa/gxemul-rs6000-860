#ifndef TLB_CACHE_H
#define TLB_CACHE_H

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

#define	N_BASE_TABLE_ENTRIES		65536
#define	PAGENR_TO_TABLE_INDEX(a)	((a) & (N_BASE_TABLE_ENTRIES-1))

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

template <typename TcPhyspage> constexpr int pagesize() { return 1 << 12; }
template <> constexpr int pagesize<struct alpha_tc_physpage>() { return 1 << 13; }

template <typename TcPhyspage> constexpr int max_vph_tlb_entries() { return 128; }
template <> constexpr int max_vph_tlb_entries<struct arm_tc_physpage>() { return 384; }

template <typename TcPhyspage> constexpr int ic_entries_per_page() { return 1024; }

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

#endif//TLB_CACHE_H
