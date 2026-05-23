add_executable(makeautodev makeautodev.cc)
add_executable(Xconv_raw_to_c fonts/Xconv_raw_to_c.c)

add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/fonts/font8x8.cc
                   COMMAND $<TARGET_FILE:Xconv_raw_to_c> ${CMAKE_CURRENT_SOURCE_DIR}/fonts/vt220l.808 font8x8 > ${CMAKE_CURRENT_SOURCE_DIR}/fonts/font8x8.cc
                   DEPENDS Xconv_raw_to_c fonts/vt220l.808
                   VERBATIM)

add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/fonts/font8x10.cc
                   COMMAND $<TARGET_FILE:Xconv_raw_to_c> ${CMAKE_CURRENT_SOURCE_DIR}/fonts/vt220l.810 font8x10 > ${CMAKE_CURRENT_SOURCE_DIR}/fonts/font8x10.cc
                   DEPENDS Xconv_raw_to_c fonts/vt220l.810
                   VERBATIM)

add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/fonts/font8x16.cc
                   COMMAND $<TARGET_FILE:Xconv_raw_to_c> ${CMAKE_CURRENT_SOURCE_DIR}/fonts/vt220l.816 font8x16 > ${CMAKE_CURRENT_SOURCE_DIR}/fonts/font8x16.cc
                   DEPENDS Xconv_raw_to_c fonts/vt220l.816
                   VERBATIM)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/autodev.cc
                    COMMAND $<TARGET_FILE:makeautodev> ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/bus_pci.cc ${CMAKE_CURRENT_BINARY_DIR}/autodev.cc
                    DEPENDS makeautodev
                    VERBATIM)

add_custom_target(fonts DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fonts/font8x8.cc ${CMAKE_CURRENT_SOURCE_DIR}/fonts/font8x10.cc ${CMAKE_CURRENT_SOURCE_DIR}/fonts/font8x16.cc)

add_library(devices
    OBJECT
    bus_isa.cc
    bus_pci.cc
    device.cc
    lk201.cc
    dev_8253.cc            dev_dc7085.cc           dev_fdc.cc           dev_lca.cc         dev_pccmos.cc     dev_rs5c313.cc        dev_ssc.cc
    dev_8259.cc            dev_dec21030.cc         dev_footbridge.cc    dev_le.cc          dev_pcic.cc       dev_rtc.cc            dev_turbochannel.cc
    dev_86mc65.cc          dev_dec21143.cc         dev_gc.cc            dev_lpt.cc         dev_pckbc.cc      dev_rtl8139c.cc       dev_uninorth.cc
    dev_adb.cc             dev_dec5800.cc          dev_gt.cc            dev_lsi53c895a.cc  dev_pmagja.cc     dev_scc.cc            dev_unreadable.cc
    dev_ahc.cc             dev_dec_ioasic.cc       dev_hammerhead.cc    dev_m8820x.cc      dev_pmppc.cc      dev_sfb.cc            dev_v3.cc
    dev_algor.cc           dev_disk.cc             dev_i80321.cc        dev_malta_lcd.cc   dev_prep.cc       dev_sgi_gbe.cc        dev_vga.cc
    dev_asc.cc             dev_dreamcast_asic.cc   dev_ibm_isa.cc       dev_mb8696x.cc     dev_ps2_ether.cc  dev_sgi_ip19.cc       dev_vme.cc
    dev_bebox.cc           dev_dreamcast_g2.cc     dev_igsfb.cc         dev_mc146818.cc    dev_ps2_gif.cc    dev_sgi_ip20.cc       dev_vr41xx.cc
    dev_bt431.cc           dev_dreamcast_gdrom.cc  dev_iq80321_7seg.cc  dev_mk48txx.cc     dev_ps2_gs.cc     dev_sgi_ip22.cc       dev_wd90c00.cc
    dev_bt455.cc           dev_dreamcast_maple.cc  dev_irqc.cc          dev_mp.cc          dev_ps2_spd.cc    dev_sgi_ip30.cc       dev_wdc.cc
    dev_bt459.cc           dev_dreamcast_rtc.cc    dev_jazz.cc          dev_mvme187.cc     dev_ps2_stuff.cc  dev_sgi_ip32.cc       dev_z8530.cc
    dev_clmpcc.cc          dev_eagle.cc            dev_kn01.cc          dev_ns16550.cc     dev_pvr.cc        dev_sgi_mardigras.cc  dev_zero.cc
    dev_colorplanemask.cc  dev_ether.cc            dev_kn02.cc          dev_ohci.cc        dev_px.cc         dev_sh4.cc
    dev_cons.cc            dev_fb.cc               dev_kn220.cc         dev_osiop.cc       dev_ram.cc        dev_sii.cc
    dev_cpc700.cc          dev_fbctrl.cc           dev_kn230.cc         dev_pcc2.cc        dev_random.cc     dev_sn.cc ${CMAKE_CURRENT_BINARY_DIR}/autodev.cc
)

add_dependencies(devices fonts)