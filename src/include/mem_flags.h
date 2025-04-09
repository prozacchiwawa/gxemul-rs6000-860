#ifndef MEM_FLAGS_H
#define MEM_FLAGS_H

/*  Writeflag:  */
#define	MEM_READ			0
#define	MEM_WRITE			1
#define	MEM_DOWNGRADE			128

/*  Misc. flags:  */
#define	CACHE_DATA			0
#define	CACHE_INSTRUCTION		1
#define	CACHE_NONE			2
#define	CACHE_FLAGS_MASK		0x3
#define	NO_EXCEPTIONS			16
#define	PHYSICAL			32
#define	MEMORY_USER_ACCESS		64	/*  for ARM and M88K  */

/*  Dyntrans Memory flags:  */
#define	DM_DEFAULT				0
#define	DM_DYNTRANS_OK				1
#define	DM_DYNTRANS_WRITE_OK			2
#define	DM_READS_HAVE_NO_SIDE_EFFECTS		4
#define	DM_EMULATED_RAM				8

#define FLAG_WRITEFLAG          1
#define FLAG_NOEXCEPTIONS       2
#define FLAG_INSTR              4
#define FLAG_VERBOSE            0x80000000

#define	MEMORY_ACCESS_FAILED		0
#define	MEMORY_ACCESS_OK		1
#define	MEMORY_ACCESS_OK_WRITE		2
#define	MEMORY_NOT_FULL_PAGE		256

#endif//MEM_FLAGS_H
