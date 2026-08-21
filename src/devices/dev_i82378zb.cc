#include "bus_pci.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "memory.h"
#include "machine.h"
#include "misc.h"

struct i82378zb_data {
  uint32_t pci_status;
  uint32_t pci_command;
  uint8_t error_enabling_1, error_detection_1, bus_status_60x;
};

DEVICE_ACCESS(i82378zb_pci_config)
{
  struct i82378zb_data *d = (struct i82378zb_data *) extra;
  uint64_t idata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN);  
  
  switch (relative_addr) {
    // XXX These accesses are early ram bank detection.  I hadn't previously understood them.
  case 0x04:
    if (writeflag == MEM_WRITE) {
      d->pci_status &= ~(idata >> 16);
      d->pci_command = idata;
    }
    idata = ((uint32_t)d->pci_status) << 16 | d->pci_command;
    break;

  case 0xc0:
    if (writeflag == MEM_WRITE) {
      d->error_detection_1 &= ~(idata >> 8);
      d->error_enabling_1 = idata;
      d->bus_status_60x = idata >> 24;
    }
    idata = (d->bus_status_60x << 24) | (d->error_detection_1 << 8) | d->error_enabling_1;
    break;
  }

  if (writeflag == MEM_READ)
    memory_writemax64(cpu, data, len|MEM_PCI_LITTLE_ENDIAN, idata);

  return 1;
}

DEVINIT(i82378zb)
{
  struct i82378zb_data *d = nullptr;
  CHECK_ALLOCATION(d = (struct i82378zb_data *) malloc(sizeof(struct i82378zb_data)));
  memset(d, 0, sizeof(struct i82378zb_data));

  memory_device_register
    (devinit->machine->memory, "i82378zb_pci_config",
     DEV_PCI_CONFIG_AREA, DEV_PCI_CONFIG_CARD_SIZE,
     dev_i82378zb_pci_config_access, d, DM_DEFAULT, NULL);

  return 1;
}
