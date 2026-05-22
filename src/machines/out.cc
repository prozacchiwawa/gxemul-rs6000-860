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
 *  Automatically register all machines in the src/machines/ subdir.
 *
 *  NOTE: automachine_head.c, plus a line for each machine, plus
 *  automachine_middle.c, plus another line (again) for each machine, plus
 *  automachine_tail.c should be combined into one. See makeautomachine.sh
 *  for more info.
 */

#include <stdio.h>

#include "machine.h"


void machine_register_hpcsh(void);
void machine_register_qemu_mips(void);
void machine_register_netwinder(void);
void machine_register_macppc(void);
void machine_register_evbmips(void);
void machine_register_mvmeppc(void);
void machine_register_hpcarm(void);
void machine_register_hpcmips(void);
void machine_register_dreamcast(void);
void machine_register_alpha(void);
void machine_register_barearm(void);
void machine_register_testarm(void);
void machine_register_barem88k(void);
void machine_register_oldtestm88k(void);
void machine_register_baremips(void);
void machine_register_oldtestmips(void);
void machine_register_bareppc(void);
void machine_register_testppc(void);
void machine_register_baresh(void);
void machine_register_testsh(void);
void machine_register_pmax(void);
void machine_register_prep(void);
void machine_register_mvme88k(void);
void machine_register_iyonix(void);
void machine_register_playstation2(void);
void machine_register_sgi(void);
void machine_register_cats(void);
void machine_register_algor(void);
void machine_register_pmppc(void);
void machine_register_cobalt(void);
void machine_register_iq80321(void);
void machine_register_arc(void);
void machine_register_landisk(void);
/*
 *  automachine_init():
 */
void automachine_init(void)
{
	/*  printf("automachine_init()\n");  */

	/*  automachine_middle.c ends here.  */

	machine_register_hpcsh();
	machine_register_qemu_mips();
	machine_register_netwinder();
	machine_register_macppc();
	machine_register_evbmips();
	machine_register_mvmeppc();
	machine_register_hpcarm();
	machine_register_hpcmips();
	machine_register_dreamcast();
	machine_register_alpha();
	machine_register_barearm();
	machine_register_testarm();
	machine_register_barem88k();
	machine_register_oldtestm88k();
	machine_register_baremips();
	machine_register_oldtestmips();
	machine_register_bareppc();
	machine_register_testppc();
	machine_register_baresh();
	machine_register_testsh();
	machine_register_pmax();
	machine_register_prep();
	machine_register_mvme88k();
	machine_register_iyonix();
	machine_register_playstation2();
	machine_register_sgi();
	machine_register_cats();
	machine_register_algor();
	machine_register_pmppc();
	machine_register_cobalt();
	machine_register_iq80321();
	machine_register_arc();
	machine_register_landisk();

}

