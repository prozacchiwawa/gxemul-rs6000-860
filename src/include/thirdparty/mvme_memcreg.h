/*	$OpenBSD: memcreg.h,v 1.3 2003/06/02 07:06:56 deraadt Exp $ */

/*
 * Copyright (c) 1995 Theo de Raadt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * the MEMC's registers are a subset of the MCECC chip
 */
struct memcreg {
	volatile uint8_t		memc_chipid;
	volatile uint8_t		xx0[3];
	volatile uint8_t		memc_chiprev;
	volatile uint8_t		xx1[3];
	volatile uint8_t		memc_memconf;
#define MEMC_MEMCONF_MSIZ	0x07
#define MEMC_MEMCONF_RTOB(x) ((4*1024*1024) << ((x) & MEMC_MEMCONF_MSIZ))
	volatile uint8_t		xx2[3];
	volatile uint8_t		memc_x0;
	volatile uint8_t		xx3[3];
	volatile uint8_t		memc_x1;
	volatile uint8_t		xx4[3];
	volatile uint8_t		memc_baseaddr;
	volatile uint8_t		xx5[3];
	volatile uint8_t		memc_control;
	volatile uint8_t		xx6[3];
	volatile uint8_t		memc_bclk;
	volatile uint8_t		xx7[3];

	/* the following registers only exist on the MCECC */
	volatile uint8_t		memc_datactl;
	volatile uint8_t		xx8[3];
	volatile uint8_t		memc_scrubctl;
	volatile uint8_t		xx9[3];
	volatile uint8_t		memc_scrubperh;
	volatile uint8_t		xx10[3];
	volatile uint8_t		memc_scrubperl;
	volatile uint8_t		xx11[3];
	volatile uint8_t		memc_chipprescale;
	volatile uint8_t		xx12[3];
	volatile uint8_t		memc_scrubtime;
	volatile uint8_t		xx13[3];
	volatile uint8_t		memc_scrubprescaleh;
	volatile uint8_t		xx14[3];
	volatile uint8_t		memc_scrubprescalem;
	volatile uint8_t		xx15[3];
	volatile uint8_t		memc_scrubprescalel;
	volatile uint8_t		xx16[3];
	volatile uint8_t		memc_scrubtimeh;
	volatile uint8_t		xx17[3];
	volatile uint8_t		memc_scrubtimel;
	volatile uint8_t		xx18[3];
	volatile uint8_t		memc_scrubaddrhh;
	volatile uint8_t		xx19[3];
	volatile uint8_t		memc_scrubaddrhm;
	volatile uint8_t		xx20[3];
	volatile uint8_t		memc_scrubaddrlm;
	volatile uint8_t		xx21[3];
	volatile uint8_t		memc_scrubaddrll;
	volatile uint8_t		xx22[3];
	volatile uint8_t		memc_errlog;
	volatile uint8_t		xx23[3];
	volatile uint8_t		memc_errloghh;
	volatile uint8_t		xx24[3];
	volatile uint8_t		memc_errloghm;
	volatile uint8_t		xx25[3];
	volatile uint8_t		memc_errloglm;
	volatile uint8_t		xx26[3];
	volatile uint8_t		memc_errlogll;
	volatile uint8_t		xx27[3];
	volatile uint8_t		memc_errsyndrome;
	volatile uint8_t		xx28[3];
	volatile uint8_t		memc_defaults1;
	volatile uint8_t		xx29[3];
	volatile uint8_t		memc_defaults2;
	volatile uint8_t		xx30[3];
};

#define MEMC_CHIPID		0x80
#define MCECC_CHIPID		0x81
