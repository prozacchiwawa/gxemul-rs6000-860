/* GXemul: $Id: alpha_autoconf.h,v 1.2 2006-06-17 09:35:37 debug Exp $ */
/* $NetBSD: autoconf.h,v 1.22 2005/12/11 12:16:16 christos Exp $ */

#ifndef	ALPHA_AUTOCONF_H
#define	ALPHA_AUTOCONF_H

#include "misc.h"

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 * Machine-dependent structures for autoconfiguration
 */

#if 0	/*  not in gxemul  */
struct mainbus_attach_args {
	const char *ma_name;		/* device name */
	int	    ma_slot;		/* CPU "slot" number; only meaningful
					   when attaching CPUs */
};

struct bootdev_data {
	char	*protocol;
	int	bus;
	int	slot;
	int	channel;
	char	*remote_address;
	int	unit;
	int	boot_dev_type;
	char	*ctrl_dev_type;
};
#endif

/*
 * The boot program passes a pointer (in the boot environment virtual
 * address address space; "BEVA") to a bootinfo to the kernel using
 * the following convention:
 *
 *	a0 contains first free page frame number
 *	a1 contains page number of current level 1 page table
 *	if a2 contains BOOTINFO_MAGIC and a4 is nonzero:
 *		a3 contains pointer (BEVA) to bootinfo
 *		a4 contains bootinfo version number
 *	if a2 contains BOOTINFO_MAGIC and a4 contains 0 (backward compat):
 *		a3 contains pointer (BEVA) to bootinfo version
 *		    (uint64_t), then the bootinfo
 */

#define	ALPHA_BOOTINFO_MAGIC		0xdeadbeeffeedfaceULL

struct bootinfo_v1 {
	uint64_t	ssym;			/* 0: start of kernel sym table	*/
	uint64_t	esym;			/* 8: end of kernel sym table	*/
	char	boot_flags[64];		/* 16: boot flags		*/
	char	booted_kernel[64];	/* 80: name of booted kernel	*/
	void	*hwrpb;			/* 144: hwrpb pointer (BEVA)	*/
	uint64_t	hwrpbsize;		/* 152: size of hwrpb data	*/
	int	(*cngetc)(void);	/* 160: console getc pointer	*/
	void	(*cnputc)(int);		/* 168: console putc pointer	*/
	void	(*cnpollc)(int);	/* 176: console pollc pointer	*/
	uint64_t	pad[9];			/* 184: rsvd for future use	*/
					/* 256: total size		*/
};

/*
 * Kernel-internal structure used to hold important bits of boot
 * information.  NOT to be used by boot blocks.
 *
 * Note that not all of the fields from the bootinfo struct(s)
 * passed by the boot blocks aren't here (because they're not currently
 * used by the kernel!).  Fields here which aren't supplied by the
 * bootinfo structure passed by the boot blocks are supposed to be
 * filled in at startup with sane contents.
 */
struct bootinfo_kernel {
	uint64_t	ssym;			/* start of syms */
	uint64_t	esym;			/* end of syms */
	uint64_t	hwrpb_phys;		/* hwrpb physical address */
	uint64_t	hwrpb_size;		/* size of hwrpb data */
	char	boot_flags[64];		/* boot flags */
	char	booted_kernel[64];	/* name of booted kernel */
	char	booted_dev[64];		/* name of booted device */
};

/*
 * Lookup table entry for Alpha system variations.
 */
struct alpha_variation_table {
	u_int64_t	avt_variation;	/* variation, from HWRPB */
	const char	*avt_model;	/* model string */
};

#ifdef _KERNEL
extern struct bootdev_data *bootdev_data;
extern struct bootinfo_kernel bootinfo;

const char *alpha_variation_name(u_int64_t,
    const struct alpha_variation_table *);
const char *alpha_unknown_sysname(void);
#endif /* _KERNEL */

#endif	/*  ALPHA_AUTOCONF_H  */
