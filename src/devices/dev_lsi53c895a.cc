/*
 * QEMU LSI53C895A SCSI Host Bus Adapter emulation
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 */

/* Note:
 * LSI53C810 emulation is incorrect, in the sense that it supports
 * features added in later evolutions. This should not be a problem,
 * as well-behaved operating systems will not try to use them.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "diskimage.h"
#include "interrupt.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
// #include "qemu/osdep.h"

// #include "hw/irq.h"
// #include "hw/pci/pci.h"
// #include "hw/scsi/scsi.h"
// #include "migration/vmstate.h"
// #include "sysemu/dma.h"
// #include "qemu/log.h"
// #include "qemu/module.h"
// #include "trace.h"
// #include "qom/object.h"

#define DEV_LSI53C895A_LENGTH 0x60
#define	LSI_TICK_SHIFT	17

#define MEMTXATTRS_UNSPECIFIED 1
#define ARRAY_SIZE(a) ((sizeof(a))/sizeof(a[0]))
#define ABORT() do { fprintf(stderr, "ABORT %s:%d\n", __FILE__, __LINE__); abort(); } while(0)

static const char *names[] = {
    "SCNTL0", "SCNTL1", "SCNTL2", "SCNTL3", "SCID", "SXFER", "SDID", "GPREG",
    "SFBR", "SOCL", "SSID", "SBCL", "DSTAT", "SSTAT0", "SSTAT1", "SSTAT2",
    "DSA0", "DSA1", "DSA2", "DSA3", "ISTAT", "0x15", "0x16", "0x17",
    "CTEST0", "CTEST1", "CTEST2", "CTEST3", "TEMP0", "TEMP1", "TEMP2", "TEMP3",
    "DFIFO", "CTEST4", "CTEST5", "CTEST6", "DBC0", "DBC1", "DBC2", "DCMD",
    "DNAD0", "DNAD1", "DNAD2", "DNAD3", "DSP0", "DSP1", "DSP2", "DSP3",
    "DSPS0", "DSPS1", "DSPS2", "DSPS3", "SCRATCHA0", "SCRATCHA1", "SCRATCHA2", "SCRATCHA3",
    "DMODE", "DIEN", "SBR", "DCNTL", "ADDER0", "ADDER1", "ADDER2", "ADDER3",
    "SIEN0", "SIEN1", "SIST0", "SIST1", "SLPAR", "0x45", "MACNTL", "GPCNTL",
    "STIME0", "STIME1", "RESPID", "0x4b", "STEST0", "STEST1", "STEST2", "STEST3",
    "SIDL", "0x51", "0x52", "0x53", "SODL", "0x55", "0x56", "0x57",
    "SBDL", "0x59", "0x5a", "0x5b", "SCRATCHB0", "SCRATCHB1", "SCRATCHB2", "SCRATCHB3",
};

#define LSI_MAX_DEVS 7

#define LSI_SCNTL0_TRG    0x01
#define LSI_SCNTL0_AAP    0x02
#define LSI_SCNTL0_EPC    0x08
#define LSI_SCNTL0_WATN   0x10
#define LSI_SCNTL0_START  0x20

#define LSI_SCNTL1_SST    0x01
#define LSI_SCNTL1_IARB   0x02
#define LSI_SCNTL1_AESP   0x04
#define LSI_SCNTL1_RST    0x08
#define LSI_SCNTL1_CON    0x10
#define LSI_SCNTL1_DHP    0x20
#define LSI_SCNTL1_ADB    0x40
#define LSI_SCNTL1_EXC    0x80

#define LSI_SCNTL2_WSR    0x01
#define LSI_SCNTL2_VUE0   0x02
#define LSI_SCNTL2_VUE1   0x04
#define LSI_SCNTL2_WSS    0x08
#define LSI_SCNTL2_SLPHBEN 0x10
#define LSI_SCNTL2_SLPMD  0x20
#define LSI_SCNTL2_CHM    0x40
#define LSI_SCNTL2_SDU    0x80

#define LSI_ISTAT0_DIP    0x01
#define LSI_ISTAT0_SIP    0x02
#define LSI_ISTAT0_INTF   0x04
#define LSI_ISTAT0_CON    0x08
#define LSI_ISTAT0_SEM    0x10
#define LSI_ISTAT0_SIGP   0x20
#define LSI_ISTAT0_SRST   0x40
#define LSI_ISTAT0_ABRT   0x80

#define LSI_ISTAT1_SI     0x01
#define LSI_ISTAT1_SRUN   0x02
#define LSI_ISTAT1_FLSH   0x04

#define LSI_SSTAT0_SDP0   0x01
#define LSI_SSTAT0_RST    0x02
#define LSI_SSTAT0_WOA    0x04
#define LSI_SSTAT0_LOA    0x08
#define LSI_SSTAT0_AIP    0x10
#define LSI_SSTAT0_OLF    0x20
#define LSI_SSTAT0_ORF    0x40
#define LSI_SSTAT0_ILF    0x80

#define LSI_SIST0_PAR     0x01
#define LSI_SIST0_RST     0x02
#define LSI_SIST0_UDC     0x04
#define LSI_SIST0_SGE     0x08
#define LSI_SIST0_RSL     0x10
#define LSI_SIST0_SEL     0x20
#define LSI_SIST0_CMP     0x40
#define LSI_SIST0_MA      0x80

#define LSI_SIST1_HTH     0x01
#define LSI_SIST1_GEN     0x02
#define LSI_SIST1_STO     0x04
#define LSI_SIST1_SBMC    0x10

#define LSI_SOCL_IO       0x01
#define LSI_SOCL_CD       0x02
#define LSI_SOCL_MSG      0x04
#define LSI_SOCL_ATN      0x08
#define LSI_SOCL_SEL      0x10
#define LSI_SOCL_BSY      0x20
#define LSI_SOCL_ACK      0x40
#define LSI_SOCL_REQ      0x80

#define LSI_DSTAT_IID     0x01
#define LSI_DSTAT_SIR     0x04
#define LSI_DSTAT_SSI     0x08
#define LSI_DSTAT_ABRT    0x10
#define LSI_DSTAT_BF      0x20
#define LSI_DSTAT_MDPE    0x40
#define LSI_DSTAT_DFE     0x80

#define LSI_DCNTL_COM     0x01
#define LSI_DCNTL_IRQD    0x02
#define LSI_DCNTL_STD     0x04
#define LSI_DCNTL_IRQM    0x08
#define LSI_DCNTL_SSM     0x10
#define LSI_DCNTL_PFEN    0x20
#define LSI_DCNTL_PFF     0x40
#define LSI_DCNTL_CLSE    0x80

#define LSI_DMODE_MAN     0x01
#define LSI_DMODE_BOF     0x02
#define LSI_DMODE_ERMP    0x04
#define LSI_DMODE_ERL     0x08
#define LSI_DMODE_DIOM    0x10
#define LSI_DMODE_SIOM    0x20

#define LSI_CTEST2_DACK   0x01
#define LSI_CTEST2_DREQ   0x02
#define LSI_CTEST2_TEOP   0x04
#define LSI_CTEST2_PCICIE 0x08
#define LSI_CTEST2_CM     0x10
#define LSI_CTEST2_CIO    0x20
#define LSI_CTEST2_SIGP   0x40
#define LSI_CTEST2_DDIR   0x80

#define LSI_CTEST5_BL2    0x04
#define LSI_CTEST5_DDIR   0x08
#define LSI_CTEST5_MASR   0x10
#define LSI_CTEST5_DFSN   0x20
#define LSI_CTEST5_BBCK   0x40
#define LSI_CTEST5_ADCK   0x80

#define LSI_CCNTL0_DILS   0x01
#define LSI_CCNTL0_DISFC  0x10
#define LSI_CCNTL0_ENNDJ  0x20
#define LSI_CCNTL0_PMJCTL 0x40
#define LSI_CCNTL0_ENPMJ  0x80

#define LSI_CCNTL1_EN64DBMV  0x01
#define LSI_CCNTL1_EN64TIBMV 0x02
#define LSI_CCNTL1_64TIMOD   0x04
#define LSI_CCNTL1_DDAC      0x08
#define LSI_CCNTL1_ZMOD      0x80

#define LSI_SBCL_ATN         0x08
#define LSI_SBCL_BSY         0x20
#define LSI_SBCL_ACK         0x40
#define LSI_SBCL_REQ         0x80

/* Enable Response to Reselection */
#define LSI_SCID_RRE      0x60

#define LSI_CCNTL1_40BIT (LSI_CCNTL1_EN64TIBMV|LSI_CCNTL1_64TIMOD)

#define PHASE_DO          0
#define PHASE_DI          1
#define PHASE_CMD         2
#define PHASE_ST          3
#define PHASE_MO          6
#define PHASE_MI          7
#define PHASE_MASK        7

/* Maximum length of MSG IN data.  */
#define LSI_MAX_MSGIN_LEN 8

/* Flag set if this is a tagged command.  */
#define LSI_TAG_VALID     (1 << 16)

/* Maximum instructions to process. */
#define LSI_MAX_INSN    10000

#define LOG_UNIMP 1
#define LSI53C895A(x) ((lsi53c895a_data *)(x))

#define PCI_BASE_ADDRESS_SPACE_IO 0x10
#define PCI_BASE_ADDRESS_SPACE_MEMORY 0x14
#define PCI_INTERRUPT_PIN 0x3d
#define PCI_LATENCY_TIMER 0x0d
#define DEVICE_LITTLE_ENDIAN MEM_PCI_LITTLE_ENDIAN

typedef uint32_t dma_addr_t;
typedef uint32_t hwaddr;

struct SCSIDevice {
    int id;
    int sense_data_len;
    char sense_data[64];
};

typedef struct SCSIDevice DeviceState;

struct QBus {
    struct lsi53c895a_data *parent;
};

struct SCSIRequest;
struct SCSIBus;

#define SCSI_CMD_BUF_SIZE 16

typedef struct SCSICommand {
    uint8_t buf[SCSI_CMD_BUF_SIZE];
    int len;
    size_t xfer;
    uint64_t lba;
    int mode;
} SCSICommand;

struct lsi_request;
struct SCSIRequest {
    SCSIRequest *next;
    SCSIRequest *prev;

    SCSIDevice *dev;
    SCSIBus *bus;
    SCSICommand cmd;
    struct lsi_request *hba_private;
    int status;
    int tag;
    int refct;
    int transferred;
    int enqueued;
    uint8_t *result_buf;
    int result_len;
    struct scsi_transfer xfer;
};

struct SCSIBus {
    QBus qbus;
    SCSIRequest queue;
    SCSIDevice devices[8];
};

#define BUS(bus) ((SCSIBus *)&(bus))

static void scsi_bus_init(SCSIBus *bus) {
    ABORT();
}

static void bus_cold_reset(SCSIBus *bus) {
    fprintf(stderr, "lsi: bus_cold_reset\n");
}

static void lsi_soft_reset(struct lsi53c895a_data *s);

static void device_cold_reset(struct lsi53c895a_data *device) {
    fprintf(stderr, "lsi: device_cold_reset (myself)\n");
    lsi_soft_reset(device);
}

typedef struct lsi_request {
    struct lsi_request *next;
    struct lsi_request *prev;

    SCSIRequest *req;
    uint32_t tag;
    uint32_t dma_len;
    uint8_t *dma_buf;
    uint32_t pending;
    int out;
} lsi_request;

enum {
    LSI_NOWAIT, /* SCRIPTS are running or stopped */
    LSI_WAIT_RESELECT, /* Wait Reselect instruction has been issued */
    LSI_DMA_SCRIPTS, /* processing DMA from lsi_execute_script */
    LSI_DMA_IN_PROGRESS, /* DMA operation is in progress */
};

enum {
    LSI_MSG_ACTION_COMMAND = 0,
    LSI_MSG_ACTION_DISCONNECT = 1,
    LSI_MSG_ACTION_DOUT = 2,
    LSI_MSG_ACTION_DIN = 3,
};

typedef struct interrupt qemu_irq;

#define QTAILQ_INIT(HEAD) \
    do {                  \
        (HEAD)->prev = (HEAD); \
        (HEAD)->next = (HEAD); \
    } while (0)

#define QTAILQ_FOREACH(p, HEAD, next)                   \
    for (p = (HEAD)->next; p != (HEAD); p = (p)->next)

#define QTAILQ_FOREACH_SAFE(p, HEAD, next, _)             \
    for (p = (HEAD)->next; p != (HEAD); p = (p)->next)

#define QTAILQ_INSERT_TAIL(HEAD, ELT, _) \
    do { \
        fprintf(stderr, "lsi: insert tail %p\n", ELT); \
        (ELT)->next = (HEAD)->next; \
        (ELT)->prev = (HEAD)->prev; \
        (ELT)->prev->next = (ELT); \
        (HEAD)->prev = (ELT); \
    } while(0)

#define QTAILQ_REMOVE(HEAD, ELT, _) \
    do { \
        (ELT)->next->prev = (ELT)->prev; \
        (ELT)->prev->next = (ELT)->next; \
    } while(0)

struct AddressSpace {
    uint64_t range_start;
    uint32_t range_length;
};

struct MemoryRegion {
    uint64_t range_start;
    uint32_t range_length;
};

struct MemoryRegionImplDetails {
    int min_access_size;
    int max_access_size;
};

struct MemoryRegionOps {
    uint64_t (*read)(void *opaque, hwaddr addr, unsigned size);
    void (*write)(void *opaque, hwaddr addr, uint64_t val, unsigned size);
    int endianness;
    MemoryRegionImplDetails impl;
};

struct lsi53c895a_data {
    /*< private >*/
    uint8_t config[256];

    // PCIDevice parent_obj;
    /*< public >*/
    int asserted;

    qemu_irq ext_irq;
    MemoryRegion mmio_io;
    MemoryRegion ram_io;
    MemoryRegion io_io;
    AddressSpace pci_io_as;

    int carry; /* ??? Should this be an a visible register somewhere?  */
    int status;
    int msg_action;
    int msg_len;
    uint8_t msg[LSI_MAX_MSGIN_LEN];
    int waiting;

    SCSIBus bus;

    int current_lun;
    /* The tag is a combination of the device ID and the SCSI tag.  */
    uint32_t select_tag;
    int command_complete;
    lsi_request queue;
    lsi_request *current;

    uint32_t dsa;
    uint32_t temp;
    uint32_t dnad;
    uint32_t dbc;
    uint8_t istat0;
    uint8_t istat1;
    uint8_t dcmd;
    uint8_t dstat;
    uint8_t dien;
    uint8_t sist0;
    uint8_t sist1;
    uint8_t sien0;
    uint8_t sien1;
    uint8_t mbox0;
    uint8_t mbox1;
    uint8_t dfifo;
    uint8_t ctest2;
    uint8_t ctest3;
    uint8_t ctest4;
    uint8_t ctest5;
    uint8_t ccntl0;
    uint8_t ccntl1;
    uint32_t dsp;
    uint32_t dsps;
    uint8_t dmode;
    uint8_t dcntl;
    uint8_t scntl0;
    uint8_t scntl1;
    uint8_t scntl2;
    uint8_t scntl3;
    uint8_t sstat0;
    uint8_t sstat1;
    uint8_t scid;
    uint8_t sxfer;
    uint8_t socl;
    uint8_t sdid;
    uint8_t ssid;
    uint8_t sfbr;
    uint8_t sbcl;
    uint8_t stest1;
    uint8_t stest2;
    uint8_t stest3;
    uint8_t sidl;
    uint8_t macntl;
    uint8_t stime0;
    uint8_t respid0;
    uint8_t respid1;
    uint32_t mmrs;
    uint32_t mmws;
    uint32_t sfs;
    uint32_t drs;
    uint32_t sbms;
    uint32_t dbms;
    uint32_t dnad64;
    uint32_t pmjad1;
    uint32_t pmjad2;
    uint32_t rbc;
    uint32_t ua;
    uint32_t ia;
    uint32_t sbc;
    uint32_t csbc;
    uint32_t scratch[18]; /* SCRATCHA-SCRATCHR */
    uint8_t sbr;
    uint32_t adder;

    uint8_t script_ram[2048 * sizeof(uint32_t)];
};

#define PCI_DEVICE(s) (s)

typedef struct lsi53c895a_data PCIDevice;
typedef struct lsi53c895a_data LSIState;

#define TYPE_LSI53C810  "lsi53c810"
#define TYPE_LSI53C895A "lsi53c895a"

// OBJECT_DECLARE_SIMPLE_TYPE(LSIState, LSI53C895A)

static const char *scsi_phases[] = {
    "DOUT",
    "DIN",
    "CMD",
    "STATUS",
    "RSVOUT",
    "RSVIN",
    "MSGOUT",
    "MSGIN"
};

#define TRACE(kind, alist, args...) fprintf(stderr, kind " %s:%d (" alist ")\n", __FILE__, __LINE__ ,##args)

#define trace_lsi_reset(args...) TRACE("trace_lsi_reset", "",##args)
#define trace_lsi_update_irq(args...) TRACE("trace_lsi_update_irq", "%d %08x %08x %08x",##args)
#define trace_lsi_update_irq_disconnected(args...) TRACE("trace_lsi_update_irq_disconnected", "",##args)
#define trace_lsi_script_scsi_interrupt(args...) TRACE("trace_lsi_script_scsi_interrupt", "%08x %08x %08x %08x",##args)
#define trace_lsi_script_dma_interrupt(args...) TRACE("trace_lsi_script_dma_interrupt", "%08x %08x",##args)
#define trace_lsi_bad_phase_jump(args...) TRACE("trace_lsi_bad_phase_jump", "%08x",##args)
#define trace_lsi_bad_phase_interrupt(args...) TRACE("trace_lsi_bad_phase_interrupt", "",##args)
#define trace_lsi_bad_selection(args...) TRACE("trace_lsi_bad_selection", "%08x",##args)
#define trace_lsi_do_dma_unavailable(args...) TRACE("trace_lsi_do_dma_unavailable", "",##args)
#define trace_lsi_do_dma(args...) TRACE("trace_lsi_do_dma", "%08x %08x",##args)
#define trace_lsi_queue_command(args...) TRACE("trace_lsi_queue_command", "%08x",##args)
#define trace_lsi_add_msg_byte_error(args...) TRACE("trace_lsi_add_msg_byte_error", "",##args)
#define trace_lsi_add_msg_byte(args...) TRACE("trace_lsi_add_msg_byte", "%02x",##args)
#define trace_lsi_reselect(args...) TRACE("trace_lsi_reselect", "%08x",##args)
#define trace_lsi_queue_req_error(args...) TRACE("trace_lsi_queue_req_error", "%p",##args)
#define trace_lsi_queue_req(args...) TRACE("trace_lsi_queue_req", "%08x",##args)
#define trace_lsi_command_complete(args...) TRACE("trace_lsi_command_complete", "%d",##args)
#define trace_lsi_transfer_data(args...) TRACE("trace_lsi_transfer_data", "%08x %08x",##args)
#define trace_lsi_do_command(args...) TRACE("trace_lsi_do_command", "%08x len=%d",##args)
#define trace_lsi_do_status(args...) TRACE("trace_lsi_do_status", "%08x %08x",##args)
#define trace_lsi_do_status_error(args...) TRACE("trace_lsi_do_status_error", "",##args)
#define trace_lsi_do_msgin(args...) TRACE("trace_lsi_do_msgin", "%08x %08x",##args)
#define trace_lsi_do_msgout(args...) TRACE("trace_lsi_do_msgout", "%08x",##args)
#define trace_lsi_do_msgout_disconnect(args...) TRACE("trace_lsi_do_msgout_disconnect", "",##args)
#define trace_lsi_do_msgout_noop(args...) TRACE("trace_lsi_do_msgout_noop", "",##args)
#define trace_lsi_do_msgout_extended(args...) TRACE("trace_lsi_do_msgout_extended", "%02x %02x",##args)
#define trace_lsi_do_msgout_ignored(args...) TRACE("trace_lsi_do_msgout_ignored", "%s",##args)
#define trace_lsi_do_msgout_simplequeue(args...) TRACE("trace_lsi_do_msgout_simplequeue", "%02x",##args)
#define trace_lsi_do_msgout_abort(args...) TRACE("trace_lsi_do_msgout_abort", "%08x",##args)
#define trace_lsi_do_msgout_clearqueue(args...) TRACE("trace_lsi_do_msgout_clearqueue", "%08x",##args)
#define trace_lsi_do_msgout_busdevicereset(args...) TRACE("trace_lsi_do_msgout_busdevicereset", "%08x",##args)
#define trace_lsi_do_msgout_select(args...) TRACE("trace_lsi_do_msgout_select", "%02x",##args)
#define trace_lsi_memcpy(args...) TRACE("trace_lsi_memcpy", "%08x %08x %d",##args)
#define trace_lsi_wait_reselect(args...) TRACE("trace_lsi_wait_reselect", "",##args)
#define trace_lsi_execute_script_stop(args...) TRACE("trace_lsi_execute_script_stop", "",##args)
#define trace_lsi_execute_script(args...) TRACE("trace_lsi_execute_script", "%08x %08x %08x",##args)
#define trace_lsi_execute_script_blockmove_delayed(args...) TRACE("trace_lsi_execute_script_blockmove_delayed", "",##args)
#define trace_lsi_execute_script_blockmove_badphase(args...) TRACE("trace_lsi_execute_script_blockmove_badphase", "%s %s",##args)
#define trace_lsi_execute_script_io_alreadyreselected(args...) TRACE("trace_lsi_execute_script_io_alreadyselected", "",##args)
#define trace_lsi_execute_script_io_selected(args...) TRACE("trace_lsi_execute_script_io_selected", "%08x %s",##args)
#define trace_lsi_execute_script_io_disconnect(args...) TRACE("trace_lsi_execute_script_io_disconnect", "",##args)
#define trace_lsi_execute_script_io_set(args...) TRACE("trace_lsi_execute_script_io_set", "%s %s %s %s",##args)
#define trace_lsi_execute_script_io_clear(args...) TRACE("trace_lsi_execute_script_io_clear", "%s %s %s %s",##args)
#define trace_lsi_execute_script_io_opcode(args...) TRACE("trace_lsi_execute_script_io_opcode", "%s %08x %s %02x %08x %s",##args)
#define trace_lsi_execute_script_tc_nop(args...) TRACE("trace_lsi_execute_script_tc_nop", "",##args)
#define trace_lsi_execute_script_tc_delayedselect_timeout(args...) TRACE("trace_lsi_execute_script_tc_delayedselect_timeout", "",##args)
#define trace_lsi_execute_script_tc_compc(args...) TRACE("trace_lsi_execute_script_tc_compc", "%d",##args)
#define trace_lsi_execute_script_tc_compp(args...) TRACE("trace_lsi_execute_script_tc_compp", "%s %c %s",##args)
#define trace_lsi_execute_script_tc_compd(args...) TRACE("trace_lsi_execute_script_tc_compd", "%08x %08x %08x %08x",##args)
#define trace_lsi_execute_script_tc_jump(args...) TRACE("trace_lsi_execute_script_tc_jump", "%08x",##args)
#define trace_lsi_execute_script_tc_call(args...) TRACE("trace_lsi_execute_script_tc_call", "%08x",##args)
#define trace_lsi_execute_script_tc_return(args...) TRACE("trace_lsi_execute_script_tc_return", "%08x",##args)
#define trace_lsi_execute_script_tc_interrupt(args...) TRACE("trace_lsi_execute_script_tc_interrupt", "%08x",##args)
#define trace_lsi_execute_script_tc_illegal(args...) TRACE("trace_lsi_execute_script_tc_illegal", "",##args)
#define trace_lsi_execute_script_tc_cc_failed(args...) TRACE("trace_lsi_execute_script_tc_cc_failed", "",##args)
#define trace_lsi_execute_script_mm_load(args...) TRACE("trace_lsi_execute_script_mm_load", "%08x %d %08x %08x",##args)
#define trace_lsi_execute_script_mm_store(args...) TRACE("trace_lsi_execute_script_mm_store", "%08x %d %08x",##args)
#define trace_lsi_reg_read(args...) TRACE("trace_lsi_reg_read", "%s %08x %08x",##args)
#define trace_lsi_reg_write(args...) TRACE("trace_lsi_reg_write", "%s %08x %08x",##args)
#define trace_lsi_awoken(args...) TRACE("trace_lsi_awoken", "",##args)

#define qemu_log_mask(_, args...) fprintf(stderr,args)

#define MIN(x,y) (((x)<(y))?(x):(y))
#define g_new0(TYPE, N) ((TYPE*)calloc(sizeof(TYPE), N))

static void g_free(void *p) {
    free(p);
}

static void scsi_req_unref(struct SCSIRequest *req) {
    if (--req->refct == 0) {
        free(req->xfer.msg_in);
        free(req->xfer.msg_out);
        free(req);
    }
}

static SCSIDevice *scsi_device_find(struct cpu *cpu, struct lsi53c895a_data *d, SCSIBus *bus, uint8_t channel, uint8_t id, uint8_t lun) {
    if (diskimage_exist(cpu->machine, id, DISKIMAGE_SCSI)) {
        fprintf(stderr, "lsi: found device %d: %p\n", id, &bus->devices[id]);
        return &bus->devices[id];
    } else {
        return nullptr;
    }
}

static SCSIRequest *scsi_req_new(struct lsi53c895a_data *d, SCSIDevice *dev, uint32_t tag, uint8_t lun, uint8_t *buf, uint32_t dbc, lsi_request *current) {
    SCSIRequest *req = (SCSIRequest *)calloc(1, sizeof(SCSIRequest));
    req->dev = dev;
    req->bus = &d->bus;
    req->hba_private = current;
    req->status = 0;
    req->tag = tag;
    req->refct = 1;
    fprintf(stderr, "lsi: create command %02x with %d bytes to ID%d\n", req->cmd.buf[0], dbc, dev->id);
    memcpy(req->cmd.buf, buf, MIN(16, dbc));
    req->cmd.len = dbc;
    return req;
}

static void lsi_transfer_data(struct cpu *cpu, SCSIRequest *req, uint32_t len);
static void lsi_command_complete(struct cpu *cpu, SCSIRequest *req, size_t resid);
static void lsi_add_msg_byte(LSIState *s, uint8_t data);

static int scsi_req_enqueue(struct cpu *cpu, SCSIRequest *req) {
    auto s = req->bus->qbus.parent;

    fprintf(stderr, "lsi: DNAD is %08x at ENQUEUE, DBC %08x DSA %08x\n", s->dnad, s->dbc, s->dsa);

    if (s->current_lun != 0) {
        fprintf(stderr, "LSI: request lun %d, give error\n", s->current_lun);
        req->status = 2; // CHECK_CONDITION;
        req->dev->sense_data_len = 18;
        memset(req->dev->sense_data, 0, sizeof(req->dev->sense_data));
        req->dev->sense_data[0] = 0xf0; // VALID + 0x70h
        req->dev->sense_data[2] = 5; // ILLEGAL_REQUEST;
        req->dev->sense_data[7] = 0;
        req->dev->sense_data[12] = 0x25; // LOGICAL_UNIT_NOT_SUPPORTED;
        return 1;
    }

    assert(!req->enqueued);
    req->enqueued = true;
    req->refct++;

    switch (req->cmd.buf[0]) {
    case 0x03: {
        if (req->cmd.buf[1] & 1) {
            ABORT();
        }

        if (!req->dev->sense_data_len) {
            req->dev->sense_data_len = 18;
            req->dev->sense_data[0] = 0xf0;
        }
        req->xfer.data_in = (uint8_t *)calloc(req->dev->sense_data_len, 1);
        req->xfer.data_in_len = req->dev->sense_data_len;
        req->dev->sense_data_len = 0;
        memcpy(req->xfer.data_in, req->dev->sense_data, req->dev->sense_data_len);
        fprintf(stderr, "LSI: Request sense returning %d bytes\n", req->dev->sense_data_len);
        return req->xfer.data_in_len;
    }

    case 0x00:
    case 0x1b:
    case 0x1e:
    case 0x35: {
        return 1;
    }

    case 0x0a:
    case 0x2a: {
        req->hba_private->out = 1;
        return -1;
    }

    case 0x08:
    case 0x12:
    case 0x1a:
    case 0x25:
    case 0x28: {
        if (req->cmd.buf[0] == 0x12 && req->cmd.buf[1] & 0x20 && req->cmd.buf[2]) {
            req->status = 2; // CHECK_CONDITION
            return 1;
        }
        req->xfer.cmd = req->cmd.buf;
        req->xfer.cmd_len = req->cmd.len;
        auto cmd_res = diskimage_scsicommand(cpu, req->dev->id, DISKIMAGE_SCSI, &req->xfer);

        fprintf(
            stderr,
            "lsi: disk cmd %d msg_out %d data_out %d data_in %d msg_in %d status %d bytes\n",
            req->xfer.cmd_len,
            req->xfer.msg_out_len,
            req->xfer.data_out_len,
            req->xfer.data_in_len,
            req->xfer.msg_in_len,
            req->xfer.status_len
            );

        if (req->xfer.data_in_len == 0) {
          req->cmd.buf[0] = 0;
          return 1;
        }

        req->status = req->xfer.status[0];
        QTAILQ_INSERT_TAIL(&req->bus->queue, req, next);
        req->result_buf = req->xfer.data_in;
        req->result_len = req->xfer.data_in_len;
        fprintf(stderr, "result_buf: %p len: %d\n", req->result_buf, req->result_len);
        return req->xfer.data_in_len;
    }
    default:
        fprintf(stderr, "lsi: unknown opcode %02x\n", req->cmd.buf[0]);
        ABORT();
    }
}

static inline uint32_t cpu_to_le32(struct cpu *cpu, uint32_t word) {
    return word;
}

static inline uint32_t sextract32(uint32_t val, uint32_t first_bit, uint32_t last_bit) {
    auto mask = (1 << last_bit) - 1;
    auto sign_bit = val & (1 << (first_bit + last_bit - 1));
    auto sign_mask = ~((1 << (first_bit + last_bit)) - 1);
    uint32_t res;
    if (sign_bit) {
        res = (val >> first_bit) | sign_mask;
    } else {
        res = (val >> first_bit) & mask;
    }
    return res;
}

static inline uint32_t deposit32(uint32_t val, uint32_t first_bit, uint32_t last_bit, uint32_t replacement) {
    auto mask = ((1 << last_bit) - 1) << first_bit;
    return (val & ~mask) | ((replacement << first_bit) & mask);
}

static void stn_le_p(uint8_t *addr, unsigned size, uint32_t val) {
    switch (size) {
    case 4: {
#ifdef HOST_LITTLE_ENDIAN
        uint32_t swapped = SWAP32(val);
#else
        uint32_t swapped = val;
#endif
        memcpy(addr, &swapped, size);
        break;
    }

    case 2: {
#ifdef HOST_LITTLE_ENDIAN
        uint32_t swapped = SWAP16(val);
#else
        uint32_t swapped = val;
#endif
        memcpy(addr, &swapped, size);
        break;
    }

    case 1:
        *addr = val;
        break;

    default:
        ABORT();
        break;
    }
}

static uint32_t ldn_le_p(uint8_t *addr, unsigned size) {
    switch (size) {
    case 4:
#ifdef HOST_LITTLE_ENDIAN
        return SWAP32(*((uint32_t *)addr));
#else
        return *((uint32_t *)addr);
#endif

    case 2:
#ifdef HOST_LITTLE_ENDIAN
        return SWAP16(*((uint16_t *)addr));
#else
        return *((uint16_t *)addr);
#endif

    case 1:
        return *addr;

    default:
        ABORT();
        break;
    }
}

struct Error {
};

static void address_space_init
(AddressSpace *addrspace,
 uint32_t addr_space_id,
 const char *name
 )
{
    ABORT();
}

static void address_space_read
(AddressSpace *addrspace,
 dma_addr_t address,
 int flags,
 void *buf,
 dma_addr_t len
 )
{
    ABORT();
}

static void address_space_write
(AddressSpace *addrspace,
 dma_addr_t address,
 int flags,
 const void *buf,
 dma_addr_t len
 )
{
    ABORT();
}

static uint8_t *scsi_req_get_buf(SCSIRequest *req) {
    return req->result_buf;
}

static void scsi_req_cancel(SCSIRequest *req) {
    ABORT();
}

static void lsi_transfer_data(struct cpu *cpu, SCSIRequest *req, uint32_t len);
static void lsi_command_complete(struct cpu *cpu, SCSIRequest *req, size_t resid);

static void scsi_req_continue(struct cpu *cpu, SCSIRequest *req) {
    size_t result_size;

    if (req->status != 0) {
        fprintf(stderr, "LSI: scsi_req_continue stop with error %d\n", req->status);
        lsi_command_complete(cpu, req, 0);
        return;
    }

    switch (req->cmd.buf[0]) {
    case 0x00:
    case 0x1b:
    case 0x1e:
    case 0x35: {
        lsi_transfer_data(cpu, req, result_size);
        lsi_command_complete(cpu, req, result_size);
        break;
    }
    case 0x0a:
    case 0x2a: {
        // Write
        if (!req->transferred) {
            req->transferred++;
            lsi_transfer_data(cpu, req, result_size);
        } else {
            req->xfer.data_out_len = req->hba_private->dma_len;
            req->xfer.data_out = (uint8_t*)malloc(req->xfer.data_out_len);
            memcpy(req->xfer.data_out, req->hba_private->dma_buf, req->xfer.data_out_len);
            auto cmd_res = diskimage_scsicommand(cpu, req->dev->id, DISKIMAGE_SCSI, &req->xfer);
            free(req->xfer.data_out);
            req->status = cmd_res;
            lsi_command_complete(cpu, req, req->xfer.data_out_len);
        }
        break;
    }
    case 0x03:
    case 0x08:
    case 0x12:
    case 0x1a:
    case 0x25:
    case 0x28: {
        fprintf(stderr, "lsi: transferred %d (cmd %02x)\n", req->transferred, req->cmd.buf[0]);
        if (!req->transferred) {
            req->transferred++;
            result_size = req->xfer.data_in_len;
            lsi_transfer_data(cpu, req, result_size);
        } else {
            lsi_command_complete(cpu, req, result_size);
        }
        break;
    }

    default:
        fprintf(stderr, "lsi: unknown opcode %02x\n", req->cmd.buf[0]);
        ABORT();
    }
}

static uint64_t lsi_mmio_read(struct cpu *cpu, LSIState *s, hwaddr addr,
                              unsigned size);

static uint64_t lsi_ram_read(struct lsi53c895a_data *s, hwaddr addr,
                             unsigned size);
static void lsi_ram_write(struct lsi53c895a_data *s, hwaddr addr,
                          uint64_t val, unsigned size);

static void pci_dma_read(struct cpu *cpu, LSIState *s, dma_addr_t addr, void *buf, dma_addr_t len) {
    if (addr & 0x80000000) {
        fprintf(stderr, "pci_dma_read(%08x,%d)\n", (int)addr, len);
        addr &= ~0x80000000;
        for (auto i = 0; i < len; i++) {
            cpu->memory_rw(cpu, cpu->mem, addr + i, ((uint8_t*)buf) + i, 1, MEM_READ, CACHE_NONE | NO_EXCEPTIONS | PHYSICAL);
        }
        if (len == 1) {
          fprintf(stderr, "lsi: 1 byte read %02x\n", *((uint8_t *)buf));
        }
    } else {
        for (auto i = 0; i < len; i++) {
            *(((uint8_t *)buf) + i) = lsi_ram_read(s, (addr + i) & 0x1fff, 1);
        }
    }
}

static void lsi_mmio_write(struct cpu *cpu, LSIState *s, hwaddr addr,
                           uint64_t val, unsigned size);

static void pci_dma_write(struct cpu *cpu, LSIState *s, dma_addr_t addr, const void *buf, dma_addr_t len) {
    if (addr & 0x80000000) {
        fprintf(stderr, "pci_dma_write(%08x,%d)\n", (int)addr, len);
        addr &= ~0x80000000;
        for (auto i = 0; i < len; i++) {
            cpu->memory_rw(cpu, cpu->mem, addr + i, ((uint8_t*)buf) + i, 1, MEM_WRITE, CACHE_NONE | NO_EXCEPTIONS | PHYSICAL);
        }
    } else {
        for (auto i = 0; i < len; i++) {
            lsi_ram_write(s, (addr + i) & 0x1fff, ((uint8_t *)buf)[i], 1);
        }
    }
}

static const char *scsi_phase_name(int phase)
{
    return scsi_phases[phase & PHASE_MASK];
}

static inline int lsi_irq_on_rsl(LSIState *s)
{
    return (s->sien0 & LSI_SIST0_RSL) && (s->scid & LSI_SCID_RRE);
}

static lsi_request *get_pending_req(LSIState *s)
{
    struct lsi_request *p;

    QTAILQ_FOREACH(p, &s->queue, next) {
        if (p->pending) {
            return p;
        }
    }
    return NULL;
}

static void lsi_soft_reset(LSIState *s)
{
    trace_lsi_reset();
    s->carry = 0;

    for (int i = 0; i < 8; i++) {
      memset(&s->bus.devices[i], 0, sizeof(s->bus.devices[i]));
      s->bus.devices[i].id = i;
    }

    s->bus.qbus.parent = s;

    s->msg_action = LSI_MSG_ACTION_COMMAND;
    s->msg_len = 0;
    s->waiting = LSI_NOWAIT;
    s->dsa = 0;
    s->dnad = 0;
    s->dbc = 0;
    s->temp = 0;
    memset(s->scratch, 0, sizeof(s->scratch));
    s->istat0 = 0;
    s->istat1 = 0;
    s->dcmd = 0x40;
    s->dstat = 0;
    s->dien = 0;
    s->sist0 = 0;
    s->sist1 = 0;
    s->sien0 = 0;
    s->sien1 = 0;
    s->mbox0 = 0;
    s->mbox1 = 0;
    s->dfifo = 0;
    s->ctest2 = LSI_CTEST2_DACK;
    s->ctest3 = 0;
    s->ctest4 = 0;
    s->ctest5 = 0;
    s->ccntl0 = 0;
    s->ccntl1 = 0;
    s->dsp = 0;
    s->dsps = 0;
    s->dmode = 0;
    s->dcntl = 0;
    s->scntl0 = 0xc0;
    s->scntl1 = 0;
    s->scntl2 = 0;
    s->scntl3 = 0;
    s->sstat0 = 0;
    s->sstat1 = 0;
    s->scid = 7;
    s->sxfer = 0;
    s->socl = 0;
    s->sdid = 0;
    s->ssid = 0;
    s->sbcl = 0;
    s->stest1 = 0;
    s->stest2 = 0;
    s->stest3 = 0;
    s->sidl = 0;
    s->stime0 = 0;
    s->respid0 = 0x80;
    s->respid1 = 0;
    s->mmrs = 0;
    s->mmws = 0;
    s->sfs = 0;
    s->drs = 0;
    s->sbms = 0;
    s->dbms = 0;
    s->dnad64 = 0;
    s->pmjad1 = 0;
    s->pmjad2 = 0;
    s->rbc = 0;
    s->ua = 0;
    s->ia = 0;
    s->sbc = 0;
    s->csbc = 0;
    s->sbr = 0;
    assert(!s->queue);
    assert(!s->current);
}

static int lsi_dma_40bit(LSIState *s)
{
    if ((s->ccntl1 & LSI_CCNTL1_40BIT) == LSI_CCNTL1_40BIT)
        return 1;
    return 0;
}

static int lsi_dma_ti64bit(LSIState *s)
{
    if ((s->ccntl1 & LSI_CCNTL1_EN64TIBMV) == LSI_CCNTL1_EN64TIBMV)
        return 1;
    return 0;
}

static int lsi_dma_64bit(LSIState *s)
{
    if ((s->ccntl1 & LSI_CCNTL1_EN64DBMV) == LSI_CCNTL1_EN64DBMV)
        return 1;
    return 0;
}

static uint8_t lsi_reg_readb(struct cpu *cpu, LSIState *s, int offset);
static void lsi_reg_writeb(struct cpu *cpu, LSIState *s, int offset, uint8_t val);
static void lsi_execute_script(struct cpu *cpu, LSIState *s);
static void lsi_reselect(LSIState *s, lsi_request *p);

static inline void lsi_mem_read(struct cpu *cpu, LSIState *s, dma_addr_t addr,
                               void *buf, dma_addr_t len)
{
    if (s->dmode & LSI_DMODE_SIOM) {
        address_space_read(&s->pci_io_as, addr, MEMTXATTRS_UNSPECIFIED,
                           buf, len);
    } else {
        pci_dma_read(cpu, PCI_DEVICE(s), addr, buf, len);
    }
}

static inline void lsi_mem_write(struct cpu *cpu, LSIState *s, dma_addr_t addr,
                                const void *buf, dma_addr_t len)
{
    if (s->dmode & LSI_DMODE_DIOM) {
        address_space_write(&s->pci_io_as, addr, MEMTXATTRS_UNSPECIFIED,
                            buf, len);
    } else {
        pci_dma_write(cpu, PCI_DEVICE(s), addr, buf, len);
    }
}

static inline uint32_t read_dword(struct cpu *cpu, LSIState *s, uint32_t addr)
{
    uint32_t buf;

    pci_dma_read(cpu, PCI_DEVICE(s), addr, &buf, 4);
    return cpu_to_le32(cpu, buf);
}

static void lsi_stop_script(LSIState *s)
{
    s->istat1 &= ~LSI_ISTAT1_SRUN;
}

static void lsi_set_irq(LSIState *s, int level)
{
    fprintf(stderr, "lsi_set_irq(%d)\n", level);
    if (!s->asserted && level) {
        eagle_comm.eagle_comm_area[8] = 0xff;
        INTERRUPT_ASSERT(s->ext_irq);
        s->asserted = true;
    } else if (s->asserted && !level) {
        INTERRUPT_DEASSERT(s->ext_irq);
        s->asserted = false;
    }
}

static void lsi_update_irq(LSIState *s)
{
    int level;
    static int last_level;

    /* It's unclear whether the DIP/SIP bits should be cleared when the
       Interrupt Status Registers are cleared or when istat0 is read.
       We currently do the formwer, which seems to work.  */
    level = 0;
    if (s->dstat) {
        if (s->dstat & s->dien)
            level = 1;
        s->istat0 |= LSI_ISTAT0_DIP;
    } else {
        s->istat0 &= ~LSI_ISTAT0_DIP;
    }

    if (s->sist0 || s->sist1) {
        if ((s->sist0 & s->sien0) || (s->sist1 & s->sien1))
            level = 1;
        s->istat0 |= LSI_ISTAT0_SIP;
    } else {
        s->istat0 &= ~LSI_ISTAT0_SIP;
    }
    if (s->istat0 & LSI_ISTAT0_INTF)
        level = 1;

    if (level != last_level) {
        trace_lsi_update_irq(level, s->dstat, s->sist1, s->sist0);
        last_level = level;
    }
    lsi_set_irq(s, level);

    if (!s->current && !level && lsi_irq_on_rsl(s) && !(s->scntl1 & LSI_SCNTL1_CON)) {
        lsi_request *p;

        trace_lsi_update_irq_disconnected();
        p = get_pending_req(s);
        if (p) {
            lsi_reselect(s, p);
        }
    }
}

/* Stop SCRIPTS execution and raise a SCSI interrupt.  */
static void lsi_script_scsi_interrupt(LSIState *s, int stat0, int stat1)
{
    uint32_t mask0;
    uint32_t mask1;

    trace_lsi_script_scsi_interrupt(stat1, stat0, s->sist1, s->sist0);
    s->sist0 |= stat0;
    s->sist1 |= stat1;
    /* Stop processor on fatal or unmasked interrupt.  As a special hack
       we don't stop processing when raising STO.  Instead continue
       execution and stop at the next insn that accesses the SCSI bus.  */
    mask0 = s->sien0 | ~(LSI_SIST0_CMP | LSI_SIST0_SEL | LSI_SIST0_RSL);
    mask1 = s->sien1 | ~(LSI_SIST1_GEN | LSI_SIST1_HTH);
    mask1 &= ~LSI_SIST1_STO;
    if (s->sist0 & mask0 || s->sist1 & mask1) {
        lsi_stop_script(s);
    }
    lsi_update_irq(s);
}

/* Stop SCRIPTS execution and raise a DMA interrupt.  */
static void lsi_script_dma_interrupt(LSIState *s, int stat)
{
    trace_lsi_script_dma_interrupt(stat, s->dstat);
    s->dstat |= stat;
    lsi_update_irq(s);
    lsi_stop_script(s);
}

static inline void lsi_set_phase(LSIState *s, int phase)
{
    s->sbcl &= ~PHASE_MASK;
    s->sbcl |= phase | LSI_SBCL_REQ;
    s->sstat1 = (s->sstat1 & ~PHASE_MASK) | phase;
}

static void lsi_bad_phase(LSIState *s, int out, int new_phase)
{
    /* Trigger a phase mismatch.  */
    if (s->ccntl0 & LSI_CCNTL0_ENPMJ) {
        if ((s->ccntl0 & LSI_CCNTL0_PMJCTL)) {
            s->dsp = out ? s->pmjad1 : s->pmjad2;
        } else {
            s->dsp = (s->scntl2 & LSI_SCNTL2_WSR ? s->pmjad2 : s->pmjad1);
        }
        trace_lsi_bad_phase_jump(s->dsp);
    } else {
        trace_lsi_bad_phase_interrupt();
        lsi_script_scsi_interrupt(s, LSI_SIST0_MA, 0);
        lsi_stop_script(s);
    }
    lsi_set_phase(s, new_phase);
}


/* Resume SCRIPTS execution after a DMA operation.  */
static void lsi_resume_script(struct cpu *cpu, LSIState *s)
{
    if (s->waiting != 2) {
        s->waiting = LSI_NOWAIT;
        lsi_execute_script(cpu, s);
    } else {
        s->waiting = LSI_NOWAIT;
    }
}

static void lsi_disconnect(LSIState *s)
{
    s->scntl1 &= ~LSI_SCNTL1_CON;
    s->sstat1 &= ~PHASE_MASK;
    s->sbcl = 0;
}

static void lsi_bad_selection(LSIState *s, uint32_t id)
{
    trace_lsi_bad_selection(id);
    lsi_script_scsi_interrupt(s, 0, LSI_SIST1_STO);
    lsi_disconnect(s);
}

/* Initiate a SCSI layer data transfer.  */
static void lsi_do_dma(struct cpu *cpu, LSIState *s, int out)
{
    uint32_t count;
    dma_addr_t addr;
    SCSIDevice *dev;

    fprintf(stderr, "lsi: current %p dma_len %08x (DNAD %08x DBC %08x)\n", s->current, s->current ? s->current->dma_len : 0, s->dnad, s->dbc);
    if (!s->current || !s->current->dma_len) {
        /* Wait until data is available.  */
        s->istat0 |= LSI_ISTAT0_DIP | LSI_ISTAT0_SIP;
        trace_lsi_do_dma_unavailable();
        return;
    }

    dev = s->current->req->dev;
    assert(dev);

    count = s->dbc;
    if (count > s->current->dma_len)
        count = s->current->dma_len;

    addr = s->dnad;
    /* both 40 and Table Indirect 64-bit DMAs store upper bits in dnad64 */
    if (lsi_dma_40bit(s) || lsi_dma_ti64bit(s))
        addr |= ((uint64_t)s->dnad64 << 32);
    else if (s->dbms)
        addr |= ((uint64_t)s->dbms << 32);
    else if (s->sbms)
        addr |= ((uint64_t)s->sbms << 32);

    trace_lsi_do_dma(addr, count);
    s->csbc += count;
    s->dnad += count;
    s->dbc -= count;
     if (s->current->dma_buf == NULL) {
        s->current->dma_buf = scsi_req_get_buf(s->current->req);
    }
    /* ??? Set SFBR to first data byte.  */
    if (out) {
        lsi_mem_read(cpu, s, addr, s->current->dma_buf, count);
    } else {
        lsi_mem_write(cpu, s, addr, s->current->dma_buf, count);
    }
    s->current->dma_len -= count;
    if (s->current->dma_len == 0) {
        s->current->dma_buf = NULL;
        scsi_req_continue(cpu, s->current->req);
    } else {
        s->current->dma_buf += count;
        lsi_resume_script(cpu, s);
    }
}


/* Add a command to the queue.  */
static void lsi_queue_command(LSIState *s)
{
    lsi_request *p = s->current;

    trace_lsi_queue_command(p->tag);
    assert(s->current != NULL);
    assert(s->current->dma_len == 0);
    QTAILQ_INSERT_TAIL(&s->queue, s->current, next);
    s->current = NULL;

    p->pending = 0;
    p->out = (s->sstat1 & PHASE_MASK) == PHASE_DO;
}

/* Queue a byte for a MSG IN phase.  */
static void lsi_add_msg_byte(LSIState *s, uint8_t data)
{
    if (s->msg_len >= LSI_MAX_MSGIN_LEN) {
        trace_lsi_add_msg_byte_error();
    } else {
        trace_lsi_add_msg_byte(data);
        s->msg[s->msg_len++] = data;
    }
}

/* Perform reselection to continue a command.  */
static void lsi_reselect(LSIState *s, lsi_request *p)
{
    int id;

    assert(s->current == NULL);
    QTAILQ_REMOVE(&s->queue, p->req, next);
    s->current = p;

    id = (p->tag >> 8) & 0xf;
    s->ssid = id | 0x80;
    /* LSI53C700 Family Compatibility, see LSI53C895A 4-73 */
    if (!(s->dcntl & LSI_DCNTL_COM)) {
        s->sfbr = 1 << (id & 0x7);
    }
    trace_lsi_reselect(id);
    s->scntl1 |= LSI_SCNTL1_CON;
    lsi_set_phase(s, PHASE_MI);
    s->msg_action = p->out ? LSI_MSG_ACTION_DOUT : LSI_MSG_ACTION_DIN;
    s->current->dma_len = p->pending;
    lsi_add_msg_byte(s, 0x80);
    if (s->current->tag & LSI_TAG_VALID) {
        lsi_add_msg_byte(s, 0x20);
        lsi_add_msg_byte(s, p->tag & 0xff);
    }

    if (lsi_irq_on_rsl(s)) {
        lsi_script_scsi_interrupt(s, LSI_SIST0_RSL, 0);
    }
}

static lsi_request *lsi_find_by_tag(LSIState *s, uint32_t tag)
{
    struct lsi_request *p;

    QTAILQ_FOREACH(p, &s->queue, next) {
        if (p->tag == tag) {
            return p;
        }
    }

    return NULL;
}

static void lsi_request_free(LSIState *s, lsi_request *p)
{
    if (p == s->current) {
        s->current = NULL;
    } else {
        QTAILQ_REMOVE(&s->queue, p->req, next);
    }
    g_free(p);
}

static void lsi_request_cancelled(SCSIRequest *req)
{
    LSIState *s = LSI53C895A(req->bus->qbus.parent);
    lsi_request *p = req->hba_private;

    req->hba_private = NULL;
    lsi_request_free(s, p);
    scsi_req_unref(req);
}

/* Record that data is available for a queued command.  Returns zero if
   the device was reselected, nonzero if the IO is deferred.  */
static int lsi_queue_req(LSIState *s, SCSIRequest *req, uint32_t len)
{
    lsi_request *p = req->hba_private;

    if (p->pending) {
        trace_lsi_queue_req_error(p);
    }
    p->pending = len;
    /* Reselect if waiting for it, or if reselection triggers an IRQ
       and the bus is free.
       Since no interrupt stacking is implemented in the emulation, it
       is also required that there are no pending interrupts waiting
       for service from the device driver. */
    if (s->waiting == LSI_WAIT_RESELECT ||
        (lsi_irq_on_rsl(s) && !(s->scntl1 & LSI_SCNTL1_CON) &&
         !(s->istat0 & (LSI_ISTAT0_SIP | LSI_ISTAT0_DIP)))) {
        /* Reselect device.  */
        lsi_reselect(s, p);
        return 0;
    } else {
        trace_lsi_queue_req(p->tag);
        p->pending = len;
        return 1;
    }
}

 /* Callback to indicate that the SCSI layer has completed a command.  */
static void lsi_command_complete(struct cpu *cpu, SCSIRequest *req, size_t resid)
{
    LSIState *s = LSI53C895A(req->bus->qbus.parent);
    int out;

    out = (s->sstat1 & PHASE_MASK) == PHASE_DO;
    trace_lsi_command_complete(req->status);
    s->status = req->status;
    s->command_complete = 2;
    if (s->waiting && s->dbc != 0) {
        /* Raise phase mismatch for short transfers.  */
        lsi_bad_phase(s, out, PHASE_ST);
    } else {
        lsi_set_phase(s, PHASE_ST);
    }

    if (req->hba_private == s->current) {
        req->hba_private = NULL;
        lsi_request_free(s, s->current);
        scsi_req_unref(req);
    }
    lsi_resume_script(cpu, s);
}

 /* Callback to indicate that the SCSI layer has completed a transfer.  */
static void lsi_transfer_data(struct cpu *cpu, SCSIRequest *req, uint32_t len)
{
    LSIState *s = LSI53C895A(req->bus->qbus.parent);
    int out;

    assert(req->hba_private);
    if (s->waiting == LSI_WAIT_RESELECT || req->hba_private != s->current ||
        (lsi_irq_on_rsl(s) && !(s->scntl1 & LSI_SCNTL1_CON))) {
        if (lsi_queue_req(s, req, len)) {
            return;
        }
    }

    out = (s->sstat1 & PHASE_MASK) == PHASE_DO;

    /* host adapter (re)connected */
    fprintf(stderr, "lsi: transfer data: DNAD %08x DBC %08x\n", s->dnad, s->dbc);
    trace_lsi_transfer_data(req->tag, len);
    s->current->dma_len = len;
    s->command_complete = 1;
    if (s->waiting) {
        if (s->waiting == LSI_WAIT_RESELECT || s->dbc == 0) {
            lsi_resume_script(cpu, s);
        } else {
            lsi_do_dma(cpu, s, out);
        }
    }
}

static void lsi_do_command(struct cpu *cpu, LSIState *s)
{
    SCSIDevice *dev;
    uint8_t buf[16];
    uint32_t id;
    int n;

    id = (s->select_tag >> 8) & 0xf;
    s->ssid = id;
    trace_lsi_do_command(cpu, s->dbc, id);
    if (s->dbc > 16)
        s->dbc = 16;
    pci_dma_read(cpu, PCI_DEVICE(s), s->dnad, buf, s->dbc);
    s->sfbr = buf[0];
    s->command_complete = 0;

    dev = scsi_device_find(cpu, s, &s->bus, 0, id, s->current_lun);
    if (!dev) {
        lsi_bad_selection(s, id);
        return;
    }

    assert(s->current == NULL);
    s->current = g_new0(lsi_request, 1);
    s->current->tag = s->select_tag;
    s->current->req = scsi_req_new(s, dev, s->current->tag, s->current_lun, buf,
                                   s->dbc, s->current);

    n = scsi_req_enqueue(cpu, s->current->req);
    fprintf(stderr, "lsi: enqueue returned %d\n", n);
    if (n) {
        if (n > 0) {
            lsi_set_phase(s, PHASE_DI);
        } else if (n < 0) {
            lsi_set_phase(s, PHASE_DO);
        }
        scsi_req_continue(cpu, s->current->req);
    }
    if (!s->command_complete) {
        if (n) {
            /* Command did not complete immediately so disconnect.  */
            lsi_add_msg_byte(s, 2); /* SAVE DATA POINTER */
            lsi_add_msg_byte(s, 4); /* DISCONNECT */
            /* wait data */
            lsi_set_phase(s, PHASE_MI);
            s->msg_action = LSI_MSG_ACTION_DISCONNECT;
            lsi_queue_command(s);
        } else {
            /* wait command complete */
            lsi_set_phase(s, PHASE_DI);
        }
    }
}

static void lsi_do_status(struct cpu *cpu, LSIState *s)
{
    uint8_t status;
    fprintf(stderr, "lsi status: DNAD is %08x, DBC is %08x\n", s->dnad, s->dbc);
    trace_lsi_do_status(s->dbc, s->status);
    status = s->status;
    s->sfbr = status;
    if (s->dbc) {
        pci_dma_write(cpu, PCI_DEVICE(s), s->dnad, &status, 1);
    }
    lsi_set_phase(s, PHASE_MI);
    s->msg_action = LSI_MSG_ACTION_DISCONNECT;
    lsi_add_msg_byte(s, 0); /* COMMAND COMPLETE */
}

static void lsi_do_msgin(struct cpu *cpu, LSIState *s)
{
    uint8_t len;
    trace_lsi_do_msgin(s->dbc, s->msg_len);
    s->sfbr = s->msg[0];
    len = s->msg_len;
    assert(len > 0 && len <= LSI_MAX_MSGIN_LEN);
    if (len > s->dbc)
        len = s->dbc;
    pci_dma_write(cpu, PCI_DEVICE(s), s->dnad, s->msg, len);
    /* Linux drivers rely on the last byte being in the SIDL.  */
    s->sidl = s->msg[len - 1];
    s->msg_len -= len;
    if (s->msg_len) {
        memmove(s->msg, s->msg + len, s->msg_len);
    } else {
        /* ??? Check if ATN (not yet implemented) is asserted and maybe
           switch to PHASE_MO.  */
        switch (s->msg_action) {
        case LSI_MSG_ACTION_COMMAND:
            lsi_set_phase(s, PHASE_CMD);
            break;
        case LSI_MSG_ACTION_DISCONNECT:
            lsi_disconnect(s);
            break;
        case LSI_MSG_ACTION_DOUT:
            lsi_set_phase(s, PHASE_DO);
            break;
        case LSI_MSG_ACTION_DIN:
            lsi_set_phase(s, PHASE_DI);
            break;
        default:
            ABORT();
        }
    }
}

/* Read the next byte during a MSGOUT phase.  */
static uint8_t lsi_get_msgbyte(struct cpu *cpu, LSIState *s)
{
    uint8_t data;
    pci_dma_read(cpu, PCI_DEVICE(s), s->dnad, &data, 1);
    s->dnad++;
    s->dbc--;
    return data;
}

/* Skip the next n bytes during a MSGOUT phase. */
static void lsi_skip_msgbytes(LSIState *s, unsigned int n)
{
    s->dnad += n;
    s->dbc  -= n;
}

static void lsi_do_msgout(struct cpu *cpu, LSIState *s)
{
    uint8_t msg;
    int len;
    uint32_t current_tag;
    lsi_request *current_req;
    struct lsi_request *p, *p_next;

    if (s->current) {
        current_tag = s->current->tag;
        current_req = s->current;
    } else {
        current_tag = s->select_tag;
        current_req = lsi_find_by_tag(s, current_tag);
    }

    trace_lsi_do_msgout(s->dbc);
    while (s->dbc) {
        msg = lsi_get_msgbyte(cpu, s);
        s->sfbr = msg;

        switch (msg) {
        case 0x04:
            trace_lsi_do_msgout_disconnect();
            lsi_disconnect(s);
            break;
        case 0x08:
            trace_lsi_do_msgout_noop();
            lsi_set_phase(s, PHASE_CMD);
            break;
        case 0x01:
            len = lsi_get_msgbyte(cpu, s);
            msg = lsi_get_msgbyte(cpu, s);
            (void)len; /* avoid a warning about unused variable*/
            trace_lsi_do_msgout_extended(msg, len);
            switch (msg) {
            case 1:
                trace_lsi_do_msgout_ignored("SDTR");
                lsi_skip_msgbytes(s, 2);
                break;
            case 3:
                trace_lsi_do_msgout_ignored("WDTR");
                lsi_skip_msgbytes(s, 1);
                break;
            case 4:
                trace_lsi_do_msgout_ignored("PPR");
                lsi_skip_msgbytes(s, 5);
                break;
            default:
                goto bad;
            }
            break;
        case 0x20: /* SIMPLE queue */
            s->select_tag |= lsi_get_msgbyte(cpu, s) | LSI_TAG_VALID;
            trace_lsi_do_msgout_simplequeue(s->select_tag & 0xff);
            break;
        case 0x21: /* HEAD of queue */
            qemu_log_mask(LOG_UNIMP, "lsi_scsi: HEAD queue not implemented\n");
            s->select_tag |= lsi_get_msgbyte(cpu, s) | LSI_TAG_VALID;
            break;
        case 0x22: /* ORDERED queue */
            qemu_log_mask(LOG_UNIMP,
                          "lsi_scsi: ORDERED queue not implemented\n");
            s->select_tag |= lsi_get_msgbyte(cpu, s) | LSI_TAG_VALID;
            break;
        case 0x0d:
            /* The ABORT TAG message clears the current I/O process only. */
            trace_lsi_do_msgout_abort(current_tag);
            if (current_req && current_req->req) {
                scsi_req_cancel(current_req->req);
                current_req = NULL;
            }
            lsi_disconnect(s);
            break;
        case 0x06:
        case 0x0e:
        case 0x0c:
            /* The ABORT message clears all I/O processes for the selecting
               initiator on the specified logical unit of the target. */
            if (msg == 0x06) {
                trace_lsi_do_msgout_abort(current_tag);
            }
            /* The CLEAR QUEUE message clears all I/O processes for all
               initiators on the specified logical unit of the target. */
            if (msg == 0x0e) {
                trace_lsi_do_msgout_clearqueue(current_tag);
            }
            /* The BUS DEVICE RESET message clears all I/O processes for all
               initiators on all logical units of the target. */
            if (msg == 0x0c) {
                trace_lsi_do_msgout_busdevicereset(current_tag);
            }

            /* clear the current I/O process */
            if (s->current) {
                scsi_req_cancel(s->current->req);
                current_req = NULL;
            }

            /* As the current implemented devices scsi_disk and scsi_generic
               only support one LUN, we don't need to keep track of LUNs.
               Clearing I/O processes for other initiators could be possible
               for scsi_generic by sending a SG_SCSI_RESET to the /dev/sgX
               device, but this is currently not implemented (and seems not
               to be really necessary). So let's simply clear all queued
               commands for the current device: */
            QTAILQ_FOREACH_SAFE(p, &s->queue, next, p_next) {
                if ((p->tag & 0x0000ff00) == (current_tag & 0x0000ff00)) {
                    scsi_req_cancel(p->req);
                }
            }

            lsi_disconnect(s);
            break;
        default:
            if ((msg & 0x80) == 0) {
                goto bad;
            }
            s->current_lun = msg & 7;
            trace_lsi_do_msgout_select(s->current_lun);
            lsi_set_phase(s, PHASE_CMD);
            break;
        }
    }
    return;
bad:
    qemu_log_mask(LOG_UNIMP, "Unimplemented message 0x%02x\n", msg);
    lsi_set_phase(s, PHASE_MI);
    lsi_add_msg_byte(s, 7); /* MESSAGE REJECT */
    s->msg_action = LSI_MSG_ACTION_COMMAND;
}

#define LSI_BUF_SIZE 4096
static void lsi_memcpy(struct cpu *cpu, LSIState *s, uint32_t dest, uint32_t src, int count)
{
    int n;
    uint8_t buf[LSI_BUF_SIZE];

    trace_lsi_memcpy(dest, src, count);
    while (count) {
        n = (count > LSI_BUF_SIZE) ? LSI_BUF_SIZE : count;
        for (auto i = 0; i < n; i++) {
            lsi_mem_read(cpu, s, src + i, buf + i, 1);
            lsi_mem_write(cpu, s, dest + i, buf + i, 1);
        }
        src += n;
        dest += n;
        count -= n;
    }
}

static void lsi_wait_reselect(LSIState *s)
{
    lsi_request *p;

    trace_lsi_wait_reselect();

    if (s->current) {
        return;
    }
    p = get_pending_req(s);
    if (p) {
        lsi_reselect(s, p);
    }
    if (s->current == NULL) {
        s->waiting = LSI_WAIT_RESELECT;
    }
}

static void lsi_execute_script(struct cpu *cpu, LSIState *s)
{
    PCIDevice *pci_dev = PCI_DEVICE(s);
    uint32_t insn;
    uint32_t addr, addr_high;
    int opcode;
    int insn_processed = 0;

    s->istat1 |= LSI_ISTAT1_SRUN;
again:
    if (++insn_processed > LSI_MAX_INSN) {
        /* Some windows drivers make the device spin waiting for a memory
           location to change.  If we have been executed a lot of code then
           assume this is the case and force an unexpected device disconnect.
           This is apparently sufficient to beat the drivers into submission.
         */
        if (!(s->sien0 & LSI_SIST0_UDC)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "lsi_scsi: inf. loop with UDC masked\n");
        }
        lsi_script_scsi_interrupt(s, LSI_SIST0_UDC, 0);
        lsi_disconnect(s);
        trace_lsi_execute_script_stop();
        return;
    }
    insn = read_dword(cpu, s, s->dsp);
    fprintf(stderr, "lsi: got insn %0x\n", (int)insn);
    if (!insn) {
        /* If we receive an empty opcode increment the DSP by 4 bytes
           instead of 8 and execute the next opcode at that location */
        s->dsp += 4;
        goto again;
    }
    addr = read_dword(cpu, s, s->dsp + 4);
    addr_high = 0;
    trace_lsi_execute_script(s->dsp, insn, addr);
    s->dsps = addr;
    s->dcmd = insn >> 24;
    s->dsp += 8;
    switch (insn >> 30) {
    case 0: /* Block move.  */
        if (s->sist1 & LSI_SIST1_STO) {
            trace_lsi_execute_script_blockmove_delayed();
            lsi_stop_script(s);
            break;
        }
        s->dbc = insn & 0xffffff;
        s->rbc = s->dbc;
        /* ??? Set ESA.  */
        s->ia = s->dsp - 8;
        if (insn & (1 << 29)) {
            /* Indirect addressing.  */
            addr = read_dword(cpu, s, addr);
        } else if (insn & (1 << 28)) {
            uint32_t buf[2];
            int32_t offset;
            /* Table indirect addressing.  */

            /* 32-bit Table indirect */
            offset = sextract32(addr, 0, 24);
            pci_dma_read(cpu, pci_dev, s->dsa + offset, buf, 8);
            /* byte count is stored in bits 0:23 only */
            s->dbc = cpu_to_le32(cpu, buf[0]) & 0xffffff;
            s->rbc = s->dbc;
            addr = cpu_to_le32(cpu, buf[1]);

            /* 40-bit DMA, upper addr bits [39:32] stored in first DWORD of
             * table, bits [31:24] */
            if (lsi_dma_40bit(s))
                addr_high = cpu_to_le32(cpu, buf[0]) >> 24;
            else if (lsi_dma_ti64bit(s)) {
                int selector = (cpu_to_le32(cpu, buf[0]) >> 24) & 0x1f;
                switch (selector) {
                case 0 ... 0x0f:
                    /* offset index into scratch registers since
                     * TI64 mode can use registers C to R */
                    addr_high = s->scratch[2 + selector];
                    break;
                case 0x10:
                    addr_high = s->mmrs;
                    break;
                case 0x11:
                    addr_high = s->mmws;
                    break;
                case 0x12:
                    addr_high = s->sfs;
                    break;
                case 0x13:
                    addr_high = s->drs;
                    break;
                case 0x14:
                    addr_high = s->sbms;
                    break;
                case 0x15:
                    addr_high = s->dbms;
                    break;
                default:
                    qemu_log_mask(LOG_GUEST_ERROR,
                          "lsi_scsi: Illegal selector specified (0x%x > 0x15) "
                          "for 64-bit DMA block move\n", selector);
                    break;
                }
            }
        } else if (lsi_dma_64bit(s)) {
            /* fetch a 3rd dword if 64-bit direct move is enabled and
               only if we're not doing table indirect or indirect addressing */
            s->dbms = read_dword(cpu, s, s->dsp);
            s->dsp += 4;
            s->ia = s->dsp - 12;
        }
        if ((s->sstat1 & PHASE_MASK) != ((insn >> 24) & 7)) {
            trace_lsi_execute_script_blockmove_badphase(
                    scsi_phase_name(s->sstat1),
                    scsi_phase_name(insn >> 24));
            lsi_script_scsi_interrupt(s, LSI_SIST0_MA, 0);
            break;
        }
        s->dnad = addr;
        s->dnad64 = addr_high;
        switch (s->sstat1 & 0x7) {
        case PHASE_DO:
            s->waiting = LSI_DMA_SCRIPTS;
            fprintf(stderr, "lsi: dma PHASE_DO\n");
            lsi_do_dma(cpu, s, 1);
            if (s->waiting)
                s->waiting = LSI_DMA_IN_PROGRESS;
            break;
        case PHASE_DI:
            s->waiting = LSI_DMA_SCRIPTS;
            fprintf(stderr, "lsi: dma PHASE_DI\n");
            lsi_do_dma(cpu, s, 0);
            if (s->waiting)
                s->waiting = LSI_DMA_IN_PROGRESS;
            break;
        case PHASE_CMD:
            fprintf(stderr, "lsi: PHASE_CMD\n");
            lsi_do_command(cpu, s);
            break;
        case PHASE_ST:
            fprintf(stderr, "lsi: PHASE_ST\n");
            lsi_do_status(cpu, s);
            break;
        case PHASE_MO:
            fprintf(stderr, "lsi: PHASE_MO\n");
            lsi_do_msgout(cpu, s);
            break;
        case PHASE_MI:
            fprintf(stderr, "lsi: PHASE_MI\n");
            lsi_do_msgin(cpu, s);
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "lsi_scsi: Unimplemented phase %s\n",
                          scsi_phase_name(s->sstat1));
        }
        s->dfifo = s->dbc & 0xff;
        s->ctest5 = (s->ctest5 & 0xfc) | ((s->dbc >> 8) & 3);
        s->sbc = s->dbc;
        s->rbc -= s->dbc;
        s->ua = addr + s->dbc;
        break;

    case 1: /* IO or Read/Write instruction.  */
        opcode = (insn >> 27) & 7;
        if (opcode < 5) {
            uint32_t id;

            if (insn & (1 << 25)) {
                uint32_t id_offset = sextract32(insn, 0, 24);
                uint32_t id_addr = s->dsa + id_offset;
                id = read_dword(cpu, s, id_addr);
                fprintf(stderr, "lsi: table id %08x\n", id);
            } else {
                id = insn;
                fprintf(stderr, "lsi: raw id %08x\n", id);
            }
            id = (id >> 16) & 0xf;
            if (insn & (1 << 26)) {
                addr = s->dsp + sextract32(addr, 0, 24);
            }
            s->dnad = addr;
            switch (opcode) {
            case 0: /* Select */
                s->sdid = id;
                fprintf(stderr, "lsi: select scsi id %d\n", id);
                if (s->scntl1 & LSI_SCNTL1_CON) {
                    trace_lsi_execute_script_io_alreadyreselected();
                    s->dsp = s->dnad;
                    break;
                }
                s->sstat0 |= LSI_SSTAT0_WOA;
                s->scntl1 &= ~LSI_SCNTL1_IARB;
                if (!scsi_device_find(cpu, s, &s->bus, 0, id, 0)) {
                    lsi_bad_selection(s, id);
                    break;
                }
                trace_lsi_execute_script_io_selected(id,
                                             insn & (1 << 3) ? " ATN" : "");
                /* ??? Linux drivers compain when this is set.  Maybe
                   it only applies in low-level mode (unimplemented).
                lsi_script_scsi_interrupt(s, LSI_SIST0_CMP, 0); */
                s->select_tag = id << 8;
                s->scntl1 |= LSI_SCNTL1_CON;
                if (insn & (1 << 3)) {
                    s->socl |= LSI_SOCL_ATN;
                    s->sbcl |= LSI_SBCL_ATN;
                }
                s->sbcl |= LSI_SBCL_BSY;
                lsi_set_phase(s, PHASE_MO);
                s->waiting = LSI_NOWAIT;
                break;
            case 1: /* Disconnect */
                trace_lsi_execute_script_io_disconnect();
                s->scntl1 &= ~LSI_SCNTL1_CON;
                /* FIXME: this is not entirely correct; the target need not ask
                 * for reselection until it has to send data, while here we force a
                 * reselection as soon as the bus is free.  The correct flow would
                 * reselect before lsi_transfer_data and disconnect as soon as
                 * DMA ends.
                 */
                if (!s->current) {
                    lsi_request *p = get_pending_req(s);
                    if (p) {
                        lsi_reselect(s, p);
                    }
                }
                break;
            case 2: /* Wait Reselect */
                if (s->istat0 & LSI_ISTAT0_SIGP) {
                    s->dsp = s->dnad;
                } else if (!lsi_irq_on_rsl(s)) {
                        lsi_wait_reselect(s);
                }
                break;
            case 3: /* Set */
                trace_lsi_execute_script_io_set(
                        insn & (1 << 3) ? " ATN" : "",
                        insn & (1 << 6) ? " ACK" : "",
                        insn & (1 << 9) ? " TM" : "",
                        insn & (1 << 10) ? " CC" : "");
                if (insn & (1 << 3)) {
                    s->socl |= LSI_SOCL_ATN;
                    s->sbcl |= LSI_SBCL_ATN;
                    lsi_set_phase(s, PHASE_MO);
                }

                if (insn & (1 << 6)) {
                    s->sbcl |= LSI_SBCL_ACK;
                }

                if (insn & (1 << 9)) {
                    qemu_log_mask(LOG_UNIMP,
                        "lsi_scsi: Target mode not implemented\n");
                }
                if (insn & (1 << 10))
                    s->carry = 1;
                break;
            case 4: /* Clear */
                trace_lsi_execute_script_io_clear(
                        insn & (1 << 3) ? " ATN" : "",
                        insn & (1 << 6) ? " ACK" : "",
                        insn & (1 << 9) ? " TM" : "",
                        insn & (1 << 10) ? " CC" : "");
                if (insn & (1 << 3)) {
                    s->socl &= ~LSI_SOCL_ATN;
                    s->sbcl &= ~LSI_SBCL_ATN;
                }

                if (insn & (1 << 6)) {
                    s->sbcl &= ~LSI_SBCL_ACK;
                }

                if (insn & (1 << 10))
                    s->carry = 0;
                break;
            }
        } else {
            uint8_t op0;
            uint8_t op1;
            uint8_t data8;
            int reg;
            int operator_;

            static const char *opcode_names[3] =
                {"Write", "Read", "Read-Modify-Write"};
            static const char *operator_names[8] =
                {"MOV", "SHL", "OR", "XOR", "AND", "SHR", "ADD", "ADC"};

            reg = ((insn >> 16) & 0x7f) | (insn & 0x80);
            data8 = (insn >> 8) & 0xff;
            opcode = (insn >> 27) & 7;
            operator_ = (insn >> 24) & 7;
            trace_lsi_execute_script_io_opcode(
                    opcode_names[opcode - 5], reg,
                    operator_names[operator_], data8, s->sfbr,
                    (insn & (1 << 23)) ? " SFBR" : "");
            op0 = op1 = 0;
            switch (opcode) {
            case 5: /* From SFBR */
                op0 = s->sfbr;
                op1 = data8;
                break;
            case 6: /* To SFBR */
                if (operator_)
                    op0 = lsi_reg_readb(cpu, s, reg);
                op1 = data8;
                break;
            case 7: /* Read-modify-write */
                if (operator_)
                    op0 = lsi_reg_readb(cpu, s, reg);
                if (insn & (1 << 23)) {
                    op1 = s->sfbr;
                } else {
                    op1 = data8;
                }
                break;
            }

            switch (operator_) {
            case 0: /* move */
                op0 = op1;
                break;
            case 1: /* Shift left */
                op1 = op0 >> 7;
                op0 = (op0 << 1) | s->carry;
                s->carry = op1;
                break;
            case 2: /* OR */
                op0 |= op1;
                break;
            case 3: /* XOR */
                op0 ^= op1;
                break;
            case 4: /* AND */
                op0 &= op1;
                break;
            case 5: /* SHR */
                op1 = op0 & 1;
                op0 = (op0 >> 1) | (s->carry << 7);
                s->carry = op1;
                break;
            case 6: /* ADD */
                op0 += op1;
                s->carry = op0 < op1;
                break;
            case 7: /* ADC */
                op0 += op1 + s->carry;
                if (s->carry)
                    s->carry = op0 <= op1;
                else
                    s->carry = op0 < op1;
                break;
            }

            switch (opcode) {
            case 5: /* From SFBR */
            case 7: /* Read-modify-write */
                lsi_reg_writeb(cpu, s, reg, op0);
                break;
            case 6: /* To SFBR */
                s->sfbr = op0;
                break;
            }
        }
        break;

    case 2: /* Transfer Control.  */
        {
            int cond;
            int jmp;

            if ((insn & 0x002e0000) == 0) {
                trace_lsi_execute_script_tc_nop();
                break;
            }
            if (s->sist1 & LSI_SIST1_STO) {
                trace_lsi_execute_script_tc_delayedselect_timeout();
                lsi_stop_script(s);
                break;
            }
            cond = jmp = (insn & (1 << 19)) != 0;
            if (cond == jmp && (insn & (1 << 21))) {
                trace_lsi_execute_script_tc_compc(s->carry == jmp);
                cond = s->carry != 0;
            }
            if (cond == jmp && (insn & (1 << 17))) {
                trace_lsi_execute_script_tc_compp(scsi_phase_name(s->sstat1),
                        jmp ? '=' : '!', scsi_phase_name(insn >> 24));
                cond = (s->sstat1 & PHASE_MASK) == ((insn >> 24) & 7);
            }
            if (cond == jmp && (insn & (1 << 18))) {
                uint8_t mask;

                mask = (~insn >> 8) & 0xff;
                trace_lsi_execute_script_tc_compd(
                        s->sfbr, mask, jmp ? '=' : '!', insn & mask);
                cond = (s->sfbr & mask) == (insn & mask);
            }
            if (cond == jmp) {
                if (insn & (1 << 23)) {
                    uint32_t want_offset = sextract32(addr, 0, 24);
                    fprintf(stderr, "lsi: jump to relative address insn %08x addr %08x offset %0x\n", insn, addr, want_offset);
                    /* Relative address.  */
                    addr = s->dsp + want_offset;
                }
                switch ((insn >> 27) & 7) {
                case 0: /* Jump */
                    trace_lsi_execute_script_tc_jump(addr);
                    s->adder = addr;
                    s->dsp = addr;
                    break;
                case 1: /* Call */
                    trace_lsi_execute_script_tc_call(addr);
                    s->temp = s->dsp;
                    s->dsp = addr;
                    break;
                case 2: /* Return */
                    trace_lsi_execute_script_tc_return(s->temp);
                    s->dsp = s->temp;
                    break;
                case 3: /* Interrupt */
                    trace_lsi_execute_script_tc_interrupt(s->dsps);
                    if ((insn & (1 << 20)) != 0) {
                        s->istat0 |= LSI_ISTAT0_INTF;
                        lsi_update_irq(s);
                    } else {
                        lsi_script_dma_interrupt(s, LSI_DSTAT_SIR);
                    }
                    break;
                default:
                    trace_lsi_execute_script_tc_illegal();
                    lsi_script_dma_interrupt(s, LSI_DSTAT_IID);
                    break;
                }
            } else {
                trace_lsi_execute_script_tc_cc_failed();
            }
        }
        break;

    case 3:
        if ((insn & (1 << 29)) == 0) {
            /* Memory move.  */
            uint32_t dest;
            /* ??? The docs imply the destination address is loaded into
               the TEMP register.  However the Linux drivers rely on
               the value being presrved.  */
            dest = read_dword(cpu, s, s->dsp);
            s->dsp += 4;
            lsi_memcpy(cpu, s, dest, addr, insn & 0xffffff);
        } else {
            uint8_t data[7];
            int reg;
            int n;
            int i;

            if (insn & (1 << 28)) {
                addr = s->dsa + sextract32(addr, 0, 24);
            }
            n = (insn & 7);
            reg = (insn >> 16) & 0xff;
            if (insn & (1 << 24)) {
                pci_dma_read(cpu, pci_dev, addr, data, n);
                trace_lsi_execute_script_mm_load(reg, n, addr, *(int *)data);
                for (i = 0; i < n; i++) {
                    lsi_reg_writeb(cpu, s, reg + i, data[i]);
                }
            } else {
                trace_lsi_execute_script_mm_store(reg, n, addr);
                for (i = 0; i < n; i++) {
                    data[i] = lsi_reg_readb(cpu, s, reg + i);
                }
                pci_dma_write(cpu, pci_dev, addr, data, n);
            }
        }
    }
    if (s->istat1 & LSI_ISTAT1_SRUN && s->waiting == LSI_NOWAIT) {
        if (s->dcntl & LSI_DCNTL_SSM) {
            lsi_script_dma_interrupt(s, LSI_DSTAT_SSI);
        } else {
            goto again;
        }
    }
    trace_lsi_execute_script_stop();
}

static uint8_t lsi_reg_readb(struct cpu *cpu, LSIState *s, int offset)
{
    uint8_t ret;

#define CASE_GET_REG24(name, addr) \
    case addr: ret = s->name & 0xff; break; \
    case addr + 1: ret = (s->name >> 8) & 0xff; break; \
    case addr + 2: ret = (s->name >> 16) & 0xff; break;

#define CASE_GET_REG32(name, addr) \
    case addr: ret = s->name & 0xff; break; \
    case addr + 1: ret = (s->name >> 8) & 0xff; break; \
    case addr + 2: ret = (s->name >> 16) & 0xff; break; \
    case addr + 3: ret = (s->name >> 24) & 0xff; break;

    switch (offset) {
    case 0x00: /* SCNTL0 */
        ret = s->scntl0;
        break;
    case 0x01: /* SCNTL1 */
        ret = s->scntl1;
        break;
    case 0x02: /* SCNTL2 */
        ret = s->scntl2;
        break;
    case 0x03: /* SCNTL3 */
        ret = s->scntl3;
        break;
    case 0x04: /* SCID */
        ret = s->scid;
        break;
    case 0x05: /* SXFER */
        ret = s->sxfer;
        break;
    case 0x06: /* SDID */
        ret = s->sdid;
        break;
    case 0x07: /* GPREG0 */
        ret = 0x7f;
        break;
    case 0x08: /* Revision ID */
        ret = 0x00;
        break;
    case 0x09: /* SOCL */
        ret = s->socl;
        break;
    case 0xa: /* SSID */
        ret = s->ssid;
        break;
    case 0xb: /* SBCL */
        ret = s->sbcl;
        break;
    case 0xc: /* DSTAT */
        ret = s->dstat | LSI_DSTAT_DFE;
        if ((s->istat0 & LSI_ISTAT0_INTF) == 0)
            s->dstat = 0;
        lsi_update_irq(s);
        break;
    case 0x0d: /* SSTAT0 */
        ret = s->sstat0;
        break;
    case 0x0e: /* SSTAT1 */
        ret = s->sstat1;
        break;
    case 0x0f: /* SSTAT2 */
        ret = s->scntl1 & LSI_SCNTL1_CON ? 0 : 2;
        break;
    CASE_GET_REG32(dsa, 0x10)
    case 0x14: /* ISTAT0 */
        ret = s->istat0;
        break;
    case 0x15: /* ISTAT1 */
        ret = s->istat1;
        break;
    case 0x16: /* MBOX0 */
        ret = s->mbox0;
        break;
    case 0x17: /* MBOX1 */
        ret = s->mbox1;
        break;
    case 0x18: /* CTEST0 */
        ret = 0xff;
        break;
    case 0x19: /* CTEST1 */
        ret = 0;
        break;
    case 0x1a: /* CTEST2 */
        ret = s->ctest2 | LSI_CTEST2_DACK | LSI_CTEST2_CM;
        if (s->istat0 & LSI_ISTAT0_SIGP) {
            s->istat0 &= ~LSI_ISTAT0_SIGP;
            ret |= LSI_CTEST2_SIGP;
        }
        break;
    case 0x1b: /* CTEST3 */
        ret = s->ctest3;
        break;
    CASE_GET_REG32(temp, 0x1c)
    case 0x20: /* DFIFO */
        ret = s->dfifo;
        break;
    case 0x21: /* CTEST4 */
        ret = s->ctest4;
        break;
    case 0x22: /* CTEST5 */
        ret = s->ctest5;
        break;
    case 0x23: /* CTEST6 */
        ret = 0;
        break;
    CASE_GET_REG24(dbc, 0x24)
    case 0x27: /* DCMD */
        ret = s->dcmd;
        break;
    CASE_GET_REG32(dnad, 0x28)
    CASE_GET_REG32(dsp, 0x2c)
    CASE_GET_REG32(dsps, 0x30)
    CASE_GET_REG32(scratch[0], 0x34)
    case 0x38: /* DMODE */
        ret = s->dmode;
        break;
    case 0x39: /* DIEN */
        ret = s->dien;
        break;
    case 0x3a: /* SBR */
        ret = s->sbr;
        break;
    case 0x3b: /* DCNTL */
        ret = s->dcntl;
        break;
    /* ADDER Output (Debug of relative jump address) */
    CASE_GET_REG32(adder, 0x3c)
    case 0x40: /* SIEN0 */
        ret = s->sien0;
        break;
    case 0x41: /* SIEN1 */
        ret = s->sien1;
        break;
    case 0x42: /* SIST0 */
        ret = s->sist0;
        s->sist0 = 0;
        lsi_update_irq(s);
        break;
    case 0x43: /* SIST1 */
        ret = s->sist1;
        s->sist1 = 0;
        lsi_update_irq(s);
        break;
    case 0x46: /* MACNTL */
        ret = s->macntl;
        break;
    case 0x47: /* GPCNTL0 */
        ret = 0x0f;
        break;
    case 0x48: /* STIME0 */
        ret = s->stime0;
        break;
    case 0x4a: /* RESPID0 */
        ret = s->respid0;
        break;
    case 0x4b: /* RESPID1 */
        ret = s->respid1;
        break;
    case 0x4d: /* STEST1 */
        ret = s->stest1;
        break;
    case 0x4e: /* STEST2 */
        ret = s->stest2;
        break;
    case 0x4f: /* STEST3 */
        ret = s->stest3;
        break;
    case 0x50: /* SIDL */
        /* This is needed by the linux drivers.  We currently only update it
           during the MSG IN phase.  */
        ret = s->sidl;
        break;
    case 0x52: /* STEST4 */
        ret = 0xe0;
        break;
    case 0x56: /* CCNTL0 */
        ret = s->ccntl0;
        break;
    case 0x57: /* CCNTL1 */
        ret = s->ccntl1;
        break;
    case 0x58: /* SBDL */
        /* Some drivers peek at the data bus during the MSG IN phase.  */
        if ((s->sstat1 & PHASE_MASK) == PHASE_MI) {
            assert(s->msg_len > 0);
            return s->msg[0];
        }
        ret = 0;
        break;
    case 0x59: /* SBDL high */
        ret = 0;
        break;
    CASE_GET_REG32(mmrs, 0xa0)
    CASE_GET_REG32(mmws, 0xa4)
    CASE_GET_REG32(sfs, 0xa8)
    CASE_GET_REG32(drs, 0xac)
    CASE_GET_REG32(sbms, 0xb0)
    CASE_GET_REG32(dbms, 0xb4)
    CASE_GET_REG32(dnad64, 0xb8)
    CASE_GET_REG32(pmjad1, 0xc0)
    CASE_GET_REG32(pmjad2, 0xc4)
    CASE_GET_REG32(rbc, 0xc8)
    CASE_GET_REG32(ua, 0xcc)
    CASE_GET_REG32(ia, 0xd4)
    CASE_GET_REG32(sbc, 0xd8)
    CASE_GET_REG32(csbc, 0xdc)
    case 0x5c ... 0x9f:
    {
        int n;
        int shift;
        n = (offset - 0x58) >> 2;
        shift = (offset & 3) * 8;
        ret = (s->scratch[n] >> shift) & 0xff;
        break;
    }
    default:
    {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "lsi_scsi: invalid read from reg %s %x\n",
                      offset < ARRAY_SIZE(names) ? names[offset] : "???",
                      offset);
        ret = 0xff;
        break;
    }
    }
#undef CASE_GET_REG24
#undef CASE_GET_REG32

    trace_lsi_reg_read(offset < ARRAY_SIZE(names) ? names[offset] : "???",
                       offset, ret);

    return ret;
}

static void lsi_reg_writeb(struct cpu *cpu, LSIState *s, int offset, uint8_t val)
{
#define CASE_SET_REG24(name, addr)                                     \
    case addr    : s->name &= 0xffffff00; s->name |= val;       break; \
    case addr + 1: s->name &= 0xffff00ff; s->name |= val << 8;  break; \
    case addr + 2: s->name &= 0xff00ffff; s->name |= val << 16; break;

#define CASE_SET_REG32(name, addr) \
    case addr    : s->name &= 0xffffff00; s->name |= val;       break; \
    case addr + 1: s->name &= 0xffff00ff; s->name |= val << 8;  break; \
    case addr + 2: s->name &= 0xff00ffff; s->name |= val << 16; break; \
    case addr + 3: s->name &= 0x00ffffff; s->name |= val << 24; break;

    trace_lsi_reg_write(offset < ARRAY_SIZE(names) ? names[offset] : "???",
                        offset, val);

    switch (offset) {
    case 0x00: /* SCNTL0 */
        s->scntl0 = val;
        if (val & LSI_SCNTL0_START) {
            qemu_log_mask(LOG_UNIMP,
                          "lsi_scsi: Start sequence not implemented\n");
        }
        break;
    case 0x01: /* SCNTL1 */
        s->scntl1 = val & ~LSI_SCNTL1_SST;
        if (val & LSI_SCNTL1_IARB) {
            qemu_log_mask(LOG_UNIMP,
                      "lsi_scsi: Immediate Arbritration not implemented\n");
        }
        if (val & LSI_SCNTL1_RST) {
            if (!(s->sstat0 & LSI_SSTAT0_RST)) {
                bus_cold_reset(BUS(s->bus));
                s->sstat0 |= LSI_SSTAT0_RST;
                lsi_script_scsi_interrupt(s, LSI_SIST0_RST, 0);
            }
        } else {
            s->sstat0 &= ~LSI_SSTAT0_RST;
        }
        break;
    case 0x02: /* SCNTL2 */
        val &= ~(LSI_SCNTL2_WSR | LSI_SCNTL2_WSS);
        s->scntl2 = val;
        break;
    case 0x03: /* SCNTL3 */
        s->scntl3 = val;
        break;
    case 0x04: /* SCID */
        s->scid = val;
        break;
    case 0x05: /* SXFER */
        s->sxfer = val;
        break;
    case 0x06: /* SDID */
        if ((s->ssid & 0x80) && (val & 0xf) != (s->ssid & 0xf)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "lsi_scsi: Destination ID does not match SSID\n");
        }
        s->sdid = val & 0xf;
        break;
    case 0x07: /* GPREG0 */
        break;
    case 0x08: /* SFBR */
        /* The CPU is not allowed to write to this register.  However the
           SCRIPTS register move instructions are.  */
        s->sfbr = val;
        break;
    case 0x0a: case 0x0b:
        /* Openserver writes to these readonly registers on startup */
        return;
    case 0x0c: case 0x0d: case 0x0e: case 0x0f:
        /* Linux writes to these readonly registers on startup.  */
        return;
    CASE_SET_REG32(dsa, 0x10)
    case 0x14: /* ISTAT0 */
        s->istat0 = (s->istat0 & 0x0f) | (val & 0xf0);
        if (val & LSI_ISTAT0_ABRT) {
            lsi_script_dma_interrupt(s, LSI_DSTAT_ABRT);
        }
        if (val & LSI_ISTAT0_INTF) {
            s->istat0 &= ~LSI_ISTAT0_INTF;
            lsi_update_irq(s);
        }
        if (s->waiting == LSI_WAIT_RESELECT && val & LSI_ISTAT0_SIGP) {
            trace_lsi_awoken();
            s->waiting = LSI_NOWAIT;
            s->dsp = s->dnad;
            lsi_execute_script(cpu, s);
        }
        if (val & LSI_ISTAT0_SRST) {
            device_cold_reset(s);
        }
        break;
    case 0x16: /* MBOX0 */
        s->mbox0 = val;
        break;
    case 0x17: /* MBOX1 */
        s->mbox1 = val;
        break;
    case 0x18: /* CTEST0 */
        /* nothing to do */
        break;
    case 0x1a: /* CTEST2 */
        s->ctest2 = val & LSI_CTEST2_PCICIE;
        break;
    case 0x1b: /* CTEST3 */
        s->ctest3 = val & 0x0f;
        break;
    CASE_SET_REG32(temp, 0x1c)
    case 0x21: /* CTEST4 */
        if (val & 7) {
            qemu_log_mask(LOG_UNIMP,
                          "lsi_scsi: Unimplemented CTEST4-FBL 0x%x\n", val);
        }
        s->ctest4 = val;
        break;
    case 0x22: /* CTEST5 */
        if (val & (LSI_CTEST5_ADCK | LSI_CTEST5_BBCK)) {
            qemu_log_mask(LOG_UNIMP,
                          "lsi_scsi: CTEST5 DMA increment not implemented\n");
        }
        s->ctest5 = val;
        break;
    CASE_SET_REG24(dbc, 0x24)
    CASE_SET_REG32(dnad, 0x28)
    case 0x2c: /* DSP[0:7] */
        s->dsp &= 0xffffff00;
        s->dsp |= val;
        break;
    case 0x2d: /* DSP[8:15] */
        s->dsp &= 0xffff00ff;
        s->dsp |= val << 8;
        break;
    case 0x2e: /* DSP[16:23] */
        s->dsp &= 0xff00ffff;
        s->dsp |= val << 16;
        break;
    case 0x2f: /* DSP[24:31] */
        s->dsp &= 0x00ffffff;
        s->dsp |= val << 24;
        /*
         * FIXME: if s->waiting != LSI_NOWAIT, this will only execute one
         * instruction.  Is this correct?
         */
        if ((s->dmode & LSI_DMODE_MAN) == 0
            && (s->istat1 & LSI_ISTAT1_SRUN) == 0)
            lsi_execute_script(cpu, s);
        break;
    CASE_SET_REG32(dsps, 0x30)
    CASE_SET_REG32(scratch[0], 0x34)
    case 0x38: /* DMODE */
        s->dmode = val;
        break;
    case 0x39: /* DIEN */
        s->dien = val;
        lsi_update_irq(s);
        break;
    case 0x3a: /* SBR */
        s->sbr = val;
        break;
    case 0x3b: /* DCNTL */
        s->dcntl = val & ~(LSI_DCNTL_PFF | LSI_DCNTL_STD);
        /*
         * FIXME: if s->waiting != LSI_NOWAIT, this will only execute one
         * instruction.  Is this correct?
         */
        if ((val & LSI_DCNTL_STD) && (s->istat1 & LSI_ISTAT1_SRUN) == 0)
            lsi_execute_script(cpu, s);
        break;
    case 0x40: /* SIEN0 */
        s->sien0 = val;
        lsi_update_irq(s);
        break;
    case 0x41: /* SIEN1 */
        s->sien1 = val;
        lsi_update_irq(s);
        break;
    case 0x46:
        s->macntl = val;
        break;
    case 0x47: /* GPCNTL0 */
        break;
    case 0x48: /* STIME0 */
        s->stime0 = val;
        break;
    case 0x49: /* STIME1 */
        if (val & 0xf) {
            qemu_log_mask(LOG_UNIMP,
                          "lsi_scsi: General purpose timer not implemented\n");
            /* ??? Raising the interrupt immediately seems to be sufficient
               to keep the FreeBSD driver happy.  */
            lsi_script_scsi_interrupt(s, 0, LSI_SIST1_GEN);
        }
        break;
    case 0x4a: /* RESPID0 */
        s->respid0 = val;
        break;
    case 0x4b: /* RESPID1 */
        s->respid1 = val;
        break;
    case 0x4d: /* STEST1 */
        s->stest1 = val;
        break;
    case 0x4e: /* STEST2 */
        if (val & 1) {
            qemu_log_mask(LOG_UNIMP,
                          "lsi_scsi: Low level mode not implemented\n");
        }
        s->stest2 = val;
        break;
    case 0x4f: /* STEST3 */
        if (val & 0x41) {
            qemu_log_mask(LOG_UNIMP,
                          "lsi_scsi: SCSI FIFO test mode not implemented\n");
        }
        s->stest3 = val;
        break;
    case 0x56: /* CCNTL0 */
        s->ccntl0 = val;
        break;
    case 0x57: /* CCNTL1 */
        s->ccntl1 = val;
        break;
    CASE_SET_REG32(mmrs, 0xa0)
    CASE_SET_REG32(mmws, 0xa4)
    CASE_SET_REG32(sfs, 0xa8)
    CASE_SET_REG32(drs, 0xac)
    CASE_SET_REG32(sbms, 0xb0)
    CASE_SET_REG32(dbms, 0xb4)
    CASE_SET_REG32(dnad64, 0xb8)
    CASE_SET_REG32(pmjad1, 0xc0)
    CASE_SET_REG32(pmjad2, 0xc4)
    CASE_SET_REG32(rbc, 0xc8)
    CASE_SET_REG32(ua, 0xcc)
    CASE_SET_REG32(ia, 0xd4)
    CASE_SET_REG32(sbc, 0xd8)
    CASE_SET_REG32(csbc, 0xdc)
    default:
        if (offset >= 0x5c && offset < 0xa0) {
            int n;
            int shift;
            n = (offset - 0x58) >> 2;
            shift = (offset & 3) * 8;
            s->scratch[n] = deposit32(s->scratch[n], shift, 8, val);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "lsi_scsi: invalid write to reg %s %x (0x%02x)\n",
                          offset < ARRAY_SIZE(names) ? names[offset] : "???",
                          offset, val);
        }
    }
#undef CASE_SET_REG24
#undef CASE_SET_REG32
}

static void lsi_mmio_write(struct cpu *cpu, LSIState *s, hwaddr addr,
                           uint64_t val, unsigned size)
{
    for (hwaddr i = 0; i < size; i++) {
        lsi_reg_writeb(cpu, s, (addr + i) & 0xff, val >> ((size - i - 1) * 8));
    }
}

static uint64_t lsi_mmio_read(struct cpu *cpu, LSIState *s, hwaddr addr,
                              unsigned size)
{
    uint64_t res = 0;
    for (hwaddr i = 0; i < size; i++) {
        uint8_t read_data = lsi_reg_readb(cpu, s, addr + i);
        res |= ((uint64_t)read_data) << ((size - i - 1) * 8);
    }
    return res;
}

/*
static const MemoryRegionOps lsi_mmio_ops = {
    .read = lsi_mmio_read,
    .write = lsi_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};
*/

static void lsi_ram_write(struct lsi53c895a_data *s, hwaddr addr,
                          uint64_t val, unsigned size)
{
    stn_le_p(s->script_ram + addr, size, val);
}

static uint64_t lsi_ram_read(struct lsi53c895a_data *s, hwaddr addr,
                             unsigned size)
{
    return ldn_le_p(s->script_ram + addr, size);
}

/*
static const MemoryRegionOps lsi_ram_ops = {
    .read = lsi_ram_read,
    .write = lsi_ram_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t lsi_io_read(struct cpu *cpu, LSIState *s, hwaddr addr,
                            unsigned size)
{
    return lsi_reg_readb(cpu, s, addr & 0xff);
}

static void lsi_io_write(struct cpu *cpu, LSIState *s, hwaddr addr,
                         uint64_t val, unsigned size)
{
    lsi_reg_writeb(cpu, s, addr & 0xff, val);
}
*/

DEVICE_ACCESS(lsi53c895a_scripts)
{
    LSIState *s = (struct lsi53c895a_data *)extra;
    uint64_t resp_data;

    if (writeflag == MEM_WRITE) {
        uint64_t idata = memory_readmax64(cpu, data, len);
        fprintf(stderr, "lsi write scripts: %08x <- %08x (%d)\n", relative_addr, (int)idata, len);
        lsi_ram_write(s, relative_addr, idata, len);
        return 1;
    } else if (len == 8) {
        uint64_t odata;
        lsi_mem_read(cpu, s, relative_addr, &odata, len);
        resp_data = odata;
        memory_writemax64(cpu, data, len, odata);
    } else if (len == 4) {
        uint32_t odata;
        lsi_mem_read(cpu, s, relative_addr, &odata, len);
        resp_data = odata;
        memory_writemax64(cpu, data, len, odata);
    } else if (len == 2) {
        uint16_t odata;
        lsi_mem_read(cpu, s, relative_addr, &odata, len);
        resp_data = odata;
        memory_writemax64(cpu, data, len, odata);
    } else if (len == 1) {
        uint8_t odata;
        lsi_mem_read(cpu, s, relative_addr, &odata, len);
        resp_data = odata;
        memory_writemax64(cpu, data, len, odata);
    } else {
        fprintf(stderr, "lsi: nonsense length in write to %08x: %d\n", relative_addr, len);
        return 0;
    }

    fprintf(stderr, "lsi: read %08x (%d) -> %08x\n", relative_addr, len, (int)resp_data);

    return 1;
}

/*
static const MemoryRegionOps lsi_io_ops = {
    .read = lsi_io_read,
    .write = lsi_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};
*/

static void lsi_scsi_reset(DeviceState *dev)
{
    ABORT();
}

static int lsi_pre_save(void *opaque)
{
    LSIState *s = (LSIState *)opaque;

    if (s->current) {
        assert(s->current->dma_buf == NULL);
        assert(s->current->dma_len == 0);
    }
    assert(QTAILQ_EMPTY(&s->queue));

    return 0;
}

static int lsi_post_load(void *opaque, int version_id)
{
    LSIState *s = (LSIState *)opaque;

    if (s->msg_len < 0 || s->msg_len > LSI_MAX_MSGIN_LEN) {
        return -EINVAL;
    }

    return 0;
}

/*
static const VMStateDescription vmstate_lsi_scsi = {
    .name = "lsiscsi",
    .version_id = 1,
    .minimum_version_id = 0,
    .pre_save = lsi_pre_save,
    .post_load = lsi_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, LSIState),

        VMSTATE_INT32(carry, LSIState),
        VMSTATE_INT32(status, LSIState),
        VMSTATE_INT32(msg_action, LSIState),
        VMSTATE_INT32(msg_len, LSIState),
        VMSTATE_BUFFER(msg, LSIState),
        VMSTATE_INT32(waiting, LSIState),

        VMSTATE_UINT32(dsa, LSIState),
        VMSTATE_UINT32(temp, LSIState),
        VMSTATE_UINT32(dnad, LSIState),
        VMSTATE_UINT32(dbc, LSIState),
        VMSTATE_UINT8(istat0, LSIState),
        VMSTATE_UINT8(istat1, LSIState),
        VMSTATE_UINT8(dcmd, LSIState),
        VMSTATE_UINT8(dstat, LSIState),
        VMSTATE_UINT8(dien, LSIState),
        VMSTATE_UINT8(sist0, LSIState),
        VMSTATE_UINT8(sist1, LSIState),
        VMSTATE_UINT8(sien0, LSIState),
        VMSTATE_UINT8(sien1, LSIState),
        VMSTATE_UINT8(mbox0, LSIState),
        VMSTATE_UINT8(mbox1, LSIState),
        VMSTATE_UINT8(dfifo, LSIState),
        VMSTATE_UINT8(ctest2, LSIState),
        VMSTATE_UINT8(ctest3, LSIState),
        VMSTATE_UINT8(ctest4, LSIState),
        VMSTATE_UINT8(ctest5, LSIState),
        VMSTATE_UINT8(ccntl0, LSIState),
        VMSTATE_UINT8(ccntl1, LSIState),
        VMSTATE_UINT32(dsp, LSIState),
        VMSTATE_UINT32(dsps, LSIState),
        VMSTATE_UINT8(dmode, LSIState),
        VMSTATE_UINT8(dcntl, LSIState),
        VMSTATE_UINT8(scntl0, LSIState),
        VMSTATE_UINT8(scntl1, LSIState),
        VMSTATE_UINT8(scntl2, LSIState),
        VMSTATE_UINT8(scntl3, LSIState),
        VMSTATE_UINT8(sstat0, LSIState),
        VMSTATE_UINT8(sstat1, LSIState),
        VMSTATE_UINT8(scid, LSIState),
        VMSTATE_UINT8(sxfer, LSIState),
        VMSTATE_UINT8(socl, LSIState),
        VMSTATE_UINT8(sdid, LSIState),
        VMSTATE_UINT8(ssid, LSIState),
        VMSTATE_UINT8(sfbr, LSIState),
        VMSTATE_UINT8(stest1, LSIState),
        VMSTATE_UINT8(stest2, LSIState),
        VMSTATE_UINT8(stest3, LSIState),
        VMSTATE_UINT8(sidl, LSIState),
        VMSTATE_UINT8(stime0, LSIState),
        VMSTATE_UINT8(respid0, LSIState),
        VMSTATE_UINT8(respid1, LSIState),
        VMSTATE_UINT8_V(sbcl, LSIState, 1),
        VMSTATE_UINT32(mmrs, LSIState),
        VMSTATE_UINT32(mmws, LSIState),
        VMSTATE_UINT32(sfs, LSIState),
        VMSTATE_UINT32(drs, LSIState),
        VMSTATE_UINT32(sbms, LSIState),
        VMSTATE_UINT32(dbms, LSIState),
        VMSTATE_UINT32(dnad64, LSIState),
        VMSTATE_UINT32(pmjad1, LSIState),
        VMSTATE_UINT32(pmjad2, LSIState),
        VMSTATE_UINT32(rbc, LSIState),
        VMSTATE_UINT32(ua, LSIState),
        VMSTATE_UINT32(ia, LSIState),
        VMSTATE_UINT32(sbc, LSIState),
        VMSTATE_UINT32(csbc, LSIState),
        VMSTATE_BUFFER_UNSAFE(scratch, LSIState, 0, 18 * sizeof(uint32_t)),
        VMSTATE_UINT8(sbr, LSIState),

        VMSTATE_BUFFER_UNSAFE(script_ram, LSIState, 0, 8192),
        VMSTATE_END_OF_LIST()
    }
};
*/

// static const struct SCSIBusInfo lsi_scsi_info = {
//     .tcq = true,
//     .max_target = LSI_MAX_DEVS,
//     .max_lun = 0,  /* LUN support is buggy */

//     .transfer_data = lsi_transfer_data,
//     .complete = lsi_command_complete,
//     .cancel = lsi_request_cancelled
// };

// static void lsi_scsi_realize(PCIDevice *dev, Error **errp)
// {
//     LSIState *s = LSI53C895A(dev);
//     DeviceState *d = DEVICE(dev);
//     uint8_t *pci_conf;

//     pci_conf = dev->config;

//     /* PCI latency timer = 255 */
//     pci_conf[PCI_LATENCY_TIMER] = 0xff;
//     /* Interrupt pin A */
//     pci_conf[PCI_INTERRUPT_PIN] = 0x01;

//     memory_region_init_io(&s->mmio_io, OBJECT(s), &lsi_mmio_ops, s,
//                           "lsi-mmio", 0x400);
//     memory_region_init_io(&s->ram_io, OBJECT(s), &lsi_ram_ops, s,
//                           "lsi-ram", 0x2000);
//     memory_region_init_io(&s->io_io, OBJECT(s), &lsi_io_ops, s,
//                           "lsi-io", 256);

//     address_space_init(&s->pci_io_as, pci_address_space_io(dev), "lsi-pci-io");
//     qdev_init_gpio_out(d, &s->ext_irq, 1);

//     pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io_io);
//     pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio_io);
//     pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->ram_io);
//     QTAILQ_INIT(&s->queue);

//     scsi_bus_init(&s->bus, sizeof(s->bus), d, &lsi_scsi_info);
// }

/*
static void lsi_scsi_exit(PCIDevice *dev)
{
    LSIState *s = LSI53C895A(dev);

    address_space_destroy(&s->pci_io_as);
}

static void lsi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = lsi_scsi_realize;
    k->exit = lsi_scsi_exit;
    k->vendor_id = PCI_VENDOR_ID_LSI_LOGIC;
    k->device_id = PCI_DEVICE_ID_LSI_53C895A;
    k->class_id = PCI_CLASS_STORAGE_SCSI;
    k->subsystem_id = 0x1000;
    dc->reset = lsi_scsi_reset;
    dc->vmsd = &vmstate_lsi_scsi;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo lsi_info = {
    .name          = TYPE_LSI53C895A,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(LSIState),
    .class_init    = lsi_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void lsi53c810_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = PCI_DEVICE_ID_LSI_53C810;
}

static const TypeInfo lsi53c810_info = {
    .name          = TYPE_LSI53C810,
    .parent        = TYPE_LSI53C895A,
    .class_init    = lsi53c810_class_init,
};

static void lsi53c895a_register_types(void)
{
    type_register_static(&lsi_info);
    type_register_static(&lsi53c810_info);
}

type_init(lsi53c895a_register_types)

void lsi53c8xx_handle_legacy_cmdline(DeviceState *lsi_dev)
{
    LSIState *s = LSI53C895A(lsi_dev);

    scsi_bus_legacy_handle_cmdline(&s->bus);
}
*/

DEVICE_TICK(lsi53c895a)
{
    struct lsi53c895a_data *d = (struct lsi53c895a_data *) extra;

    if (d->queue.next != &d->queue) {
        struct lsi_request *request = get_pending_req(d);
        if (request) {
            fprintf(stderr, "lsi: waiting command: tag %08x dma_buf %p dma_len %08x pending %d\n", request->tag, request->dma_buf, request->dma_len, request->pending);
            ABORT();
        }
    }
}

DEVICE_ACCESS(lsi53c895a_io)
{
  assert(cpu);
  struct lsi53c895a_data *d = (lsi53c895a_data *)extra;

  if (writeflag == MEM_WRITE) {
    uint64_t idata = memory_readmax64(cpu, data, len);
    lsi_mmio_write(cpu, d, relative_addr, idata, len);
  } else {
    fprintf(stderr, "LSI read to %p (%d) from %08x\n", data, len, relative_addr);
    uint64_t odata = lsi_mmio_read(cpu, d, relative_addr, len);
    memory_writemax64(cpu, data, len, odata);
  }

  return 1;
}

DEVICE_ACCESS(lsi53c895a_mem)
{
  assert(cpu);
  struct lsi53c895a_data *d = (lsi53c895a_data *)extra;

  if (writeflag == MEM_WRITE) {
    uint64_t idata = memory_readmax64(cpu, data, len);
    fprintf(stderr, "LSI mem write %08x = %08x\n", relative_addr, (int)idata);
    for (int i = 0; i < len; i++) {
      d->script_ram[relative_addr + i] = idata;
      idata >>= 8;
    }
  } else {
    fprintf(stderr, "LSI mem read to %p (%d) from %08x\n", data, len, relative_addr);
    uint64_t odata = 0;
    for (int i = 0; i < len; i++) {
      odata <<= 8;
      odata |= d->script_ram[relative_addr + i];
    }
    memory_writemax64(cpu, data, len, odata);
  }

  return 1;
}

DEVINIT(lsi53c895a)
{
    struct lsi53c895a_data *d;
    int ssize = sizeof(struct lsi53c895a_data);
    CHECK_ALLOCATION(d = (struct lsi53c895a_data *) malloc(ssize));
    memset(d, 0, ssize);

    /* Set up device register values: */

    lsi_soft_reset(d);

    d->scid = 7;
    d->macntl = 15;
    d->config[PCI_LATENCY_TIMER] = 0xff;

    INTERRUPT_CONNECT(devinit->interrupt_path, d->ext_irq);

    QTAILQ_INIT(&d->queue);
    QTAILQ_INIT(&d->bus.queue);

    memory_device_register(devinit->machine->memory, "lsi53c895a (IO)",
                           devinit->addr, DEV_LSI53C895A_LENGTH,
                           dev_lsi53c895a_io_access, d, DM_DEFAULT, NULL);

    memory_device_register(devinit->machine->memory, "lsi53c895a (ALT)",
                           0x80008000, DEV_LSI53C895A_LENGTH,
                           dev_lsi53c895a_io_access, d, DM_DEFAULT, NULL);

    machine_add_tickfunction(devinit->machine, dev_lsi53c895a_tick, d, LSI_TICK_SHIFT);

    return 1;
}
