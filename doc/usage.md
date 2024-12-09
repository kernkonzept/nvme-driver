# NVMe server   {#l4re_servers_nvme_driver}

The NVMe server is a driver for PCI Express NVMe controllers.

The NVMe server is capable of exposing entire disks (i.e. NVMe namespaces) (by
serial number and namespace identifier) or individual partitions (by their
partition UUID) of a hard drive to clients via the Virtio block interface.

The server consists of two parts. The first one is the hardware driver itself
that takes care of the communication with the underlying hardware and interrupt
handling. The second part implements a virtual block device and is responsible
to communicate with clients. The virtual block device translates commands it
receives into NVMe requests and issues them to the hardware driver.

The NVMe server allows both statically and dynamically configured clients. A
static configuration is given priority over dynamically connecting clients and
configured while the service starts. Dynamic clients can connect and disconnect
during runtime of the NVMe server.

## Building and Configuration

The NVMe server can be built using the L4Re build system. Just place
this project into your `pkg` directory. The resulting binary is called
`nvme-drv`

## Starting the service

The NVMe server can be started with Lua like this:

    local nvme_bus = L4.default_loader:new_channel();
    L4.default_loader:start({
      caps = {
        vbus = vbus_nvme,
        svr = nvme_bus:svr(),
      },
    },
    "rom/nvme-drv");

First an IPC gate (`nvme_bus`) is created which is used between the NVMe server
and a client to request access to a particular disk or partition. The
server-side is assigned to the mandatory `svr` capability of the NVMe server.
See the section below on how to configure access to a disk or partition.

The NVMe server needs access to a virtual bus capability (`vbus`). On the
virtual bus the NVMe server searches for NVMe compliant storage controllers.
Please see io's documentation about how to setup a virtual bus.

### Options

In the example above the NVMe server is started in its default configuration.
To customize the configuration of the NVMe-server it accepts the following
command line options:

* `-v`

  Enable verbose mode. You can repeat this option up to three times to increase
  verbosity up to trace level.

* `-q`

  This option enables the quiet mode. All output is silenced.

* `--client <cap_name>`

  This option starts a new static client option context. The following
  `device`, `ds-max` and `readonly` options belong to this context until a new
  client option context is created.

  The option parameter is the name of a local IPC gate capability with server
  rights.

* `--device <UUID | SN:n<NAMESPACE_ID>>`

  This option denotes the partition UUID or serial number of the preceding
  `client` option followed by a colon, letter 'n' and the identifier of the
  requested NVMe namespace.

* `--ds-max <max>`

  This option sets the upper limit of the number of dataspaces the client is
  able to register with the NVMe server for virtio DMA.

* `--readonly`

  This option sets the access to disks or partitions to read only for the
  preceding `client` option.

* `--nosgl`

  This option disables support for SGLs.

* `--nomsi`

  This option disables support for MSI interrupts.

* `--nomsix`

  This option disables support for MSI-X interrupts.

* `-d <cap_name>`, `--register-ds <cap_name>`
  This option registers a trusted dataspace capability. If this option gets
  used, it is not possible to communicate to the driver via dataspaces other
  than the registered ones. Can be used multiple times for multiple dataspaces.

  The option parameter is the name of a dataspace capability.

## Connecting a client

Prior to connecting a client to a virtual block session it has to be created
using the following Lua function. It has to be called on the client side of the
IPC gate capability whose server side is bound to the NVMe server.

    create(obj_type, "device=<UUID | SN:n<NAMESPACE_ID>>", "ds-max=<max>", "read-only")

* `obj_type`

  The type of object that should be created by the server. The type must be a
  positive integer. Currently the following objects are supported:
  * `0`: Virtio block host

* `"device=<UUID | SN>"`

  This string denotes either a partition UUID, or a disk serial number the
  client wants to be exported via the Virtio block interface followed by a
  colon, letter 'n' and the identifier of the requested NVMe namespace.

* `"ds-max=<max>"`

  Specifies the upper limit of the number of dataspaces the client is allowed
  to register with the NVMe server for virtio DMA.

* `"read-only"`

  This string sets the access to disks or partitions to read only for the
  client.

If the `create()` call is successful a new capability which references an NVMe
virtio device is returned. A client uses this capability to communicate with
the NVMe server using the Virtio block protocol.

## Examples

A couple of examples on how to request different disks or partitions are listed
below.

* Request a partition with the given UUID

      vda1 = nvme_bus:create(0, "ds-max=5", "device=88E59675-4DC8-469A-98E4-B7B021DC7FBE")

* Request complete namespace with the given serial number

      vda = nvme_bus:create(0, "ds-max=4", "device=1234:n1")

* A more elaborate example with a static client. The client uses the client
  side of the `nvme_cl1` capability to communicate with the NVMe server.

      local nvme_cl1 = L4.default_loader:new_channel();
      local nvme_bus = L4.default_loader:new_channel();
      L4.default_loader:start({
        caps = {
          vbus = vbus_nvme,
          svr = nvme_bus:svr(),
          cl1 = nvme_cl1:svr(),
        },
      },
      "rom/nvme-drv --client cl1 --device 88E59675-4DC8-469A-98E4-B7B021DC7FBE --ds-max 5");
