#ifndef CPU_TRAITS
#define CPU_TRAITS

template <class T> struct cpu_traits {
  static constexpr int instr_alignment_shift() { return 0; }
  static constexpr int ic_entries_per_page() { return 0; }
  typedef void *instr_t;
};

#endif//CPU_TRAITS
