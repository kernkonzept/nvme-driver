-- vim:set ft=lua:

local hw = Io.system_bus()

Io.add_vbus("nvmedrv", Io.Vi.System_bus
{
  PCI0 = Io.Vi.PCI_bus
  {
    pci_hd = wrap(hw:match("PCI/CC_01"));
  }
})
