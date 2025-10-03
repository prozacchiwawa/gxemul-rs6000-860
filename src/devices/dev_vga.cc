/*
 *  Copyright (C) 2004-2009  Anders Gavare.  All rights reserved.
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
 *  COMMENT: VGA framebuffer device (charcell and graphics modes)
 *
 *  It should work with 80x25 and 40x25 text modes, and with a few graphics
 *  modes as long as no fancy VGA features are used.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "bus_isa.h"

#include "vga.h"

/*  These are generated from binary font files:  */
#include "fonts/font8x8.cc"
#include "fonts/font8x10.cc"
#include "fonts/font8x16.cc"

#define S_CRTC 0
#define S_SEQ 1
#define S_GR 2
#define S_ATT 3
#define S_PRIMARY 4
#define S_EXTENDED 5
#define S_SEQ2 6
#define S_CRT2 7
#define S_BEE8 8

#define ON_READ 1
#define ON_WRITE 2
#define ON_RW 3

struct register_name_table_t {
  int source;
  int reg_index;
  const char *reg_name;
  int assert_fail_if_used;
};

const char *group_names[] = {
  "CRTC",
  "SEQ",
  "GR",
  "Attribute",
  "Primary Registers 0x3b0+",
  "Extended 0x8xxx-0xfxxx",
  "Extended Sequencer",
  "Extended CRTC",
  "BEE8 Selector",
  nullptr
};

#if 0
#define REG_WRITE(R) do { \
  if (writeflag == MEM_WRITE) {                         \
    fprintf(stderr, "@@@o%08x", relative_addr + (R));     \
    for (int ii = 0; ii < len; ii++) {                    \
      fprintf(stderr, " %02x", data[ii]);                 \
    }                                                     \
    fprintf(stderr, "\n");                                \
  }                                                       \
  } while (0)
#else
#define REG_WRITE(X) do { } while (0)
#endif

struct register_name_table_t r_name_table[] = {
  { source: S_PRIMARY,
    reg_index: 0x00,
    reg_name: "Attribute Controller Index"
  },
  { source: S_PRIMARY,
    reg_index: 0x01,
    reg_name: "Attribute Controller Data"
  },
  { source: S_PRIMARY,
    reg_index: 0x02,
    reg_name: "Misc. Output/Input Status 3c2"
  },
  { source: S_PRIMARY,
    reg_index: 0x03,
    reg_name: "Input Status 1"
  },
  { source: S_PRIMARY,
    reg_index: 0x04,
    reg_name: "VGA Sequencer Index"
  },
  { source: S_PRIMARY,
    reg_index: 0x05,
    reg_name: "VGA Sequencer Data"
  },
  { source: S_PRIMARY,
    reg_index: 0x0c,
    reg_name: "Misc. Output 3cc"
  },
  { source: S_PRIMARY,
    reg_index: 0x0a,
    reg_name: "Feature Control 3ca"
  },
  { source: S_PRIMARY,
    reg_index: 0x1a,
    reg_name: "Feature Control 3da"
  },
  { source: S_PRIMARY,
    reg_index: 0x14,
    reg_name: "CRT Control Index"
  },
  { source: S_PRIMARY,
    reg_index: 0x15,
    reg_name: "CRT Control Data"
  },
  { source: S_SEQ,
    reg_index: 0x00,
    reg_name: "Reset"
  },
  { source: S_SEQ,
    reg_index: 0x01,
    reg_name: "Clocking Mode"
  },
  { source: S_SEQ,
    reg_index: 0x02,
    reg_name: "Enable Write Planes"
  },
  { source: S_SEQ,
    reg_index: 0x03,
    reg_name: "Character Font Select"
  },
  { source: S_SEQ,
    reg_index: 0x04,
    reg_name: "Memory Mode Control"
  },
  { source: S_CRTC,
    reg_index: 0x00,
    reg_name: "Horizontal Total"
  },
  { source: S_CRTC,
    reg_index: 0x01,
    reg_name: "Horizontal Display End"
  },
  { source: S_CRTC,
    reg_index: 0x02,
    reg_name: "Start Horizontal Blank"
  },
  { source: S_CRTC,
    reg_index: 0x03,
    reg_name: "End Horizontal Blank"
  },
  { source: S_CRTC,
    reg_index: 0x04,
    reg_name: "Start Horizontal Sync Position"
  },
  { source: S_CRTC,
    reg_index: 0x05,
    reg_name: "End Horizontal Sync Position"
  },
  { source: S_CRTC,
    reg_index: 0x06,
    reg_name: "Vertial Total"
  },
  { source: S_CRTC,
    reg_index: 0x07,
    reg_name: "CRTC Overflow"
  },
  { source: S_CRTC,
    reg_index: 0x08,
    reg_name: "Preset Row Scan"
  },
  { source: S_CRTC,
    reg_index: 0x09,
    reg_name: "Maximum Scan Line"
  },
  { source: S_CRTC,
    reg_index: 0x0a,
    reg_name: "Cursor Start Scan Line"
  },
  { source: S_CRTC,
    reg_index: 0x0b,
    reg_name: "Cursor End Scan Line"
  },
  { source: S_CRTC,
    reg_index: 0x0c,
    reg_name: "Start Address High"
  },
  { source: S_CRTC,
    reg_index: 0x0d,
    reg_name: "Start Address Low"
  },
  { source: S_CRTC,
    reg_index: 0x0e,
    reg_name: "Cursor Location Address High"
  },
  { source: S_CRTC,
    reg_index: 0x0f,
    reg_name: "Cursor Location Address Low"
  },
  { source: S_CRTC,
    reg_index: 0x10,
    reg_name: "Vertical Retrace Start"
  },
  { source: S_CRTC,
    reg_index: 0x11,
    reg_name: "Vertical Retrace End"
  },
  { source: S_CRTC,
    reg_index: 0x12,
    reg_name: "Vertical Display End"
  },
  { source: S_CRTC,
    reg_index: 0x13,
    reg_name: "Offset - Line Stride in bytes"
  },
  { source: S_CRTC,
    reg_index: 0x14,
    reg_name: "Underline Location"
  },
  { source: S_CRTC,
    reg_index: 0x15,
    reg_name: "Start Vertical Blank"
  },
  { source: S_CRTC,
    reg_index: 0x16,
    reg_name: "End Vertical Blank"
  },
  { source: S_CRTC,
    reg_index: 0x17,
    reg_name: "Mode Control - CGA Emulation"
  },
  { source: S_CRTC,
    reg_index: 0x18,
    reg_name: "Line Compare"
  },
  { source: S_CRTC,
    reg_index: 0x22,
    reg_name: "CPU Latch Data"
  },
  { source: S_CRTC,
    reg_index: 0x24,
    reg_name: "Attribute Controller Flag"
  },
  { source: S_CRTC,
    reg_index: 0x26,
    reg_name: "Attribute Controller Index"
  },
  { source: S_PRIMARY,
    reg_index: 0x0e,
    reg_name: "Graphics Controller Index"
  },
  { source: S_PRIMARY,
    reg_index: 0x0f,
    reg_name: "Graphics Controller Data"
  },
  { source: S_GR,
    reg_index: 0x00,
    reg_name: "Set/Reset"
  },
  { source: S_GR,
    reg_index: 0x01,
    reg_name: "Enable Set/Reset"
  },
  { source: S_GR,
    reg_index: 0x02,
    reg_name: "Color Compare"
  },
  { source: S_GR,
    reg_index: 0x03,
    reg_name: "Raster Operation/Rotate Counter"
  },
  { source: S_GR,
    reg_index: 0x04,
    reg_name: "Read Plane Select"
  },
  { source: S_GR,
    reg_index: 0x05,
    reg_name: "Graphics Controller Mode"
  },
  { source: S_GR,
    reg_index: 0x06,
    reg_name: "Memory Map Mode Control"
  },
  { source: S_GR,
    reg_index: 0x07,
    reg_name: "Color Don't Care"
  },
  { source: S_GR,
    reg_index: 0x09,
    reg_name: "Bit Mask"
  },
  { source: S_ATT,
    reg_index: 0x00,
    reg_name: "Palette Register 0"
  },
  { source: S_ATT,
    reg_index: 0x01,
    reg_name: "Palette Register 1"
  },
  { source: S_ATT,
    reg_index: 0x02,
    reg_name: "Palette Register 2"
  },
  { source: S_ATT,
    reg_index: 0x03,
    reg_name: "Palette Register 3"
  },
  { source: S_ATT,
    reg_index: 0x04,
    reg_name: "Palette Register 4"
  },
  { source: S_ATT,
    reg_index: 0x05,
    reg_name: "Palette Register 5"
  },
  { source: S_ATT,
    reg_index: 0x06,
    reg_name: "Palette Register 6"
  },
  { source: S_ATT,
    reg_index: 0x07,
    reg_name: "Palette Register 7"
  },
  { source: S_ATT,
    reg_index: 0x08,
    reg_name: "Palette Register 8"
  },
  { source: S_ATT,
    reg_index: 0x09,
    reg_name: "Palette Register 9"
  },
  { source: S_ATT,
    reg_index: 0x0a,
    reg_name: "Palette Register a"
  },
  { source: S_ATT,
    reg_index: 0x0b,
    reg_name: "Palette Register b"
  },
  { source: S_ATT,
    reg_index: 0x0c,
    reg_name: "Palette Register c"
  },
  { source: S_ATT,
    reg_index: 0x0d,
    reg_name: "Palette Register d"
  },
  { source: S_ATT,
    reg_index: 0x0e,
    reg_name: "Palette Register e"
  },
  { source: S_ATT,
    reg_index: 0x0f,
    reg_name: "Palette Register f"
  },
  { source: S_ATT,
    reg_index: 0x10,
    reg_name: "Attribute Mode Control"
  },
  { source: S_ATT,
    reg_index: 0x11,
    reg_name: "Border Control"
  },
  { source: S_ATT,
    reg_index: 0x12,
    reg_name: "Color Plane Enabled"
  },
  { source: S_ATT,
    reg_index: 0x13,
    reg_name: "Horizontal Pixel Panning"
  },
  { source: S_ATT,
    reg_index: 0x14,
    reg_name: "Pixel Padding"
  },
  { source: S_PRIMARY,
    reg_index: 0x06,
    reg_name: "DAC Mask"
  },
  { source: S_PRIMARY,
    reg_index: 0x07,
    reg_name: "DAC Read Index"
  },
  { source: S_PRIMARY,
    reg_index: 0x08,
    reg_name: "DAC Write Index"
  },
  { source: S_PRIMARY,
    reg_index: 0x09,
    reg_name: "DAC Data"
  },
  { source: S_SEQ,
    reg_index: 0x08,
    reg_name: "Extended Sequencer Lock"
  },
  { source: S_SEQ,
    reg_index: 0x09,
    reg_name: "Extended Sequencer 9"
  },
  { source: S_SEQ,
    reg_index: 0x0a,
    reg_name: "Extended Sequencer A"
  },
  { source: S_SEQ,
    reg_index: 0x0d,
    reg_name: "Extended Sequencer D"
  },
  { source: S_SEQ,
    reg_index: 0x10,
    reg_name: "MCLK Value Low"
  },
  { source: S_SEQ,
    reg_index: 0x11,
    reg_name: "MCLK Value High"
  },
  { source: S_SEQ,
    reg_index: 0x12,
    reg_name: "DCLK Value Low"
  },
  { source: S_SEQ,
    reg_index: 0x13,
    reg_name: "DCLK Value High"
  },
  { source: S_SEQ,
    reg_index: 0x14,
    reg_name: "CLKSYN Control 1"
  },
  { source: S_SEQ,
    reg_index: 0x15,
    reg_name: "CLKSYN Control 2"
  },
  { source: S_SEQ,
    reg_index: 0x16,
    reg_name: "CLKSYN Test High SR16"
  },
  { source: S_SEQ,
    reg_index: 0x17,
    reg_name: "CLKSYN Test High SR17"
  },
  { source: S_SEQ,
    reg_index: 0x18,
    reg_name: "RAMDAC/CLKSYN Control SR18"
  },
  { source: S_SEQ,
    reg_index: 0x1a,
    reg_name: "Extended Sequencer 1a"
  },
  { source: S_SEQ,
    reg_index: 0x1b,
    reg_name: "Extended Sequencer 1b"
  },
  { source: S_SEQ,
    reg_index: 0x1c,
    reg_name: "Extended Sequencer 1c"
  },
  { source: S_SEQ,
    reg_index: 0x1d,
    reg_name: "Extended Sequencer 1d"
  },
  { source: S_SEQ,
    reg_index: 0x1e,
    reg_name: "Extended Sequencer 1e"
  },
  { source: S_SEQ,
    reg_index: 0x1f,
    reg_name: "Extended Sequencer 1f"
  },
  { source: S_SEQ,
    reg_index: 0x20,
    reg_name: "Extended Sequencer 20"
  },
  { source: S_SEQ,
    reg_index: 0x21,
    reg_name: "Extended Sequencer 21"
  },
  { source: S_SEQ,
    reg_index: 0x22,
    reg_name: "DCLK0 Value Low"
  },
  { source: S_SEQ,
    reg_index: 0x23,
    reg_name: "DCLK0 Value High"
  },
  { source: S_SEQ,
    reg_index: 0x24,
    reg_name: "DCLK1 Value Low"
  },
  { source: S_SEQ,
    reg_index: 0x25,
    reg_name: "DCLK1 Value High"
  },
  { source: S_SEQ,
    reg_index: 0x26,
    reg_name: "Paired Register Read/Write Select",
    assert_fail_if_used: ON_RW
  },
  { source: S_SEQ,
    reg_index: 0x27,
    reg_name: "MCLK Control"
  },
  { source: S_SEQ,
    reg_index: 0x28,
    reg_name: "DCLK Control"
  },
  { source: S_SEQ,
    reg_index: 0x29,
    reg_name: "Flat Panel Frame Buffer"
  },
  { source: S_SEQ,
    reg_index: 0x30,
    reg_name: "Architectural Configuration"
  },
  { source: S_SEQ,
    reg_index: 0x31,
    reg_name: "Flat Panel Display Mode"
  },
  { source: S_SEQ,
    reg_index: 0x32,
    reg_name: "Flat Panel Polarity Control"
  },
  { source: S_SEQ,
    reg_index: 0x34,
    reg_name: "Flat Panel AC Modulation"
  },
  { source: S_SEQ,
    reg_index: 0x35,
    reg_name: "Flat Panel Modulation Clock Select"
  },
  { source: S_SEQ,
    reg_index: 0x36,
    reg_name: "Flat Panel Dither Control"
  },
  { source: S_SEQ,
    reg_index: 0x37,
    reg_name: "Flat Panel FRC Weight Select RAM Index"
  },
  { source: S_SEQ,
    reg_index: 0x38,
    reg_name: "Flat Panel FRC Weight Select RAM Data"
  },
  { source: S_SEQ,
    reg_index: 0x39,
    reg_name: "Flat Panel Algorithm Control"
  },
  { source: S_SEQ,
    reg_index: 0x3a,
    reg_name: "Flat Panel FRC Tuning 1"
  },
  { source: S_SEQ,
    reg_index: 0x3b,
    reg_name: "Flat Panel FRC Tuning 2"
  },
  { source: S_SEQ,
    reg_index: 0x3c,
    reg_name: "Flat Panel FRC Tuning 3"
  },
  { source: S_SEQ,
    reg_index: 0x3d,
    reg_name: "Flat Panel Configuraiton 1"
  },
  { source: S_SEQ,
    reg_index: 0x40,
    reg_name: "Flat Panel Dither Control"
  },
  { source: S_SEQ,
    reg_index: 0x41,
    reg_name: "Flat Panel Power Sequence Control"
  },
  { source: S_SEQ,
    reg_index: 0x42,
    reg_name: "Flat Panel Power Management Control"
  },
  { source: S_SEQ,
    reg_index: 0x43,
    reg_name: "Flat Panel Standby Control"
  },
  { source: S_SEQ,
    reg_index: 0x44,
    reg_name: "Flat Panel Power Management"
  },
  { source: S_SEQ,
    reg_index: 0x45,
    reg_name: "Flat Panel PLL Power Management"
  },
  { source: S_SEQ,
    reg_index: 0x46,
    reg_name: "Flat Panel Power Management Status"
  },
  { source: S_SEQ,
    reg_index: 0x47,
    reg_name: "CLUT Control"
  },
  { source: S_SEQ,
    reg_index: 0x48,
    reg_name: "Icon Mode"
  },
  { source: S_SEQ,
    reg_index: 0x49,
    reg_name: "Icon Color Stack"
  },
  { source: S_SEQ,
    reg_index: 0x4a,
    reg_name: "Icon X Position High"
  },
  { source: S_SEQ,
    reg_index: 0x4b,
    reg_name: "Icon X Position Low"
  },
  { source: S_SEQ,
    reg_index: 0x4c,
    reg_name: "Icon Y Position High"
  },
  { source: S_SEQ,
    reg_index: 0x4d,
    reg_name: "Icon Y Position Low"
  },
  { source: S_SEQ,
    reg_index: 0x4e,
    reg_name: "Icon Address"
  },
  { source: S_SEQ,
    reg_index: 0x4f,
    reg_name: "Dual Scan STN Data Address"
  },
  { source: S_SEQ,
    reg_index: 0x50,
    reg_name: "Dual Scan STN Frame Buffer Size Low"
  },
  { source: S_SEQ,
    reg_index: 0x51,
    reg_name: "Dual Scan STN Frame Buffer Size High"
  },
  { source: S_SEQ,
    reg_index: 0x52,
    reg_name: "Flat Panel PWM Register"
  },
  { source: S_SEQ,
    reg_index: 0x53,
    reg_name: "Flat Panel PWM Duty Cycle"
  },
  { source: S_SEQ,
    reg_index: 0x54,
    reg_name: "Flat Panel Horizontal Compensation 1 SR54"
  },
  { source: S_SEQ,
    reg_index: 0x55,
    reg_name: "Flat Panel Horizontal Compensation 2 SR55"
  },
  { source: S_SEQ,
    reg_index: 0x56,
    reg_name: "Flat Panel Horizontal Compensation 1 SR56"
  },
  { source: S_SEQ,
    reg_index: 0x57,
    reg_name: "Flat Panel Vertical Compensation 1"
  },
  { source: S_SEQ,
    reg_index: 0x58,
    reg_name: "Flat Panel Horizontal Border"
  },
  { source: S_SEQ,
    reg_index: 0x59,
    reg_name: "Flat Panel Horizontal Expansion Factor SR59"
  },
  { source: S_SEQ,
    reg_index: 0x5a,
    reg_name: "Flat Panel Vertical Border"
  },
  { source: S_SEQ,
    reg_index: 0x5b,
    reg_name: "Flat Panel Horizontal Expansion Factor SR5b"
  },
  { source: S_SEQ,
    reg_index: 0x5c,
    reg_name: "Flat Panel Display Enable Position Control"
  },
  { source: S_SEQ,
    reg_index: 0x5d,
    reg_name: "Flat Panel/CRT Sync Position Control"
  },
  { source: S_SEQ,
    reg_index: 0x5f,
    reg_name: "FIFO Control SR5f"
  },
  { source: S_SEQ,
    reg_index: 0x60,
    reg_name: "Flat Panel Horizontal Total"
  },
  { source: S_SEQ,
    reg_index: 0x61,
    reg_name: "Flat Panel Horizontal Panel Size"
  },
  { source: S_SEQ,
    reg_index: 0x62,
    reg_name: "Flat Panel Horizontal Blank Start"
  },
  { source: S_SEQ,
    reg_index: 0x63,
    reg_name: "Flat Panel Horizontal Blank End"
  },
  { source: S_SEQ,
    reg_index: 0x64,
    reg_name: "Flat Panel Horizontal Sync Start"
  },
  { source: S_SEQ,
    reg_index: 0x65,
    reg_name: "Flat Panel Horizontal Sync End"
  },
  { source: S_SEQ,
    reg_index: 0x66,
    reg_name: "Flat Panel Horizontal Overflow"
  },
  { source: S_SEQ,
    reg_index: 0x68,
    reg_name: "Flat Panel Vertical Total"
  },
  { source: S_SEQ,
    reg_index: 0x69,
    reg_name: "Flat Panel Vertical Panel Size"
  },
  { source: S_SEQ,
    reg_index: 0x6a,
    reg_name: "Flat Panel Vertical Blank Start"
  },
  { source: S_SEQ,
    reg_index: 0x6b,
    reg_name: "Flat Panel Vertical Blank End"
  },
  { source: S_SEQ,
    reg_index: 0x6c,
    reg_name: "Flat Panel Vertical Sync Start"
  },
  { source: S_SEQ,
    reg_index: 0x6d,
    reg_name: "Flat Panel Vertical Sync End"
  },
  { source: S_SEQ,
    reg_index: 0x6e,
    reg_name: "Flat Panel Vertical Overflow 1"
  },
  { source: S_SEQ,
    reg_index: 0x6f,
    reg_name: "Flat Panel Vertical Overflow 2"
  },
  { source: S_CRTC,
    reg_index: 0x22,
    reg_name: "CRT Test 1"
  },
  { source: S_CRTC,
    reg_index: 0x2d,
    reg_name: "Device ID High"
  },
  { source: S_CRTC,
    reg_index: 0x2e,
    reg_name: "Device ID Low"
  },
  { source: S_CRTC,
    reg_index: 0x2f,
    reg_name: "Revision"
  },
  { source: S_CRTC,
    reg_index: 0x30,
    reg_name: "Chip ID/Rev"
  },
  { source: S_CRTC,
    reg_index: 0x31,
    reg_name: "Memory Configuration",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x32,
    reg_name: "Backward Compatibility 1"
  },
  { source: S_CRTC,
    reg_index: 0x33,
    reg_name: "Backward Compatibiltiy 2"
  },
  { source: S_CRTC,
    reg_index: 0x34,
    reg_name: "Backward Compatibility 3"
  },
  { source: S_CRTC,
    reg_index: 0x35,
    reg_name: "CRT Register Lock"
  },
  { source: S_CRTC,
    reg_index: 0x36,
    reg_name: "Configuration 1",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x37,
    reg_name: "Configuration 2",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x38,
    reg_name: "Register Lock 1"
  },
  { source: S_CRTC,
    reg_index: 0x39,
    reg_name: "Register Lock 2"
  },
  { source: S_CRTC,
    reg_index: 0x3a,
    reg_name: "Misc. 1 CR3a",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x3b,
    reg_name: "Start Display FIFO Fetch",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x3c,
    reg_name: "Interlace Retrace Start"
  },
  { source: S_CRTC,
    reg_index: 0x3d,
    reg_name: "NTSC/PAL Control"
  },
  { source: S_CRTC,
    reg_index: 0x40,
    reg_name: "System Configuration",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x41,
    reg_name: "BIOS Flag 1"
  },
  { source: S_CRTC,
    reg_index: 0x42,
    reg_name: "Mode Control CR42"
  },
  { source: S_CRTC,
    reg_index: 0x43,
    reg_name: "Extended Mode CR43"
  },
  { source: S_CRTC,
    reg_index: 0x45,
    reg_name: "Hardware Graphics Cursor Mode"
  },
  { source: S_CRTC,
    reg_index: 0x46,
    reg_name: "Hardware Graphics Cursor Origin X Low"
  },
  { source: S_CRTC,
    reg_index: 0x47,
    reg_name: "Hardware Graphics Cursor Origin X High"
  },
  { source: S_CRTC,
    reg_index: 0x48,
    reg_name: "Hardware Graphics Cursor Origin Y Low"
  },
  { source: S_CRTC,
    reg_index: 0x48,
    reg_name: "Hardware Graphics Cursor Origin Y High"
  },
  { source: S_CRTC,
    reg_index: 0x4a,
    reg_name: "Hardware Graphics Cursor Foreground Stack"
  },
  { source: S_CRTC,
    reg_index: 0x4b,
    reg_name: "Hardware Graphics Cursor Background Stack"
  },
  { source: S_CRTC,
    reg_index: 0x4c,
    reg_name: "Hardware Graphics Cursor Start Address Low"
  },
  { source: S_CRTC,
    reg_index: 0x4d,
    reg_name: "Hardware Graphics Cursor Start Address High"
  },
  { source: S_CRTC,
    reg_index: 0x4e,
    reg_name: "Hardware Graphics Cursor Pattern Display Start X Pixel Position"
  },
  { source: S_CRTC,
    reg_index: 0x4f,
    reg_name: "Hardware Graphics Cursor Pattern Display Start Y Pixel Position"
  },
  { source: S_CRTC,
    reg_index: 0x50,
    reg_name: "Extended System Control 1"
  },
  { source: S_CRTC,
    reg_index: 0x51,
    reg_name: "Extended System Control 2"
  },
  { source: S_CRTC,
    reg_index: 0x52,
    reg_name: "Extended BIOS Flag 1"
  },
  { source: S_CRTC,
    reg_index: 0x53,
    reg_name: "Extended Memory Control 1",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x54,
    reg_name: "Extended Memory Control 2",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x55,
    reg_name: "Extended DAC Control"
  },
  { source: S_CRTC,
    reg_index: 0x56,
    reg_name: "External Sync Control 1"
  },
  { source: S_CRTC,
    reg_index: 0x58,
    reg_name: "Linear Address Window Control",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x59,
    reg_name: "Linear Address Window Position High"
  },
  { source: S_CRTC,
    reg_index: 0x5a,
    reg_name: "Linear Address Window Position Low"
  },
  { source: S_CRTC,
    reg_index: 0x5c,
    reg_name: "General Output Port"
  },
  { source: S_CRTC,
    reg_index: 0x5d,
    reg_name: "Extended Horizontal Overflow CR5D"
  },
  { source: S_CRTC,
    reg_index: 0x5e,
    reg_name: "Extended Vertical Ovreflow CR5E"
  },
  { source: S_CRTC,
    reg_index: 0x60,
    reg_name: "Extended Memory Control 3",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x61,
    reg_name: "Extended Memory Control 4",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x63,
    reg_name: "Horizontal Timing Control CR63"
  },
  { source: S_CRTC,
    reg_index: 0x65,
    reg_name: "Extended Miscellaneous Control CR65"
  },
  { source: S_CRTC,
    reg_index: 0x66,
    reg_name: "Extended Miscellaneous Control 1 CR66"
  },
  { source: S_CRTC,
    reg_index: 0x67,
    reg_name: "Extended Miscellaneous Control 2 CR67"
  },
  { source: S_CRTC,
    reg_index: 0x68,
    reg_name: "Configuration 3 CR68"
  },
  { source: S_CRTC,
    reg_index: 0x69,
    reg_name: "Extended System Control 3 CR69"
  },
  { source: S_CRTC,
    reg_index: 0x6a,
    reg_name: "Extended System Control 4 CR6a"
  },
  { source: S_CRTC,
    reg_index: 0x6b,
    reg_name: "Extended BIOS Flag 3"
  },
  { source: S_CRTC,
    reg_index: 0x6c,
    reg_name: "Extended BIOS Flag 4"
  },
  { source: S_CRTC,
    reg_index: 0x6d,
    reg_name: "Extended BIOS Flag 5"
  },
  { source: S_CRTC,
    reg_index: 0x6e,
    reg_name: "Extended BIOS Flag 6"
  },
  { source: S_CRTC,
    reg_index: 0x6f,
    reg_name: "Configuration 4 CR6f"
  },
  { source: S_CRTC,
    reg_index: 0x71,
    reg_name: "Extended Miscellaneous Control 2 CR71"
  },
  { source: S_CRTC,
    reg_index: 0x72,
    reg_name: "Extended Memory Control 5",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x73,
    reg_name: "Extended Memory Control 6",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x74,
    reg_name: "Extended Memory Control 7",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x75,
    reg_name: "Extended Memory Control 8",
    assert_fail_if_used: ON_RW
  },
  { source: S_CRTC,
    reg_index: 0x76,
    reg_name: "Extended Memory Control 9",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x42e8,
    reg_name: "Subsystem Status",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x4ae8,
    reg_name: "Advanced Function Control",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x82e8,
    reg_name: "Current Y Position",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x86e8,
    reg_name: "Current X Position",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x8ae8,
    reg_name: "Destination Y Position"
  },
  { source: S_EXTENDED,
    reg_index: 0x8ee8,
    reg_name: "Destination X Position"
  },
  { source: S_EXTENDED,
    reg_index: 0x92e8,
    reg_name: "Line Error Term",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x96e8,
    reg_name: "Major Axis Pixel Count",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x9ae8,
    reg_name: "Graphics Processor Status",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x9ee8,
    reg_name: "Short Stroke Transfer Vector"
  },
  { source: S_EXTENDED,
    reg_index: 0xa2e8,
    reg_name: "Background Color"
  },
  { source: S_EXTENDED,
    reg_index: 0xa6e8,
    reg_name: "Foreground Color"
  },
  { source: S_EXTENDED,
    reg_index: 0xaae8,
    reg_name: "Bitplane Write Mask"
  },
  { source: S_EXTENDED,
    reg_index: 0xaee8,
    reg_name: "Bitplane Read Mask"
  },
  { source: S_EXTENDED,
    reg_index: 0xb2e8,
    reg_name: "Color Compare"
  },
  { source: S_EXTENDED,
    reg_index: 0xb6e8,
    reg_name: "Background Mix"
  },
  { source: S_EXTENDED,
    reg_index: 0xbae8,
    reg_name: "Foreground Mix"
  },
  { source: S_EXTENDED,
    reg_index: 0xbee8,
    reg_name: "Read Register Data",
    assert_fail_if_used: ON_RW
  },
  { source: S_BEE8,
    reg_index: 0,
    reg_name: "Minor Axis Pixel Count"
  },
  { source: S_BEE8,
    reg_index: 1,
    reg_name: "Top Scissors"
  },
  { source: S_BEE8,
    reg_index: 2,
    reg_name: "Left Scissors"
  },
  { source: S_BEE8,
    reg_index: 3,
    reg_name: "Bottom Scissors"
  },
  { source: S_BEE8,
    reg_index: 4,
    reg_name: "Right Scissors"
  },
  { source: S_BEE8,
    reg_index: 0x0a,
    reg_name: "Pixel Control"
  },
  { source: S_BEE8,
    reg_index: 0x0d,
    reg_name: "Multifunction Control Miscellaneous 2",
    assert_fail_if_used: ON_RW
  },
  { source: S_BEE8,
    reg_index: 0x0e,
    reg_name: "Multifunction Control Miscellaneous",
    assert_fail_if_used: ON_RW
  },
  { source: S_BEE8,
    reg_index: 0x0f,
    reg_name: "Read Register Select"
  },
  { source: S_EXTENDED,
    reg_index: 0xe2e8,
    reg_name: "Pixel Data Transfer"
  },
  { source: S_EXTENDED,
    reg_index: 0xe2ea,
    reg_name: "Pixel Data Transfer Extended"
  },
  { source: S_EXTENDED,
    reg_index: 0x8180,
    reg_name: "Primary Stream Control",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x8184,
    reg_name: "Color/Chroma Key Control",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x8190,
    reg_name: "Secondary Stream Control",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x8194,
    reg_name: "Chroma Key Upper Bound",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x8198,
    reg_name: "Secondary Stream Stretch/Filter Constants",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81a0,
    reg_name: "Blend Control",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81c0,
    reg_name: "Primary Stream Frame Buffer Address 0",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81c4,
    reg_name: "Primary Stream Frame Buffer Address 1",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81c8,
    reg_name: "Primary Stream Stride",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81cc,
    reg_name: "Double Buffer/LPB Support",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81d0,
    reg_name: "Secondary Stream Frame Buffer Address 0",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81d4,
    reg_name: "Secondary Stream Frame Buffer Address 1",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81d8,
    reg_name: "Secondary Stream Stride",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81dc,
    reg_name: "Blend Control",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81e0,
    reg_name: "K1 Vertical Scale Factor",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81e4,
    reg_name: "K2 Vertical Scale Factor",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81e8,
    reg_name: "DDA Vertical Accumulator Initial Value",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81ec,
    reg_name: "Streams FIFO and RAS Controls",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81f0,
    reg_name: "Primary Stream Window Start Coordinates",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81f4,
    reg_name: "Primary Stream Widnow Size",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81f8,
    reg_name: "Secondary Stream Window Start Coordinates",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0x81fc,
    reg_name: "Secondary Stream Window Size",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0xff00,
    reg_name: "LPB Mode",
    assert_fail_if_used: ON_RW,
  },
  { source: S_EXTENDED,
    reg_index: 0xff04,
    reg_name: "LPB FIFO Status",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0xff08,
    reg_name: "LPB Interrupt Flags",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0xff0c,
    reg_name: "LPB Frame Buffer Address 0",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0xff10,
    reg_name: "LPB Frame Buffer Address 1",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0xff14,
    reg_name: "LPB Direct Read/Write Address",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0xff18,
    reg_name: "LPB Direct Read/Write Data",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0xff1c,
    reg_name: "LPB General Purpose Input/Output Port",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0xff20,
    reg_name: "Serial Port",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0xff24,
    reg_name: "LPB Video Input Window Size",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0xff28,
    reg_name: "LPB Video Data Offsets",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0xff2c,
    reg_name: "LPB Horizontal Decimation Control",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0xff30,
    reg_name: "LPB Vertical Decimation Control",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0xff34,
    reg_name: "LPB Line Stride",
    assert_fail_if_used: ON_RW
  },
  { source: S_EXTENDED,
    reg_index: 0xff40,
    reg_name: "LPB Output FIFO",
    assert_fail_if_used: ON_RW
  },
  { source: 0,
    reg_index: 0,
    reg_name: nullptr,
    assert_fail_if_used: 0,
  }
};

#define VGA_DEBUG 0

#if VGA_DEBUG
#define L(x) do { x; } while (0)
#else
#define L(x) do { } while(0)
#endif

#define G(x) do { if (d->window_mapped) { x; } } while (0)

/*  For videomem -> framebuffer updates:  */
#define	VGA_TICK_SHIFT		18

#define	MAX_RETRACE_SCANLINES	420
#define	N_IS1_READ_THRESHOLD	50

#define	GFX_ADDR_WINDOW		(32 * 1024 * 1024)

#define	VGA_FB_ADDR	0x1c00000000ULL

#define	MODE_CHARCELL		1
#define	MODE_GRAPHICS		2

#define	GRAPHICS_MODE_8BIT	1
#define	GRAPHICS_MODE_4BIT	2

#define S3_PIO_SEQ_LEN 11

struct vga_data {
	uint64_t	videomem_base;
	uint64_t	control_base;

	struct vfb_data *fb;
	uint32_t	fb_size;

	int		fb_max_x;		/*  pixels  */
	int		fb_max_y;		/*  pixels  */
	int		max_x;			/*  charcells or pixels  */
	int		max_y;			/*  charcells or pixels  */

	/*  Selects charcell mode or graphics mode:  */
	int		cur_mode;

	/*  Common for text and graphics modes:  */
	int		pixel_repx, pixel_repy;

	/*  Textmode:  */
	int		font_width;
	int		font_height;
	unsigned char	*font;
	size_t		charcells_size;
	unsigned char	*charcells;		/*  2 bytes per char  */
	unsigned char	*charcells_outputed;	/*  text  */
	unsigned char	*charcells_drawn;	/*  framebuffer  */

	/*  Graphics:  */
	int		graphics_mode;
	int		bits_per_pixel;
	unsigned char	*gfx_mem;
	uint32_t	gfx_mem_size;

	/*  Registers:  */
	int		attribute_state;	/*  0 or 1  */
	unsigned char	attribute_reg_select;
	unsigned char	attribute_reg[256];

	unsigned char	misc_output_reg;

	unsigned char	sequencer_reg_select;
	unsigned char	sequencer_reg[256];

	unsigned char	graphcontr_reg_select;
	unsigned char	graphcontr_reg[256];

	unsigned char	crtc_reg_select;
	unsigned char	crtc_reg[256];

	unsigned char	palette_read_index;
	char		palette_read_subindex;
	unsigned char	palette_write_index;
	char		palette_write_subindex;

	int		current_retrace_line;
	int		input_status_1;

	/*  Palette per scanline during retrace:  */
	unsigned char	*retrace_palette;
	int		use_palette_per_line;
	int64_t		n_is1_reads;

	/*  Misc.:  */
	int		console_handle;

	int		cursor_x;
	int		cursor_y;

	int		modified;
	int		palette_modified;
	int		update_x1;
	int		update_y1;
	int		update_x2;
	int		update_y2;

	/* S3 */
  int   s3_pio_select;
	int   s3_cur_x, s3_cur_y, s3_pix_x, s3_pix_y, s3_draw_width;
  int   s3_fg_color, s3_bg_color;
  int   s3_fg_color_mix, s3_bg_color_mix;
  int   s3_v_dir, s3_h_dir;
  bool  s3_y_major, s3_last_pof, s3_no_draw;
  int   s3_rem_height;
  int   s3_destx, s3_desty;
  int   s3_current_command;
  uint32_t s3_color_compare;

  /* BEE8H */
  uint16_t bee8_regs[16];

  /* Command */
  int s3_cmd_mx, s3_cmd_bus_size, s3_cmd_swap, s3_cmd_pxtrans;

  /* Ext sequencer */
  bool ext_seq_unlock;

  /* Mapping */
  bool window_mapped;
  int window_address;

  uint32_t plane_read_mask, plane_write_mask;
  uint32_t adv_fun_4ae8, line_error_term, short_stroke_transfer;

  uint8_t reg_ff00_data[0x100];
};

inline uint16_t get_le_16(bool already, uint64_t idata) {
  if (already) {
    return idata;
  } else {
    return ((idata >> 8) & 0xff) | ((idata << 8) & 0xff00);
  }
}


inline uint32_t get_le_32(bool already, uint64_t idata) {
  if (already) {
    return idata;
  } else {
    return get_le_16(false, idata >> 16) | (get_le_16(false, idata) << 16);
  }
}

const char *vga_find_register_prim(int source, int id) {
  for (int i = 0; r_name_table[i].reg_name; i++) {
    if (r_name_table[i].source == source && r_name_table[i].reg_index == id) {
      return r_name_table[i].reg_name;
    }
  }

  return nullptr;
}

char vga_find_register_name_buf[100];
const char *vga_find_register_name(struct vga_data *d, int source, int relative_addr) {
  const char *result = nullptr;
  if (source == S_PRIMARY && relative_addr == 0x01) {
    result = vga_find_register_prim(S_ATT, d->attribute_reg_select);
    if (result) {
      return result;
    }
  }
  if (source == S_PRIMARY && relative_addr == 0x05) {
    if ((d->sequencer_reg[8] & 15) == 6) {
      result = vga_find_register_prim(S_SEQ2, d->sequencer_reg_select);
    }
    if (!result) {
      result = vga_find_register_prim(S_SEQ, d->sequencer_reg_select);
    }

    if (result) {
      return result;
    }
  }
  if (source == S_PRIMARY && relative_addr == 0x0f) {
    result = vga_find_register_prim(S_GR, d->graphcontr_reg_select);
    if (result) {
      return result;
    }
  }

  if (source == S_PRIMARY && relative_addr == 0x15) {
    if (((d->crtc_reg[0x38] & 0xcc) == 0x48) && (d->crtc_reg[0x39] & 1)) {
      result = vga_find_register_prim(S_CRT2, d->crtc_reg_select);
    }
    if (!result) {
      result = vga_find_register_prim(S_CRTC, d->crtc_reg_select);
    }

    if (result) {
      return result;
    }
  }

  result = vga_find_register_prim(S_EXTENDED, relative_addr);
  if (result) {
    return result;
  }

  result = vga_find_register_prim(S_PRIMARY, relative_addr);
  if (result) {
    return result;
  }

  sprintf(vga_find_register_name_buf, "vga unknown register %s %x", group_names[source], relative_addr);
  return vga_find_register_name_buf;
}

/*
 *  recalc_cursor_position():
 *
 *  Should be called whenever the cursor location _or_ the display
 *  base has been changed.
 */
static void recalc_cursor_position(struct vga_data *d)
{
	int base = (d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] << 8)
	    + d->crtc_reg[VGA_CRTC_START_ADDR_LOW];
	int ofs = d->crtc_reg[VGA_CRTC_CURSOR_LOCATION_HIGH] * 256 +
	    d->crtc_reg[VGA_CRTC_CURSOR_LOCATION_LOW];
	ofs -= base;
	d->cursor_x = ofs % d->max_x;
	d->cursor_y = ofs / d->max_x;
}


/*
 *  register_reset():
 *
 *  Resets many registers to sane values.
 */
static void register_reset(struct vga_data *d)
{
	/*  Home cursor and start at the top:  */
	d->crtc_reg[VGA_CRTC_CURSOR_LOCATION_HIGH] =
	    d->crtc_reg[VGA_CRTC_CURSOR_LOCATION_LOW] = 0;
	d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] =
	    d->crtc_reg[VGA_CRTC_START_ADDR_LOW] = 0;

	recalc_cursor_position(d);

	/*  Reset cursor scanline stuff:  */
	d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START] = d->font_height - 2;
	d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_END] = d->font_height - 1;

	d->sequencer_reg[VGA_SEQ_MAP_MASK] = 0x0f;
  d->sequencer_reg[0x61] = 0x63;
  d->sequencer_reg[0x66] = 0;
  d->sequencer_reg[0x69] = 0x57;
  d->sequencer_reg[0x6e] = 0x22;
	d->graphcontr_reg[VGA_GRAPHCONTR_MASK] = 0xff;

	d->misc_output_reg = VGA_MISC_OUTPUT_IOAS;
	d->n_is1_reads = 0;

  d->crtc_reg[0x13] = 0x50;
  d->crtc_reg[0x51] = 0;
  d->crtc_reg[0x30] = 0xe1;
  d->crtc_reg[0x2d] = 0x88;
  d->crtc_reg[0x2e] = 0x12;
  // Revision 0x40
  d->crtc_reg[0x2f] = 0x40;
  d->crtc_reg[0x36] = 4 << 5; // 2mb
}


static void c_putstr(struct vga_data *d, const char *s)
{
	while (*s)
		console_putchar(d->console_handle, *s++);
}


/*
 *  reset_palette():
 */
static void reset_palette(struct vga_data *d, int grayscale)
{
	int i, r, g, b;

	/*  TODO: default values for entry 16..255?  */
	for (i=16; i<256; i++)
		d->fb->rgb_palette[i*3 + 0] = d->fb->rgb_palette[i*3 + 1] =
		    d->fb->rgb_palette[i*3 + 2] = (i & 15) * 4;
	d->palette_modified = 1;
	i = 0;

	if (grayscale) {
		for (r=0; r<2; r++)
		    for (g=0; g<2; g++)
			for (b=0; b<2; b++) {
				d->fb->rgb_palette[i + 0] =
				    d->fb->rgb_palette[i + 1] =
				    d->fb->rgb_palette[i + 2] =
				    (r+g+b) * 0xaa / 3;
				d->fb->rgb_palette[i + 8*3 + 0] =
				    d->fb->rgb_palette[i + 8*3 + 1] =
				    d->fb->rgb_palette[i + 8*3 + 2] =
				    (r+g+b) * 0xaa / 3 + 0x55;
				i+=3;
			}
		return;
	}

	for (r=0; r<2; r++)
		for (g=0; g<2; g++)
			for (b=0; b<2; b++) {
				d->fb->rgb_palette[i + 0] = r * 0xaa;
				d->fb->rgb_palette[i + 1] = g * 0xaa;
				d->fb->rgb_palette[i + 2] = b * 0xaa;
				i+=3;
			}
	for (r=0; r<2; r++)
		for (g=0; g<2; g++)
			for (b=0; b<2; b++) {
				d->fb->rgb_palette[i + 0] = r * 0xaa + 0x55;
				d->fb->rgb_palette[i + 1] = g * 0xaa + 0x55;
				d->fb->rgb_palette[i + 2] = b * 0xaa + 0x55;
				i+=3;
			}
}


/*
 *  vga_update_textmode():
 *
 *  Called from vga_update() when x11 in_use is false. This causes modified
 *  character cells to be "simulated" by outputing ANSI escape sequences
 *  that draw the characters in a terminal window instead.
 */
static void vga_update_textmode(struct machine *machine,
	struct vga_data *d, int base, int start, int end)
{
	char s[50];
	int i, oldcolor = -1, printed_last = 0;

	for (i=start; i<=end; i+=2) {
		unsigned char ch = d->charcells[base+i];
		int fg = d->charcells[base+i+1] & 15;
		int bg = (d->charcells[base+i+1] >> 4) & 15;
			/*  top bit of bg = blink  */
		int x = (i/2) % d->max_x;
		int y = (i/2) / d->max_x;

		if (d->charcells[base+i] == d->charcells_outputed[i] &&
		    d->charcells[base+i+1] == d->charcells_outputed[i+1]) {
			printed_last = 0;
			continue;
		}

		d->charcells_outputed[i] = d->charcells[base+i];
		d->charcells_outputed[i+1] = d->charcells[base+i+1];

		if (!printed_last || x == 0) {
			snprintf(s, sizeof(s), "\033[%i;%iH", y + 1, x + 1);
			c_putstr(d, s);
		}
		if (oldcolor < 0 || (bg<<4)+fg != oldcolor || !printed_last) {
			snprintf(s, sizeof(s), "\033[0;"); c_putstr(d, s);

			switch (fg & 7) {
			case 0:	c_putstr(d, "30"); break;
			case 1:	c_putstr(d, "34"); break;
			case 2:	c_putstr(d, "32"); break;
			case 3:	c_putstr(d, "36"); break;
			case 4:	c_putstr(d, "31"); break;
			case 5:	c_putstr(d, "35"); break;
			case 6:	c_putstr(d, "33"); break;
			case 7:	c_putstr(d, "37"); break;
			}
			if (fg & 8)
				c_putstr(d, ";1");
			c_putstr(d, ";");
			switch (bg & 7) {
			case 0:	c_putstr(d, "40"); break;
			case 1:	c_putstr(d, "44"); break;
			case 2:	c_putstr(d, "42"); break;
			case 3:	c_putstr(d, "46"); break;
			case 4:	c_putstr(d, "41"); break;
			case 5:	c_putstr(d, "45"); break;
			case 6:	c_putstr(d, "43"); break;
			case 7:	c_putstr(d, "47"); break;
			}
			/*  TODO: blink  */
			c_putstr(d, "m");
		}

		if (ch >= 0x20 && ch != 127)
			console_putchar(d->console_handle, ch);

		oldcolor = (bg << 4) + fg;
		printed_last = 1;
	}

	/*  Restore the terminal's cursor position:  */
	snprintf(s, sizeof(s), "\033[%i;%iH", d->cursor_y + 1, d->cursor_x + 1);
	c_putstr(d, s);
}


/*
 *  vga_update_graphics():
 *
 *  This function should be called whenever any part of d->gfx_mem[] has
 *  been written to. It will redraw all pixels within the range x1,y1
 *  .. x2,y2 using the right palette.
 */
static void vga_update_graphics(struct machine *machine, struct vga_data *d,
	int x1, int y1, int x2, int y2)
{
	int x, y, ix, iy, c, rx = d->pixel_repx, ry = d->pixel_repy;
	unsigned char pixel[3];

  auto logical_width_high = (d->crtc_reg[0x51] >> 4) & 3;
  auto logical_width = (d->crtc_reg[0x13] + (logical_width_high << 8)) * 8;

	for (y=y1; y<=y2; y++) {
		for (x=x1; x<=x2; x++) {
			/*  addr is where to read from VGA memory, addr2 is
			    where to write on the 24-bit framebuffer device  */
			int addr = (y * logical_width + x) * d->bits_per_pixel;
      addr += (d->crtc_reg[0x51] & 0xc) << 16;
      addr += (d->crtc_reg[0x35] & 0xf) << 14;
			switch (d->bits_per_pixel) {
			case 8:	addr >>= 3;
				if (addr >= d->gfx_mem_size) {
					break;
				}
				c = d->gfx_mem[addr];
				pixel[0] = d->fb->rgb_palette[c*3+0];
				pixel[1] = d->fb->rgb_palette[c*3+1];
				pixel[2] = d->fb->rgb_palette[c*3+2];
				break;
			case 4:	addr >>= 2;
				if (addr >> 1 >= d->gfx_mem_size) {
					break;
				}
				if (addr & 1)
					c = d->gfx_mem[addr >> 1] >> 4;
				else
					c = d->gfx_mem[addr >> 1] & 0xf;
				pixel[0] = d->fb->rgb_palette[c*3+0];
				pixel[1] = d->fb->rgb_palette[c*3+1];
				pixel[2] = d->fb->rgb_palette[c*3+2];
				break;
			}

      for (iy=y*ry; iy<(y+1)*ry; iy++) {
        for (ix=x*rx; ix<(x+1)*rx; ix++) {
          uint32_t addr2 = (d->fb_max_x * iy
                            + ix) * 3;
          if (addr2 < d->fb_size)
            dev_fb_access(machine->cpus[0],
                          machine->memory, addr2,
                          pixel, sizeof(pixel),
                          MEM_WRITE, d->fb);
        }
      }
    }
  }
}


/*
 *  vga_update_text():
 *
 *  This function should be called whenever any part of d->charcells[] has
 *  been written to. It will redraw all characters within the range x1,y1
 *  .. x2,y2 using the right palette.
 */
static void vga_update_text(struct machine *machine, struct vga_data *d,
	int x1, int y1, int x2, int y2)
{
	int fg, bg, x,y, subx, line;
	size_t i, start, end, base;
	int font_size = d->font_height;
	int font_width = d->font_width;
	unsigned char *pal = d->fb->rgb_palette;

	if (d->pixel_repx * font_width > 8*8) {
		fatal("[ too large font ]\n");
		return;
	}

	/*  Hm... I'm still using the old start..end code:  */
	start = (d->max_x * y1 + x1) * 2;
	end   = (d->max_x * y2 + x2) * 2;

	start &= ~1;
	end |= 1;

	if (end >= d->charcells_size)
		end = d->charcells_size - 1;

	base = ((d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] << 8)
          + d->crtc_reg[VGA_CRTC_START_ADDR_LOW]) * 2;

	if (!machine->x11_md.in_use)
		vga_update_textmode(machine, d, base, start, end);

	for (i=start; i<=end; i+=2) {
		unsigned char ch = d->charcells[i + base];

		if (!d->palette_modified && d->charcells_drawn[i] == ch &&
		    d->charcells_drawn[i+1] == d->charcells[i+base+1])
			continue;

		d->charcells_drawn[i] = ch;
		d->charcells_drawn[i+1] = d->charcells[i + base + 1];

		fg = d->charcells[i+base + 1] & 15;
		bg = (d->charcells[i+base + 1] >> 4) & 7;

		/*  Blink is hard to do :-), but inversion might be ok too:  */
		if (d->charcells[i+base + 1] & 128) {
			int tmp = fg; fg = bg; bg = tmp;
		}

		x = (i/2) % d->max_x; x *= font_width;
		y = (i/2) / d->max_x; y *= font_size;

		/*  Draw the character:  */
		for (line = 0; line < font_size; line++) {
			/*  hardcoded for max 8 scaleup... :-)  */
			unsigned char rgb_line[3 * 8 * 8];
			int iy;

			for (subx = 0; subx < font_width; subx++) {
				int ix, color_index;

				if (d->use_palette_per_line) {
					int sline = d->pixel_repy * (line+y);
					if (sline < MAX_RETRACE_SCANLINES)
						pal = d->retrace_palette
						    + sline * 256*3;
					else
						pal = d->fb->rgb_palette;
				}

				if (d->font[ch * font_size + line] &
				    (128 >> subx))
					color_index = fg;
				else
					color_index = bg;

				for (ix=0; ix<d->pixel_repx; ix++)
					memcpy(rgb_line + (subx*d->pixel_repx +
					    ix) * 3, &pal[color_index * 3], 3);
			}

			for (iy=0; iy<d->pixel_repy; iy++) {
				uint32_t addr = (d->fb_max_x * (d->pixel_repy *
				    (line+y) + iy) + x * d->pixel_repx) * 3;
				if (addr >= d->fb_size)
					continue;
				dev_fb_access(machine->cpus[0],
				    machine->memory, addr, rgb_line,
				    3 * machine->x11_md.scaleup * font_width,
				    MEM_WRITE, d->fb);
			}
		}
	}
}


/*
 *  vga_update_cursor():
 */
static void vga_update_cursor(struct machine *machine, struct vga_data *d)
{
	int onoff = 1, height = d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_END]
	    - d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START] + 1;

	if (d->cur_mode != MODE_CHARCELL)
		onoff = 0;

	if (d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START] >
	    d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_END]) {
		onoff = 0;
		height = 1;
	}

	if (d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START] >= d->font_height)
		onoff = 0;

	dev_fb_setcursor(d->fb,
	    d->cursor_x * d->font_width * d->pixel_repx, (d->cursor_y *
	    d->font_height + d->crtc_reg[VGA_CRTC_CURSOR_SCANLINE_START]) *
	    d->pixel_repy, onoff, d->font_width * d->pixel_repx, height *
	    d->pixel_repy);
}


DEVICE_TICK(vga)
{
	struct vga_data *d = (struct vga_data *) extra;
	int64_t low = -1, high;

  auto logical_width_high = (d->crtc_reg[0x51] >> 4) & 3;
  auto logical_width = (d->crtc_reg[0x13] + (logical_width_high << 8)) * 8;

	vga_update_cursor(cpu->machine, d);

	/*  TODO: text vs graphics tick?  */
	memory_device_dyntrans_access(cpu, cpu->mem, extra,
	    (uint64_t *) &low, (uint64_t *) &high);

	if (low != -1) {
		int base = ((d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] << 8)
		    + d->crtc_reg[VGA_CRTC_START_ADDR_LOW]) * 2;
		int new_u_y1, new_u_y2;
		debug("[ dev_vga_tick: dyntrans access, %" PRIx64" .. %"
		    PRIx64" ]\n", (uint64_t) low, (uint64_t) high);
		low -= base;
		high -= base;
		d->update_x1 = 0;
		d->update_x2 = d->max_x - 1;
		new_u_y1 = (low/2) / logical_width;
		new_u_y2 = ((high/2) / logical_width) + 1;
		if (new_u_y1 < d->update_y1)
			d->update_y1 = new_u_y1;
		if (new_u_y2 > d->update_y2)
			d->update_y2 = new_u_y2;
		if (d->update_y1 < 0)
			d->update_y1 = 0;
		if (d->update_y2 >= d->max_y)
			d->update_y2 = d->max_y - 1;
		d->modified = 1;
	}

	if (d->n_is1_reads > N_IS1_READ_THRESHOLD &&
	    d->retrace_palette != NULL) {
		d->use_palette_per_line = 1;
		d->update_x1 = 0;
		d->update_x2 = d->max_x - 1;
		d->update_y1 = 0;
		d->update_y2 = d->max_y - 1;
		d->modified = 1;
	} else {
		if (d->use_palette_per_line) {
			d->use_palette_per_line = 0;
			d->update_x1 = 0;
			d->update_x2 = d->max_x - 1;
			d->update_y1 = 0;
			d->update_y2 = d->max_y - 1;
			d->modified = 1;
		}
	}

	if (!cpu->machine->x11_md.in_use) {
		/*  NOTE: 2 > 0, so this only updates the cursor, no
		    character cells.  */
		vga_update_textmode(cpu->machine, d, 0, 2, 0);
	}

	if (d->modified) {
		if (d->cur_mode == MODE_CHARCELL)
			vga_update_text(cpu->machine, d, d->update_x1,
			    d->update_y1, d->update_x2, d->update_y2);
		else
			vga_update_graphics(cpu->machine, d, d->update_x1,
                          d->update_y1, d->update_x2, d->update_y2);

		d->palette_modified = 0;
		d->modified = 0;
		d->update_x1 = 999999;
		d->update_x2 = -1;
		d->update_y1 = 999999;
		d->update_y2 = -1;
	}

	if (d->n_is1_reads > N_IS1_READ_THRESHOLD)
		d->n_is1_reads = 0;
}

/*
 *  Reads and writes to the VGA video memory (pixels).
 */
DEVICE_ACCESS(vga_graphics)
{
	struct vga_data *d = (struct vga_data *) extra;
	int j, x=0, y=0, x2=0, y2=0, modified = 0;
	size_t i;

  // fprintf(stderr, "@@@%08x", relative_addr);
  for (int ii = 0; ii < len; ii++) {
    // fprintf(stderr, " %02x", data[ii]);
  }
  // fprintf(stderr, "\n");

	// L(fprintf(stderr, "VGA: mode %d gfx %d relative_addr %08" PRIx64" len %08" PRIx64"\n", d->cur_mode, d->graphics_mode, relative_addr, len));

  //                 0x38000000
  if (relative_addr >= 0x10a0000) {
    fprintf(stderr, "Access S3d register %04x\n", relative_addr - 0x10a0000);
    return 0;
  }
  if (relative_addr >= 0x1008000) {
    uint8_t data_copy[8];
    if (d->window_mapped) {
      for (i = 0; i < len; i++) {
        data_copy[len - i - 1] = data[i];
      }
    } else {
      memcpy(data_copy, data, len);
    }
    bool result = cpu->memory_rw(cpu, cpu->mem, VIRTUAL_ISA_PORTBASE + 0x80000000 + relative_addr - 0x1000000, data_copy, len, writeflag, PHYSICAL) == MEMORY_ACCESS_OK;
    fprintf(stderr, "[ vga: windowed, io address %s relative_addr %04x", writeflag == MEM_WRITE ? "write" : "read", relative_addr);
    for (i = 0; i < len; i++) {
      fprintf(stderr, " %02x", data_copy[i]);
    }
    fprintf(stderr, " ]\n");
    if (d->window_mapped && relative_addr != 0x1009ae8) {
      for (i = 0; i < len; i++) {
        data[len - i - 1] = data_copy[i];
      }
    } else {
      memcpy(data, data_copy, len);
    }
    return result;
  }
  if (relative_addr >= 0x1000000) {
    // fprintf(stderr, "[ vga: windowed, pixel transfer? ]\n", relative_addr);
    int size = len;
    while (size > 0) {
      bool result = cpu->memory_rw(cpu, cpu->mem, VIRTUAL_ISA_PORTBASE + 0x80000000 + 0xe2e8, data, std::min(2, size), writeflag, PHYSICAL) == MEMORY_ACCESS_OK;
      if (!result) {
        return 0;
      }
      data += 2;
      size -= 2;
    }
    return 1;
  }

  auto logical_width_high = (d->crtc_reg[0x51] >> 4) & 3;
  auto logical_width = (d->crtc_reg[0x13] + (logical_width_high << 8)) * 8;

	if (relative_addr + len >= d->gfx_mem_size) {
    fprintf(stderr, "[ vga: failed access at %08x+%x greater than %08x ]\n", relative_addr, len, d->gfx_mem_size);
		return 0;
  }

	if (d->cur_mode != MODE_GRAPHICS)
		return 1;

	switch (d->graphics_mode) {
	case GRAPHICS_MODE_8BIT:
		y = relative_addr / logical_width;
		x = relative_addr % logical_width;
		y2 = (relative_addr+len-1) / logical_width;
		x2 = (relative_addr+len-1) % logical_width;

		if (writeflag == MEM_WRITE) {
			memcpy(d->gfx_mem + relative_addr, data, len);
			modified = 1;
		} else
			memcpy(data, d->gfx_mem + relative_addr, len);
		break;
	case GRAPHICS_MODE_4BIT:
		y = relative_addr * 8 / d->max_x;
		x = relative_addr * 8 % d->max_x;
		y2 = ((relative_addr+len)*8-1) / d->max_x;
		x2 = ((relative_addr+len)*8-1) % d->max_x;
		/*  TODO: color stuff  */

		/*  Read/write d->gfx_mem in 4-bit color:  */
		if (writeflag == MEM_WRITE) {
			/*  i is byte index to write, j is bit index  */
			for (i=0; i<len; i++)
				for (j=0; j<8; j++) {
					int pixelmask = 1 << (7-j);
					int b = data[i] & pixelmask;
					int m = d->sequencer_reg[
					    VGA_SEQ_MAP_MASK] & 0x0f;
					uint32_t addr = (y * d->max_x + x +
					    i*8 + j) * d->bits_per_pixel / 8;
					unsigned char byte;
					if (!(d->graphcontr_reg[
					    VGA_GRAPHCONTR_MASK] & pixelmask))
						continue;
					if (addr >= d->gfx_mem_size)
						continue;
					byte = d->gfx_mem[addr];
					if (b && j&1)
						byte |= m << 4;
					if (b && !(j&1))
						byte |= m;
					if (!b && j&1)
						byte &= ~(m << 4);
					if (!b && !(j&1))
						byte &= ~m;
					d->gfx_mem[addr] = byte;
				}
			modified = 1;
		} else {
			fatal("TODO: 4 bit graphics read, mask=0x%02x\n",
			    d->sequencer_reg[VGA_SEQ_MAP_MASK]);
			for (i=0; i<len; i++)
				data[i] = random();
		}
		break;
	default:fatal("dev_vga: Unimplemented graphics mode %i\n",
		    d->graphics_mode);
		/*cpu->running = 0;*/
	}

	if (modified) {
		d->modified = 1;
		if (x < d->update_x1)  d->update_x1 = x;
		if (x > d->update_x2)  d->update_x2 = x;
		if (y < d->update_y1)  d->update_y1 = y;
		if (y > d->update_y2)  d->update_y2 = y;
		if (x2 < d->update_x1)  d->update_x1 = x2;
		if (x2 > d->update_x2)  d->update_x2 = x2;
		if (y2 < d->update_y1)  d->update_y1 = y2;
		if (y2 > d->update_y2)  d->update_y2 = y2;
		if (y != y2) {
			d->update_x1 = 0;
			d->update_x2 = d->max_x - 1;
		}
	}
	return 1;
}


/*
 *  Reads and writes the VGA video memory (charcells).
 */
DEVICE_ACCESS(vga)
{
	struct vga_data *d = (struct vga_data *) extra;
	uint64_t idata = 0, odata = 0;
	int x, y, x2, y2, r, base;
	size_t i;

  auto logical_width_high = (d->crtc_reg[0x51] >> 4) & 3;
  auto logical_width = (d->crtc_reg[0x13] + (logical_width_high << 8)) * 8;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	base = ((d->crtc_reg[VGA_CRTC_START_ADDR_HIGH] << 8)
	    + d->crtc_reg[VGA_CRTC_START_ADDR_LOW]) * 2;
	r = relative_addr - base;
	y = r / (logical_width * 2);
	x = (r/2) % logical_width;
	y2 = (r+len-1) / (logical_width * 2);
	x2 = ((r+len-1)/2) % logical_width;

	if (relative_addr + len - 1 < d->charcells_size) {
		if (writeflag == MEM_WRITE) {
			for (i=0; i<len; i++) {
				int old = d->charcells[relative_addr + i];
				if (old != data[i]) {
					d->charcells[relative_addr + i] =
					    data[i];
					d->modified = 1;
				}
			}

			if (d->modified) {
				if (x < d->update_x1)  d->update_x1 = x;
				if (x > d->update_x2)  d->update_x2 = x;
				if (y < d->update_y1)  d->update_y1 = y;
				if (y > d->update_y2)  d->update_y2 = y;
				if (x2 < d->update_x1)  d->update_x1 = x2;
				if (x2 > d->update_x2)  d->update_x2 = x2;
				if (y2 < d->update_y1)  d->update_y1 = y2;
				if (y2 > d->update_y2)  d->update_y2 = y2;

				if (y != y2) {
					d->update_x1 = 0;
					d->update_x2 = d->max_x - 1;
				}
			}
		} else
			memcpy(data, d->charcells + relative_addr, len);
		return 1;
	}

	switch (relative_addr) {
	default:
		if (writeflag==MEM_READ) {
			debug("[ vga: read from 0x%08lx ]\n",
			    (long)relative_addr);
		} else {
			debug("[ vga: write to  0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


void vga_hack_start(struct vga_data *d) {
  if (d->cur_mode != MODE_GRAPHICS) {
    d->cur_mode = MODE_GRAPHICS;
    d->max_x = 800; d->max_y = 600;
    d->graphics_mode = GRAPHICS_MODE_8BIT;
    d->bits_per_pixel = 8;
    d->pixel_repx = d->pixel_repy = 1;

    d->gfx_mem_size = 2 * 1024 * 1024; /*d->max_x * d->max_y /
      (d->graphics_mode == GRAPHICS_MODE_8BIT? 1 : 2);*/

    CHECK_ALLOCATION(d->gfx_mem = (unsigned char *) malloc(d->gfx_mem_size));

    /*  Clear screen and reset the palette:  */
    memset(d->charcells_outputed, 0, d->charcells_size);
    memset(d->charcells_drawn, 0, d->charcells_size);
    memset(d->gfx_mem, 0, d->gfx_mem_size);

    reset_palette(d, 0);
    register_reset(d);

    d->bee8_regs[3] = 600;
    d->bee8_regs[4] = 800;
    d->s3_fg_color_mix = 0x27;
    d->s3_bg_color_mix = 0x27;
  }
}


int do_color_mix(struct vga_data *d, int src_bits, int op_bits, int cpu, int bitmap) {
  int src_color_1 = 0;
  switch (src_bits) {
  case 0:
    src_color_1 = d->s3_bg_color;
    break;

  case 1:
    src_color_1 = d->s3_fg_color;
    break;

  case 2:
    src_color_1 = cpu;
    break;

  case 3:
    src_color_1 = bitmap;
    break;
  }

  int output = 0;
  switch (op_bits) {
  case 0:
    output = ~bitmap;
    break;

  case 1:
    output = 0;
    break;

  case 2:
    output = ~0;
    break;

  case 3:
    output = bitmap;
    break;

  case 4:
    output = ~src_color_1;
    break;

  case 5:
    output = src_color_1 ^ bitmap;
    break;

  case 6:
    output = ~(src_color_1 ^ bitmap);
    break;

  case 7:
    output = src_color_1;
    break;

  case 8:
    output = ~src_color_1 | ~bitmap;
    break;

  case 9:
    output = bitmap | ~src_color_1;
    break;

  case 10:
    output = ~bitmap | src_color_1;
    break;

  case 11:
    output = bitmap | src_color_1;
    break;

  case 12:
    output = bitmap & src_color_1;
    break;

  case 13:
    output = ~bitmap & src_color_1;
    break;

  case 14:
    output = bitmap & ~src_color_1;
    break;

  case 15:
    output = ~bitmap & ~src_color_1;
    break;
  }

  if (d->bee8_regs[0xe] & 0x100) {
    auto src_ne = d->bee8_regs[0xe] & 0x80;
    if (!src_ne) {
      if (output == d->s3_color_compare) {
        output = bitmap;
      }
    } else {
      if (output != d->s3_color_compare) {
        output = bitmap;
      }
    }
  }

  return output;
}

int color_mix_function(struct vga_data *d, int mask_override, int cpu, int bitmap) {
  bool mask_bit = 0;
  if (mask_override != -1) {
    mask_bit = mask_override;
  } else {
    switch ((d->bee8_regs[0x0a] >> 6) & 3) {
    case 0:
      mask_bit = 1;
      break;

    case 1:
      mask_bit = 0;
      break;

    case 2:
      mask_bit = cpu == 0xff;
      break;

    case 3:
      mask_bit = bitmap == 0xff;
      break;
    }
  }

  int use_mix = mask_bit ? d->s3_fg_color_mix : d->s3_bg_color_mix;
  int res = do_color_mix(d, (use_mix >> 5) & 3, use_mix & 15, cpu, bitmap);
#if 0
  fprintf
    (stderr, "[ vga: color mix choice %d %x compare %x cpu %x bitmap %x (M %d) fg %x bg %x => %x %02x %02x ]\n",
     d->bee8_regs[0x0a] >> 6,
     use_mix,
     d->s3_color_compare,
     cpu,
     bitmap,
     mask_bit,
     d->s3_fg_color,
     d->s3_bg_color,
     res,
     d->s3_fg_color_mix,
     d->s3_bg_color_mix
     );
#endif
  
  return res;
}


void pixel_transfer(cpu *cpu, struct vga_data *d, bool across_the_plane, uint8_t *pixel_p, int write_len) {
  uint64_t target = 0;
  int prev_pixel;
  int nowrite;
  uint8_t pixel = 0;
  int pix;

  auto logical_width_high = (d->crtc_reg[0x51] >> 4) & 3;
  auto logical_width = (d->crtc_reg[0x13] + (logical_width_high << 8)) * 8;

  write_len = std::min(write_len, d->s3_cur_x + d->s3_draw_width - d->s3_pix_x);

  /* Compute write target */
  d->update_x1 = d->s3_pix_x;
  d->update_x2 = d->s3_pix_x + write_len;
  d->update_y1 = d->s3_pix_y;
  d->update_y2 = d->s3_pix_y + write_len;
  d->modified = 1;

  if (d->s3_rem_height == 0) {
    G(fprintf(stderr, "[ s3: out of copy height (%d) ]\n", d->bee8_regs[0]));
    return;
  }

  for (pix = 0; pix < write_len; pix++) {
    target = (d->s3_pix_y * logical_width) + d->s3_pix_x;

    nowrite =
      ((d->s3_pix_y < d->bee8_regs[1]) &&
       (d->s3_pix_y > d->bee8_regs[3]) &&
       (d->s3_pix_x < d->bee8_regs[2]) &&
       (d->s3_pix_x > d->bee8_regs[4])) ||
      (target >= d->gfx_mem_size);

    d->s3_pix_x += d->s3_h_dir;

    prev_pixel = d->gfx_mem[target];
    pixel = color_mix_function(d, across_the_plane ? pixel_p[pix] : -1, (!across_the_plane) ? pixel_p[pix] : (pixel_p[pix] ? d->s3_fg_color : d->s3_bg_color), prev_pixel);

    if (!nowrite) {
      d->gfx_mem[target] = pixel;
    }

    // G(fprintf(stderr, "[ vga write (%d,%d) %08" PRIx64 " = %04x clip %d,%d,%d,%d h %d rem %d mix %04x %04x ]\n", d->s3_pix_x, d->s3_pix_y, target, pixel, d->bee8_regs[1], d->bee8_regs[2], d->bee8_regs[3], d->bee8_regs[4], d->bee8_regs[0], d->s3_rem_height, d->s3_fg_color_mix, d->s3_bg_color_mix));
  }

  if (d->s3_pix_x >= d->s3_cur_x + d->s3_draw_width) {
    d->s3_pix_x = d->s3_cur_x;
    d->s3_pix_y += d->s3_v_dir;
    d->s3_rem_height -= 1;
  }

  vga_update_graphics(cpu->machine, d, d->update_x1,
                      d->update_y1, d->update_x2, d->update_y2);
}


void bitblt(cpu *cpu, struct vga_data *d) {
  int rectangle_height = d->bee8_regs[0];
  int clipping_top = d->bee8_regs[1];
  int clipping_left = d->bee8_regs[2];
  int clipping_bottom = d->bee8_regs[3];
  int clipping_right = d->bee8_regs[4];
  int copy_start = d->s3_destx;
  int src_x = d->s3_cur_x;
  if (copy_start < clipping_left) {
    auto diff = clipping_left - d->s3_cur_x;
    src_x += diff;
    copy_start += diff;
  }
  int copy_end = std::min(copy_start + d->s3_draw_width, clipping_right);
  int width_of_copy = copy_end - copy_start;
  int rows = std::min(clipping_bottom - clipping_top, d->s3_rem_height);
  int target_row = d->s3_desty;

  G(fprintf(stderr, "[ s3: BITBLT: R(%d,%d,%d,%d) SRC (%d,%d) ]\n", d->s3_destx, d->s3_desty, clipping_right, clipping_top + rows, src_x, d->s3_cur_y));

  auto logical_width_high = (d->crtc_reg[0x51] >> 4) & 3;
  auto logical_width = (d->crtc_reg[0x13] + (logical_width_high << 8)) * 8;

  for (; rows > 0; d->s3_cur_y++, rows--, target_row++) {
    uint8_t *source = &d->gfx_mem[d->s3_cur_y * logical_width + src_x];
    uint8_t *target = &d->gfx_mem[target_row * logical_width + d->s3_destx];
    memmove(target, source, width_of_copy);
  }
  vga_update_graphics
    (cpu->machine, d,
     clipping_left, clipping_top, clipping_right, clipping_bottom
     );
}


void patblt(cpu *cpu, struct vga_data *d) {
  int rectangle_height = d->bee8_regs[0];
  int clipping_top = d->bee8_regs[1];
  int clipping_left = d->bee8_regs[2];
  int clipping_bottom = d->bee8_regs[3];
  int clipping_right = d->bee8_regs[4];
  auto start_row = std::max(d->s3_desty, clipping_top);
  auto end_row = std::min(d->s3_desty + d->s3_rem_height, clipping_bottom);
  auto start_column = std::max(d->s3_destx, clipping_left);
  auto end_column = std::min(d->s3_destx + d->s3_draw_width, clipping_right);

  auto src_x = d->s3_cur_x;
  auto src_y = d->s3_cur_y;

  auto dest_end_y = d->s3_v_dir < 0 ? start_row : end_row;
  auto dest_end_x = d->s3_h_dir < 0 ? start_column : end_column;

  fprintf(stderr, "[ s3: patblt x=%d-%d y=%d-%d ]\n", start_column, end_column, start_row, end_row);

  for (auto desty = start_row; desty != end_row; desty = std::min(desty + 8, end_row)) {
    for (auto destx = start_column; destx != end_column; destx = std::min(destx + 8, end_column)) {
      d->s3_desty = desty;
      d->s3_destx = destx;
      d->s3_cur_x = src_x;
      d->s3_cur_y = src_y;
      d->bee8_regs[1] = destx;
      d->bee8_regs[2] = desty;
      d->bee8_regs[3] = std::min(desty + 8, end_row);
      d->bee8_regs[4] = std::min(destx + 8, end_column);
      fprintf(stderr, "[ s3: patblt %d,%d ]\n", destx, desty);
      bitblt(cpu, d);
    }
  }

  d->bee8_regs[2] = clipping_top;
  d->bee8_regs[1] = clipping_left;
  d->bee8_regs[4] = clipping_right;
  d->bee8_regs[3] = clipping_bottom;
  d->s3_cur_x = src_x;
  d->s3_cur_y = src_y;
  d->s3_destx = dest_end_x;
  d->s3_desty = start_row;
}


void fillrect(cpu *cpu, struct vga_data *d, uint16_t command) {
  int leftmost_allowed = d->bee8_regs[0];
  uint8_t transfer_color[2];
  int color_source = (d->s3_fg_color_mix >> 5) & 3;
  int mix_op = d->s3_fg_color_mix & 0xf;
  int lines_written = 0;

  switch (color_source) {
  case 1:
    memset(transfer_color, d->s3_fg_color, sizeof(transfer_color));
    break;

  default:
    G(fprintf(stderr, "[ s3: unknown color source %d ]\n", color_source));
    break;
  }

  if (command & 0x10) {
    // Actually draw
    d->s3_v_dir = 1;
    auto start_x = d->s3_cur_x;
    auto start_y = d->s3_cur_y;

    while (d->s3_rem_height > 0) {
      pixel_transfer(cpu, d, false, transfer_color, sizeof(transfer_color));
    }

    d->s3_cur_x = start_x;
    d->s3_cur_y = start_y;
  }
}

DEVICE_ACCESS(vga_s3_control) { // 9ae8, CMD
	struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  REG_WRITE( 0x9ae8);

  if (writeflag != MEM_WRITE) {
    uint16_t outval = 1 << 10; // tell them all entries are clear?
    if (len == 1) {
      outval >>= relative_addr * 16;
    }
    if (d->window_mapped) {
      // XXX There's almost certainly an endian switch that's wanted.
      outval = outval >> 8 | (outval << 8);
    }
    memory_writemax64(cpu, data, len, outval);
  } else {
		written = get_le_16(d->window_mapped, memory_readmax64(cpu, data, len));
    L(fprintf(stderr, "[ s3: command = %d (raw %04x) ]\n", (int)written, (int)written));
    d->s3_cmd_mx = !!(written & 2);
    d->s3_cmd_pxtrans = !!(written & 0x100);
    bool draws_up = written & (1 << 7);
    d->s3_pix_x = d->s3_cur_x;
    d->s3_pix_y = d->s3_cur_y;
    d->s3_v_dir = draws_up ? 1 : -1;
    bool draws_right = written & (1 << 5);
    d->s3_h_dir = draws_right ? 1 : -1;
    d->s3_y_major = written & (1 << 6);
    d->s3_last_pof = written & (1 << 2);
    d->s3_cmd_bus_size = (written >> 9) & 3;
    d->s3_cmd_swap = (written >> 12) & 1;
    d->s3_no_draw = !(written & (1 << 4));
    d->s3_current_command = written >> 13;

    if (d->s3_no_draw) {
      fprintf(stderr, "[ s3: just move not implemented ]\n");
      d->s3_cur_x = d->s3_destx;
      d->s3_cur_y = d->s3_desty;
    } else {
      switch (d->s3_current_command) {
      case 0: // Nop
        break;

      case 1: // Line draw
        fprintf(stderr, "[ s3: line draw not implemented ]\n");
        break;

      case 2: // Rectangle Fill
        if (!(written & 0x100)) {
          L(fprintf(stderr, "[ s3 fillrect ]\n"));
          fillrect(cpu, d, written & 0x1fff);
        } // Otherwise accept pixel fill below.
        break;

      case 6: // BitBlt
        bitblt(cpu, d);
        break;

      case 7: // PatBLT
        fprintf(stderr, "[ s3: patblt not implemented ]\n");
        patblt(cpu, d);
        break;

      default:
        G(fprintf(stderr, "[ s3: unimplemented cmd %d (%04x) ]\n", written >> 13, written));
        break;
      }
    }
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_curx) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  REG_WRITE( 0x86e8);

  if (writeflag == MEM_WRITE) {
    written = get_le_16(d->window_mapped, memory_readmax64(cpu, data, len)) & 0x7ff;
    d->s3_cur_x = written;
    G(fprintf(stderr, "[ s3: set pix x = %d (raw %04x) ]\n", (int)written, (int)written));
  } else {
    fprintf(stderr, "[ s3: get pix x ]\n");
    memory_writemax64(cpu, data, len, d->s3_cur_x);
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_cury) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  REG_WRITE( 0x82e8);

  if (writeflag == MEM_WRITE) {
    written = get_le_16(d->window_mapped, memory_readmax64(cpu, data, len)) & 0x7ff;
    d->s3_cur_y = written;
    G(fprintf(stderr, "[ s3: set pix y = %d (raw %04x) ]\n", (int)written, (int)written));
  } else {
    fprintf(stderr, "[ s3: get pix y ]\n");
    memory_writemax64(cpu, data, len, d->s3_cur_y);
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_bg_color) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  REG_WRITE( 0xa2e8);

  if (writeflag == MEM_WRITE) {
    d->s3_bg_color = get_le_16(d->window_mapped, memory_readmax64(cpu, data, len));
    G(fprintf(stderr, "[ s3: set bg color = %d (raw %04x) ]\n", (int)d->s3_bg_color, (int)written));
  } else {
    fprintf(stderr, "[ s3: get bg color ]\n");
    memory_writemax64(cpu, data, len, d->s3_bg_color);
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_fg_color) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  REG_WRITE( 0xbae8);

  if (writeflag == MEM_WRITE) {
    d->s3_fg_color = get_le_16(d->window_mapped, memory_readmax64(cpu, data, len));
    G(fprintf(stderr, "[ s3: set fg color = %d (raw %04x) ]\n", (int)d->s3_fg_color, (int)written));
  } else {
    fprintf(stderr, "[ s3: get fg color ]\n");
    memory_writemax64(cpu, data, len, d->s3_fg_color);
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_fg_color_mix) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  REG_WRITE( 0xbae8);

  if (writeflag == MEM_WRITE) {
    d->s3_fg_color_mix = get_le_16(d->window_mapped, memory_readmax64(cpu, data, len));
    G(fprintf(stderr, "[ s3: set fg color mix = %d (raw %04x) ]\n", (int)d->s3_fg_color_mix, (int)written));
  } else {
    memory_writemax64(cpu, data, len, d->s3_fg_color_mix);
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_bg_color_mix) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  REG_WRITE( 0xb6e8);

  if (writeflag == MEM_WRITE) {
    d->s3_bg_color_mix = get_le_16(d->window_mapped, memory_readmax64(cpu, data, len));
    G(fprintf(stderr, "[ s3: set bg color mix = %d (raw %04x) ]\n", (int)d->s3_bg_color_mix, (int)written));
  } else {
    memory_writemax64(cpu, data, len, d->s3_bg_color_mix);
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_destx) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  REG_WRITE( 0x8ee8);

  if (writeflag == MEM_WRITE) {
    d->s3_destx = get_le_16(d->window_mapped, memory_readmax64(cpu, data, len)) & 0x7ff;
    G(fprintf(stderr, "[ s3: set destx = %d (raw %04x) ]\n", (int)d->s3_destx, (int)written));
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_desty) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  REG_WRITE( 0x8ae8);

  if (writeflag == MEM_WRITE) {
    d->s3_desty = get_le_16(d->window_mapped, memory_readmax64(cpu, data, len)) & 0x7ff;
    G(fprintf(stderr, "[ s3: set desty = %d ]\n", (int)d->s3_desty));
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_major_axis_len) {
  struct vga_data *d = (struct vga_data *) extra;
  uint16_t written;

  REG_WRITE( 0x96e8);

  if (writeflag == MEM_WRITE) {
    written = get_le_16(d->window_mapped, memory_readmax64(cpu, data, len));
    d->s3_draw_width = written + 1;
    G(fprintf(stderr, "[ s3: set draw width %d (raw %04x) ]\n", d->s3_draw_width, (int)written));
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_color_compare) {
  struct vga_data *d = (struct vga_data *) extra;
  int write_index;

  REG_WRITE( 0xb2e8);

  if (writeflag == MEM_WRITE) {
    d->s3_color_compare = get_le_32(d->window_mapped, memory_readmax64(cpu, data, len));
    G(fprintf(stderr, "[ s3: set color compare %08x ]\n", (int)d->s3_color_compare));
  }

  return 1;
}


DEVICE_ACCESS(vga_s3_pio_cmd) {
  struct vga_data *d = (struct vga_data *) extra;
  uint64_t idata = 0;
  uint16_t written;
  int write_index;

  REG_WRITE( 0xbee8);

  if (writeflag == MEM_WRITE) {
    written = get_le_16(d->window_mapped, memory_readmax64(cpu, data, len));
    write_index = written >> 12;
    d->bee8_regs[write_index] = written & 0xfff;

    fprintf(stderr, "[ s3: bee8 write reg %x = %03x ]\n", write_index, (int)(written & 0xfff));

    switch (write_index) {
    case 0:
      d->s3_rem_height = d->bee8_regs[0] + 1;
      break;

    case 13:
      if ((written >> 8) & 15) { // apply command?
        uint8_t command = written & 0xff;
        if (command == 0x31) {
        }
      }
      break;
    }

  }

  return 1;
}

DEVICE_ACCESS(vga_s3_pix_transfer) {
  struct vga_data *d = (struct vga_data *) extra;
  uint64_t idata;
  uint8_t to_write[4];

  REG_WRITE( 0xe2e8);

  if (writeflag == MEM_WRITE) {
    idata = memory_readmax64(cpu, data, len);
    int swizzle = d->s3_cmd_swap ? len - 1 : 0;
    for (int i = 0; i < len; i++) {
      to_write[i ^ swizzle] = idata >> (i * 8);
    }

    int pxcount = std::max(8 + (8 * d->s3_cmd_bus_size), (int)(8 * len));
    int old_fg_color_mix = d->s3_fg_color_mix;
    uint8_t pixels[32];

    if (d->s3_cmd_mx) {
      // Transfer across the plane, 1bpp
      L(fprintf(stderr, "[ vga: transfer cross the plane (width %d) ", d->s3_draw_width));
      for (int i = 0; i < pxcount; i++) {
        bool cpu = !!(to_write[i / 8] & (1 << ((7 - i) % 8)));
        L(fprintf(stderr, "%c", cpu ? '#' : '.'));
        pixels[i] = cpu ? 0xff : 0;
      }
      L(fprintf(stderr, " ]\n"));
      // d->s3_fg_color_mix = 0x67;
      pixel_transfer(cpu, d, true, pixels, pxcount);
      d->s3_fg_color_mix = old_fg_color_mix;
    } else {
      G(fprintf(stderr, "Pixel transfer, not s3_cmd_mx\n"));
      pixel_transfer(cpu, d, false, to_write, 1 + d->s3_cmd_bus_size);
    }
  }

  return 1;
}


/*
 *  vga_crtc_reg_write():
 *
 *  Writes to VGA CRTC registers.
 */
static void vga_crtc_reg_write(struct machine *machine, struct cpu *cpu, struct vga_data *d,
	int regnr, int idata)
{
	int i, grayscale;
  uint8_t dev_address[4] = { }, value[4] = { };
  uint8_t idata_byte;

  fatal("[ vga_crtc_reg_write: regnr=0x%02x idata=0x%02x (%s) ]\n",
		    regnr, idata, vga_find_register_name(d, S_PRIMARY, 0x15));
	switch (regnr) {
	case VGA_CRTC_CURSOR_SCANLINE_START:		/*  0x0a  */
	case VGA_CRTC_CURSOR_SCANLINE_END:		/*  0x0b  */
		break;
	case VGA_CRTC_START_ADDR_HIGH:			/*  0x0c  */
	case VGA_CRTC_START_ADDR_LOW:			/*  0x0d  */
		d->update_x1 = 0;
		d->update_x2 = d->max_x - 1;
		d->update_y1 = 0;
		d->update_y2 = d->max_y - 1;
		d->modified = 1;
		recalc_cursor_position(d);
		break;
	case VGA_CRTC_CURSOR_LOCATION_HIGH:		/*  0x0e  */
	case VGA_CRTC_CURSOR_LOCATION_LOW:		/*  0x0f  */
		recalc_cursor_position(d);
		break;
	case 0xff:
		grayscale = 0;
		switch (d->crtc_reg[0xff]) {
		case 0x00:
			grayscale = 1;
		case 0x01:
			d->cur_mode = MODE_CHARCELL;
			d->max_x = 40; d->max_y = 25;
			d->pixel_repx = machine->x11_md.scaleup * 2;
			d->pixel_repy = machine->x11_md.scaleup;
			d->font_width = 8;
			d->font_height = 16;
			d->font = font8x16;
			break;
		case 0x02:
			grayscale = 1;
		case 0x03:
			d->cur_mode = MODE_CHARCELL;
			d->max_x = 80; d->max_y = 25;
			d->pixel_repx = d->pixel_repy = machine->x11_md.scaleup;
			d->font_width = 8;
			d->font_height = 16;
			d->font = font8x16;
			break;
		case 0x08:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 160;	d->max_y = 200;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = 4 * machine->x11_md.scaleup;
			d->pixel_repy = 2 * machine->x11_md.scaleup;
			break;
		case 0x09:
		case 0x0d:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 320;	d->max_y = 200;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = d->pixel_repy =
			    2 * machine->x11_md.scaleup;
			break;
		case 0x0e:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 640;	d->max_y = 200;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = machine->x11_md.scaleup;
			d->pixel_repy = machine->x11_md.scaleup * 2;
			break;
		case 0x10:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 640; d->max_y = 350;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = d->pixel_repy = machine->x11_md.scaleup;
			break;
		case 0x12:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 640; d->max_y = 480;
			d->graphics_mode = GRAPHICS_MODE_4BIT;
			d->bits_per_pixel = 4;
			d->pixel_repx = d->pixel_repy = machine->x11_md.scaleup;
			break;
		case 0x13:
			d->cur_mode = MODE_GRAPHICS;
			d->max_x = 320;	d->max_y = 200;
			d->graphics_mode = GRAPHICS_MODE_8BIT;
			d->bits_per_pixel = 8;
			d->pixel_repx = d->pixel_repy =
			    2 * machine->x11_md.scaleup;
			break;
		default:
			fatal("TODO! video mode change hack (mode 0x%02x)\n",
			    d->crtc_reg[0xff]);
			exit(1);
		}

		if (d->cur_mode == MODE_CHARCELL) {
			dev_fb_resize(d->fb, d->max_x * d->font_width *
			    d->pixel_repx, d->max_y * d->font_height *
			    d->pixel_repy);
			d->fb_size = d->max_x * d->pixel_repx * d->font_width *
			     d->max_y * d->pixel_repy * d->font_height * 3;
		} else {
			dev_fb_resize(d->fb, d->max_x * d->pixel_repx,
			    d->max_y * d->pixel_repy);
			d->fb_size = d->max_x * d->pixel_repx *
			     d->max_y * d->pixel_repy * 3;
		}

		for (i=0; i<machine->ncpus; i++)
			machine->cpus[i]->invalidate_translation_caches(
			    machine->cpus[i], 0, INVALIDATE_ALL);

		if (d->gfx_mem != NULL)
			free(d->gfx_mem);
		d->gfx_mem_size = 1;
		if (d->cur_mode == MODE_GRAPHICS)
			d->gfx_mem_size = d->max_x * d->max_y /
			    (d->graphics_mode == GRAPHICS_MODE_8BIT? 1 : 2);

		CHECK_ALLOCATION(d->gfx_mem = (unsigned char *) malloc(d->gfx_mem_size));

		/*  Clear screen and reset the palette:  */
		memset(d->charcells_outputed, 0, d->charcells_size);
		memset(d->charcells_drawn, 0, d->charcells_size);
		memset(d->gfx_mem, 0, d->gfx_mem_size);
		d->update_x1 = 0;
		d->update_x2 = d->max_x - 1;
		d->update_y1 = 0;
		d->update_y2 = d->max_y - 1;
		d->modified = 1;
		reset_palette(d, grayscale);
		register_reset(d);
		break;
  case 0x58:
    if (idata & 1) {
      d->window_mapped = true;
      debug("[ vga_crtc: non-linear mode ]\n");
    } else {
      d->window_mapped = false;
      debug("[ vga_crtc: linear mode ]\n");
    }
    break;
  case 0x59:
    debug("[ vga_crtc: set PCI bar upper byte ]\n");
    idata_byte = idata;
    dev_address[0] = 0x10;
    dev_address[1] = 14 << 3;
    dev_address[3] = 0x80;
    // Set a pci address
    cpu->memory_rw(cpu, cpu->mem, 0x80000cf8, dev_address, 4, MEM_WRITE, PHYSICAL);
    cpu->memory_rw(cpu, cpu->mem, 0x80000cfc, value, 4, MEM_READ, PHYSICAL);
    value[3] = idata_byte;
    value[1] = 0x55;
    value[0] = 0xaa;
    cpu->memory_rw(cpu, cpu->mem, 0x80000cfc, value, 4, MEM_WRITE, PHYSICAL);
    break;
  case 0x5a:
    debug("[ vga_crtc: set PCI bar lower upper byte ]\n");
    idata_byte = idata;
    dev_address[0] = 0x10;
    dev_address[1] = 14 << 3;
    dev_address[3] = 0x80;
    // Set a pci address
    cpu->memory_rw(cpu, cpu->mem, 0x80000cf8, dev_address, 4, MEM_WRITE, PHYSICAL);
    // Write a byte
    cpu->memory_rw(cpu, cpu->mem, 0x80000cfc, value, 4, MEM_READ, PHYSICAL);
    value[2] = idata_byte;
    value[1] = 0x55;
    value[0] = 0xaa;
    cpu->memory_rw(cpu, cpu->mem, 0x80000cfc, value, 4, MEM_WRITE, PHYSICAL);
    break;
	default:
    break;
	}
}


/*
 *  vga_sequencer_reg_write():
 *
 *  Writes to VGA Sequencer registers.
 */
static void vga_sequencer_reg_write(struct machine *machine, struct cpu *cpu, struct vga_data *d,
	int regnr, int idata)
{
	switch (regnr) {
	case VGA_SEQ_RESET:
	case VGA_SEQ_MAP_MASK:
	case VGA_SEQ_SEQUENCER_MEMORY_MODE:
		debug("[ vga_sequencer_reg_write: select %i: TODO ]\n", regnr);
		break;
	default:fatal("[ vga_sequencer_reg_write: select %i ]\n", regnr);
		/*  cpu->running = 0;  */
	}
}


/*
 *  vga_graphcontr_reg_write():
 *
 *  Writes to VGA Graphics Controller registers.
 */
static void vga_graphcontr_reg_write(struct machine *machine,
	struct vga_data *d, int regnr, int idata)
{
	switch (regnr) {
	case VGA_GRAPHCONTR_READMAPSELECT:
	case VGA_GRAPHCONTR_GRAPHICSMODE:
	case VGA_GRAPHCONTR_MISC:
	case VGA_GRAPHCONTR_MASK:
		debug("[ vga_graphcontr_reg_write: select %i: TODO ]\n", regnr);
		break;
	default:fatal("[ vga_graphcontr_reg_write: select %i ]\n", regnr);
		/*  cpu->running = 0;  */
	}
}


/*
 *  vga_attribute_reg_write():
 *
 *  Writes to VGA Attribute registers.
 */
static void vga_attribute_reg_write(struct machine *machine, struct vga_data *d,
	int regnr, int idata)
{
	/*  0-15 are palette registers: TODO  */
	if (regnr >= 0 && regnr <= 0xf)
		return;

	switch (regnr) {
	default:fatal("[ vga_attribute_reg_write: select %i ]\n", regnr);
		/*  cpu->running = 0;  */
	}
}


/*
 *  dev_vga_ctrl_access():
 *
 *  Reads and writes of the VGA control registers.
 */
DEVICE_ACCESS(vga_ctrl)
{
	struct vga_data *d = (struct vga_data *) extra;
	size_t i;
	uint64_t idata = 0, odata = 0;

  REG_WRITE( 0x3c0);

	for (i=0; i<len; i++) {
		idata = data[i];

		/*  0x3C0 + relative_addr...  */

		switch (relative_addr) {

		case VGA_ATTRIBUTE_ADDR:		/*  0x00  */
			switch (d->attribute_state) {
			case 0:	if (writeflag == MEM_READ)
					odata = d->attribute_reg_select;
				else {
					d->attribute_reg_select = 1;
					d->attribute_state = 1;
				}
				break;
			case 1:	d->attribute_state = 0;
				d->attribute_reg[d->attribute_reg_select] =
				    idata;
				vga_attribute_reg_write(cpu->machine, d,
				    d->attribute_reg_select, idata);
				break;
			}
			break;
		case VGA_ATTRIBUTE_DATA_READ:		/*  0x01  */
			if (writeflag == MEM_WRITE)
				fatal("[ dev_vga: WARNING: Write to "
				    "VGA_ATTRIBUTE_DATA_READ? ]\n");
			else {
				if (d->attribute_state == 0)
					fatal("[ dev_vga: WARNING: Read from "
					    "VGA_ATTRIBUTE_DATA_READ, but no"
					    " register selected? ]\n");
				else
					odata = d->attribute_reg[
					    d->attribute_reg_select];
			}
			break;

		case VGA_MISC_OUTPUT_W:			/*  0x02  */
			if (writeflag == MEM_WRITE)
				d->misc_output_reg = idata;
			else {
				/*  Reads: Input Status 0  */
				odata = 0x00;
			}
			break;

		case VGA_SEQUENCER_ADDR:		/*  0x04  */
			if (writeflag == MEM_READ)
				odata = d->sequencer_reg_select;
			else
				d->sequencer_reg_select = idata;
			break;
		case VGA_SEQUENCER_DATA:		/*  0x05  */
			if (writeflag == MEM_READ)
				odata = d->sequencer_reg[
				    d->sequencer_reg_select];
			else {
        d->sequencer_reg[d->sequencer_reg_select] = idata;
        fprintf(stderr, "[ dev_vga: sequencer %02x = %02x (%s) ]\n", d->sequencer_reg_select, (unsigned int)idata, vga_find_register_name(d, S_PRIMARY, relative_addr));
        vga_sequencer_reg_write(cpu->machine, cpu, d, d->sequencer_reg_select, idata);
			}
			break;

		case VGA_DAC_ADDR_READ:			/*  0x07  */
			if (writeflag == MEM_WRITE) {
				d->palette_read_index = idata;
				d->palette_read_subindex = 0;
			} else {
				debug("[ dev_vga: WARNING: Read from "
				    "VGA_DAC_ADDR_READ? TODO ]\n");
				/*  TODO  */
			}
			break;
		case VGA_DAC_ADDR_WRITE:		/*  0x08  */
			if (writeflag == MEM_WRITE) {
				d->palette_write_index = idata;
				d->palette_write_subindex = 0;

				d->palette_read_index = idata;
				d->palette_read_subindex = 0;
			} else {
				fatal("[ dev_vga: WARNING: Read from "
				    "VGA_DAC_ADDR_WRITE? ]\n");
				odata = d->palette_write_index;
			}
			break;
		case VGA_DAC_DATA:			/*  0x09  */
			if (writeflag == MEM_WRITE) {
        L(fprintf(stderr, "[ vga: writing dac data (index %d sub %d) ]\n", d->palette_write_index, d->palette_write_subindex));
				int new_ = (idata & 63) << 2;
				int old = d->fb->rgb_palette[d->
				    palette_write_index*3+d->
				    palette_write_subindex];
				d->fb->rgb_palette[d->palette_write_index * 3 +
				    d->palette_write_subindex] = new_;
				/*  Redraw whole screen, if the
				    palette changed:  */
				if (new_ != old) {
					d->modified = 1;
					d->palette_modified = 1;
					d->update_x1 = d->update_y1 = 0;
					d->update_x2 = d->max_x - 1;
					d->update_y2 = d->max_y - 1;
				}
				d->palette_write_subindex ++;
				if (d->palette_write_subindex == 3) {
					d->palette_write_index ++;
					d->palette_write_subindex = 0;
				}
        if (d->palette_write_index > 255) {
          d->palette_write_index = 0;
        }
			} else {
				odata = (d->fb->rgb_palette[d->
				    palette_read_index * 3 +
				    d->palette_read_subindex] >> 2) & 63;
				d->palette_read_subindex ++;
				if (d->palette_read_subindex == 3) {
					d->palette_read_index ++;
					d->palette_read_subindex = 0;
				}
			}
			break;

		case VGA_MISC_OUTPUT_R:
			odata = d->misc_output_reg;
			break;

		case VGA_GRAPHCONTR_ADDR:		/*  0x0e  */
			if (writeflag == MEM_READ)
				odata = d->graphcontr_reg_select;
			else
				d->graphcontr_reg_select = idata;
			break;
		case VGA_GRAPHCONTR_DATA:		/*  0x0f  */
			if (writeflag == MEM_READ)
				odata = d->graphcontr_reg[
				    d->graphcontr_reg_select];
			else {
				d->graphcontr_reg[d->
				    graphcontr_reg_select] = idata;
				vga_graphcontr_reg_write(cpu->machine, d,
				    d->graphcontr_reg_select, idata);
			}
			break;

		case VGA_CRTC_ADDR:			/*  0x14  */
			if (writeflag == MEM_READ)
				odata = d->crtc_reg_select;
			else
				d->crtc_reg_select = idata;
			break;
		case VGA_CRTC_DATA:			/*  0x15  */
			if (writeflag == MEM_READ) {
				odata = d->crtc_reg[d->crtc_reg_select];
        fatal("[ vga_crtc_reg_read: regnr=0x%02x idata=0x%02x (%s) ]\n",
              d->crtc_reg_select, (unsigned int)odata, vga_find_register_name(d, S_PRIMARY, relative_addr));
      } else {
        if (d->crtc_reg_select != 0x30) {
          d->crtc_reg[d->crtc_reg_select] = idata;
        }
				vga_crtc_reg_write(cpu->machine, cpu, d,
				    d->crtc_reg_select, idata);
			}
			break;

		case VGA_INPUT_STATUS_1:	/*  0x1A  */
			odata = 0;
			d->n_is1_reads ++;
			d->current_retrace_line ++;
			d->current_retrace_line %= (MAX_RETRACE_SCANLINES * 8);
			/*  Whenever we are "inside" a scan line, copy the
			    current palette into retrace_palette[][]:  */
			if ((d->current_retrace_line & 7) == 7) {
				if (d->retrace_palette == NULL &&
				    d->n_is1_reads > N_IS1_READ_THRESHOLD) {
					CHECK_ALLOCATION(d->retrace_palette =
					    (unsigned char *) malloc(
					    MAX_RETRACE_SCANLINES * 256*3));
				}
				if (d->retrace_palette != NULL)
					memcpy(d->retrace_palette + (d->
					    current_retrace_line >> 3) * 256*3,
					    d->fb->rgb_palette, d->cur_mode ==
					    MODE_CHARCELL? (16*3) : (256*3));
			}
			/*  These need to go on and off, to fake the
			    real vertical and horizontal retrace info.  */
			if (d->current_retrace_line < 20*8)
				odata |= VGA_IS1_DISPLAY_VRETRACE;
			else {
				if ((d->current_retrace_line & 7) == 0)
					odata = VGA_IS1_DISPLAY_VRETRACE | VGA_IS1_DISPLAY_DISPLAY_DISABLE;
			}
			L(fprintf(stderr, "input status 1: %08" PRIx64"\n", odata));
			break;
		}

		if (writeflag == MEM_READ)
			data[i] = odata;

    fprintf(stderr, "vga ctr at %08x [%04x] %s %02x (%s)\n", (unsigned int)cpu->pc, relative_addr, writeflag == MEM_READ ? "read" : "write", data[i], vga_find_register_name(d, S_PRIMARY, relative_addr));

		/*  For multi-byte accesses:  */
		relative_addr ++;
	}

	return 1;
}

DEVICE_ACCESS(vga_s3_42e8) {
	struct vga_data *d = (struct vga_data *) extra;

  REG_WRITE( 0x42e8);

  fprintf(stderr, "vga 42e8 (%s)\n", vga_find_register_name(d, S_EXTENDED, 0x42e8));
  return 0;
}

DEVICE_ACCESS(vga_s3_4ae8) {
	struct vga_data *d = (struct vga_data *) extra;
  fprintf(stderr, "vga 4ae8 (%s)\n", vga_find_register_name(d, S_EXTENDED, 0x4ae8));

  REG_WRITE( 0x4ae8);

  if (writeflag == MEM_WRITE) {
		d->adv_fun_4ae8 = memory_readmax64(cpu, data, len);
  } else {
    memory_writemax64(cpu, data, len, d->adv_fun_4ae8);
  }

  return 1;
}

DEVICE_ACCESS(vga_s3_92e8) {
	struct vga_data *d = (struct vga_data *) extra;
  fprintf(stderr, "vga 92e8 (%s)\n", vga_find_register_name(d, S_EXTENDED, 0x92e8));

  REG_WRITE( 0x92e8);

  if (writeflag == MEM_WRITE) {
		d->line_error_term = memory_readmax64(cpu, data, len);
  } else {
    memory_writemax64(cpu, data, len, d->line_error_term);
  }

  return 1;
}

DEVICE_ACCESS(vga_s3_9ee8) {
	struct vga_data *d = (struct vga_data *) extra;
  fprintf(stderr, "vga 9ee8 (%s)\n", vga_find_register_name(d, S_EXTENDED, 0x9ee8));

  REG_WRITE( 0x9ee8);

  if (writeflag == MEM_WRITE) {
		d->short_stroke_transfer = memory_readmax64(cpu, data, len);
  } else {
    memory_writemax64(cpu, data, len, d->short_stroke_transfer);
  }

  return 1;
}

DEVICE_ACCESS(vga_s3_aae8) {
	struct vga_data *d = (struct vga_data *) extra;

  REG_WRITE( 0xaae8);

  if (writeflag == MEM_WRITE) {
		d->plane_write_mask = memory_readmax64(cpu, data, len);
  } else {
    memory_writemax64(cpu, data, len, d->plane_write_mask);
  }
  return 1;
}

DEVICE_ACCESS(vga_s3_aee8) {
	struct vga_data *d = (struct vga_data *) extra;

  REG_WRITE( 0xaee8);

  if (writeflag == MEM_WRITE) {
		d->plane_read_mask = memory_readmax64(cpu, data, len);
  } else {
    memory_writemax64(cpu, data, len, d->plane_read_mask);
  }
  return 1;
}

DEVICE_ACCESS(vga_s3_8100_range) {
	struct vga_data *d = (struct vga_data *) extra;
  int register_map[][3] = {
    { 0x00, 0x82e8, -1 },
    { 0x02, 0x86e8, -1 },
    { 0x08, 0x8ae8, -1 },
    { 0x0a, 0x8ee8, -1 },
    { 0x10, 0x92e8, -1 },
    { 0x18, 0x9ae8, -1 },
    { 0x1c, 0x9ee8, -1 },
    { 0x20, 0xa2e8, -1 },
    { 0x24, 0xa6e8, -1 },
    { 0x28, 0xaae8, -1 },
    { 0x2c, 0xaee8, -1 },
    { 0x30, 0xb2e8, -1 },
    { 0x34, 0xb6e8, -1 },
    { 0x36, 0xbae8, -1 },
    { 0x38, 0xbee8, 1 },
    { 0x3a, 0xbee8, 2 },
    { 0x3c, 0xbee8, 3 },
    { 0xe3, 0xbee8, 4 },
    { 0x40, 0xbee8, 0xa },
    { 0x42, 0xbee8, 0xd },
    { 0x44, 0xbee8, 0xe },
    { 0x46, 0xbee8, 0xf },
    { 0x48, 0xbee8, 0 },
    { 0x4a, 0x96e8, -1 },
    { 0, 0, 0 },
  };

  REG_WRITE( 0x8100);

  uint8_t *dselect = data + len - 2;
  if (writeflag == MEM_WRITE) {
    bool found = false;
    while (len > 0) {
      int amount = MIN(len, 2);
      for (int i = 0; register_map[i][1]; i++) {
        if (relative_addr != register_map[i][0]) {
          continue;
        }

        found = true;
        if (register_map[i][2] != -1) {
          d->bee8_regs[0xf] = register_map[i][2];
        }
        fprintf(stderr, "[ vga: write %04x via 8100 compressed area %02x %02x ]\n", register_map[i][1], dselect[0], dselect[1]);
        cpu->memory_rw(cpu, cpu->mem, VIRTUAL_ISA_PORTBASE + 0x80000000 + register_map[i][1], dselect, amount, MEM_WRITE, PHYSICAL);
        found = true;
        break;
      }
      relative_addr += 2;
      dselect -= 2;
      len -= amount;
    }
    if (!found) {
      cpu->memory_rw(cpu, cpu->mem, VIRTUAL_ISA_PORTBASE + 0x80000000 + 0xe2e8, data, len, MEM_WRITE, PHYSICAL);
    }
  } else {
    for (int i = 0; register_map[i][1]; i++) {
      if (relative_addr ^ 2 == register_map[i][0]) {
        if (register_map[i][2] != -1) {
          d->bee8_regs[0xf] = register_map[i][2];
        }
        memset(data, 0, len);
        cpu->memory_rw(cpu, cpu->mem, VIRTUAL_ISA_PORTBASE + 0x80000000 + register_map[i][1], data, 2, MEM_READ, PHYSICAL);
        fprintf(stderr, "[ vga: read %04x via 8100 compressed area yielding", register_map[i][1]);
        for (int j = 0; j < len; i++) {
          fprintf(stderr, " %02x", data[j]);
        }
        fprintf(stderr, "]\n");
        break;
      }
    }
  }
  return 1;
}

DEVICE_ACCESS(vga_s3_ff00_range) {
	struct vga_data *d = (struct vga_data *) extra;

  REG_WRITE( 0xff00);

  relative_addr &= 0xff;
  if (writeflag == MEM_WRITE) {
    memcpy(&d->reg_ff00_data[relative_addr], data, len);
  } else {
    memcpy(data, &d->reg_ff00_data[relative_addr], len);
  }
  fprintf(stderr, "vga ff00 %s", writeflag == MEM_WRITE ? "read" : "write");
  for (int i = 0; i < len; i++) {
    fprintf(stderr, " %02x", data[i]);
  }
  fprintf(stderr, " (%s)\n", vga_find_register_name(d, S_EXTENDED, 0xff00 + relative_addr));
  return 1;
}

DEVICE_ACCESS(vga_option_rom)
{
  uint8_t rom_data[0x2000] = {
    0x55, 0xaa, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
    //          Target Loc
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 10
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 20
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 30
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 40
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 50
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 60
    0x01, 0xff, 0x6e, 0x64, 0x20, 0x4d, 0x75, 0x6c,
    0x74, 0x69, 0x6d, 0x65, 0x64, 0x69, 0x61, 0x20, // 70
    0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x73, 0x2c,
    0x20, 0x49, 0x6e, 0x63, 0x2e, 0x20, 0x20, 0x4d, // 80
    0x42, 0x20, 0x64, 0x69, 0x73, 0x70, 0x6c, 0x61,
    0x79, 0x20, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, // 90
    0x20, 0x69, 0x6e, 0x73, 0x74, 0x61, 0x6c, 0x6c,
    0x65, 0x64, 0x30, 0x35, 0x2f, 0x30, 0x31, 0x2f, // a0
    0x39, 0x35, 0x30, 0x35, 0x2f, 0x30, 0x31, 0x2f,
    0x39, 0x35, 0x09, 0x02, 0x03, 0xd0, 0x30, 0x00, // b0
    0x7f, 0x11, 0x00, 0x00, 0x00, 0x00, 0x94, 0x38,
    0xa4, 0x38, 0xce, 0x38, 0x00, 0x00, 0x00, 0x00, // c0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x4a,
    0xff, 0x7f, 0xd6, 0x00, 0x21, 0x01, 0x25, 0x38, // d0
    0x48, 0x39, 0xa5, 0x31, 0x05, 0x50, 0x00, 0x51,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // e0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // f0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    //                      Size______
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03,
    0x04, 0x05, 0x06, 0x07, 0x00, 0x09, 0x0a, 0x0b,
    0x01, 0x0d, 0x0f, 0x0f
  };
  int rom_size = sizeof(rom_data);
  if (writeflag != MEM_WRITE) {
    uint32_t result;

    if (len == 1) {
      result = rom_data[(relative_addr) % rom_size];
    } else if (len == 2) {
      result = (rom_data[relative_addr % rom_size] << 8) | rom_data[(relative_addr + 1) % rom_size];
    } else {
      result = rom_data[relative_addr % rom_size] | (rom_data[(relative_addr + 1) % rom_size] << 8) | (rom_data[(relative_addr + 2) % rom_size] << 16) | (rom_data[(relative_addr + 3) % rom_size] << 24);
    }

    fprintf(stderr, "vga: access option rom: %08x @ %08x = %08x\n", relative_addr, cpu->pc, result);
    memory_writemax64(cpu, data, len, result);
  }
  return 1;
}

/*
 *  dev_vga_init():
 *
 *  Register a VGA text console device. max_x and max_y could be something
 *  like 80 and 25, respectively.
 */
void dev_vga_init(struct machine *machine, struct memory *mem,
	uint64_t videomem_base, uint64_t control_base, const char *name)
{
	struct vga_data *d;
	size_t allocsize, i;

	CHECK_ALLOCATION(d = (struct vga_data *) malloc(sizeof(struct vga_data)));
	memset(d, 0, sizeof(struct vga_data));

	d->console_handle = console_start_slave(machine, "vga",
	    CONSOLE_OUTPUT_ONLY);

	d->videomem_base  = videomem_base;
	d->control_base   = control_base | VIRTUAL_ISA_PORTBASE;
	d->max_x          = 100;
	d->max_y          = 38;
	d->cur_mode       = MODE_CHARCELL;
	d->crtc_reg[0xff] = 0x03;
	d->charcells_size = 0x8000;
	d->gfx_mem_size   = 64;	/*  Nothing, as we start in text mode,
			but size large enough to make gfx_mem aligned.  */
	d->pixel_repx = d->pixel_repy = machine->x11_md.scaleup;

	/*  Allocate in full pages, to make it possible to use dyntrans:  */
	allocsize = ((d->charcells_size-1) | (machine->arch_pagesize-1)) + 1;
	CHECK_ALLOCATION(d->charcells = (unsigned char *) malloc(d->charcells_size));
	CHECK_ALLOCATION(d->charcells_outputed = (unsigned char *) malloc(d->charcells_size));
	CHECK_ALLOCATION(d->charcells_drawn = (unsigned char *) malloc(d->charcells_size));
	CHECK_ALLOCATION(d->gfx_mem = (unsigned char *) malloc(d->gfx_mem_size));

	memset(d->charcells_drawn, 0, d->charcells_size);

	for (i=0; i<d->charcells_size; i+=2) {
		d->charcells[i] = ' ';
		d->charcells[i+1] = 0x07;  /*  Default color  */
		d->charcells_drawn[i] = ' ';
		d->charcells_drawn[i+1] = 0x07;
	}

	memset(d->charcells_outputed, 0, d->charcells_size);
	memset(d->gfx_mem, 0, d->gfx_mem_size);

	d->font = font8x16;
	d->font_width  = 8;
	d->font_height = 16;

	d->fb_max_x = d->pixel_repx * d->max_x;
	d->fb_max_y = d->pixel_repy * d->max_y;
	if (d->cur_mode == MODE_CHARCELL) {
		d->fb_max_x *= d->font_width;
		d->fb_max_y *= d->font_height;
	}

	/*
	memory_device_register(mem, "vga_charcells", videomem_base + 0x18000,
	    allocsize, dev_vga_access, d, DM_DYNTRANS_OK |
	    DM_DYNTRANS_WRITE_OK | DM_READS_HAVE_NO_SIDE_EFFECTS,
	    d->charcells);
	*/
	memory_device_register(mem, "vga_gfx", videomem_base, GFX_ADDR_WINDOW,
                         dev_vga_graphics_access, d, DM_DEFAULT |
                         DM_READS_HAVE_NO_SIDE_EFFECTS, d->gfx_mem);

	// memory_device_register(mem, "vga_gfx", 0xc0000000, 0xc0000,
  //                        dev_vga_graphics_access, d, DM_DEFAULT |
  //                        DM_READS_HAVE_NO_SIDE_EFFECTS, d->gfx_mem);

  // memory_device_register(mem, "vga_rom", 0xc00c0000,
  //     65536, dev_vga_option_rom_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_ctrl", control_base,
	    32, dev_vga_ctrl_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_subsystem_control", VIRTUAL_ISA_PORTBASE | 0x800042e8, 4,
                         dev_vga_s3_42e8_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_adv_func_control", VIRTUAL_ISA_PORTBASE | 0x80004ae8, 4,
                         dev_vga_s3_4ae8_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_8100_range", VIRTUAL_ISA_PORTBASE | 0x80008100, 0x100,
                         dev_vga_s3_8100_range_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_cury", VIRTUAL_ISA_PORTBASE | 0x800082e8, 4,
                         dev_vga_s3_cury_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_curx", VIRTUAL_ISA_PORTBASE | 0x800086e8, 4,
	    dev_vga_s3_curx_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_desty", VIRTUAL_ISA_PORTBASE | 0x80008ae8, 4,
                         dev_vga_s3_desty_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_destx", VIRTUAL_ISA_PORTBASE | 0x80008ee8, 4,
                         dev_vga_s3_destx_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_line_term_error", VIRTUAL_ISA_PORTBASE | 0x800092e8, 4,
                         dev_vga_s3_92e8_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_major_axis_len", VIRTUAL_ISA_PORTBASE | 0x800096e8, 4,
	    dev_vga_s3_major_axis_len_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_control", VIRTUAL_ISA_PORTBASE | 0x80009ae8, 4,
                         dev_vga_s3_control_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_short_stroke_transfer", VIRTUAL_ISA_PORTBASE | 0x80009ee8, 4,
                         dev_vga_s3_9ee8_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_bg_color", VIRTUAL_ISA_PORTBASE | 0x8000a2e8, 4,
                         dev_vga_s3_bg_color_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_fg_color", VIRTUAL_ISA_PORTBASE | 0x8000a6e8, 4,
                         dev_vga_s3_fg_color_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_bitplane_write_mask", VIRTUAL_ISA_PORTBASE | 0x8000aae8, 4,
                         dev_vga_s3_aae8_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_bitplane_read_mask", VIRTUAL_ISA_PORTBASE | 0x8000aee8, 4,
                         dev_vga_s3_aee8_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_color_compare", VIRTUAL_ISA_PORTBASE | 0x8000b2e8, 4,
                         dev_vga_s3_color_compare_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_bg_color_mix", VIRTUAL_ISA_PORTBASE | 0x8000b6e8, 4,
                         dev_vga_s3_bg_color_mix_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_fg_color_mix", VIRTUAL_ISA_PORTBASE | 0x8000bae8, 4,
                         dev_vga_s3_fg_color_mix_access, d, DM_DEFAULT, NULL);

  memory_device_register(mem, "vga_s3_pio_cmd", VIRTUAL_ISA_PORTBASE | 0x8000bee8, 4,
                         dev_vga_s3_pio_cmd_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_pix_transfer", VIRTUAL_ISA_PORTBASE | 0x8000e2e8, 4,
	    dev_vga_s3_pix_transfer_access, d, DM_DEFAULT, NULL);

	memory_device_register(mem, "vga_s3_ff00_range", VIRTUAL_ISA_PORTBASE | 0x8000ff00, 0x80,
                         dev_vga_s3_ff00_range_access, d, DM_DEFAULT, NULL);

	d->fb = dev_fb_init(machine, mem, VGA_FB_ADDR, VFB_GENERIC,
	    d->fb_max_x, d->fb_max_y, d->fb_max_x, d->fb_max_y, 24, "VGA");
	d->fb_size = d->fb_max_x * d->fb_max_y * 3;

	reset_palette(d, 0);

	/*  This will force an initial redraw/resynch:  */
	d->update_x1 = 0;
	d->update_x2 = d->max_x - 1;
	d->update_y1 = 0;
	d->update_y2 = d->max_y - 1;
	d->modified = 1;

  // Used when window_mapped is true.
  d->window_address = 0;

	machine_add_tickfunction(machine, dev_vga_tick, d, VGA_TICK_SHIFT);

	register_reset(d);

	vga_update_cursor(machine, d);
  vga_hack_start(d);
}

