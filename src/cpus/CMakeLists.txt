# Disclosure: GenAI partially used.

add_executable(generate_head generate_head.c)
add_executable(generate_tail generate_tail.c)
add_executable(generate_alpha_misc generate_alpha_misc.c)
add_executable(generate_arm_dpi generate_arm_dpi.c)
add_executable(generate_arm_loadstore generate_arm_loadstore.c)
add_executable(generate_arm_multi generate_arm_multi.c)
add_executable(generate_arm_r generate_arm_r.c)
add_executable(generate_m88k_bcnd generate_m88k_bcnd.c)
add_executable(generate_m88k_loadstore generate_m88k_loadstore.c)
add_executable(generate_mips_loadstore generate_mips_loadstore.c)
add_executable(generate_mips_loadstore_multi generate_mips_loadstore_multi.c)
add_executable(generate_ppc_loadstore generate_ppc_loadstore.c)
add_executable(make_ppc_spr_strings ${CMAKE_SOURCE_DIR}/src/include/make_ppc_spr_strings_new.cc)

set(CPU_TOOLS
generate_alpha_misc
generate_arm_dpi
generate_arm_loadstore 
generate_arm_multi
generate_arm_r
generate_m88k_bcnd
generate_m88k_loadstore
generate_mips_loadstore
generate_mips_loadstore
generate_ppc_loadstore
make_ppc_spr_strings)

function(add_generated_cc output generator args deps)
  separate_arguments(sep_args UNIX_COMMAND "${args}")
  add_custom_command(
    OUTPUT ${output}
    COMMAND $<TARGET_FILE:${generator}> ${sep_args} > ${output}
    DEPENDS ${deps} ${generator}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
  )
endfunction()

set(GENERATED_SOURCES
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_alpha_misc.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_alpha_head.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_alpha_tail.cc

  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_head.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_tail.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p0_u0_w0.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p0_u0_w1.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p0_u1_w0.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p0_u1_w1.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p1_u0_w0.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p1_u0_w1.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p1_u1_w0.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p1_u1_w1.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_multi.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_dpi.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r0.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r1.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r2.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r3.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r4.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r5.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r6.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r7.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r8.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r9.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_ra.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_rb.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_rc.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_rd.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_re.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_rf.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r.cc

  ${CMAKE_CURRENT_BINARY_DIR}/tmp_m88k_bcnd.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_m88k_loadstore.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_m88k_head.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_m88k_tail.cc

  ${CMAKE_CURRENT_BINARY_DIR}/tmp_mips_loadstore.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_mips_loadstore_multi.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_mips_head.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_mips_tail.cc

  ${CMAKE_CURRENT_BINARY_DIR}/tmp_ppc_loadstore.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_ppc_head.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_ppc_tail.cc

  ${CMAKE_CURRENT_BINARY_DIR}/tmp_sh_head.cc
  ${CMAKE_CURRENT_BINARY_DIR}/tmp_sh_tail.cc
  ${CMAKE_CURRENT_BINARY_DIR}/ppc_spr_strings.h
)

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_alpha_misc.cc"
  generate_alpha_misc "" cpu_alpha_instr_loadstore.cc)

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_alpha_head.cc"
  generate_head "alpha Alpha 2" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_alpha_tail.cc"
  generate_tail "alpha Alpha" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore.cc"
  generate_arm_loadstore "" cpu_arm_instr_loadstore.cc)

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p0_u0_w0.cc"
  generate_arm_loadstore "0 0 0" cpu_arm_instr_loadstore.cc)
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p0_u0_w1.cc"
  generate_arm_loadstore "0 0 1" cpu_arm_instr_loadstore.cc)
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p0_u1_w0.cc"
  generate_arm_loadstore "0 1 0" cpu_arm_instr_loadstore.cc)
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p0_u1_w1.cc"
  generate_arm_loadstore "0 1 1" cpu_arm_instr_loadstore.cc)
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p1_u0_w0.cc"
  generate_arm_loadstore "1 0 0" cpu_arm_instr_loadstore.cc)
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p1_u0_w1.cc"
  generate_arm_loadstore "1 0 1" cpu_arm_instr_loadstore.cc)
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p1_u1_w0.cc"
  generate_arm_loadstore "1 1 0" cpu_arm_instr_loadstore.cc)
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_loadstore_p1_u1_w1.cc"
  generate_arm_loadstore "1 1 1" cpu_arm_instr_loadstore.cc)

file(STRINGS "cpu_arm_multi.txt" arm_multi)

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_multi.cc"
  generate_arm_multi " ${arm_multi}" cpu_arm_multi.txt)

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_dpi.cc"
  generate_arm_dpi "" cpu_arm_instr_dpi.cc)

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r0.cc"
  generate_arm_r "0x000 0x0ff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r1.cc"
  generate_arm_r "0x100 0x1ff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r2.cc"
  generate_arm_r "0x200 0x2ff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r3.cc"
  generate_arm_r "0x300 0x3ff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r4.cc"
  generate_arm_r "0x400 0x4ff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r5.cc"
  generate_arm_r "0x500 0x5ff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r6.cc"
  generate_arm_r "0x600 0x6ff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r7.cc"
  generate_arm_r "0x700 0x7ff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r8.cc"
  generate_arm_r "0x800 0x8ff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r9.cc"
  generate_arm_r "0x900 0x9ff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_ra.cc"
  generate_arm_r "0xa00 0xaff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_rb.cc"
  generate_arm_r "0xb00 0xbff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_rc.cc"
  generate_arm_r "0xc00 0xcff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_rd.cc"
  generate_arm_r "0xd00 0xdff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_re.cc"
  generate_arm_r "0xe00 0xeff" "")
add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_rf.cc"
  generate_arm_r "0xf00 0xfff" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_r.cc"
  generate_arm_r "0 0" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_head.cc"
  generate_head "arm ARM 1" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_arm_tail.cc"
  generate_tail "arm ARM" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_m88k_bcnd.cc"
  generate_m88k_bcnd "" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_m88k_loadstore.cc"
  generate_m88k_loadstore "" cpu_m88k_instr_loadstore.cc)

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_m88k_head.cc"
  generate_head "m88k M88K 1" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_m88k_tail.cc"
  generate_tail "m88k M88K" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_mips_loadstore.cc"
  generate_mips_loadstore "" cpu_mips_instr_loadstore.cc)

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_mips_loadstore_multi.cc"
  generate_mips_loadstore_multi "" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_mips_head.cc"
  generate_head "mips MIPS 1" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_mips_tail.cc"
  generate_tail "mips MIPS" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_ppc_loadstore.cc"
  generate_ppc_loadstore "" cpu_ppc_instr_loadstore.cc)

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_ppc_head.cc"
  generate_head "ppc PPC 3" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_ppc_tail.cc"
  generate_tail "ppc PPC" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_sh_head.cc"
  generate_head "sh SH 1" "")

add_generated_cc("${CMAKE_CURRENT_BINARY_DIR}/tmp_sh_tail.cc"
  generate_tail "sh SH" "")

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ppc_spr_strings.h"
    COMMAND $<TARGET_FILE:make_ppc_spr_strings> "${CMAKE_SOURCE_DIR}/src/include/thirdparty/ppc_spr.h" "${CMAKE_CURRENT_BINARY_DIR}/ppc_spr_strings.h"
    DEPENDS make_ppc_spr_strings ${CMAKE_SOURCE_DIR}/src/include/thirdparty/ppc_spr.h
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
)
 
add_library(cpus OBJECT
  cpu.cc
  cpu_alpha.cc
  cpu_arm.cc
  cpu_m88k.cc
  cpu_mips.cc
  cpu_ppc.cc
  cpu_sh.cc
  memory_m88k.cc
  memory_sh.cc
  memory_alpha.cc
)

set_source_files_properties(${GENERATED_SOURCES} PROPERTIES GENERATED TRUE)

target_include_directories(cpus PRIVATE
  ${SOFTFLOAT_INCLUDE_DIR}
)

target_compile_definitions(cpus PRIVATE
  SOFTFLOAT_ROUND_ODD
  INLINE_LEVEL=5
  SOFTFLOAT_FAST_DIV32TO16
  SOFTFLOAT_FAST_DIV64TO32
)

add_custom_target(cpus_generated_sources DEPENDS ${GENERATED_SOURCES})
add_custom_target(cpus_tools DEPENDS generate_head generate_tail ${CPU_TOOLS})

add_dependencies(cpus cpus_generated_sources)
add_dependencies(cpus_generated_sources cpus_tools)

set_property(TARGET cpus APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES "${GENERATED_SOURCES}")

target_include_directories(cpus PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_SOURCE_DIR}/src/thirdparty/softfloat/source/include/")
