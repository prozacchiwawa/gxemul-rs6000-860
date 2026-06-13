/*	$NetBSD: siireg.h,v 1.4 1994/10/26 21:09:22 cgd Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)siireg.h	8.1 (Berkeley) 6/10/93
 *
 * sii.h --
 *
 * 	SII registers.
 *
 *	Copyright (C) 1989 Digital Equipment Corporation.
 *	Permission to use, copy, modify, and distribute this software and
 *	its documentation for any purpose and without fee is hereby granted,
 *	provided that the above copyright notice appears in all copies.
 *	Digital Equipment Corporation makes no representations about the
 *	suitability of this software for any purpose.  It is provided "as is"
 *	without express or implied warranty.
 *
 * from: Header: /sprite/src/kernel/dev/ds3100.md/RCS/sii.h,
 *	v 1.2 89/08/15 19:53:04 rab Exp  SPRITE (DECWRL)
 */

#ifndef _SII
#define _SII

/*
 * SII hardware registers
 */
typedef /*volatile*/ struct {
	uint16_t	sdb;		/* SCSI Data Bus and Parity */
	uint16_t	pad0;
	uint16_t	sc1;		/* SCSI Control Signals One */
	uint16_t	pad1;
	uint16_t	sc2;		/* SCSI Control Signals Two */
	uint16_t	pad2;
	uint16_t	csr;		/* Control/Status register */
	uint16_t	pad3;
	uint16_t	id;		/* Bus ID register */
	uint16_t	pad4;
	uint16_t	slcsr;		/* Select Control and Status Register */
	uint16_t	pad5;
	uint16_t	destat;		/* Selection Detector Status Register */
	uint16_t	pad6;
	uint16_t	dstmo;		/* DSSI Timeout Register */
	uint16_t	pad7;
	uint16_t	data;		/* Data Register */
	uint16_t	pad8;
	uint16_t	dmctrl;		/* DMA Control Register */
	uint16_t	pad9;
	uint16_t	dmlotc;		/* DMA Length of Transfer Counter */
	uint16_t	pad10;
	uint16_t	dmaddrl;	/* DMA Address Register Low */
	uint16_t	pad11;
	uint16_t	dmaddrh;	/* DMA Address Register High */
	uint16_t	pad12;
	uint16_t	dmabyte;	/* DMA Initial Byte Register */
	uint16_t	pad13;
	uint16_t	stlp;		/* DSSI Short Target List Pointer */
	uint16_t	pad14;
	uint16_t	ltlp;		/* DSSI Long Target List Pointer */
	uint16_t	pad15;
	uint16_t	ilp;		/* DSSI Initiator List Pointer */
	uint16_t	pad16;
	uint16_t	dsctrl;		/* DSSI Control Register */
	uint16_t	pad17;
	uint16_t	cstat;		/* Connection Status Register */
	uint16_t	pad18;
	uint16_t	dstat;		/* Data Transfer Status Register */
	uint16_t	pad19;
	uint16_t	comm;		/* Command Register */
	uint16_t	pad20;
	uint16_t	dictrl;		/* Diagnostic Control Register */
	uint16_t	pad21;
	uint16_t	clock;		/* Diagnostic Clock Register */
	uint16_t	pad22;
	uint16_t	bhdiag;		/* Bus Handler Diagnostic Register */
	uint16_t	pad23;
	uint16_t	sidiag;		/* SCSI IO Diagnostic Register */
	uint16_t	pad24;
	uint16_t	dmdiag;		/* Data Mover Diagnostic Register */
	uint16_t	pad25;
	uint16_t	mcdiag;		/* Main Control Diagnostic Register */
	uint16_t	pad26;
} SIIRegs;

/*
 * SC1 - SCSI Control Signals One
 */
#define SII_SC1_MSK	0x1ff		/* All possible signals on the bus */
#define SII_SC1_SEL	0x80		/* SCSI SEL signal active on bus */
#define SII_SC1_ATN	0x08		/* SCSI ATN signal active on bus */

/*
 * SC2 - SCSI Control Signals Two
 */
#define SII_SC2_IGS	0x8		/* SCSI drivers for initiator mode */

/*
 * CSR - Control/Status Register
 */
#define SII_HPM	0x10			/* SII in on an arbitrated SCSI bus */
#define	SII_RSE	0x08			/* 1 = respond to reselections */
#define SII_SLE	0x04			/* 1 = respond to selections */
#define SII_PCE	0x02			/* 1 = report parity errors */
#define SII_IE	0x01			/* 1 = enable interrupts */

/*
 * ID - Bus ID Register
 */
#define SII_ID_IO	0x8000		/* I/O */

/*
 * DESTAT - Selection Detector Status Register
 */
#define SII_IDMSK	0x7		/* ID of target reselected the SII */

/*
 * DMCTRL - DMA Control Register
 */
#define SII_ASYNC	0x00		/* REQ/ACK Offset for async mode */
#define SII_SYNC	0x03		/* REQ/ACK Offset for sync mode */

/*
 * DMLOTC - DMA Length Of Transfer Counter
 */
#define SII_TCMSK	0x1fff		/* transfer count mask */

/*
 * CSTAT - Connection Status Register
 */
#define	SII_CI		0x8000	/* composite interrupt bit for CSTAT */
#define SII_DI		0x4000	/* composite interrupt bit for DSTAT */
#define SII_RST		0x2000	/* 1 if reset is asserted on SCSI bus */
#define	SII_BER		0x1000	/* Bus error */
#define	SII_OBC		0x0800	/* Out_en Bit Cleared (DSSI mode) */
#define SII_TZ		0x0400	/* Target pointer Zero (STLP or LTLP is zero) */
#define	SII_BUF		0x0200	/* Buffer service - outbound pkt to non-DSSI */
#define SII_LDN		0x0100	/* List element Done */
#define SII_SCH		0x0080	/* State Change */
#define SII_CON		0x0040	/* SII is Connected to another device */
#define SII_DST		0x0020	/* SII was Destination of current transfer */
#define SII_TGT		0x0010	/* SII is operating as a Target */
#define SII_STATE_MSK	0x0070	/* State Mask */
#define SII_SWA		0x0008	/* Selected With Attention */
#define SII_SIP		0x0004	/* Selection In Progress */
#define SII_LST		0x0002	/* Lost arbitration */

/*
 * DSTAT - Data Transfer Status Register
 */
#define SII_DNE		0x2000	/* DMA transfer Done */
#define SII_TCZ		0x1000	/* Transfer Count register is Zero */
#define SII_TBE		0x0800	/* Transmit Buffer Empty */
#define SII_IBF		0x0400	/* Input Buffer Full */
#define SII_IPE		0x0200	/* Incoming Parity Error */
#define SII_OBB		0x0100	/* Odd Byte Boundry */
#define SII_MIS		0x0010	/* Phase Mismatch */
#define SII_ATN		0x0008	/* ATN set by initiator if in Target mode */
#define SII_MSG		0x0004	/* current bus state of MSG */
#define SII_CD		0x0002	/* current bus state of C/D */
#define SII_IO		0x0001	/* current bus state of I/O */
#define SII_PHASE_MSK	0x0007	/* Phase Mask */

/*
 * The different phases.
 */
#define SII_MSG_IN_PHASE	0x7
#define SII_MSG_OUT_PHASE	0x6
#define SII_STATUS_PHASE	0x3
#define SII_CMD_PHASE		0x2
#define SII_DATA_IN_PHASE	0x1
#define SII_DATA_OUT_PHASE	0x0

/*
 * COMM - Command Register
 */
#define	SII_DMA		0x8000	/* DMA mode */
#define SII_DO_RST	0x4000	/* Assert reset on SCSI bus for 25 usecs */
#define SII_RSL		0x1000	/* 0 = select, 1 = reselect desired device */

/* Commands: I - Initiator, T - Target, D - Disconnected */
#define SII_INXFER	0x0800	/* Information Transfer command	(I,T) */
#define SII_SELECT	0x0400	/* Select command		(D) */
#define SII_REQDATA	0x0200	/* Request Data command		(T) */
#define	SII_DISCON	0x0100	/* Disconnect command		(I,T,D) */
#define SII_CHRESET	0x0080	/* Chip Reset command		(I,T,D) */

/* Command state bits same as connection status register */
/* Command phase bits same as data transfer status register */

/*
 * DICTRL - Diagnostic Control Register
 */
#define SII_PRE		0x4	/* Enable the SII to drive the SCSI bus */

#define SII_WAIT_COUNT		10000	/* Delay count used for the SII chip */
/*
 * Max DMA transfer length for SII
 * The SII chip only has a 13 bit counter. If 8192 is used as the max count,
 * you can't tell the difference between a count of zero and 8192.
 * 8190 is used instead of 8191 so the count is even.
 */
#define SII_MAX_DMA_XFER_LENGTH	8192

#endif /* _SII */
