/*  gxemul: $Id: arcbios_other.h,v 1.3 2005-03-05 12:34:02 debug Exp $  */
/*	$NetBSD: arcbios.h,v 1.2 2000/01/23 21:01:50 soda Exp $	*/
/*	$OpenBSD: arcbios.h,v 1.4 1997/05/01 15:13:30 pefo Exp $	*/

#ifndef ARCBIOS_OTHER_H
#define ARCBIOS_OTHER_H

/*-
 * Copyright (c) 1996 M. Warner Losh.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 */

#include <sys/types.h>

typedef struct arc_sid
{
	char vendor[8];
	char prodid[8];
} arc_sid_t;

/* struct arc_config::class */
#define arc_SystemClass		0
#define arc_ProcessorClass	1
#define arc_CacheClass		2
#define arc_AdapterClass	3
#define arc_ControllerClass	4
#define arc_PeripheralClass	5
#define arc_MemoryClass		6

/* struct arc_config::type */
#define arc_System

#define arc_CentralProcessor
#define arc_FloatingPointProcessor

#define arc_PrimaryIcache
#define arc_PrimaryDcache
#define arc_SecondaryIcache
#define arc_SecondaryDcache
#define arc_SecondaryCache

#define arc_EisaAdapter			/* Eisa adapter         */
#define arc_TcAdapter			/* Turbochannel adapter */
#define arc_ScsiAdapter			/* SCSI adapter         */
#define arc_DtiAdapter			/* AccessBus adapter    */
#define arc_MultiFunctionAdapter

#define arc_DiskController	0
#define arc_TapeController	1
#define arc_CdromController	2
#define arc_WormController	3
#define arc_SerialController	4
#define arc_NetworkController	5
#define arc_DisplayController	6
#define arc_ParallelController	7
#define arc_PointerController	8
#define arc_KeyboardController	9
#define arc_AudioController	10
#define arc_OtherController	11	/* denotes a controller not otherwise defined */

#define arc_DiskPeripheral	12
#define arc_FloppyDiskPeripheral	13
#define arc_TapePeripheral	14
#define arc_ModemPeripheral	15
#define arc_MonitorPeripheral	16
#define arc_PrinterPeripheral	17
#define arc_PointerPeripheral	18
#define arc_KeyboardPeripheral	19
#define arc_TerminalPeripheral	20
#define arc_OtherPeripheral	21	/* denotes a peripheral not otherwise defined   */
#define arc_LinePeripheral	22
#define arc_NetworkPeripheral	23

#define arc_SystemMemory	24

/* struct arc_config::flag */
#define arc_PeripheralFailed		0x01
#define arc_PeripheralReadOnly		0x02
#define arc_PeripheralRemovable		0x04
#define arc_PeripheralConsoleIn		0x08
#define arc_PeripheralConsoleOut	0x10
#define arc_PeripheralInput	    	0x20
#define arc_PeripheralOutput   		0x40

/* Wonder how this is aligned... */
typedef struct arc_config
{
	uint32_t		cclass;		// class
	uint32_t		type;
	uint32_t		flags;
	uint16_t		version;
	uint16_t		revision;
	uint32_t		key;
	uint32_t		affinity_mask;
	uint32_t		config_data_len;
	uint32_t		id_len;
	char 			*id;
} arc_config_t;

typedef enum arc_cm_resource_type {
	arc_CmResourceTypeNull,
	arc_CmResourceTypePort,
	arc_CmResourceTypeInterrupt,
	arc_CmResourceTypeMemory,
	arc_CmResourceTypeDMA,
	arc_CmResourceTypeDeviceSpecific,
	arc_CmResourceTypeVendor,
	arc_CmResourceTypeProductName,
	arc_CmResourceTypeSerialNumber
} arc_cm_resource_type_t;

/* do not use uint64_t to avoid alignment problem */
typedef struct {
	uint32_t	loword;
	uint32_t	hiword;
} arc_paddr_t;

typedef struct arc_cm_partial_resource {
	uint8_t	type;
	uint8_t	share_disposition;
	uint16_t	flags;

	union {
		struct {
			arc_paddr_t	start;
			uint32_t	length;
		} port;
		struct {
			uint32_t	level;
			uint32_t	vector;
			uint32_t	reserved1;
		} interrupt;
		struct {
			arc_paddr_t	start;
			uint32_t	length;
		} memory;
		struct {
			uint32_t	channel;
			uint32_t	port;
			uint32_t	reserved1;
		} dma;
		struct {
			uint8_t	vendor[12];
		} vendor;
		struct {
			uint8_t	product_name[12];
		} product_name;
		struct {
			uint8_t	serial_number[12];
		} serial_number;
		struct {
			uint32_t	data_size;
			uint32_t	reserved1;
			uint32_t	reserved2;
			/* the data is followed */
		} device_specific_data;
	} u;
} arc_cm_partial_resource_t;

typedef struct arc_cm_partial_resource_list {
	uint16_t	version;
	uint16_t	revision;
	uint32_t	count;
	arc_cm_partial_resource_t partial_descriptors[1];
} arc_cm_partial_resource_list_t;

typedef enum arc_cm_share_disposition {
	arc_CmResourceShareUndetermined,
	arc_CmResourceShareDeviceExclusive,
	arc_CmResourceShareDriverExclusive,
	arc_CmResourceShareShared
} arc_cm_share_disposition_t;

/* arc_cm_partial_resource_t::flags when type == arc_CmResourceTypeInterrupt */
typedef enum arc_cm_flags_interrupt {
	arc_CmResourceInterruptLevelSensitive,
	arc_CmResourceInterruptLatched
} arc_cm_flags_interrupt_t;

/* arc_cm_partial_resource_t::flags when type == arc_CmResourceTypeMemory */
typedef enum arc_cm_flags_memory { 
	arc_CmResourceMemoryReadWrite,
	arc_CmResourceMemoryReadOnly,
	arc_CmResourceMemoryWriteOnly
} arc_cm_flags_memory_t;

/* arc_cm_partial_resource_t::flags when type == arc_CmResourceTypePort */
typedef enum arc_cm_flags_port { 
	arc_CmResourcePortMemory,
	arc_CmResourcePortIO
} arc_cm_flags_port;

typedef enum arc_status
{
	arc_ESUCCESS,			/* Success                   */
	arc_E2BIG,			/* Arg list too long         */
	arc_EACCES,			/* No such file or directory */
	arc_EAGAIN,			/* Try again                 */
	arc_EBADF,			/* Bad file number           */
	arc_EBUSY,			/* Device or resource busy   */
	arc_EFAULT,			/* Bad address               */
	arc_EINVAL,			/* Invalid argument          */
	arc_EIO,			/* I/O error                 */
	arc_EISDIR,			/* Is a directory            */
	arc_EMFILE,			/* Too many open files       */
	arc_EMLINK,			/* Too many links            */
	arc_ENAMETOOLONG,		/* File name too long        */
	arc_ENODEV,			/* No such device            */
	arc_ENOENT,			/* No such file or directory */
	arc_ENOEXEC,			/* Exec format error         */
	arc_ENOMEM,			/* Out of memory             */
	arc_ENOSPC,			/* No space left on device   */
	arc_ENOTDIR,			/* Not a directory           */
	arc_ENOTTY,			/* Not a typewriter          */
	arc_ENXIO,			/* No such device or address */
	arc_EROFS,			/* Read-only file system     */
} arc_status_t;

typedef enum {
	ExeceptionBlock,
	SystemParameterBlock,
	FreeMemory,
	BadMemory,
	LoadedProgram,
	FirmwareTemporary,
	FirmwarePermanent,
	FreeContigous
} arc_mem_type_t;

typedef struct arc_mem {
	arc_mem_type_t	Type;		/* Memory chunk type */
	uint32_t	BasePage;	/* Page no, first page */
	uint32_t	PageCount;	/* Number of pages */
} arc_mem_t;

typedef uint8_t* arc_time_t; /* XXX */

typedef struct arc_dsp_stat {
	uint16_t	CursorXPosition;
	uint16_t	CursorYPosition;
	uint16_t	CursorMaxXPosition;
	uint16_t	CursorMaxYPosition;
	uint8_t	ForegroundColor;
	uint8_t	BackgroundColor;
	uint8_t	HighIntensity;
	uint8_t	Underscored;
	uint8_t	ReverseVideo;
} arc_dsp_stat_t;

typedef uint8_t* arc_dirent_t; /* XXX */
typedef uint32_t arc_seek_mode_t; /* XXX */
typedef uint32_t arc_mount_t; /* XXX */

typedef enum arc_open_mode {
	arc_OpenReadOnly,
	arc_OpenWriteOnly,
	arc_OpenReadWrite,
	arc_CreateWriteOnly,
	arc_CreateReadWrite,
	arc_SupersedeWriteOnly,
	arc_SupersedeReadWrite,
	arc_OpenDirectory,
	arc_createDirectory
} arc_open_mode_t;

typedef struct arc_calls
{
	arc_status_t (*load)(		/* Load 1 */
		char *,			/* Image to load */
		uint32_t,		/* top address */
		uint32_t *,		/* Entry address */
		uint32_t *);		/* Low address */

	arc_status_t (*invoke)(		/* Invoke 2 */
		uint32_t,		/* Entry Address */
		uint32_t,		/* Stack Address */
		uint32_t,		/* Argc */
		char **,		/* argv */
		char **);		/* envp */

	arc_status_t (*execute)(	/* Execute 3 */
		char *,			/* Image path */
		uint32_t,		/* Argc */
		char **,		/* argv */
		char **);		/* envp */

	void (*halt)(void);	/* Halt 4 */

	void (*power_down)(void); /* PowerDown 5 */

	void (*restart)(void);	/* Restart 6 */

	void (*reboot)(void);	/* Reboot 7 */

	void (*enter_interactive_mode)(void); /* EnterInteractiveMode 8 */

	void (*return_from_main)(void); /* ReturnFromMain 9 */

	arc_config_t *(*get_peer)(	/* GetPeer 10 */
		arc_config_t *); 	/* Component */

	arc_config_t *(*get_child)(	/* GetChild 11 */
		arc_config_t *);	/* Component */

	arc_config_t *(*get_parent)(	/* GetParent 12 */
		arc_config_t *);	/* Component */

	arc_status_t (*get_config_data)( /* GetConfigurationData 13 */
		uint8_t*,		/* Configuration Data */
		arc_config_t *);	/* Component */

	arc_config_t *(*add_child)(	/* AddChild 14 */
		arc_config_t *,		/* Component */
		arc_config_t *);	/* New Component */

	arc_status_t (*delete_component)( /* DeleteComponent 15 */
		arc_config_t *);	/* Component */

	arc_config_t *(*get_component)( /* GetComponent 16 */
		char *);		/* Path */

	arc_status_t (*save_config)(void); /* SaveConfiguration 17 */

	arc_sid_t *(*get_system_id)(void); /* GetSystemId 18 */

	arc_mem_t *(*get_memory_descriptor)( /* GetMemoryDescriptor 19 */
		arc_mem_t *);		/* MemoryDescriptor */

	void (*signal)(			/* Signal 20 */
		uint32_t,		/* Signal number */
/**/		uint8_t*);		/* Handler */

	arc_time_t *(*get_time)(void);	/* GetTime 21 */

	uint32_t (*get_relative_time)(void); /* GetRelativeTime 22 */

	arc_status_t (*get_dir_entry)(	/* GetDirectoryEntry 23 */
		uint32_t,		/* FileId */
		arc_dirent_t *,		/* Directory entry */
		uint32_t,		/* Length */
		uint32_t *);		/* Count */

	arc_status_t (*open)(		/* Open 24 */
		char *,			/* Path */
		arc_open_mode_t,	/* Open mode */
		uint32_t *);		/* FileId */

	arc_status_t (*close)(		/* Close 25 */
		uint32_t);		/* FileId */

	arc_status_t (*read)(		/* Read 26 */
		uint32_t,		/* FileId */
		uint8_t*,		/* Buffer */
		uint32_t,		/* Length */
		uint32_t *);		/* Count */

	arc_status_t (*get_read_status)( /* GetReadStatus 27 */
		uint32_t);		/* FileId */

	arc_status_t (*write)(		/* Write 28 */
		uint32_t,		/* FileId */
		uint8_t*,		/* Buffer */
		uint32_t,		/* Length */
		uint32_t *);		/* Count */

	arc_status_t (*seek)(		/* Seek 29 */
		uint32_t,		/* FileId */
		int64_t *,		/* Offset */
		arc_seek_mode_t); 	/* Mode */

	arc_status_t (*mount)(		/* Mount 30 */
		char *,			/* Path */
		arc_mount_t);		/* Operation */

	char *(*getenv)(			/* GetEnvironmentVariable 31 */
		char *);		/* Variable */

	arc_status_t (*putenv)(		/* SetEnvironmentVariable 32 */
		char *,			/* Variable */
		char *);		/* Value */

	arc_status_t (*get_file_info)(void);	/* GetFileInformation 33 */

	arc_status_t (*set_file_info)(void);	/* SetFileInformation 34 */

	void (*flush_all_caches)(void);	/* FlushAllCaches 35 */

	arc_status_t (*test_unicode)(	/* TestUnicodeCharacter 36 */
		uint32_t,		/* FileId */
		uint16_t);		/* UnicodeCharacter */

	arc_dsp_stat_t *(*get_display_status)( /* GetDisplayStatus 37 */
		uint32_t);		/* FileId */
} arc_calls_t;

#define ARC_PARAM_BLK_MAGIC	0x53435241	/* "ARCS" in little endian */
#define ARC_PARAM_BLK_MAGIC_BUG	0x41524353	/* This is wrong... but req */

typedef struct arc_param_blk 
{
	uint32_t	magic;		/* Magic Number */
	uint32_t	length;		/* Length of parameter block */
	uint16_t	version;	/* ?? */
	uint16_t	revision;	/* ?? */
/**/	uint8_t*		restart_block;	/* ?? */
/**/	uint8_t*		debug_block;	/* Debugging info -- unused */
/**/	uint8_t*		general_exp_vect; /* ?? */
/**/	uint8_t*		tlb_miss_exp_vect; /* ?? */
	uint32_t	firmware_length; /* Size of Firmware jumptable in bytes */
	arc_calls_t	*firmware_vect;	/* Firmware jumptable */
	uint32_t	vendor_length;	/* Size of Vendor specific jumptable */
/**/	uint8_t*		vendor_vect;	/* Vendor specific jumptable */
	uint32_t	adapter_count;	/* ?? */
	struct arc_adapter_param {
		uint32_t	adapter_type;	/* ?? */
		uint32_t	adapter_length; /* ?? */
/**/		uint8_t*		adapter_vect;	/* ?? */
	} adapters[1];
} arc_param_blk_t;

#define ArcBiosBase ((arc_param_blk_t *) 0x80001000)
#define ArcBios (ArcBiosBase->firmware_vect)

#if 0
arc_status_t Bios_Read __P((int, void *, int, int *));
arc_status_t Bios_Write __P((int, void *, int, int *));
arc_status_t Bios_Open __P((char *, int, unsigned int *));
arc_status_t Bios_Close __P((unsigned int));
arc_mem_t *Bios_GetMemoryDescriptor __P((arc_mem_t *));
arc_sid_t *Bios_GetSystemId __P((void));
arc_config_t *Bios_GetChild __P((arc_config_t *));
arc_config_t *Bios_GetPeer __P((arc_config_t *));
int Bios_GetConfigurationData __P((void *, arc_config_t *));
arc_dsp_stat_t *Bios_GetDisplayStatus __P((int));

const char *const arc_strerror __P((int error));

int  biosgetc __P((dev_t));
void biosputc __P((dev_t, int));
#endif

#endif	/*  ARCBIOS_OTHER_H  */

