local hw = Io.system_bus()

Io.add_vbusses
{
  nvmedrv = Io.Vi.System_bus
  {
    PCI0 = Io.Vi.PCI_bus
    {
      pci_hd = wrap(hw:match("PCI/storage"));
    }
  };

  l4linux = Io.Vi.System_bus
  {
    -- Add a new virtual PCI root bridge
    PCI0 = Io.Vi.PCI_bus
    {
      pci_l4x = wrap(hw:match("PCI/network", "PCI/media"));
    };
  };
}

