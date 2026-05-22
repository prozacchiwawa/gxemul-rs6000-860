add_executable(makeautomachine makeautomachine.cc)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/automachine.cc
                    COMMAND "$<TARGET_FILE:makeautomachine> ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/automachine.cc"
                    DEPENDS makeautomachine
                    VERBATIM)

add_library(machines
    OBJECT
    machine_algor.cc    machine_alpha.cc    machine_arc.cc        machine_cats.cc          machine_cobalt.cc  machine_dreamcast.cc  machine_evbmips.cc
    machine_hpcarm.cc   machine_hpcmips.cc  machine_hpcsh.cc      machine_iq80321.cc       machine_iyonix.cc  machine_landisk.cc    machine_macppc.cc
    machine_mvme88k.cc  machine_mvmeppc.cc  machine_netwinder.cc  machine_playstation2.cc  machine_pmax.cc    machine_pmppc.cc      machine_prep.cc
    machine_qemu.cc     machine_sgi.cc      machine_test.cc ${CMAKE_CURRENT_BINARY_DIR}/automachine.cc
)